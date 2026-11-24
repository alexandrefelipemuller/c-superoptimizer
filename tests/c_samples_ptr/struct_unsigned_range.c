/* Log 208d: unsigned fields, (*r).f access. */
typedef struct Range {
    unsigned lo;
    unsigned hi;
} Range;
int struct_unsigned_range(const Range *r) {
    if ((*r).hi < (*r).lo) return 0;
    return (int)((*r).hi - r->lo);
}
