/* src_musl/musl-1.2.5/src/math/powf.c:145 in powf: (2 * ix < 2 * 0x3f800000) == !(iy & 0x80000000) */
static int region(unsigned ix, unsigned iy) {
    return (2 * ix < 2 * 0x3f800000) == !(iy & 0x80000000);
}
