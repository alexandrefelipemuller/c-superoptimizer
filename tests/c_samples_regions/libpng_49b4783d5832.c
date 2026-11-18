/* src200/libpng-1.6.44/libpng-1.6.44/pngrutil.c:4021 in png_read_filter_row_paeth_1byte_pixel: (p + pc) < 0 ? -(p + pc) : p + pc */
static int region(int p, int pc) {
    return (p + pc) < 0 ? -(p + pc) : p + pc;
}
