/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// zone.c

#include "quakedef.h"

#define	DYNAMIC_SIZE 58254 // 	All of dynamic memory allocation will be confined to one memory block in 
//								SRAM 0

#define	ZONEID	0x1d4a11
#define MINFRAGMENT	64

//Representative of the avalible bitmap of bytes, where a 1 indicates that the byte is used and
//0 indicates that the byte is open and avalible to be alloc-ed.

//The start of the memory map start at the beginning of SRAM 0
//Texture Cache will be separate, and will largely be an extension of 
//the XIP cache
static byte[7282] *memory_map = 0x20000000;
static void* mem_map_end = memory_map + sizeof(*memory_map)

/*
typedef struct memblock_s
{
	int	size;		// including the header and possibly tiny fragments
	int	tag;		// a tag of 0 is a free block
	int	id;			// should be ZONEID
	int	pad;		// pad to 64 bit boundary
	struct	memblock_s	*next, *prev;
} memblock_t;

typedef struct
{
	int		size;		// total bytes malloced, including header
	memblock_t	blocklist;	// start / end cap for linked list
	memblock_t	*rover;
} memzone_t;
*/

void Cache_FreeLow 	(int new_low_hunk);
void Cache_FreeHigh (int new_high_hunk);


/*
==============================================================================

						ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

The zone calls are pretty much only used for small strings and structures,
all big things are allocated on the hunk.
==============================================================================
*/

//static memzone_t	*mainzone;

typedef struct forbidden_zone_s{
	void* start;
	unsigned length;
	forbidden_zone_s* next;
} forbidden_zone_t;

/*
========================
Z_Free
========================
*/
void Z_Free (void *ptr, unsigned size) {
	unsigned i = 0;
	unsigned offset = (ptr - mem_map_end);
	uint8_t mask = 0b10000000;
	uint8_t shift = 0;
	
	for(uint8_t x = 0; x < size;) {
	    if(offset) {
	        mask = mask>>1;
	        if(mask == 0) {
	            mask = 0b10000000;
	            i++;
                continue;
	        }
	        offset--;
	    } else {
	        if((mask>>shift) == 0) {
	            mask = 0b10000000;
	            shift = 0;
	            i++;
	        }
	        ((uint8_t*)&mem_map)[i] ^= mask>>shift;
	        shift++;
	        x++;
	    }
	}
}

/*
========================
Z_Malloc
========================
*/
void *Z_Malloc (unsigned size)
{
	void* buf;
	unsigned i = 0;
	uint8_t mask = 0b10000000;
	unsigned offset = 0;

	//Search the bitfield for enough bytes to allocate the object.
	for(unsigned x = 0; x < size;) {
        if(~((uint8_t*)&mem_map)[i] & mask) {
            x++;
        } else {
            x = 0;
        }
        mask = mask>>1;
        offset++;
        if(mask == 0) {
            mask = 0b10000000;
            i++;
        }
		if((offset+size) > DYNAMIC_SIZE) {
			return NULL;
		}
	}
	
	//Mask is always 1 too far left, temporary fix
	mask = mask<<1;
	
	//Claim relavant bytes for object through the bitfield
    for(unsigned x = 0; x < size;) {
        ((uint8_t*)&mem_map)[i] ^= mask;
        mask = mask<<1;
        x++;
        if(mask == 0) {
            mask = 0b00000001;
            i++;
        }
    }

	buf = (void*)((int)(mem_map_end) + (int)(offset))
	Q_memset (buf, 0, size);

	return buf;
}

/*
========================
Z_Realloc
========================
*/
void *Z_Realloc(void *ptr, int size)
{
	int old_size;
	void *old_ptr;

	if (!ptr)
		return Z_Malloc (size);

	block = (memblock_t *) ((byte *) ptr - sizeof (memblock_t));
	if (block->id != ZONEID)
		Sys_Error ("Z_Realloc: realloced a pointer without ZONEID");
	if (block->tag == 0)
		Sys_Error ("Z_Realloc: realloced a freed pointer");

	old_size = block->size;
	old_size -= (4 + (int)sizeof(memblock_t));	/* see Z_TagMalloc() */
	old_ptr = ptr;

	Z_Free (ptr);
	ptr = Z_Malloc (size);
	if (!ptr)
		Sys_Error ("Z_Realloc: failed on allocation of %i bytes", size);

	if (ptr != old_ptr)
		memmove (ptr, old_ptr, size);

	return ptr;
}

