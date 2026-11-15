/* musl-1.2.5/src/locale/__mo_lookup.c : swapc (MIT, see LICENSES.md) */
#include <stdint.h>
static inline uint32_t swapc(uint32_t x, int c)
{
	return c ? x>>24 | x>>8&0xff00 | x<<8&0xff0000 | x<<24 : x;
}
