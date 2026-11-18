/* src200/mbedtls/mbedtls-3.6.2/library/aria.c:361 in mbedtls_aria_setkey_enc: keybits != 128 && keybits != 192 && keybits != 256 */
static int region(unsigned keybits) {
    return keybits != 128 && keybits != 192 && keybits != 256;
}
