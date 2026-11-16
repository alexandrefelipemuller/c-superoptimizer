/* musl src/string/strlen.c, the portable byte loop (the word-at-a-time part
 * is __GNUC__-only), ported by hand to the int-array ABI (log 199): the string
 * is a[0..], one char per element, NUL-terminated within a[0..n). */
int strlen_bytes(int *a, int n)
{
	int i;
	for (i = 0; a[i]; i++);
	return i;
}
