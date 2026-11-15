/* musl-1.2.5/src/time/strftime.c : is_leap (MIT, see LICENSES.md) */
#include <limits.h>
static int is_leap(int y)
{
	/* Avoid overflow */
	if (y>INT_MAX-1900) y -= 2000;
	y += 1900;
	return !(y%4) && ((y%100) || !(y%400));
}
