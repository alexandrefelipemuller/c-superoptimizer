/* src_musl/musl-1.2.5/src/time/strftime.c:17 in is_leap: !(y % 4) && ((y % 100) || !(y % 400)) */
static int region(int y) {
    return !(y % 4) && ((y % 100) || !(y % 400));
}
