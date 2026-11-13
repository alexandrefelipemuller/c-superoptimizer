/* Writes its length parameter: exercises the private copy of v0. */
int countdown_n(int *a, int n) {
    int s = 0;
    while (n > 0) {
        n--;
        s = s * 31 + a[n];
    }
    return s;
}
