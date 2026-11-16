/* Hand version (log 200): both compares, no short circuit. */
static int is_cr_or_lf(char c)
{
  return (c == '\r') | (c == '\n');
}
