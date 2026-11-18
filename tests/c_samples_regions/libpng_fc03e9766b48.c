/* src200/libpng-1.6.44/libpng-1.6.44/contrib/libtests/makepng.c:1815 in main: color_type < 0 || color_type > 6 || color_type == 1 || color_type == 5 */
static int region(int color_type) {
    return color_type < 0 || color_type > 6 || color_type == 1 || color_type == 5;
}
