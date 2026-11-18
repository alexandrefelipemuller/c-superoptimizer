/* src_musl/musl-1.2.5/src/thread/pthread_rwlock_unlock.c:11 in __pthread_rwlock_unlock: (cnt == 0x7fffffff || cnt == 1) ? 0 : val - 1 */
static int region(int cnt, int val) {
    return (cnt == 0x7fffffff || cnt == 1) ? 0 : val - 1;
}
