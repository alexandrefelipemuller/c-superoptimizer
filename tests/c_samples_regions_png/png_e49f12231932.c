/* bench205/png/pp/deflate.c:1012 in slide_hash: m >= wsize ? m - wsize : 0 */
static unsigned region(unsigned m, unsigned wsize) {
    return m >= wsize ? m - wsize : 0;
}
