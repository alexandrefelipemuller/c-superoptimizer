int h_sum_below(int n) {
    n &= 4095;
    return n * (n - 1) / 2;
}
