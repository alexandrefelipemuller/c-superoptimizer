/* Deoptimized: x mod 9 for x >= 0 via repeated digit sums. */
int d_digit_sum_mod(int x) {
    x &= 0xffff;
    while (x >= 10) {
        int s = 0;
        while (x > 0) {
            s += x % 10;
            x /= 10;
        }
        x = s;
    }
    if (x == 9) return 0;
    return x;
}
