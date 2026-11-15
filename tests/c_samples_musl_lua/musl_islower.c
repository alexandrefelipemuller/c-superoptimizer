/* musl-1.2.5/src/ctype/islower.c : islower (MIT, see LICENSES.md) */
int islower(int c)
{
	return (unsigned)c-'a' < 26;
}
