/* musl-1.2.5/src/ctype/isalpha.c : isalpha (MIT, see LICENSES.md) */
int isalpha(int c)
{
	return ((unsigned)c|32)-'a' < 26;
}
