int h_popcount16(int x) {
    unsigned v = (unsigned)x & 0xffffu;
    v = v - ((v >> 1) & 0x5555u);
    v = (v & 0x3333u) + ((v >> 2) & 0x3333u);
    v = (v + (v >> 4)) & 0x0f0fu;
    return (int)((v + (v >> 8)) & 0x1fu);
}
