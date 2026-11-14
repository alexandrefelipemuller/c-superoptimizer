/* Decimal digits of |x| (INT_MIN counts as 1: -INT_MIN wraps). */
int count_digits(int x) {
    if (x < 0) x = -x;
    int d = 1;
    while (x >= 10) {
        x /= 10;
        d++;
    }
    return d;
}
