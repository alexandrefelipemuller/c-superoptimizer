/* src_musl/musl-1.2.5/src/math/powl.c:484 in powil: (e == 0) || (e > 64) || (e < -64) */
static int region(int e) {
    return (e == 0) || (e > 64) || (e < -64);
}
