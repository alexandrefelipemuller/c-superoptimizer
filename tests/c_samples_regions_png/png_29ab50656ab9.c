/* bench205/png/pp/pngwutil.c:3102 in png_setup_sub_row: (v < 128) ? v : 256 - v */
static unsigned region(unsigned v) {
    return (v < 128) ? v : 256 - v;
}
