/* musl-1.2.5/src/crypt/crypt_des.c : ascii_is_unsafe (MIT, see LICENSES.md) */
static inline int ascii_is_unsafe(unsigned char ch)
{
	return !ch || ch == '\n' || ch == ':';
}
