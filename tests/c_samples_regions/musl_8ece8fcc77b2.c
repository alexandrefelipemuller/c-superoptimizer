/* src_musl/musl-1.2.5/src/internal/floatscan.c:167 in decfloat: lnz < 9 && lnz <= rp && rp < 18 */
static int region(int lnz, int rp) {
    return lnz < 9 && lnz <= rp && rp < 18;
}
