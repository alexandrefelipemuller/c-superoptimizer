/* sq204b/sqlite3.c:72420 in allocateSpace: gap + 2 + nByte > top */
static int region(int gap, int nByte, int top) {
    return gap + 2 + nByte > top;
}
