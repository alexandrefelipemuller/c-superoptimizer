/* musl-1.2.5/src/prng/rand_r.c : temper (MIT, see LICENSES.md) */
static unsigned temper(unsigned x)
{
	x ^= x>>11;
	x ^= x<<7 & 0x9D2C5680;
	x ^= x<<15 & 0xEFC60000;
	x ^= x>>18;
	return x;
}