char *Z_Strdup (const char *s)
{
	size_t sz = strlen(s) + 1;
	char *ptr = (char *) Z_Malloc (sz);
	Q_memcpy (ptr, s, sz);
	return ptr;
}


//============================================================================

#define	HUNK_SENTINEL	0x1df001ed

#define HUNKNAME_LEN	24
typedef struct
{
	int		sentinel;
	int		size;		// including sizeof(hunk_t), -1 = not allocated
	char	name[HUNKNAME_LEN];
} hunk_t;

byte	*hunk_base;
int		hunk_size;

int		hunk_low_used;
int		hunk_high_used;

qboolean	hunk_tempactive;
int		hunk_tempmark;

/*
==============
Hunk_Check

Run consistancy and sentinel trahing checks
==============
*/
void Hunk_Check (void)
{
	hunk_t	*h;

	for (h = (hunk_t *)hunk_base ; (byte *)h != hunk_base + hunk_low_used ; )
	{
		if (h->sentinel != HUNK_SENTINEL)
			Sys_Error ("Hunk_Check: trashed sentinel");
		if (h->size < (int) sizeof(hunk_t) || h->size + (byte *)h - hunk_base > hunk_size)
			Sys_Error ("Hunk_Check: bad size");
		h = (hunk_t *)((byte *)h+h->size);
	}
}

/*
==============
Hunk_Print

If "all" is specified, every single allocation is printed.
Otherwise, allocations with the same name will be totaled up before printing.
==============
*/
void Hunk_Print (qboolean all)
{
	hunk_t	*h, *next, *endlow, *starthigh, *endhigh;
	int		count, sum;
	int		totalblocks;
	char	name[HUNKNAME_LEN];

	count = 0;
	sum = 0;
	totalblocks = 0;

	h = (hunk_t *)hunk_base;
	endlow = (hunk_t *)(hunk_base + hunk_low_used);
	starthigh = (hunk_t *)(hunk_base + hunk_size - hunk_high_used);
	endhigh = (hunk_t *)(hunk_base + hunk_size);

	Con_Printf ("          :%8i total hunk size\n", hunk_size);
	Con_Printf ("-------------------------\n");

	while (1)
	{
	//
	// skip to the high hunk if done with low hunk
	//
		if ( h == endlow )
		{
			Con_Printf ("-------------------------\n");
			Con_Printf ("          :%8i REMAINING\n", hunk_size - hunk_low_used - hunk_high_used);
			Con_Printf ("-------------------------\n");
			h = starthigh;
		}

	//
	// if totally done, break
	//
		if ( h == endhigh )
			break;

	//
	// run consistancy checks
	//
		if (h->sentinel != HUNK_SENTINEL)
			Sys_Error ("Hunk_Check: trashed sentinel");
		if (h->size < (int) sizeof(hunk_t) || h->size + (byte *)h - hunk_base > hunk_size)
			Sys_Error ("Hunk_Check: bad size");

		next = (hunk_t *)((byte *)h+h->size);
		count++;
		totalblocks++;
		sum += h->size;

	//
	// print the single block
	//
		Q_memcpy (name, h->name, HUNKNAME_LEN);
		if (all)
			Con_Printf ("%8p :%8i %8s\n",h, h->size, name);

	//
	// print the total
	//
		if (next == endlow || next == endhigh ||
		    strncmp (h->name, next->name, HUNKNAME_LEN - 1))
		{
			if (!all)
				Con_Printf ("          :%8i %8s (TOTAL)\n",sum, name);
			count = 0;
			sum = 0;
		}

		h = next;
	}

	Con_Printf ("-------------------------\n");
	Con_Printf ("%8i total blocks\n", totalblocks);

}

/*
===================
Hunk_Print_f -- johnfitz -- console command to call hunk_print
===================
*/
void Hunk_Print_f (void)
{
	Hunk_Print (false);
}

