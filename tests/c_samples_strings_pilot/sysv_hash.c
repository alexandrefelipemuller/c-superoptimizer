/* musl ldso/dynlink.c sysv_hash, ported by hand to the int-array ABI (log 199);
 * uint_fast32_t's upper bits never reach the 28-bit result, so 32 bits suffice. */
int sysv_hash_bytes(int *a, int n)
{
	unsigned h = 0;
	int i = 0;
	while (a[i]) {
		h = 16 * h + a[i]; i++;
		h ^= h >> 24 & 0xf0;
	}
	return h & 0xfffffff;
}
