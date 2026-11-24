/* Planted (log 208, held-out): 0 + 1 + ... + (n - 1), strict bound, for loop. */
int h_sum_below(int n) {
    n &= 4095;
    int s = 0;
    for (int i = 0; i < n; i++) s += i;
    return s;
}
