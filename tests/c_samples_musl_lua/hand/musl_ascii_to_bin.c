/* Log 198: the best version known for this sample, written by hand (not search output). */
#include <stdint.h>
static uint32_t ascii_to_bin(int ch)
{
	int sch = (ch < 0x80) ? ch : ch - 0x100;
	return (uint32_t)(sch - '.' - 7 * (sch >= 'A') - 6 * (sch >= 'a')) & 0x3f;
}
