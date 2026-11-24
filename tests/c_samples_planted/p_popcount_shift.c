/* Planted (log 208, dev): popcount over all 32 bit positions. Classical: SWAR. */
int p_popcount_shift(int x) {
    int c = 0;
    for (int i = 0; i < 32; i++) c += (x >> i) & 1;
    return c;
}
