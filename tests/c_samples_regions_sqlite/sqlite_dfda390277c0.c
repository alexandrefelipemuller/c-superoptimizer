/* sq204b/sqlite3.c:72400 in allocateSpace: (data[hdr + 2] || data[hdr + 1]) && gap + 2 <= top */
static int region(unsigned char in0, unsigned char in1, int gap, int top) {
    return (in0 || in1) && gap + 2 <= top;
}
