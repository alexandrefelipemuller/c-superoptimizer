/* sq204b/sqlite3.c:89070 in sqlite3VdbeRecordUnpack: idx < szHdr && d <= (unsigned)nKey */
static int region(unsigned idx, unsigned szHdr, unsigned d, int nKey) {
    return idx < szHdr && d <= (unsigned)nKey;
}
