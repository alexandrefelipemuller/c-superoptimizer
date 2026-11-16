/* sqlite-3.46.0 amalgamation sqlite3.c : memsys3Roundup (see LICENSES.md) */
static int memsys3Roundup(int n){
  if( n<=12 ){
    return 12;
  }else{
    return ((n+11)&~7) - 4;
  }
}
