int count_trailing_zeros(int x) {
    if (x == 0) return 32;
    int n = 0;
    do {
        if (x & 1) break;
        x = x >> 1;
        n += 1;
    } while (1);
    return n;
}
