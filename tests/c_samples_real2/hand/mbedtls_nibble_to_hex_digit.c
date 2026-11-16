/* Hand version (log 200): '0' + i, plus 7 when i >= 10, no branch. */
static char nibble_to_hex_digit(int i)
{
    return i + '0' + (-(i >= 10) & 7);
}
