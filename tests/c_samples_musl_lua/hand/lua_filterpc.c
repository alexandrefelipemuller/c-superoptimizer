/* Log 198: the best version known for this sample, written by hand (not search output). */
static int filterpc (int pc, int jmptarget) { return pc | -(pc < jmptarget); }
