/* sq204b/sqlite3.c:73902 in lockBtree: ((pageSize - 1) & pageSize) != 0 */
static int region(unsigned pageSize) {
    return ((pageSize - 1) & pageSize) != 0;
}
