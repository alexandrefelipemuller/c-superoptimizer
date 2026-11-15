/* musl-1.2.5/src/crypt/crypt_sha256.c : ror (MIT, see LICENSES.md) */
#include <stdint.h>
static uint32_t ror(uint32_t n, int k) { return (n >> k) | (n << (32-k)); }
