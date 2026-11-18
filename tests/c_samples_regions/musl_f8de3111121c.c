/* src_musl/musl-1.2.5/src/ctype/isspace.c:6 in isspace: c == 32 || (unsigned)c - 9 < 5 */
static int region(int c) {
    return c == 32 || (unsigned)c - 9 < 5;
}
