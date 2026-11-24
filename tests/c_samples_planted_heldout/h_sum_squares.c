/* Planted (log 208, held-out control, no rule): 1^2 + ... + n^2. */
int h_sum_squares(int n) {
    n &= 1023;
    int s = 0;
    for (int i = 1; i <= n; i++) s += i * i;
    return s;
}
