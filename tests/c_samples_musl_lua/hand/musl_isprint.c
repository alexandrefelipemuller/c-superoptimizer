/* Log 198: the best version known for this sample, written by hand (not search output). */
int isprint(int c) { return (int)(((unsigned)c - 0x20) ^ 0x80000000u) < (int)(0x5fu ^ 0x80000000u); }
