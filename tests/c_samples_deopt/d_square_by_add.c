/* Deoptimized: x * x by adding x to itself x times. */
int d_square_by_add(int x) {
    x &= 1023;
    int s = 0;
    for (int i = 0; i < x; i++) s = s + x;
    return s;
}
