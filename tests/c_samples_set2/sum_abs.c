/* A data-dependent branch per element. */
int sum_abs(const int *a, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) {
        if (a[i] < 0) s -= a[i];
        else s += a[i];
    }
    return s;
}
