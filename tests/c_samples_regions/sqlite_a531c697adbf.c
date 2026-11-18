/* src200/sqlite-amalgamation-3460000/sqlite-amalgamation-3460000/shell.c:23322 in readHexDb: pgsz < 512 || pgsz > 65536 || (pgsz & (pgsz - 1)) != 0 */
static int region(int pgsz) {
    return pgsz < 512 || pgsz > 65536 || (pgsz & (pgsz - 1)) != 0;
}
