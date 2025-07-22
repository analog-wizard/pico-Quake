/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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
// cmd.c -- Quake script command processing module

#include "quakedef.h"

void Cmd_ForwardToServer (void);

#define	MAX_ALIAS_NAME	32
#define CMDLINE_LENGTH 256 //johnfitz -- mirrored in common.c

typedef struct cmdalias_s
{
	//struct 	cmdalias_s	*next;
	char	name[MAX_ALIAS_NAME];
	char	*value;
} cmdalias_t;

typedef struct cmdalias_node_s{
	cmdalias_t cmdalias;
	struct cmdalias_node_s *next;
} cmdalias_node_t

//The core set of cmd_aliases are no longer a forwardly linked list, but instead are an array of fixed length
//Additional aliases may be added in a forwardly linked list.
#define CORE_CMD_ALIAS_LENGTH 25
struct {
	//index must be large enough to hold all of CORE_CMD_ALIAS_LENGTH
	cmdalias_t cmdaliases[CORE_CMD_FUNCTION_LENGTH];
	cmdalias_node_t* next;
	uint8_t index;
} cmd_alias_main_container;

qboolean	cmd_wait;

//=============================================================================

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
============
*/
void Cmd_Wait_f (void)
{
	cmd_wait = true;
}

/*
=============================================================================

						COMMAND BUFFER

=============================================================================
*/

sizebuf_t	cmd_text;

/*
============
Cbuf_Init
============
*/
void Cbuf_Init (void)
{
	SZ_Alloc (&cmd_text, 1<<18);
	// space for commands and script files. 
	// spike -- was 8192, but modern configs can be _HUGE_, at least if they contain lots of comments/docs for things.
}


/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddText (const char *text)
{
	int	l;

	l = Q_strlen (text);

	if (cmd_text.cursize + l >= cmd_text.maxsize)
	{
		Con_Printf ("Cbuf_AddText: overflow\n");
		return;
	}

	SZ_Write (&cmd_text, text, Q_strlen (text));
}


/*
============
Cbuf_InsertText

Adds command text immediately after the current command
Adds a \n to the text
FIXME: actually change the command buffer to do less copying
============
*/
void Cbuf_InsertText (const char *text)
{
	char	*temp;
	int		templen;

// copy off any commands still remaining in the exec buffer
	templen = cmd_text.cursize;
	if (templen)
	{
		temp = (char *) Z_Malloc (templen);
		Q_memcpy (temp, cmd_text.data, templen);
		SZ_Clear (&cmd_text);
	}
	else
		temp = NULL;	// shut up compiler

// add the entire text of the file
	Cbuf_AddText (text);
	SZ_Write (&cmd_text, "\n", 1);
// add the copied off data
	if (templen)
	{
		SZ_Write (&cmd_text, temp, templen);
		Z_Free (temp);
	}
}

/*
============
Cbuf_Execute
============
*/
void Cbuf_Execute (void)
{
	int		i;
	char	*text;
	char	line[1024];
	int		quotes;

	while (cmd_text.cursize)
	{
// find a \n or ; line break
		text = (char *)cmd_text.data;

		quotes = 0;
		for (i=0 ; i< cmd_text.cursize ; i++)
		{
			if (text[i] == '"')
				quotes++;
			if ( !(quotes&1) &&  text[i] == ';')
				break;	// don't break if inside a quoted string
			if (text[i] == '\n')
				break;
		}

		if (i > (int)sizeof(line) - 1)
		{
			memcpy (line, text, sizeof(line) - 1);
			line[sizeof(line) - 1] = 0;
		}
		else
		{
			memcpy (line, text, i);
			line[i] = 0;
		}

// delete the text from the command buffer and move remaining commands down
// this is necessary because commands (exec, alias) can insert data at the
// beginning of the text buffer

		if (i == cmd_text.cursize)
			cmd_text.cursize = 0;
		else
		{
			i++;
			cmd_text.cursize -= i;
			memmove (text, text + i, cmd_text.cursize);
		}

// execute the command line
		Cmd_ExecuteString (line, src_command);

		if (cmd_wait)
		{	// skip out while text still remains in buffer, leaving it
			// for next frame
			cmd_wait = false;
			break;
		}
	}
}

