/* Deoptimized: max of three with every comparison spelled out. */
int d_max3(int a, int b, int c) {
    if (a >= b && a >= c) return a;
    if (b >= a && b >= c) return b;
    return c;
}
