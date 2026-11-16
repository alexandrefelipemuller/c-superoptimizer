/* musl src/stdlib/atoi.c with ctype.h's isspace/isdigit inlined and the
 * switch as ifs, ported by hand to the int-array ABI (log 199). */
int atoi_bytes(int *a, int n)
{
	int i = 0, v = 0, neg = 0;
	while (a[i] == ' ' || (unsigned)a[i] - '\t' < 5) i++;
	if (a[i] == '-') { neg = 1; i++; }
	else if (a[i] == '+') i++;
	/* Compute v as a negative number to avoid overflow on INT_MIN */
	while ((unsigned)a[i] - '0' < 10)
		{ v = 10 * v - (a[i] - '0'); i++; }
	return neg ? v : -v;
}
