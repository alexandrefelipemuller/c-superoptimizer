/* musl-1.2.5/src/ctype/isprint.c : isprint (MIT, see LICENSES.md) */
int isprint(int c)
{
	return (unsigned)c-0x20 < 0x5f;
}
