/* bench205/png/pp/pngwutil.c:3233 in png_setup_paeth_row: (p + pc) < 0 ? -(p + pc) : p + pc */
static int region(int p, int pc) {
    return (p + pc) < 0 ? -(p + pc) : p + pc;
}
