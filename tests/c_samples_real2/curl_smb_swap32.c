/* curl-8.11.0/lib/smb.c : smb_swap32 (see LICENSES.md) */
static unsigned int smb_swap32(unsigned int x)
{
  return (x << 24) | ((x << 8) & 0xff0000) | ((x >> 8) & 0xff00) |
    ((x >> 24) & 0xff);
}
