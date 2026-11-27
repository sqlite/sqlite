/*
** A negative blob length passed to sqlite3changegroup_change_blob() must
** be rejected with SQLITE_RANGE.  C is needed because the TCL interface
** always derives the length from a byte array and so cannot pass one.
** Bug report https://sqlite.org/bugs/info/b624e45831f7a55e
*/
#include <stdio.h>
#include <limits.h>
#include "sqlite3.h"

int main(void){
#ifdef SQLITE_ENABLE_SESSION
  static const struct { int nVal; int rcExpect; } aTest[] = {
    { 0,       SQLITE_OK    },
    { 4,       SQLITE_OK    },
    { -1,      SQLITE_RANGE },
    { -2,      SQLITE_RANGE },
    { INT_MIN, SQLITE_RANGE },
  };
  sqlite3 *db;
  sqlite3_changegroup *pGrp;
  char *zErr = 0;
  const char *zVal = "abcd";
  int i;
  int nErr = 0;

  sqlite3_open(":memory:", &db);
  sqlite3_exec(db, "CREATE TABLE t1(a INTEGER PRIMARY KEY, b BLOB);", 0, 0, 0);
  sqlite3changegroup_new(&pGrp);
  sqlite3changegroup_schema(pGrp, db, "main");
  sqlite3changegroup_change_begin(pGrp, SQLITE_INSERT, "t1", 0, &zErr);
  sqlite3changegroup_change_int64(pGrp, 1, 0, 42);

  for(i=0; i<(int)(sizeof(aTest)/sizeof(aTest[0])); i++){
    int rc = sqlite3changegroup_change_blob(pGrp, 1, 1, zVal, aTest[i].nVal);
    if( rc!=aTest[i].rcExpect ){
      printf("nVal=%d: expected rc=%d, got rc=%d\n",
             aTest[i].nVal, aTest[i].rcExpect, rc);
      nErr++;
    }
  }

  sqlite3changegroup_delete(pGrp);
  sqlite3_close(db);
  return nErr ? 1 : 0;
#else
  return 0;
#endif
}
