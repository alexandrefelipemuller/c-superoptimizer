/* sq204b/sqlite3.c:72501 in freeSpace: iFreeBlk && iEnd + 3 >= iFreeBlk */
static int region(unsigned short iFreeBlk, unsigned iEnd) {
    return iFreeBlk && iEnd + 3 >= iFreeBlk;
}
