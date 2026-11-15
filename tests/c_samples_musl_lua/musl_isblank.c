/* musl-1.2.5/src/ctype/isblank.c : isblank (MIT, see LICENSES.md) */
int isblank(int c)
{
	return (c == ' ' || c == '\t');
}
