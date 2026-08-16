
#include "sqlite3.h"
#include <stdio.h>

int main(void) {
  void *p = 0;
#ifdef SQLITE_OMIT_AUTOINIT
  sqlite3_initialize();
#endif
  p = sqlite3_malloc(32);
  if( !p ) return 1;
  sqlite3_free(p);
  return 0;
}
