/* Deoptimized: sign of x with three separate tests and a temporary. */
int d_sign_loop(int x) {
    int r = 0;
    if (x > 0) r = 1;
    if (x < 0) r = -1;
    if (x == 0) r = 0;
    return r;
}
