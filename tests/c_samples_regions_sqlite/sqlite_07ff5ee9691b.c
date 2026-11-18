/* sq204b/sqlite3.c:90042 in sqlite3VdbeIdxRowid: typeRowid < 1 || typeRowid > 9 || typeRowid == 7 */
static int region(unsigned typeRowid) {
    return typeRowid < 1 || typeRowid > 9 || typeRowid == 7;
}
