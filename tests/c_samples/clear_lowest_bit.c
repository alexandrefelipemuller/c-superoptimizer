/* Hacker's Delight 2-1: turn off the rightmost 1-bit. */
int clear_lowest_bit(int x) { return x & (x - 1); }
