/*
** When initialization of the vfsstat extension fails, it must not leave
** its VFS registered or its auto-extension installed.  Otherwise, once
** sqlite3_load_extension() unloads the failed extension, the next
** database open calls into the unloaded code.  C is needed because the
** failure is an OOM that only a custom allocator can inject.
** Bug report https://sqlite.org/bugs/info/4a07d3f1c7
**
** The extension is compiled into this program, so the test checks that
** sqlite3_vfsstat_init() undoes its registrations after each fault.
*/
#include <stdio.h>
#include "sqlite3.h"
#ifndef SQLITE_OMIT_VIRTUALTABLE
# define SQLITE_CORE 1
# include "../../ext/misc/vfsstat.c"

static sqlite3_mem_methods origMethods;
static struct {
  int active;
  int at;
  int count;
} Fault = {0,0,0};

/* Return true if the current allocation should fail. */
static int allocShouldFail(void){
  return Fault.active && ++Fault.count==Fault.at;
}
static void *fiMalloc(int n){
  return allocShouldFail() ? 0 : origMethods.xMalloc(n);
}
static void fiFree(void *p){
  origMethods.xFree(p);
}
static void *fiRealloc(void *p, int n){
  return allocShouldFail() ? 0 : origMethods.xRealloc(p, n);
}
static int fiSize(void *p){
  return origMethods.xSize(p);
}
static int fiRoundup(int n){
  return origMethods.xRoundup(n);
}
static int fiInit(void *p){
  return origMethods.xInit(p);
}
static void fiShutdown(void *p){
  origMethods.xShutdown(p);
}

/* Return true if connection db can see the vfsstat table. */
static int hasVfsstat(sqlite3 *db){
  sqlite3_stmt *pStmt = 0;
  int rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM vfsstat", -1,
                              &pStmt, 0);
  sqlite3_finalize(pStmt);
  return rc==SQLITE_OK;
}

static int nErr = 0;
#endif /* SQLITE_OMIT_VIRTUALTABLE */

int main(void){
#ifndef SQLITE_OMIT_VIRTUALTABLE
  sqlite3_mem_methods mm;
  sqlite3_vfs *pOrig;
  sqlite3 *db = 0;
  sqlite3 *db2 = 0;
  char *zErr;
  int rc;

#define CHECK(X) if( !(X) ){                      \
  printf("fault %d, line %d: check failed: %s\n", \
         Fault.at, __LINE__, #X);                  \
  nErr++;                                         \
}

  sqlite3_shutdown();
  rc = sqlite3_config(SQLITE_CONFIG_GETMALLOC, &origMethods);
  CHECK( rc==0 );
  mm.xMalloc   = fiMalloc;
  mm.xFree     = fiFree;
  mm.xRealloc  = fiRealloc;
  mm.xSize     = fiSize;
  mm.xRoundup  = fiRoundup;
  mm.xInit     = fiInit;
  mm.xShutdown = fiShutdown;
  mm.pAppData  = origMethods.pAppData;

  if( sqlite3_config(SQLITE_CONFIG_MALLOC, &mm)!=SQLITE_OK ) return 1;
  if( sqlite3_initialize()!=SQLITE_OK ) return 1;
  if( sqlite3_open(":memory:", &db)!=SQLITE_OK ) return 1;
  pOrig = sqlite3_vfs_find(0);
  CHECK( !!pOrig );

  /* Fail each allocation made by the initializer in turn, until it
  ** succeeds.  Each failure must leave no trace behind. */
  for(Fault.at=1; Fault.at<1000; Fault.at++){
    zErr = 0;
    Fault.count = 0;
    Fault.active = 1;
    rc = sqlite3_vfsstat_init(db, &zErr, 0);
    Fault.active = 0;
    sqlite3_free(zErr);
    if( rc==SQLITE_OK || rc==SQLITE_OK_LOAD_PERMANENTLY ) break;

    CHECK( sqlite3_vfs_find(0)==pOrig );
    CHECK( sqlite3_vfs_find("vfslog")==0 );
    CHECK( !hasVfsstat(db) );
    CHECK( sqlite3_open(":memory:", &db2)==SQLITE_OK );
    CHECK( !hasVfsstat(db2) );
    sqlite3_close(db2);
    db2 = 0;
  }
  CHECK( Fault.at>1 && Fault.at<1000 );

  /* After a successful initialization, vfslog is the default VFS and
  ** new connections see the vfsstat table. */
  CHECK( sqlite3_vfs_find(0)==sqlite3_vfs_find("vfslog") );
  CHECK( hasVfsstat(db) );
  CHECK( 0==db2 );
  CHECK( sqlite3_open(":memory:", &db2)==SQLITE_OK );
  CHECK( hasVfsstat(db2) );
  sqlite3_close(db2);
  sqlite3_close(db);

  printf("%d faults injected, %d errors\n", Fault.at-1, nErr);
  return nErr!=0;
#else
  return 0;
#endif
}
