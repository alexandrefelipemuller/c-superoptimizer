/* Hand version (log 200): the byte-swap builtin. */
static unsigned short smb_swap16(unsigned short x)
{
  return __builtin_bswap16(x);
}
