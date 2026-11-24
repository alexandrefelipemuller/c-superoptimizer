/* Planted (log 208, dev): reverse the 32 bits, one per iteration. Classical: mask swaps. */
int p_reverse32(int x) {
    int r = 0;
    for (int i = 0; i < 32; i++) {
        r = (r << 1) | (x & 1);
        x >>= 1;
    }
    return r;
}
