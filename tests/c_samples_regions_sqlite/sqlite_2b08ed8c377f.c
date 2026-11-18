/* sq204b/sqlite3.c:79032 in balance_nonroot: cntNew[i] <= (i > 0 ? cntNew[i - 1] : 0) */
static int region(int in0, int i, int in2) {
    return in0 <= (i > 0 ? in2 : 0);
}
