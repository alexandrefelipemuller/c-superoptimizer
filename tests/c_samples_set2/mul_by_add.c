/* Multiplication by repeated addition: the loop runs |b| times. */
int mul_by_add(int a, int b) {
    int r = 0;
    if (b < 0) {
        b = -b;
        a = -a;
    }
    while (b > 0) {
        r += a;
        b--;
    }
    return r;
}
