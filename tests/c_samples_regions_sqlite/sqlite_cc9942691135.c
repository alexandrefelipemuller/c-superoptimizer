/* sq204b/sqlite3.c:103669 in vdbeSorterCompareInt: ((v1[0] ^ v2[0]) & 0x80) != 0 */
static int region(unsigned char in0, unsigned char in1) {
    return ((in0 ^ in1) & 0x80) != 0;
}
