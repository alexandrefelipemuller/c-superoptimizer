/* Log 208d: sum through a walking pointer. */
int ptr_sum(const int *a, int n) {
    int s = 0;
    for (const int *p = a; p < a + n; p++) s += *p;
    return s;
}
