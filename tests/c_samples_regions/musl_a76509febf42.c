/* src_musl/musl-1.2.5/src/network/res_send.c:14 in __res_send: r < 0 || !anslen ? -1 : anslen */
static int region(int r, int anslen) {
    return r < 0 || !anslen ? -1 : anslen;
}
