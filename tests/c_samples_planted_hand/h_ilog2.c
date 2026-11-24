int h_ilog2(int x) {
    if (x <= 0) return -1;
    return 31 - __builtin_clz((unsigned)x);
}
