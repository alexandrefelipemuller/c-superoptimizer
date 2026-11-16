/* Log 198: the best version known for this sample, written by hand (not search output). */
int isgraph(int c) { return (int)(((unsigned)c - 0x21) ^ 0x80000000u) < (int)(0x5eu ^ 0x80000000u); }
