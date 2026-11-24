/* Planted (log 208, dev): 1 + 2 + ... + n by a loop. Classical: Gauss closed form. */
int p_sum_range(int n) {
    n &= 4095;
    int s = 0;
    int i = 1;
    while (i <= n) {
        s += i;
        i++;
    }
    return s;
}
