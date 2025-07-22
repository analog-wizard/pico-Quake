#ifndef PALLETE_H
#define PALLETE_H

#include <stdint.h>

//typedef uint16_t b555;
//typedef uint16_t b565;
//typedef uint8_t[3] b888;

//https://stackoverflow.com/questions/40062883/how-to-use-a-macro-in-an-include-directive
#define STRINGIFY_MACRO(x) STR(x)
#define STR(x) #x
#define EXPAND(x) x
#define CONCAT(n1, n2) STRINGIFY_MACRO(EXPAND(n1)EXPAND(n2))

#ifdef COLOR_MODE
    #include CONCAT(COLOR_MODE,_palette.h)
#else
    #include "888_palette.h"
#endif 

#endif