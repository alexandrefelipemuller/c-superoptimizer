/* musl-1.2.5/src/crypt/crypt_md5.c : rol (MIT, see LICENSES.md) */
#include <stdint.h>
static uint32_t rol(uint32_t n, int k) { return (n << k) | (n >> (32-k)); }
