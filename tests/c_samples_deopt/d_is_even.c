/* Deoptimized (log 208b): evenness by counting down two at a time. */
int d_is_even(int x) {
    x &= 0xffff;
    while (x >= 2) x = x - 2;
    return x == 0;
}
