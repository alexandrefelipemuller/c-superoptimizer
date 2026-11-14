/* Early return from a for loop; `continue` on the common case. */
int is_sorted(const int *a, int n) {
    for (int i = 1; i < n; i++) {
        if (a[i - 1] <= a[i])
            continue;
        return 0;
    }
    return 1;
}
