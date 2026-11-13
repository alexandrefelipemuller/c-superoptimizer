/* Population count by clearing the lowest set bit (Kernighan). */
int popcount_loop(int x) {
    int c = 0;
    while (x != 0) {
        x &= x - 1;
        c++;
    }
    return c;
}
