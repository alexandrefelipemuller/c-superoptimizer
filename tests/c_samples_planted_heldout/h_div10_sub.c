/* Planted (log 208, held-out): quotient by 10, counter first, constant divisor. */
int h_div10_sub(int x) {
    if (x < 0) return 0;
    x &= 8191;
    int q = 0;
    while (x >= 10) {
        q++;
        x -= 10;
    }
    return q;
}
