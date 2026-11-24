/* Deoptimized: popcount with division and modulo by 2. */
int d_count_set_bits_div(int x) {
    x &= 0x7fffffff;
    int c = 0;
    while (x > 0) {
        if (x % 2 == 1) c++;
        x = x / 2;
    }
    return c;
}
