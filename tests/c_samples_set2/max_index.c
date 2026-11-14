/* Index of the first maximum; reloads a[best] at every comparison. */
int max_index(const int *a, int n) {
    int best = 0;
    for (int i = 1; i < n; i++)
        if (a[i] > a[best]) best = i;
    return best;
}
