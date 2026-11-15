/* musl-1.2.5/src/network/inet_pton.c : hexval (MIT, see LICENSES.md) */
static int hexval(unsigned c)
{
	if (c-'0'<10) return c-'0';
	c |= 32;
	if (c-'a'<6) return c-'a'+10;
	return -1;
}
