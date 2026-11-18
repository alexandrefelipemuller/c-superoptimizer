/* src_musl/musl-1.2.5/src/locale/iconv.c:414 in iconv: d - 0x40 >= 191 || d == 127 */
static int region(unsigned d) {
    return d - 0x40 >= 191 || d == 127;
}
