/* Log 198: the best version known for this sample, written by hand (not search output). */
#include <stdint.h>
static inline uint32_t swapc(uint32_t x, int c) { return c ? __builtin_bswap32(x) : x; }
