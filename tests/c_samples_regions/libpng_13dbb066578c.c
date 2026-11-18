/* src200/libpng-1.6.44/libpng-1.6.44/pngwutil.c:259 in optimize_cmf: (z_cmf & 0x0f) == 8 && (z_cmf & 0xf0) <= 0x70 */
static int region(unsigned z_cmf) {
    return (z_cmf & 0x0f) == 8 && (z_cmf & 0xf0) <= 0x70;
}
