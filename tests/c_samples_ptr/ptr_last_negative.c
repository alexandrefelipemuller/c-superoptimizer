/* Log 208d: index of the last negative element (-1 if none), &a[i] and a backward pointer. */
int ptr_last_negative(const int *a, int n) {
    const int *q = &a[n - 1];
    while (q >= a && *q >= 0) q--;
    return q - a;
}
