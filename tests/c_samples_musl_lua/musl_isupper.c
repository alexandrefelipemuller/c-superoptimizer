/* musl-1.2.5/src/ctype/isupper.c : isupper (MIT, see LICENSES.md) */
int isupper(int c)
{
	return (unsigned)c-'A' < 26;
}
