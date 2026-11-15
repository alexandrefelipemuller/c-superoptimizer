/* musl-1.2.5/src/ctype/isascii.c : isascii (MIT, see LICENSES.md) */
int isascii(int c)
{
	return !(c&~0x7f);
}