/*
===================
Hunk_AllocName
===================
*/
void *Hunk_AllocName (int size, const char *name)
{
	hunk_t	*h;

	if (size < 0)
		Sys_Error ("Hunk_Alloc: bad size: %i", size);

	size = sizeof(hunk_t) + ((size+15)&~15);

	if (hunk_size - hunk_low_used - hunk_high_used < size)
		Sys_Error ("Hunk_Alloc: failed on %i bytes",size);

	h = (hunk_t *)(hunk_base + hunk_low_used);
	hunk_low_used += size;

	Cache_FreeLow (hunk_low_used);

	Q_memset (h, 0, size);

	h->size = size;
	h->sentinel = HUNK_SENTINEL;
	q_strlcpy (h->name, name, HUNKNAME_LEN);

	return (void *)(h+1);
}

/*
===================
Hunk_Alloc
===================
*/
void *Hunk_Alloc (int size)
{
	return Hunk_AllocName (size, "unknown");
}

int	Hunk_LowMark (void)
{
	return hunk_low_used;
}

void Hunk_FreeToLowMark (int mark)
{
	if (mark < 0 || mark > hunk_low_used)
		Sys_Error ("Hunk_FreeToLowMark: bad mark %i", mark);
	Q_memset (hunk_base + mark, 0, hunk_low_used - mark);
	hunk_low_used = mark;
}

int	Hunk_HighMark (void)
{
	if (hunk_tempactive)
	{
		hunk_tempactive = false;
		Hunk_FreeToHighMark (hunk_tempmark);
	}

	return hunk_high_used;
}

void Hunk_FreeToHighMark (int mark)
{
	if (hunk_tempactive)
	{
		hunk_tempactive = false;
		Hunk_FreeToHighMark (hunk_tempmark);
	}
	if (mark < 0 || mark > hunk_high_used)
		Sys_Error ("Hunk_FreeToHighMark: bad mark %i", mark);
	Q_memset (hunk_base + hunk_size - hunk_high_used, 0, hunk_high_used - mark);
	hunk_high_used = mark;
}


/*
===================
Hunk_HighAllocName
===================
*/
void *Hunk_HighAllocName (int size, const char *name)
{
	hunk_t	*h;

	if (size < 0)
		Sys_Error ("Hunk_HighAllocName: bad size: %i", size);

	if (hunk_tempactive)
	{
		Hunk_FreeToHighMark (hunk_tempmark);
		hunk_tempactive = false;
	}

	size = sizeof(hunk_t) + ((size+15)&~15);

	if (hunk_size - hunk_low_used - hunk_high_used < size)
	{
		Con_Printf ("Hunk_HighAlloc: failed on %i bytes\n",size);
		return NULL;
	}

	hunk_high_used += size;
	Cache_FreeHigh (hunk_high_used);

	h = (hunk_t *)(hunk_base + hunk_size - hunk_high_used);

	Q_memset (h, 0, size);
	h->size = size;
	h->sentinel = HUNK_SENTINEL;
	q_strlcpy (h->name, name, HUNKNAME_LEN);

	return (void *)(h+1);
}


/*
=================
Hunk_TempAlloc

Return space from the top of the hunk
=================
*/
void *Hunk_TempAlloc (int size)
{
	void	*buf;

	size = (size+15)&~15;

	if (hunk_tempactive)
	{
		Hunk_FreeToHighMark (hunk_tempmark);
		hunk_tempactive = false;
	}

	hunk_tempmark = Hunk_HighMark ();

	buf = Hunk_HighAllocName (size, "temp");

	hunk_tempactive = true;

	return buf;
}

char *Hunk_Strdup (const char *s, const char *name)
{
	size_t sz = strlen(s) + 1;
	char *ptr = (char *) Hunk_AllocName (sz, name);
	Q_memcpy (ptr, s, sz);
	return ptr;
}

/*
===============================================================================

CACHE MEMORY

===============================================================================
*/

#define CACHENAME_LEN	32
typedef struct cache_system_s
{
	int						size;		// including this header
	cache_user_t			*user;
	char					name[CACHENAME_LEN];
	struct cache_system_s	*prev, *next;
	struct cache_system_s	*lru_prev, *lru_next;	// for LRU flushing
} cache_system_t;

cache_system_t 	*Cache_TryAlloc (int size, qboolean nobottom);

cache_system_t	cache_head;

