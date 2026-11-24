int p_sum_range(int n) {
    n &= 4095;
    return n * (n + 1) / 2;
}
