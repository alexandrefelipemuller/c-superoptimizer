/* src_musl/musl-1.2.5/src/locale/catgets.c:13 in cmp: x < y ? -1 : x > y ? 1 : 0 */
static int region(unsigned x, unsigned y) {
    return x < y ? -1 : x > y ? 1 : 0;
}
