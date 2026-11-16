/* Log 198: the best version known for this sample, written by hand (not search output). */
int isalpha(int c) { return (int)((((unsigned)c | 32) - 'a') ^ 0x80000000u) < (int)(26u ^ 0x80000000u); }