/*
==============================================================================

						SCRIPT COMMANDS

==============================================================================
*/

/*
===============
Cmd_StuffCmds_f -- johnfitz -- rewritten to read the "cmdline" cvar, for use with dynamic mod loading

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
quake +prog jctest.qp +cmd amlev1
quake -nosound +cmd amlev1
===============
*/
void Cmd_StuffCmds_f (void)
{
	extern cvar_t cmdline;
	char	cmds[CMDLINE_LENGTH];
	int		i, j, plus;

	plus = false;	// On Unix, argv[0] is command name

	for (i = 0, j = 0; cmdline.string[i]; i++)
	{
		if (cmdline.string[i] == '+')
		{
			plus = true;
			if (j > 0)
			{
				cmds[j-1] = ';';
				cmds[j++] = ' ';
			}
		}
		else if (cmdline.string[i] == '-' &&
			(i==0 || cmdline.string[i-1] == ' ')) //johnfitz -- allow hypenated map names with +map
				plus = false;
		else if (plus)
			cmds[j++] = cmdline.string[i];
	}
	cmds[j] = 0;

	Cbuf_InsertText (cmds);
}

/* id1/pak0.pak from 2021 re-release doesn't have a default.cfg
 * embedding Quakespasm's customized default.cfg for that...  */
#include "default_cfg.h"

/*
===============
Cmd_Exec_f
===============
*/
void Cmd_Exec_f (void)
{
	const char	*f;
	int		mark;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("exec <filename> : execute a script file\n");
		return;
	}

	mark = Hunk_LowMark ();
	f = (const char *)COM_LoadHunkFile (Cmd_Argv(1), NULL);
	if (!f && !strcmp(Cmd_Argv(1), "default.cfg")) {
		f = default_cfg;	/* see above.. */
	}
	if (!f)
	{
		Con_Printf ("couldn't exec %s\n",Cmd_Argv(1));
		return;
	}
	Con_Printf ("execing %s\n",Cmd_Argv(1));

	Cbuf_InsertText (f);
	if (f != default_cfg) {
		Hunk_FreeToLowMark (mark);
	}
}


/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
void Cmd_Echo_f (void)
{
	int		i;

	for (i=1 ; i<Cmd_Argc() ; i++)
		Con_Printf ("%s ",Cmd_Argv(i));
	Con_Printf ("\n");
}

/*
===============
Cmd_Alias_f -- johnfitz -- rewritten

Creates a new command that executes a command string (possibly ; seperated)
===============
*/
void Cmd_Alias_f (void)
{
	cmdalias_t	*a;
	char		cmd[1024];
	int			i, c;
	const char	*s;


	switch (Cmd_Argc()) {
	case 1: //list all aliases
		for (uint8_t x = 0, i = 0; x<CORE_CMD_FUNCTION_LENGTH; x++, i++) {
			Con_SafePrintf ("   %s: %s", cmd_alias_main_container.cmdaliases[i].name, cmd_alias_main_container.cmdaliases[i].value);
		}
		for (a = cmd_alias_main_container.next, i = 0; a; a=a->next, i++)
			Con_SafePrintf ("   %s: %s", ((cmdalias_t*)a)->name, ((cmdalias_t*)a)->value);
		if (i)
			Con_SafePrintf ("%i alias command(s)\n", i);
		else
			Con_SafePrintf ("no alias commands found\n");
		break;
	case 2: //output current alias string
		for (uint8_t i = 0; i<CORE_CMD_FUNCTION_LENGTH; i++) {
			if (!strcmp(Cmd_Argv(1), cmd_alias_main_container.cmdaliases[i].name))
				Con_Printf ("   %s: %s", cmd_alias_main_container.cmdaliases[i].name, cmd_alias_main_container.cmdaliases[i].value);
		}
		for (a = cmd_alias_main_container.next ; a ; a=a->next)
			if (!strcmp(Cmd_Argv(1), ((cmdalias_t*)a)->name))
				Con_Printf ("   %s: %s", ((cmdalias_t*)a)->name, ((cmdalias_t*)a)->value);
		break;
	default: //set alias string
		s = Cmd_Argv(1);
		if (strlen(s) >= MAX_ALIAS_NAME) {
			Con_Printf ("Alias name is too long\n");
			return;
		}

		for (uint8_t i = 0; i<CORE_CMD_FUNCTION_LENGTH; i++) {
			if (!strcmp(s, cmd_alias_main_container.cmdaliases[i].name)) {
				Z_Free(cmd_alias_main_container.cmdaliases[i].value);
				break;
			}
		}

		// if the alias already exists, reuse it
		for (a = cmd_alias_main_container.next ; a ; a=a->next) {
			if (!strcmp(s, ((cmdalias_t*)a)->name)) {
				Z_Free (((cmdalias_t*)a)->value);
				break;
			}
		}

		if (!a) {
			a = (cmdalias_t *) Z_Malloc (sizeof(cmdalias_t));
			a->next = cmd_alias_main_container.next;
			cmd_alias_main_container.next = a;
		}
		strcpy (((cmdalias_t*)a)->name, s);

		// copy the rest of the command line
		cmd[0] = 0;		// start out with a null string
		c = Cmd_Argc();
		for (i = 2; i < c; i++) {
			q_strlcat (cmd, Cmd_Argv(i), sizeof(cmd));
			if (i != c - 1)
				q_strlcat (cmd, " ", sizeof(cmd));
		}
		if (q_strlcat(cmd, "\n", sizeof(cmd)) >= sizeof(cmd)) {
			Con_Printf("alias value too long!\n");
			cmd[0] = '\n';	// nullify the string
			cmd[1] = 0;
		}

		a->value = Z_Strdup (cmd);
		break;
	}
}

