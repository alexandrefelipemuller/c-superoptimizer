/* Planted (log 208, dev): remainder by repeated subtraction. Classical: one modulo. */
int p_mod_sub(int x, int d) {
    if (d <= 0 || x < 0) return 0;
    x &= 4095;
    int q = 0;
    while (x >= d) {
        x -= d;
        q++;
    }
    return x;
}
