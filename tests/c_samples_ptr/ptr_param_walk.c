/* Log 208d: the parameter pointer itself moves. */
int ptr_param_walk(const int *a, int n) {
    int best = 0;
    while (n > 0) {
        if (*a > best) best = *a;
        a++;
        n--;
    }
    return best;
}
