/* Constants from #define, `continue` inside a for loop with a step. */
#define LOW 10
#define HIGH 20

int count_in_range(const int *a, int n) {
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (a[i] < LOW)
            continue;
        if (a[i] > HIGH)
            continue;
        count++;
    }
    return count;
}
