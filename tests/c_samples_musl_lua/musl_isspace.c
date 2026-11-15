/* musl-1.2.5/src/ctype/isspace.c : isspace (MIT, see LICENSES.md) */
int isspace(int c)
{
	return c == ' ' || (unsigned)c-'\t' < 5;
}
