/* sq204b/sqlite3.c:79346 in balance_nonroot: i >= 0 || cntOld[iPg - 1] >= cntNew[iPg - 1] */
static int region(int i, int in1, int in2) {
    return i >= 0 || in1 >= in2;
}
