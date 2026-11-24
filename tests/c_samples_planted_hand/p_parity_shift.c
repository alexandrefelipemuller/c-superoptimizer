int p_parity_shift(int x) {
    unsigned v = (unsigned)x;
    v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (int)(v & 1u);
}
