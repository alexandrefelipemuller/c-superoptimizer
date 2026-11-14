int count_positive(const int *a, int n) {
    int c = 0;
    for (int i = 0; i < n; i++)
        if (a[i] > 0) c++;
    return c;
}
