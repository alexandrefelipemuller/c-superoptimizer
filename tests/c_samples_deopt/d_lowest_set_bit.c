/* Deoptimized: value of the lowest set bit by testing every power. */
int d_lowest_set_bit(int x) {
    int p = 1;
    for (int i = 0; i < 31; i++) {
        if ((x & p) != 0) return p;
        p = p * 2;
    }
    if (x != 0) return x & (-2147483647 - 1);
    return 0;
}
