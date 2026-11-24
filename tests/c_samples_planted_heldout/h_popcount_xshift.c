/* Planted (log 208, held-out): popcount, shifting x instead of indexing bits. */
int h_popcount_xshift(int x) {
    int c = 0;
    for (int i = 0; i < 32; i++) {
        c += x & 1;
        x >>= 1;
    }
    return c;
}
