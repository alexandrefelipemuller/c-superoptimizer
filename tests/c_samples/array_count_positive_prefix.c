/* Short-circuit guard: a[i] must not be read once i == n. */
int array_count_positive_prefix(int a[], int n) {
    int i = 0;
    while (i < n && a[i] > 0) i++;
    return i;
}
