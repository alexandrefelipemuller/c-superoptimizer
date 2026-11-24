int p_mod_sub(int x, int d) {
    if (d <= 0 || x < 0) return 0;
    return (x & 4095) % d;
}
