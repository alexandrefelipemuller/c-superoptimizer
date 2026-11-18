/* sq204b/sqlite3.c:79076 in balance_nonroot: cntNew[i - 1] <= (i > 1 ? cntNew[i - 2] : 0) */
static int region(int in0, int i, int in2) {
    return in0 <= (i > 1 ? in2 : 0);
}
