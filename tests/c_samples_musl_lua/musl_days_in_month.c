/* musl-1.2.5/src/time/__tz.c : days_in_month (MIT, see LICENSES.md) */
static int days_in_month(int m, int is_leap)
{
	if (m==2) return 28+is_leap;
	else return 30+((0xad5>>(m-1))&1);
}
