/* src_musl/musl-1.2.5/src/math/atan2l.c:70 in atan2l: ex + 120 < ey || ey == 0x7fff */
static int region(int ex, int ey) {
    return ex + 120 < ey || ey == 0x7fff;
}
