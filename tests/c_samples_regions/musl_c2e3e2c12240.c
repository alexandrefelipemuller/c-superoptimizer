/* src_musl/musl-1.2.5/src/math/log.c:69 in log: (top & 0x8000) || (top & 0x7ff0) == 0x7ff0 */
static int region(unsigned top) {
    return (top & 0x8000) || (top & 0x7ff0) == 0x7ff0;
}
