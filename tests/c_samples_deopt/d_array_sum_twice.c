/* Deoptimized: sum computed twice and halved. */
int d_array_sum_twice(const int *a, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    for (int i = 0; i < n; i++) s += a[i];
    return s / 2;
}
