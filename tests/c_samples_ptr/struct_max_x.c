/* Log 208d: array of typedef'd structs, p[i].x. */
typedef struct {
    int x, y;
} Pt;
int struct_max_x(const Pt *p, int n) {
    if (n <= 0) return 0;
    int m = p[0].x;
    for (int i = 1; i < n; i++)
        if (p[i].x > m) m = p[i].x;
    return m;
}
