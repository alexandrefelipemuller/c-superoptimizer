/* musl-1.2.5/src/prng/random.c : lcg31 (MIT, see LICENSES.md) */
#include <stdint.h>
static uint32_t lcg31(uint32_t x) {
	return (1103515245*x + 12345) & 0x7fffffff;
}
