int is_pow2_loop(int x) {
    if (x <= 0) return 0;
    while ((x & 1) == 0) x >>= 1;
    return x == 1;
}
