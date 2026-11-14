/* Saturating addition with INT_MAX / INT_MIN from <limits.h>. */
#include <limits.h>

int sat_add(int a, int b) {
    if (a > 0 && b > INT_MAX - a)
        return INT_MAX;
    if (a < 0 && b < INT_MIN - a)
        return INT_MIN;
    return a + b;
}
