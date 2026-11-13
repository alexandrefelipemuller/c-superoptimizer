/* Floor average without overflow (Hacker's Delight 2-5). */
int avg_floor(int x, int y) { return (x & y) + ((x ^ y) >> 1); }
