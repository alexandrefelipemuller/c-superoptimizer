/* Log 208d: a void function that writes struct fields. */
struct Point {
    int x;
    int y;
};
void struct_update(struct Point *p, int n) {
    for (int i = 0; i < n; i++) {
        p[i].x *= 2;
        p[i].y += p[i].x;
    }
}
