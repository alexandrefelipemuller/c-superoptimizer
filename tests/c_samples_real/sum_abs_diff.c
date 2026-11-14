/* Total variation of an array: an abs() helper and a for loop. */
static int iabs(int x) { return x < 0 ? -x : x; }

int sum_abs_diff(const int *a, int n) {
    int total = 0;
    for (int i = 1; i < n; i++)
        total += iabs(a[i] - a[i - 1]);
    return total;
}
