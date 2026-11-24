/* Planted (log 208, held-out): reverse the low 16 bits, reading bit i. */
int h_reverse16(int x) {
    int r = 0;
    for (int i = 0; i < 16; i++) r = (r << 1) | ((x >> i) & 1);
    return r;
}
