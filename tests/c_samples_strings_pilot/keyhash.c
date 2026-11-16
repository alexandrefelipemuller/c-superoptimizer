/* musl src/search/hsearch.c keyhash, ported by hand to the int-array ABI with
 * a 32-bit hash (log 199; musl's is size_t). */
int keyhash_bytes(int *a, int n)
{
	unsigned h = 0;
	int i = 0;
	while (a[i])
		{ h = 31 * h + a[i]; i++; }
	return h;
}
