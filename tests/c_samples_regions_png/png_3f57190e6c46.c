/* bench205/png/pp/pngwutil.c:3232 in png_setup_paeth_row: pc < 0 ? -pc : pc */
static int region(int pc) {
    return pc < 0 ? -pc : pc;
}