/*
===============
Cmd_Unalias_f -- johnfitz
===============
*/
void Cmd_Unalias_f (void)
{
	cmdalias_node_t *a, *prev;
	prev = cmd_alias_main_container.next

	switch (Cmd_Argc()) {
	default:
		Con_Printf("unalias <name> : delete alias\n");
		break;
	case 2:
		for (uint8_t i = 0; i<CORE_CMD_ALIAS_LENGTH; i++) {
			if (!strcmp(Cmd_Argv(1), cmd_alias_main_container.cmdaliases[i].name)) {
				//Not quite right and no good way to re-add aliases to main array but
				//for right now it should be fine.
				Z_Free(cmd_alias_main_container.cmdaliases[i]);
				return;
			}
		}

		prev = NULL;
		for (a = cmd_alias_main_container.next; a; a = a->next) {
			if (!strcmp(Cmd_Argv(1), ((cmdalias_t*)a)->name)) {
				if (prev)
					prev->next = a->next;
				else
					cmd_alias_main_container.next  = a->next;

				Z_Free (((cmdalias_t*)a)->value);
				Z_Free (a);
				return;
			}
			prev = a;
		}
		Con_Printf ("No alias named %s\n", Cmd_Argv(1));
		break;
	}
}

/*
===============
Cmd_Unaliasall_f -- johnfitz
===============
*/
void Cmd_Unaliasall_f (void)
{
	cmdalias_node_t *one,*two;
	one = cmd_alias_main_container.next;

	while (one)
	{	
		two = one->next;
		Z_Free(((cmdalias_t*)one)->value);
		Z_Free(one);
		one = two;
	}
}

/*
=============================================================================

					COMMAND EXECUTION

=============================================================================
*/

typedef struct cmd_function_s
{
	//struct cmd_function_s	*next;
	const char		*name;
	xcommand_t		function;
} cmd_function_t;

typedef struct cmd_function_node_s {
	cmd_function_t cmd_function;
	struct cmd_function_node_s* next;
} cmd_function_node_t

//The core set of cmd_functions are no longer a forwardly linked list, but instead are an array of fixed length
//Additional cmd_functions may be added in a forwardly linked list.
#define CORE_CMD_FUNCTION_LENGTH 25
struct {
	//index must be large enough to hold all of CORE_CMD_FUNCTION_LENGTH
	cmd_function_t cmd_functions[CORE_CMD_FUNCTION_LENGTH];
	cmd_function_node_t* next;
	uint8_t index;
} cmd_functions_main_container;

#define	MAX_ARGS		80

static	int			cmd_argc;
static	char		*cmd_argv[MAX_ARGS];
static	char		cmd_null_string[] = "";
static	const char	*cmd_args = NULL;

