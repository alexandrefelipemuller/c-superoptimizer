int h_parity8(int x) {
    unsigned v = (unsigned)x & 0xffu;
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (int)(v & 1u);
}
