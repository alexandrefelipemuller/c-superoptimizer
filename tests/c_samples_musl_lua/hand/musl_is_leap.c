/* Log 198: the best version known for this sample, written by hand (not search output). */
#include <limits.h>
static int is_leap(int y)
{
	if (y>INT_MAX-1900) y -= 2000;
	y += 1900;
	return !(y & ((y % 25) ? 3 : 15));
}