/*
============
Cmd_List_f -- johnfitz
============
*/
void Cmd_List_f (void)
{
	cmd_function_node_t	*cmd;
	const char	*partial;
	int		len, count;

	if (Cmd_Argc() > 1)
	{
		partial = Cmd_Argv (1);
		len = Q_strlen(partial);
	}
	else
	{
		partial = NULL;
		len = 0;
	}

	count = CORE_CMD_FUNCTION_LENGTH;
	for (uint8_t i = 0; i<CORE_CMD_FUNCTION_LENGTH; i++) {
		if (partial && Q_strncmp (partial, cmd_functions_main_container.cmd_functions[i].name, len)) {
			continue;
		}
		Con_SafePrintf ("   %s\n", cmd_functions_main_container.cmd_functions[i].name);
	}
	for (cmd = cmd_functions_main_container.next ; cmd ; cmd=cmd->next) {
		if (partial && Q_strncmp (partial,((cmd_function_t*)cmd)->name, len))
		{
			continue;
		}
		Con_SafePrintf ("   %s\n", ((cmd_function_t*)cmd)->name);
		count++;
	}

	Con_SafePrintf ("%i commands", count);
	if (partial)
	{
		Con_SafePrintf (" beginning with \"%s\"", partial);
	}
	Con_SafePrintf ("\n");
}

static char *Cmd_TintSubstring(const char *in, const char *substr, char *out, size_t outsize)
{
	int l;
	char *m;
	q_strlcpy(out, in, outsize);
	while ((m = q_strcasestr(out, substr)))
	{
		l = strlen(substr);
		while (l-->0)
			if (*m >= ' ' && *m < 127)
				*m++ |= 0x80;
	}
	return out;
}

/*
============
Cmd_Apropos_f

scans through each command and cvar names+descriptions for the given substring
we don't support descriptions, so this isn't really all that useful, but even without the sake of consistency it still combines cvars+commands under a single command.
============
*/
void Cmd_Apropos_f(void)
{
	char tmpbuf[256];
	int hits = 0;
	cmd_function_node_t	*cmd;
	cvar_node_t 		*var;
	const char *substr = Cmd_Argv (1);
	if (!*substr)
	{
		Con_SafePrintf ("%s <substring> : search through commands and cvars for the given substring\n", Cmd_Argv(0));
		return;
	}

	for (uint8_t i = 0; i<CORE_CMD_FUNCTION_LENGTH; i++) {
		if (q_strcasestr(cmd_functions_main_container.cmd_functions[i].name, substr))
		{
			hits++;
			Con_SafePrintf ("%s\n", Cmd_TintSubstring(cmd_functions_main_container.cmd_functions[i].name, substr, tmpbuf, sizeof(tmpbuf)));
		}
	}

	for (cmd = cmd_functions_main_container.next ; cmd ; cmd=cmd->next) {
		if (q_strcasestr(((cmd_function_t*)cmd)->name, substr))
		{
			hits++;
			Con_SafePrintf ("%s\n", Cmd_TintSubstring(((cmd_function_t*)cmd)->name, substr, tmpbuf, sizeof(tmpbuf)));
		}
	}
	
	for (uint8_t i = 0; i<CORE_CVAR_LENGTH; i++) {
		if (q_strcasestr(cvar_main_container.cvars[i].name, substr)) {
			hits++;
			Con_SafePrintf ("%s\n", Cmd_TintSubstring(cvar_main_container.cvars[i].name, substr, tmpbuf, sizeof(tmpbuf)));
		}
	}

	for (var = cvar_main_container.next ; var ; var=var->next)
	{
		if (q_strcasestr(((cvar_t*)var)->name, substr)) {
			hits++;
			Con_SafePrintf ("%s (current value: \"%s\")\n", Cmd_TintSubstring(((cvar_t*)var)->name, substr, tmpbuf, sizeof(tmpbuf)), ((cvar_t*)var)->string);
		}
	}

	if (!hits)
		Con_SafePrintf ("no cvars nor commands contain that substring\n");
}

