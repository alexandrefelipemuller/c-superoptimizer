/* src200/curl-8.11.0/curl-8.11.0/lib/smb.c:332 in smb_swap32: (x << 24) | ((x << 8) & 0xff0000) | ((x >> 8) & 0xff00) | ((x >> 24) & 0xff) */
static unsigned region(unsigned x) {
    return (x << 24) | ((x << 8) & 0xff0000) | ((x >> 8) & 0xff00) | ((x >> 24) & 0xff);
}
