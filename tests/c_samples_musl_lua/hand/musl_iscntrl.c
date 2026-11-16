/* Log 198: the best version known for this sample, written by hand (not search output). */
int iscntrl(int c) { return ((int)((unsigned)c ^ 0x80000000u) < (int)(0x20u ^ 0x80000000u)) | (c == 0x7f); }
