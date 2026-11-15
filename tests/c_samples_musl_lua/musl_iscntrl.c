/* musl-1.2.5/src/ctype/iscntrl.c : iscntrl (MIT, see LICENSES.md) */
int iscntrl(int c)
{
	return (unsigned)c < 0x20 || c == 0x7f;
}
