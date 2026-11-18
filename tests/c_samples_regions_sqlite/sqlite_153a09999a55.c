/* sq204b/sqlite3.c:81514 in checkTreePage: (prev & 0xffff) >= (x >> 16) */
static int region(unsigned prev, unsigned x) {
    return (prev & 0xffff) >= (x >> 16);
}
