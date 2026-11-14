/* Population count looking at all 32 bit positions. */
int popcount_naive(int x) {
    int c = 0;
    for (int i = 0; i < 32; i++) c += (x >> i) & 1;
    return c;
}
