/* Deoptimized: is bit k of x set, by shifting one position at a time. */
int d_bit_test_loop(int x, int k) {
    k &= 31;
    for (int i = 0; i < k; i++) x = x >> 1;
    return x & 1;
}
