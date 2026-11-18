/* src200/libpng-1.6.44/libpng-1.6.44/pngrutil.c:3851 in png_do_read_interlace: (1 - ((final_width + 1) & 0x01)) << 2 */
static unsigned region(unsigned final_width) {
    return (1 - ((final_width + 1) & 0x01)) << 2;
}
