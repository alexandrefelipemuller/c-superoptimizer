/* Planted (log 208, held-out): parity of the low byte. */
int h_parity8(int x) {
    int p = 0;
    for (int i = 0; i < 8; i++) p ^= (x >> i) & 1;
    return p;
}
