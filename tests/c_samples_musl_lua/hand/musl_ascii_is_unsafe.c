/* Log 198: the best version known for this sample, written by hand (not search output). */
static inline int ascii_is_unsafe(unsigned char ch) { return ch < 64 && ((0x0400000000000401ULL >> ch) & 1); }
