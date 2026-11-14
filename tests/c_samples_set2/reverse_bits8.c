/* Reverse the low 8 bits of x, one bit per iteration. */
int reverse_bits8(int x) {
    int r = 0;
    for (int i = 0; i < 8; i++) {
        r = (r << 1) | (x & 1);
        x >>= 1;
    }
    return r;
}
