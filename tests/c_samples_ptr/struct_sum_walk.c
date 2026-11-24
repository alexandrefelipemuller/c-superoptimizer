/* Log 208d: walk an array of structs with a pointer to its end. */
struct Item {
    int key;
    int value;
    int weight;
};
int struct_sum_walk(const struct Item *it, int n) {
    const struct Item *end = it + n;
    int s = 0;
    for (const struct Item *p = it; p < end; p++) s += p->value * p->weight;
    return s;
}
