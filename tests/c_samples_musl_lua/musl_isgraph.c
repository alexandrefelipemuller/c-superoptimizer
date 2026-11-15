/* musl-1.2.5/src/ctype/isgraph.c : isgraph (MIT, see LICENSES.md) */
int isgraph(int c)
{
	return (unsigned)c-0x21 < 0x5e;
}
