/* Planted (log 208, held-out): popcount of the low 16 bits. */
int h_popcount16(int x) {
    int c = 0;
    for (int i = 0; i < 16; i++) c += (x >> i) & 1;
    return c;
}
