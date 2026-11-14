/* 8-bit additive checksum, the mask kept after every step. */
#define MASK 0xFF

int checksum8(const int *a, int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum = (sum + (a[i] & MASK)) & MASK;
    }
    return sum;
}
