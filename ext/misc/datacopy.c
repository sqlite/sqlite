/*
** 2025-07-25
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This SQLite extension implements the DATACOPY collating sequence
** which always returns zero.
**
** Hints for compiling:
**
**   Linux:    gcc -shared -fPIC -o datacopy.so datacopy.c
**   OSX:      gcc -dynamiclib -fPIC -o datacopy.dylib datacopy.c
**   Windows:  cl datacopy.c -link -dll -out:datacopy.dll
*/
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <assert.h>
#include <string.h>

/*
** Implement the DATACOPY collating sequence so that if
**
**      x=y COLLATE DATACOPY
**
** always returns zero (meaning that x is always equal to y).
*/
static int datacopyCollFunc(
  void *notUsed,
  int nKey1, const void *pKey1,
  int nKey2, const void *pKey2
){
  (void)notUsed;
  (void)nKey1;
  (void)pKey1;
  (void)nKey2;
  (void)pKey2;
  return 0;
}


#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_datacopy_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  rc = sqlite3_create_collation(db, "DATACOPY", SQLITE_UTF8,
                                0, datacopyCollFunc);
  return rc;
}
