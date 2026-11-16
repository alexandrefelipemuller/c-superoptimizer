/* Hand version (log 200): clamp to 12 first (a select), then the formula,
 * which gives 12 at n = 12. */
static int memsys3Roundup(int n){
  int m = n < 12 ? 12 : n;
  return ((m+11)&~7) - 4;
}
