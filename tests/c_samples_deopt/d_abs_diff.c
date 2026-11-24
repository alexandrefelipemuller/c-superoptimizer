/* Deoptimized: |a - b| through nested branches. */
int d_abs_diff(int a, int b) {
    a &= 0xfffff; b &= 0xfffff;
    if (a > b) {
        return a - b;
    } else {
        if (b > a) return b - a;
        return 0;
    }
}
