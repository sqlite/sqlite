
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sqlite3.h"

int main(void){
#ifdef SQLITE_ENABLE_SESSION
  sqlite3 *db;
  sqlite3_changegroup *pGrp;
  char *zErr = 0;
  char *buf = malloc(64);
  int rc = SQLITE_OK;

  sqlite3_open(":memory:", &db);
  sqlite3_exec(db, "CREATE TABLE t1(a INTEGER PRIMARY KEY, b TEXT);", 0, 0, 0);

  sqlite3changegroup_new(&pGrp);
  sqlite3changegroup_schema(pGrp, db, "main");
  sqlite3changegroup_change_begin(pGrp, SQLITE_INSERT, "t1", 0, &zErr);
  sqlite3changegroup_change_int64(pGrp, 1, 0, 42);

  memset(buf, 'X', 64);

  /* This should return an OOM error: */
  rc = sqlite3changegroup_change_blob(pGrp, 1, 1, buf, 2147483647);

  free(buf);
  sqlite3changegroup_delete(pGrp);
  sqlite3_close(db);
  return (rc==7) ? 0 : -1;
#else
  return 0;
#endif
}
