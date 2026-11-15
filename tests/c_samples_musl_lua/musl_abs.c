/* musl-1.2.5/src/stdlib/abs.c : abs (MIT, see LICENSES.md) */
int abs(int a)
{
	return a>0 ? a : -a;
}
