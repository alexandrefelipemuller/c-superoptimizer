/* Bits counted one at a time through a helper, the way it is often written. */
static int bit_at(int x, int i) { return (x >> i) & 1; }

int bit_count_helper(int x) {
    int c = 0;
    for (int i = 0; i < 32; i++)
        c += bit_at(x, i);
    return c;
}
