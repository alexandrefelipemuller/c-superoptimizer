/* src200/mbedtls/mbedtls-3.6.2/library/des.c:428 in mbedtls_des_setkey: ((Y << 1) | (Y >> 27)) & 0x0FFFFFFF */
static unsigned region(unsigned Y) {
    return ((Y << 1) | (Y >> 27)) & 0x0FFFFFFF;
}
