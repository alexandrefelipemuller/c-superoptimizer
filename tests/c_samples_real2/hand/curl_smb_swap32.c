/* Hand version (log 200): the byte-swap builtin. */
static unsigned int smb_swap32(unsigned int x)
{
  return __builtin_bswap32(x);
}
