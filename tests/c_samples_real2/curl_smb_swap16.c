/* curl-8.11.0/lib/smb.c : smb_swap16 (see LICENSES.md) */
static unsigned short smb_swap16(unsigned short x)
{
  return (unsigned short) ((x << 8) | ((x >> 8) & 0xff));
}
