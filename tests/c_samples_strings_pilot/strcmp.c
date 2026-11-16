/* musl src/string/strcmp.c ported by hand to the int-array ABI (log 199):
 * l is a[0..h), r is a[h..n), h = n/2, each NUL-terminated within its half,
 * chars already unsigned (0..255). */
int strcmp_halves(int *a, int n)
{
	int h = n / 2, i = 0;
	for (; a[i] == a[h + i] && a[i]; i++);
	return a[i] - a[h + i];
}
