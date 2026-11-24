/* Log 208d: length of the nonzero prefix, end pointer and pointer difference. */
int ptr_prefix_len(const int *a, int n) {
    const int *p = a, *end = a + n;
    while (p != end && *p != 0) p++;
    return p - a;
}
