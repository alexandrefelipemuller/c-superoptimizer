/* src200/mbedtls/mbedtls-3.6.2/library/ssl_msg.c:6324 in mbedtls_ssl_read_version: ~(tls_version - (tls_version == 0xfeff ? 0x0202 : 0x0201)) */
static int region(unsigned short tls_version) {
    return ~(tls_version - (tls_version == 0xfeff ? 0x0202 : 0x0201));
}