/*
===========
Cache_Move
===========
*/
void Cache_Move ( cache_system_t *c)
{
	cache_system_t		*new_cs;

// we are clearing up space at the bottom, so only allocate it late
	new_cs = Cache_TryAlloc (c->size, true);
	if (new_cs)
	{
//		Con_Printf ("cache_move ok\n");

		Q_memcpy ( new_cs+1, c+1, c->size - sizeof(cache_system_t) );
		new_cs->user = c->user;
		Q_memcpy (new_cs->name, c->name, sizeof(new_cs->name));
		Cache_Free (c->user, false); //johnfitz -- added second argument
		new_cs->user->data = (void *)(new_cs+1);
	}
	else
	{
//		Con_Printf ("cache_move failed\n");

		Cache_Free (c->user, true); // tough luck... //johnfitz -- added second argument
	}
}

/*
============
Cache_FreeLow

Throw things out until the hunk can be expanded to the given point
============
*/
void Cache_FreeLow (int new_low_hunk)
{
	cache_system_t	*c;

	while (1)
	{
		c = cache_head.next;
		if (c == &cache_head)
			return;		// nothing in cache at all
		if ((byte *)c >= hunk_base + new_low_hunk)
			return;		// there is space to grow the hunk
		Cache_Move ( c );	// reclaim the space
	}
}

/*
============
Cache_FreeHigh

Throw things out until the hunk can be expanded to the given point
============
*/
void Cache_FreeHigh (int new_high_hunk)
{
	cache_system_t	*c, *prev;

	prev = NULL;
	while (1)
	{
		c = cache_head.prev;
		if (c == &cache_head)
			return;		// nothing in cache at all
		if ( (byte *)c + c->size <= hunk_base + hunk_size - new_high_hunk)
			return;		// there is space to grow the hunk
		if (c == prev)
			Cache_Free (c->user, true);	// didn't move out of the way //johnfitz -- added second argument
		else
		{
			Cache_Move (c);	// try to move it
			prev = c;
		}
	}
}

void Cache_UnlinkLRU (cache_system_t *cs)
{
	if (!cs->lru_next || !cs->lru_prev)
		Sys_Error ("Cache_UnlinkLRU: NULL link");

	cs->lru_next->lru_prev = cs->lru_prev;
	cs->lru_prev->lru_next = cs->lru_next;

	cs->lru_prev = cs->lru_next = NULL;
}

void Cache_MakeLRU (cache_system_t *cs)
{
	if (cs->lru_next || cs->lru_prev)
		Sys_Error ("Cache_MakeLRU: active link");

	cache_head.lru_next->lru_prev = cs;
	cs->lru_next = cache_head.lru_next;
	cs->lru_prev = &cache_head;
	cache_head.lru_next = cs;
}

/*
============
Cache_TryAlloc

Looks for a free block of memory between the high and low hunk marks
Size should already include the header and padding
============
*/
cache_system_t *Cache_TryAlloc (int size, qboolean nobottom)
{
	cache_system_t	*cs, *new_cs;

// is the cache completely empty?

	if (!nobottom && cache_head.prev == &cache_head)
	{
		if (hunk_size - hunk_high_used - hunk_low_used < size)
			Sys_Error ("Cache_TryAlloc: %i is greater then free hunk", size);

		new_cs = (cache_system_t *) (hunk_base + hunk_low_used);
		Q_memset (new_cs, 0, sizeof(*new_cs));
		new_cs->size = size;

		cache_head.prev = cache_head.next = new_cs;
		new_cs->prev = new_cs->next = &cache_head;

		Cache_MakeLRU (new_cs);
		return new_cs;
	}

// search from the bottom up for space

	new_cs = (cache_system_t *) (hunk_base + hunk_low_used);
	cs = cache_head.next;

	do
	{
		if (!nobottom || cs != cache_head.next)
		{
			if ( (byte *)cs - (byte *)new_cs >= size)
			{	// found space
				Q_memset (new_cs, 0, sizeof(*new_cs));
				new_cs->size = size;

				new_cs->next = cs;
				new_cs->prev = cs->prev;
				cs->prev->next = new_cs;
				cs->prev = new_cs;

				Cache_MakeLRU (new_cs);

				return new_cs;
			}
		}

	// continue looking
		new_cs = (cache_system_t *)((byte *)cs + cs->size);
		cs = cs->next;

	} while (cs != &cache_head);

// try to allocate one at the very end
	if ( hunk_base + hunk_size - hunk_high_used - (byte *)new_cs >= size)
	{
		Q_memset (new_cs, 0, sizeof(*new_cs));
		new_cs->size = size;

		new_cs->next = &cache_head;
		new_cs->prev = cache_head.prev;
		cache_head.prev->next = new_cs;
		cache_head.prev = new_cs;

		Cache_MakeLRU (new_cs);

		return new_cs;
	}

	return NULL;		// couldn't allocate
}

