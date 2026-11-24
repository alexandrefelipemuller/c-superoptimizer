/* Planted (log 208, dev): quotient by repeated subtraction. Classical: one division. */
int p_div_sub(int x, int d) {
    if (d <= 0 || x < 0) return 0;
    x &= 4095;
    int q = 0;
    while (x >= d) {
        x -= d;
        q++;
    }
    return q;
}
