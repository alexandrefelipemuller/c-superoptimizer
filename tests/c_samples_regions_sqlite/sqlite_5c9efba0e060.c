/* sq204b/sqlite3.c:78924 in balance_nonroot: i < nOld - 1 && !leafData */
static int region(int i, int nOld, int leafData) {
    return i < nOld - 1 && !leafData;
}
