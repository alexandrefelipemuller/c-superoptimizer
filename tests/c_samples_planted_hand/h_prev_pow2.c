int h_prev_pow2(int x) {
    unsigned v = (unsigned)x & 0x3fffffffu;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return (int)(v - (v >> 1));
}
