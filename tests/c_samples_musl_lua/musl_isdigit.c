/* musl-1.2.5/src/ctype/isdigit.c : isdigit (MIT, see LICENSES.md) */
int isdigit(int c)
{
	return (unsigned)c-'0' < 10;
}
