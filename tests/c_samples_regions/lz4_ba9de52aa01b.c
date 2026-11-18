/* src200/lz4/lz4-1.10.0/lib/lz4hc.c:289 in LZ4HC_encodeSequence: (ll >= 15) ? ((ll - 15) / 255) + 1 : 0 */
static unsigned region(unsigned ll) {
    return (ll >= 15) ? ((ll - 15) / 255) + 1 : 0;
}
