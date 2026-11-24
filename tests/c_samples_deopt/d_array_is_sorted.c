/* Deoptimized: sortedness by comparing every pair. */
int d_array_is_sorted(const int *a, int n) {
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (a[i] > a[j]) return 0;
    return 1;
}
