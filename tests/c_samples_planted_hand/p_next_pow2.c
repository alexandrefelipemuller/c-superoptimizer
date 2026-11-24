int p_next_pow2(int x) {
    x &= 0x3fffffff;
    if (x <= 1) return 1;
    unsigned v = (unsigned)x - 1;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return (int)(v + 1);
}
