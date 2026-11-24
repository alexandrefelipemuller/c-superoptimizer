int h_reverse16(int x) {
    unsigned v = (unsigned)x & 0xffffu;
    v = ((v >> 1) & 0x5555u) | ((v & 0x5555u) << 1);
    v = ((v >> 2) & 0x3333u) | ((v & 0x3333u) << 2);
    v = ((v >> 4) & 0x0f0fu) | ((v & 0x0f0fu) << 4);
    return (int)(((v >> 8) | (v << 8)) & 0xffffu);
}
