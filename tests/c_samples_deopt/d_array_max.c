/* Deoptimized: array maximum, rescanning to confirm each candidate. */
int d_array_max(const int *a, int n) {
    for (int i = 0; i < n; i++) {
        int ok = 1;
        for (int j = 0; j < n; j++)
            if (a[j] > a[i]) ok = 0;
        if (ok) return a[i];
    }
    return 0;
}
