/* src200/sqlite-amalgamation-3460000/sqlite-amalgamation-3460000/shell.c:10154 in zipfileMtimeToDos: (mUnixTime % (24 * 60 * 60)) / (60 * 60) */
static unsigned region(unsigned mUnixTime) {
    return (mUnixTime % (24 * 60 * 60)) / (60 * 60);
}
