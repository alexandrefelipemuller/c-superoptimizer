/* sq204b/sqlite3.c:81519 in checkTreePage: (x >> 16) - (prev & 0xffff) - 1 */
static unsigned region(unsigned x, unsigned prev) {
    return (x >> 16) - (prev & 0xffff) - 1;
}
