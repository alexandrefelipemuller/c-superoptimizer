/* Planted (log 208, dev): parity of the 32 bits, one bit per iteration. Classical: xor fold. */
int p_parity_shift(int x) {
    int p = 0;
    for (int i = 0; i < 32; i++) p ^= (x >> i) & 1;
    return p;
}
