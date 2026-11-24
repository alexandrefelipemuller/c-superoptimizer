/* Log 208d: in-place reversal with two pointers. */
void ptr_reverse(int *a, int n) {
    int *lo = a, *hi = a + n - 1;
    while (lo < hi) {
        int t = *lo;
        *lo = *hi;
        *hi = t;
        lo++;
        hi--;
    }
}
