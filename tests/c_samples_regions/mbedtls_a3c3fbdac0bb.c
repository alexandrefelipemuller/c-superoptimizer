/* src200/mbedtls/mbedtls-3.6.2/library/base64.c:205 in mbedtls_base64_decode: *src == 13 || *src == 10 || *src == 32 */
static int region(unsigned char in0) {
    return in0 == 13 || in0 == 10 || in0 == 32;
}
