/* Log 198: the best version known for this sample, written by hand (not search output). */
static int days_in_month(int m, int is_leap)
{
	return 28 + (int)((0xaaaaaaaaaaeefbb3ULL >> (2 * m - 2)) & 3) + (m == 2 ? is_leap : 0);
}
