/* sq204b/sqlite3.c:79107 in balance_nonroot: 1 + (i == (iParentIdx - nxDiv)) */
static int region(int i, int iParentIdx, int nxDiv) {
    return 1 + (i == (iParentIdx - nxDiv));
}
