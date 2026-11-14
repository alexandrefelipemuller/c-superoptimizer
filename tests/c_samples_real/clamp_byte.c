/* Phase 4a: small static helpers, inlined. Clamps to a byte. */
static inline int min_i(int a, int b) { return a < b ? a : b; }
static inline int max_i(int a, int b) { return a > b ? a : b; }

int clamp_byte(int x) {
    return min_i(max_i(x, 0), 255);
}
