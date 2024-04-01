#ifndef PALLETE_H
#define PALLETE_H

#include <stdint.h>

typedef uint16_t b555;

typedef uint16_t b565;

typedef byte[3] b888;

//Palette can be addressed from 0-255
typedef union {
    b555[255] 15bit_pallete;
    b565[255] 16bit_pallete;
    b888[255] 24bit_palette;
} palette;
//765 bytes large

palette main_palette;
palette* default_palette_location = 0x20041000;

#ifdef COLOR_MODE
    #define a(x) #x ".h"
    main_palette = a(COLOR_MODE);
#endif

typedef struct {
    //location flag, represents if the resource is in memory/flash or is on a external filesystem
    //if true then the resource is in conventionally addressable space, otherwise a file.
    bool loc_flag;
    union {
        void* mem_position;
        char* filename;
    } location;
} resource_location;

void palette_swap(palette* palette_ptr, resource_location* res_position);

#endif