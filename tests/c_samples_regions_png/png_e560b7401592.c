/* bench205/png/pp/pngwutil.c:3231 in png_setup_paeth_row: p < 0 ? -p : p */
static int region(int p) {
    return p < 0 ? -p : p;
}
