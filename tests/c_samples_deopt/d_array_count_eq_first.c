/* Deoptimized: count elements equal to a[0], re-reading a[0] with a flag. */
int d_array_count_eq_first(const int *a, int n) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        int same = 0;
        if (a[i] == a[0]) same = 1;
        if (same == 1) c = c + 1;
    }
    return c;
}
