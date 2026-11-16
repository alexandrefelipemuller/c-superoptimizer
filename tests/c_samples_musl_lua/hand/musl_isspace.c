/* Log 198: the best version known for this sample, written by hand (not search output). */
int isspace(int c) { unsigned d = (unsigned)c - 1; return d < 32 && ((0x80001F00u >> d) & 1); }
