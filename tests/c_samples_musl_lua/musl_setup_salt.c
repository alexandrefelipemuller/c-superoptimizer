/* musl-1.2.5/src/crypt/crypt_des.c : setup_salt (MIT, see LICENSES.md) */
#include <stdint.h>
static uint32_t setup_salt(uint32_t salt)
{
	uint32_t obit, saltbit, saltbits;
	unsigned int i;

	saltbits = 0;
	saltbit = 1;
	obit = 0x800000;
	for (i = 0; i < 24; i++) {
		if (salt & saltbit)
			saltbits |= obit;
		saltbit <<= 1;
		obit >>= 1;
	}

	return saltbits;
}
