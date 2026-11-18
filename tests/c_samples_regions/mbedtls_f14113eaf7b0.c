/* src200/mbedtls/mbedtls-3.6.2/library/rsa.c:1093 in mbedtls_rsa_gen_key: (nbits >= 200) ? ((nbits >> 1) - 99) : 0 */
static unsigned region(unsigned nbits) {
    return (nbits >= 200) ? ((nbits >> 1) - 99) : 0;
}