/*
============
Cache_Flush

Throw everything out, so new data will be demand cached
============
*/
void Cache_Flush (void)
{
	while (cache_head.next != &cache_head)
		Cache_Free ( cache_head.next->user, true); // reclaim the space //johnfitz -- added second argument
}

/*
============
Cache_Print

============
*/
void Cache_Print (void)
{
	cache_system_t	*cd;

	for (cd = cache_head.next ; cd != &cache_head ; cd = cd->next)
	{
		Con_Printf ("%8i : %s\n", cd->size, cd->name);
	}
}

/*
============
Cache_Report

============
*/
void Cache_Report (void)
{
	Con_DPrintf ("%u byte data cache\n", (hunk_size - hunk_high_used - hunk_low_used));
}

/*
============
Cache_Init

============
*/
void Cache_Init (void)
{
	cache_head.next = cache_head.prev = &cache_head;
	cache_head.lru_next = cache_head.lru_prev = &cache_head;

	Cmd_AddCommand ("flush", Cache_Flush);
}

/*
==============
Cache_Free

Frees the memory and removes it from the LRU list
==============
*/
void Cache_Free (cache_user_t *c, qboolean freetextures) //johnfitz -- added second argument
{
	cache_system_t	*cs;

	if (!c->data)
		Sys_Error ("Cache_Free: not allocated");

	cs = ((cache_system_t *)c->data) - 1;

	cs->prev->next = cs->next;
	cs->next->prev = cs->prev;
	cs->next = cs->prev = NULL;

	c->data = NULL;

	Cache_UnlinkLRU (cs);

	//johnfitz -- if a model becomes uncached, free the gltextures.  This only works
	//becuase the cache_user_t is the last component of the qmodel_t struct.  Should
	//fail harmlessly if *c is actually part of an sfx_t struct.  I FEEL DIRTY
	if (freetextures)
		TexMgr_FreeTexturesForOwner ((qmodel_t *)(c + 1) - 1);
}



/*
==============
Cache_Check
==============
*/
void *Cache_Check (cache_user_t *c)
{
	cache_system_t	*cs;

	if (!c->data)
		return NULL;

	cs = ((cache_system_t *)c->data) - 1;

// move to head of LRU
	Cache_UnlinkLRU (cs);
	Cache_MakeLRU (cs);

	return c->data;
}


/*
==============
Cache_Alloc
==============
*/
void *Cache_Alloc (cache_user_t *c, int size, const char *name)
{
	cache_system_t	*cs;

	if (c->data)
		Sys_Error ("Cache_Alloc: already allocated");

	if (size <= 0)
		Sys_Error ("Cache_Alloc: size %i", size);

	size = (size + sizeof(cache_system_t) + 15) & ~15;

// find memory for it
	while (1)
	{
		cs = Cache_TryAlloc (size, false);
		if (cs)
		{
			q_strlcpy (cs->name, name, CACHENAME_LEN);
			c->data = (void *)(cs+1);
			cs->user = c;
			break;
		}

	// free the least recently used cahedat
		if (cache_head.lru_prev == &cache_head)
			Sys_Error ("Cache_Alloc: out of memory"); // not enough memory at all

		Cache_Free (cache_head.lru_prev->user, true); //johnfitz -- added second argument
	}

	return Cache_Check (c);
}

//============================================================================

/*
========================
Memory_Init
========================
*/
void Memory_Init (void *buf, int size)
{
	int p;
	int zonesize = DYNAMIC_SIZE;

	hunk_base = (byte *) buf;
	hunk_size = size;
	hunk_low_used = 0;
	hunk_high_used = 0;

	Cache_Init ();
	p = COM_CheckParm ("-zone");
	if (p)
	{
		if (p < com_argc-1)
			zonesize = Q_atoi (com_argv[p+1]) * 1024;
		else
			Sys_Error ("Memory_Init: you must specify a size in KB after -zone");
	}
	mainzone = (memzone_t *) Hunk_AllocName (zonesize, "zone" );
	Memory_InitZone (mainzone, zonesize);

	Cmd_AddCommand ("hunk_print", Hunk_Print_f); //johnfitz
}