/*
============
Cmd_Init
============
*/
void Cmd_Init (void)
{
	Cmd_AddCommand ("cmdlist", Cmd_List_f); //johnfitz
	Cmd_AddCommand ("unalias", Cmd_Unalias_f); //johnfitz
	Cmd_AddCommand ("unaliasall", Cmd_Unaliasall_f); //johnfitz

	Cmd_AddCommand ("stuffcmds",Cmd_StuffCmds_f);
	Cmd_AddCommand ("exec",Cmd_Exec_f);
	Cmd_AddCommand ("echo",Cmd_Echo_f);
	Cmd_AddCommand ("alias",Cmd_Alias_f);
	Cmd_AddCommand ("cmd", Cmd_ForwardToServer);
	Cmd_AddCommand ("wait", Cmd_Wait_f);

	Cmd_AddCommand ("apropos", Cmd_Apropos_f);
	Cmd_AddCommand ("find", Cmd_Apropos_f);
}

/*
============
Cmd_Argc
============
*/
int	Cmd_Argc (void)
{
	return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
const char	*Cmd_Argv (int arg)
{
	if (arg < 0 || arg >= cmd_argc)
		return cmd_null_string;
	return cmd_argv[arg];
}

/*
============
Cmd_Args
============
*/
const char	*Cmd_Args (void)
{
	return cmd_args;
}


/*
============
Cmd_TokenizeString

Parses the given string into command line tokens.
============
*/
void Cmd_TokenizeString (const char *text)
{
	int		i;

// clear the args from the last string
	for (i=0 ; i<cmd_argc ; i++)
		Z_Free (cmd_argv[i]);

	cmd_argc = 0;
	cmd_args = NULL;

	while (1)
	{
// skip whitespace up to a /n
		while (*text && *text <= ' ' && *text != '\n')
		{
			text++;
		}

		if (*text == '\n')
		{	// a newline seperates commands in the buffer
			text++;
			break;
		}

		if (!*text)
			return;

		if (cmd_argc == 1)
			 cmd_args = text;

		text = COM_Parse (text);
		if (!text)
			return;

		if (cmd_argc < MAX_ARGS)
		{
			cmd_argv[cmd_argc] = Z_Strdup (com_token);
			cmd_argc++;
		}
	}

}

/*
============
Cmd_AddCommand
============
*/
void	Cmd_AddCommand (const char *cmd_name, xcommand_t function)
{
	cmd_function_node_t	*cmd;
	cmd_function_node_t	*cursor,*prev; //johnfitz -- sorted list insert

	if (host_initialized)	// because hunk allocation would get stomped
		Sys_Error ("Cmd_AddCommand after host_initialized");

// fail if the command is a variable name
	if (Cvar_VariableString(cmd_name)[0]) {
		Con_Printf ("Cmd_AddCommand: %s already defined as a var\n", cmd_name);
		return;
	}

// fail if the command already exists
	for (uint8_t i = 0; i<CORE_CMD_FUNCTION_LENGTH; i++) {
		if (!Q_strcmp (cmd_name, cmd_functions_main_container.cmd_functions[i].name)) {
			Con_Printf ("Cmd_AddCommand: %s already defined\n", cmd_name);
			return;
		}
	}

	for (cmd = cmd_functions_main_container.next ; cmd ; cmd=cmd->next) {
		if (!Q_strcmp (cmd_name, ((cmd_function_t*)cmd)->name)) {
			Con_Printf ("Cmd_AddCommand: %s already defined\n", cmd_name);
			return;
		}
	}

	if(cmd_functions_main_container.index < CORE_CVAR_LENGTH) {
		cmd_functions_main_container.cmd_functions[cmd_functions_main_container.index] = cmd_function_t{cmd_name , function};
		//would love to dealloc variable but it doesnt really matter.
		cmd_functions_main_container.index++;
	} else {
		for(cmd = cmd_functions_main_container.next; cmd; cmd->next);
		cmd->next = (cmd_function_node_t *) Hunk_Alloc (sizeof(cmd_function_node_t));
		cmd = cmd->next;
		((cmd_function_t*)cmd)->name = cmd_name;
		((cmd_function_t*)cmd)->function = function;
	}
}

/*
============
Cmd_Exists
============
*/
qboolean	Cmd_Exists (const char *cmd_name)
{
	cmd_function_node_t	*cmd;

	for (uint8_t i = 0; i<CORE_CMD_FUNCTION_LENGTH; i++) {
		if (!Q_strcmp (cmd_name,cmd_functions_main_container.cmd_functions[i].name))
			return true;
	}

	for (cmd = cmd_functions_main_container.next ; cmd ; cmd = cmd->next) {
		if (!Q_strcmp (cmd_name,((cmd_function_t*)cmd)->name))
			return true;
	}

	return false;
}



/*
============
Cmd_CompleteCommand
============
*/
const char *Cmd_CompleteCommand (const char *partial)
{
	cmd_function_t	*cmd;
	int		len;

	len = Q_strlen(partial);

	if (!len)
		return NULL;

// check functions
	for (uint8_t i = 0; i<CORE_CMD_FUNCTION_LENGTH; i++) {
		if (!Q_strncmp (partial,cmd_functions_main_container.cmd_functions[i].name, len))
			return ((cmd_function_t*)cmd)->name;
	}

	for (cmd = cmd_functions_main_container.next ; cmd ; cmd=cmd->next)
		if (!Q_strncmp (partial,((cmd_function_t*)cmd)->name, len))
			return ((cmd_function_t*)cmd)->name;

	return NULL;
}

/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
FIXME: lookupnoadd the token to speed search?
============
*/
void	Cmd_ExecuteString (const char *text, cmd_source_t src)
{
	cmd_function_node_t	*cmd;
	cmdalias_node_t		*a;

	//cmd_source = src;
	Cmd_TokenizeString (text);

// execute the command line
	if (!Cmd_Argc())
		return;		// no tokens

// check functions
	for (uint8_t i = 0; i<CORE_CMD_ALIAS_LENGTH; i++) {
		if (!q_strcasecmp (cmd_argv[0], cmd_functions_main_container.cmd_functions[i].name)) {
			cmd_functions_main_container.cmd_functions[i].function();
			return;
		}
	}

	for (cmd=cmd_functions_main_container.next ; cmd ; cmd=cmd->next) {
		if (!q_strcasecmp (cmd_argv[0],((cmd_function_t*)cmd)->name)) {
			((cmd_function_t*)cmd)->function();
			return;
		}
	}

// check alias
	for (uint8_t i = 0; i<CORE_CMD_ALIAS_LENGTH; i++) {
		if (!q_strcasecmp (cmd_argv[0], cmd_alias_main_container.cmdaliases[i].name)) {
			Cbuf_InsertText (cmd_alias_main_container.cmdaliases[i].name);
			return;
		}
	}

	for (a=cmd_alias_main_container.next; a ; a=a->next) {
		if (!q_strcasecmp (cmd_argv[0], ((cmdalias_t*)a)->name)) {
			Cbuf_InsertText (((cmdalias_t*)a)->value);
			return;
		}
	}

// check cvars
	if (!Cvar_Command ())
		Con_Printf ("Unknown command \"%s\"\n", Cmd_Argv(0));
}


/*
===================
Cmd_ForwardToServer

Sends the entire command line over to the server
===================
*/
void Cmd_ForwardToServer (void)
{
	if (cls.state != ca_connected)
	{
		Con_Printf ("Can't \"%s\", not connected\n", Cmd_Argv(0));
		return;
	}

	if (cls.demoplayback)
		return;		// not really connected

	MSG_WriteByte (&cls.message, clc_stringcmd);
	if (q_strcasecmp(Cmd_Argv(0), "cmd") != 0)
	{
		SZ_Print (&cls.message, Cmd_Argv(0));
		SZ_Print (&cls.message, " ");
	}
	if (Cmd_Argc() > 1)
		SZ_Print (&cls.message, Cmd_Args());
	else
		SZ_Print (&cls.message, "\n");
}


/*
================
Cmd_CheckParm

Returns the position (1 to argc-1) in the command's argument list
where the given parameter apears, or 0 if not present
================
*/

int Cmd_CheckParm (const char *parm)
{
	int i;

	if (!parm)
		Sys_Error ("Cmd_CheckParm: null input\n");

	for (i = 1; i < Cmd_Argc (); i++)
		if ( !q_strcasecmp (parm, Cmd_Argv (i)) )
			return i;

	return 0;
}

