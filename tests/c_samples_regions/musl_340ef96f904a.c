/* src_musl/musl-1.2.5/src/math/powf.c:115 in checkint: iy & (1 << (0x7f + 23 - e)) */
static unsigned region(unsigned iy, int e) {
    return iy & (1 << (0x7f + 23 - e));
}
