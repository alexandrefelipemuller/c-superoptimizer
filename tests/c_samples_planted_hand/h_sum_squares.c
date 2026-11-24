int h_sum_squares(int n) {
    n &= 1023;
    return (int)((long long)n * (n + 1) * (2 * n + 1) / 6);
}
