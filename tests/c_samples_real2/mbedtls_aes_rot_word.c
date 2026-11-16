/* mbedtls-3.6.2/library/aesce.c : aes_rot_word (see LICENSES.md) */
#include <stdint.h>
static inline uint32_t aes_rot_word(uint32_t word)
{
    return (word << (32 - 8)) | (word >> 8);
}
