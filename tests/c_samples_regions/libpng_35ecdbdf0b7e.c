/* src200/libpng-1.6.44/libpng-1.6.44/pngrtran.c:3270 in png_do_compose: p | (p << 2) | (p << 4) | (p << 6) */
static unsigned region(unsigned p) {
    return p | (p << 2) | (p << 4) | (p << 6);
}
