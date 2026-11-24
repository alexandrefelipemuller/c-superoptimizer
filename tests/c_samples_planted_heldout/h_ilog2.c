/* Planted (log 208, held-out control, no rule): floor(log2 x) for x > 0. */
int h_ilog2(int x) {
    if (x <= 0) return -1;
    int r = 0;
    while (x > 1) {
        x >>= 1;
        r++;
    }
    return r;
}
