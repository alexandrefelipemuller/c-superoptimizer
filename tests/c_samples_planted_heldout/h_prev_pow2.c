/* Planted (log 208, held-out): largest power of two <= x (x >= 1), doubling with a test ahead. */
int h_prev_pow2(int x) {
    x &= 0x3fffffff;
    if (x == 0) return 0;
    int p = 1;
    while (p <= x / 2) p *= 2;
    return p;
}
