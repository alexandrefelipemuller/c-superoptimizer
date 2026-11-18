/* sq204b/sqlite3.c:92032 in vdbeUnbind: i >= 31 ? 0x80000000 : (unsigned)1 << i */
static unsigned region(unsigned i) {
    return i >= 31 ? 0x80000000 : (unsigned)1 << i;
}
