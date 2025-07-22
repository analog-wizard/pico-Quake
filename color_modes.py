#from PIL import Image
import numpy as np
import math
import sys

palette = open("./palette.lmp", "rb")
array = np.array(np.split(np.array([i for i in palette.read()], dtype=np.uint8), 256))
palette.close()

bit_mode = list(map(int, list(sys.argv[1])))
assert(True not in [i>=9 for i in bit_mode])

array = np.array(list(map(lambda t: [round(t[i]*((2**bit_mode[i]-1)/255)) for i in range(len(t))], array)), dtype=np.uint64)
bin_array = list(map(lambda t: ''.join([(('0'*(bit_mode[i] - len(bin(t[i])[2:]))) + bin(t[i])[2:])[:bit_mode[i]] for i in range(len(t))]), array))
hex_array = list(map(lambda t: hex(int(t,2)), bin_array))

array_type = "uint8_t" if sum(bit_mode) <= 8 else ("uint16_t" if sum(bit_mode) <= 16 else "uint8_t[3]")

output_header = f"""//Generated with the "color_modes.py" script
#ifndef {sys.argv[1]}_PALETTE_H
#define {sys.argv[1]}_PALETTE_H

#include <stdint.h>

{array_type}[256] *palette = 0x20041000; // Start of SRAM 5
//Conversion (or not) of the original Quake palette to the indicated color bit mode
palette* = [""" + ', '.join(hex_array) + """"];

#endif"""

header = open(f"{sys.argv[1]}_palette.h", "w")
header.write(output_header)
header.close()

#array = array.reshape((16, 16, 3))
#im = Image.fromarray(array)
#im.save("test.bmp")