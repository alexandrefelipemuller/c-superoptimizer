/* mbedtls-3.6.2/library/x509.c : nibble_to_hex_digit (see LICENSES.md) */
static char nibble_to_hex_digit(int i)
{
    return (i < 10) ? (i + '0') : (i - 10 + 'A');
}
