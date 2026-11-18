/* src200/libpng-1.6.44/libpng-1.6.44/contrib/libtests/makepng.c:1828 in main: bit_depth <= 0 || bit_depth > 16 || (bit_depth & -bit_depth) != bit_depth */
static int region(int bit_depth) {
    return bit_depth <= 0 || bit_depth > 16 || (bit_depth & -bit_depth) != bit_depth;
}
