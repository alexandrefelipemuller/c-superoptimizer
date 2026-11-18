/* sq204b/sqlite3.c:36179 in sqlite3GetVarint: ((unsigned)(p[0] & 0x7f) << 7) | p[1] */
static unsigned region(unsigned char in0, unsigned char in1) {
    return ((unsigned)(in0 & 0x7f) << 7) | in1;
}
