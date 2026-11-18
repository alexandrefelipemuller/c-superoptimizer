/* sq204b/sqlite3.c:81397 in checkTreePage: pc < contentOffset || pc > usableSize - 4 */
static int region(unsigned pc, unsigned contentOffset, unsigned usableSize) {
    return pc < contentOffset || pc > usableSize - 4;
}
