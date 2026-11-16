/* Log 198: the best version known for this sample, written by hand (not search output). */
#include <stdint.h>
static inline int checkint(uint32_t iy)
{
	int e = iy >> 23 & 0xff;
	if (e < 0x7f) return 0;
	int s = 0x7f + 23 - e;
	if (s < 0) return 2;
	int tz = __builtin_ctz(iy | 0x800000);
	return (tz >= s) + (tz > s);
}
