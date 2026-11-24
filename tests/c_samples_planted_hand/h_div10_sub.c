int h_div10_sub(int x) {
    if (x < 0) return 0;
    return (x & 8191) / 10;
}
