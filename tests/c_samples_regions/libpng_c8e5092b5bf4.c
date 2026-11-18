/* src200/libpng-1.6.44/libpng-1.6.44/pngrutil.c:3145 in png_check_chunk_name: c < 65 || c > 122 || (c > 90 && c < 97) */
static int region(int c) {
    return c < 65 || c > 122 || (c > 90 && c < 97);
}
