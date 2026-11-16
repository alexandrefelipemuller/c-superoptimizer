/* musl src/string/memcmp.c ported by hand to the int-array ABI (log 199):
 * l is a[0..h), r is a[h..n), h = n/2 bytes each. */
int memcmp_halves(int *a, int n)
{
	int h = n / 2, i = 0;
	for (; i < h && a[i] == a[h + i]; i++);
	return i < h ? a[i] - a[h + i] : 0;
}
