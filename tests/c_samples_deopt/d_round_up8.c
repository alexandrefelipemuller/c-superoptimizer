/* Deoptimized: round up to a multiple of 8 by incrementing. */
int d_round_up8(int x) {
    x &= 0xffffff;
    while (x % 8 != 0) x++;
    return x;
}
