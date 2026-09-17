#include "sqlite3.h"
#include <stdio.h>

int main(void) {
  const char *zExpect = "2023.000";
  char szBuffer[32];
  double val = 2023.0;
#ifdef SQLITE_OMIT_AUTOINIT
  sqlite3_initialize();
#endif
  sqlite3_snprintf(17, szBuffer, "%.3f", val);
  printf("size 17: '%s'\n", szBuffer);
  if( sqlite3_stricmp(zExpect, szBuffer) ) return 1;
  sqlite3_snprintf(16, szBuffer, "%.3f", val);
  printf("size 16: '%s'\n", szBuffer);
  if( sqlite3_stricmp(zExpect, szBuffer) ) return 1;
  return 0;
}
