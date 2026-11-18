/* sq204b/sqlite3.c:73240 in sqlite3BtreeOpen: (zDbHeader[16] << 8) | (zDbHeader[17] << 16) */
static int region(unsigned char in0, unsigned char in1) {
    return (in0 << 8) | (in1 << 16);
}
