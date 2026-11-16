/* musl ldso/dynlink.c gnu_hash, ported by hand to the int-array ABI (log 199). */
int gnu_hash_bytes(int *a, int n)
{
	unsigned h = 5381;
	int i = 0;
	for (; a[i]; i++)
		h += h * 32 + a[i];
	return h;
}
