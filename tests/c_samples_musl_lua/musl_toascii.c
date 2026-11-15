/* musl-1.2.5/src/ctype/toascii.c : toascii (MIT, see LICENSES.md) */
int toascii(int c)
{
	return c & 0x7f;
}
