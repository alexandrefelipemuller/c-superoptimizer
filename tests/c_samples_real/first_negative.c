/* Index of the first negative element, -1 if none: do/while with continue. */
int first_negative(const int *a, int n) {
    int i = 0;
    if (n <= 0)
        return -1;
    do {
        if (a[i] >= 0) {
            i++;
            continue;
        }
        return i;
    } while (i < n);
    return -1;
}
