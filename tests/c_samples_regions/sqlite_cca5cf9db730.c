/* src200/sqlite-amalgamation-3460000/sqlite-amalgamation-3460000/shell.c:6459 in re_next_char: c <= 0x7ff || (c >= 0xd800 && c <= 0xdfff) */
static int region(unsigned c) {
    return c <= 0x7ff || (c >= 0xd800 && c <= 0xdfff);
}
