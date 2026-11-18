/* sq204b/sqlite3.c:104583 in sqlite3VdbeSorterWrite: t > 10 && (t & 0x01) */
static int region(int t) {
    return t > 10 && (t & 0x01);
}
