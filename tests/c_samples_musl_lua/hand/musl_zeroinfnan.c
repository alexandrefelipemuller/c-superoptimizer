/* Log 198: the best version known for this sample, written by hand (not search output). */
/* musl-1.2.5/src/math/powf.c : zeroinfnan (MIT, see LICENSES.md) */
#include <stdint.h>
static inline int zeroinfnan(uint32_t ix)
{
	return 2 * ix - 1 >= 2u * 0x7f800000 - 1;
}
