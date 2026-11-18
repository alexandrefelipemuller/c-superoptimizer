/* src200/sqlite-amalgamation-3460000/sqlite-amalgamation-3460000/sqlite3.c:35533 in sqlite3AtoF: e < 10000 ? (e * 10 + (*z - 48)) : 10000 */
static int region(int e, char in1) {
    return e < 10000 ? (e * 10 + (in1 - 48)) : 10000;
}
