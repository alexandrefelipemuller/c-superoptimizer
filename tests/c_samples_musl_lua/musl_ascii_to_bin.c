/* musl-1.2.5/src/crypt/crypt_des.c : ascii_to_bin (MIT, see LICENSES.md) */
#include <stdint.h>
static uint32_t ascii_to_bin(int ch)
{
	int sch = (ch < 0x80) ? ch : -(0x100 - ch);
	int retval;

	retval = sch - '.';
	if (sch >= 'A') {
		retval = sch - ('A' - 12);
		if (sch >= 'a')
			retval = sch - ('a' - 38);
	}
	retval &= 0x3f;

	return retval;
}
