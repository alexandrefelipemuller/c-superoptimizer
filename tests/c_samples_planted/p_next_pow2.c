/* Planted (log 208, dev): smallest power of two >= x by doubling. Classical: bit smear. */
int p_next_pow2(int x) {
    x &= 0x3fffffff;
    int p = 1;
    while (p < x) p <<= 1;
    return p;
}
