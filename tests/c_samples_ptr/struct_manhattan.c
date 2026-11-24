/* Log 208d: one struct by pointer. */
struct Point {
    int x;
    int y;
};
int struct_manhattan(const struct Point *p) {
    int dx = p->x, dy = p->y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx + dy;
}
