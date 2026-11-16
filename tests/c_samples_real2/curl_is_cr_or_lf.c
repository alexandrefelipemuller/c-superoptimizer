/* curl-8.11.0/lib/vtls/schannel_verify.c : is_cr_or_lf (see LICENSES.md) */
static int is_cr_or_lf(char c)
{
  return c == '\r' || c == '\n';
}
