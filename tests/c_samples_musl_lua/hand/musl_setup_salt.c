/* Log 198: the best version known for this sample, written by hand (not search output). */
#include <stdint.h>
static uint32_t setup_salt(uint32_t salt)
{
	uint32_t x = __builtin_bswap32(salt);
	x = (x >> 4 & 0x0f0f0f0f) | (x & 0x0f0f0f0f) << 4;
	x = (x >> 2 & 0x33333333) | (x & 0x33333333) << 2;
	x = (x >> 1 & 0x55555555) | (x & 0x55555555) << 1;
	return x >> 8;
}
