/* musl src/string/strcasecmp.c with tolower (src/ctype/tolower.c) inlined,
 * ported by hand to the int-array ABI as strcmp.c (log 199). */
int strcasecmp_halves(int *a, int n)
{
	int h = n / 2, i = 0;
	for (; a[i] && a[h + i] && (a[i] == a[h + i] ||
	       ((unsigned)a[i] - 'A' < 26 ? a[i] | 32 : a[i]) == ((unsigned)a[h + i] - 'A' < 26 ? a[h + i] | 32 : a[h + i])); i++);
	return ((unsigned)a[i] - 'A' < 26 ? a[i] | 32 : a[i]) - ((unsigned)a[h + i] - 'A' < 26 ? a[h + i] | 32 : a[h + i]);
}
