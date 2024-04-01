#include "palette.h"

void palette_swap(palette* palette_ptr, resource_location* res_position) {
    if(res_position->loc_flag) {
        memcpy(res_position->location.mem_position, palette_ptr);
    } else {
        //file stuff
    }
}