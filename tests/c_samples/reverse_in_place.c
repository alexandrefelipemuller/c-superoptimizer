void reverse_in_place(int *a, int n) {
    int i = 0, j = n - 1;
    while (i < j) {
        int t = a[i];
        a[i] = a[j];
        a[j] = t;
        i++;
        j--;
    }
}
