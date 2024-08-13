/*
** 2024-08-13
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** A program to test I/O through a VFS implementations.
**
** This program is intended as a template for custom test module used
** to verify ports of SQLite to non-standard platforms.  Typically
** porting SQLite to a new platform requires writing a new VFS object
** to implement the I/O methods required by SQLite.  This file contains
** code that attempts exercise such a VFS object and verify that it is
** operating correctly.
*/
#include "sqlite3.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Number of elements in static array X */
#define COUNT(X)  (sizeof(X)/sizeof(X[0]))

/*
** State of the tester.
*/
typedef struct IOTester IOTester;
struct IOTester {
  int pgsz;                    /* Databases should use this page size */
  const char *zJMode;          /* Databases should use this journal mode */
  int isExclusive;             /* True for exclusive locking mode */
  sqlite3_int64 szMMap;        /* Memory-map size */
  const char *zTestModule;     /* Test module currently running */
  int iTestNum;                /* Test number currently running */
  int nTest;                   /* Total number of tests run */
  int nFault;                  /* Number of test failures */
  int nOOM;                    /* Out of memory faults */
  int eVerbosity;              /* How much output */
  sqlite3_str *errTxt;         /* Error text written here */
};


/* Forward declarations of subroutines and functions. */
void iotestError(IOTester*,const char *zFormat, ...);
sqlite3 *iotestOpenDb(IOTester*,const char *zDbName);
void iotestUnlink(IOTester *p, const char *zFilename);
void iotestBeginTest(IOTester *p, int iTestnum);


void iotestBasic1(IOTester*);


/********************************** Utilities *******************************
**
** Functions intended for common use by all test modules.
*/

/*
** Record a test failure.
*/
void iotestError(IOTester *p, const char *zFormat, ...){
  va_list ap;
  p->nFault++;
  if( p->errTxt==0 ){
    p->errTxt = sqlite3_str_new(0);
    if( p==0 ){
      p->nOOM++;
      return;
    }
  }
  sqlite3_str_appendf(p->errTxt,
     "FAULT: %s-%d pgsz=%d journal-mode=%s mmap-size=%lld",
     p->zTestModule, p->iTestNum,
     p->pgsz, p->zJMode, p->szMMap);
  if( p->isExclusive ){
     sqlite3_str_appendf(p->errTxt, " exclusive-locking-mode");
  }
  sqlite3_str_appendf(p->errTxt, "\n");
  if( zFormat ){
    va_start(ap, zFormat);
    sqlite3_str_vappendf(p->errTxt, zFormat, ap);
    va_end(ap);
  }
}

/*
** Create a new prepared statement based on SQL text.
*/
sqlite3_stmt *iotestVPrepare(
  IOTester *p,
  sqlite3 *db,
  const char *zFormat,
  va_list ap
){
  char *zSql = 0;
  int rc;
  sqlite3_stmt *pStmt = 0;
  if( db==0 ) return 0;
  zSql = sqlite3_vmprintf(zFormat, ap);
  if( zSql==0 ){
    p->nOOM++;
    return 0;
  }
  rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
  if( rc!=SQLITE_OK || pStmt==0 ){
    iotestError(p, "unable to prepare statement: \"%s\"\n", zSql);
    sqlite3_free(zSql);
    sqlite3_finalize(pStmt);
  }
  return pStmt;
}


/*
** Run a statement against a database.  Expect no results.
*/
void iotestRun(
  IOTester *p,
  sqlite3 *db,
  const char *zSql,
  ...
){
  va_list ap;
  sqlite3_stmt *pStmt = 0;
  if( db==0 ) return;
  va_start(ap, zSql);
  pStmt = iotestVPrepare(p, db, zSql, ap);
  va_end(ap);
  if( pStmt ){
    int rc;
    while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ){}
    if( rc==SQLITE_ERROR ){
      iotestError(p, "error running SQL statement %s: %s\n",
                   sqlite3_sql(pStmt), sqlite3_errmsg(db));
    }
  }
  sqlite3_finalize(pStmt);
}

/*
** Run a query against a database that returns a single integer.
*/
sqlite3_int64 iotestQueryInt(
  IOTester *p,
  sqlite3 *db,
  sqlite3_int64 iDflt,
  const char *zSql,
  ...
){
  va_list ap;
  sqlite3_stmt *pStmt = 0;
  sqlite3_int64 res = iDflt;
  int rc;
  if( db==0 ) return iDflt;
  va_start(ap, zSql);
  pStmt = iotestVPrepare(p, db, zSql, ap);
  va_end(ap);
  if( pStmt==0 ){
    return iDflt;
  }
  rc = sqlite3_step(pStmt);
  if( rc==SQLITE_ROW ){
    res = sqlite3_column_int64(pStmt, 0);
  }else if( rc==SQLITE_ERROR ){
    iotestError(p, "error while running \"%s\": %s\n",
                 sqlite3_sql(pStmt), sqlite3_errmsg(db));
    res = iDflt;
  }
  sqlite3_finalize(pStmt);
  return res;
}

/*
** Delete a file by name using the xDelete method of the default VFS.
*/
void iotestDeleteFile(IOTester *p, const char *zFilename){
  sqlite3_vfs *pVfs = sqlite3_vfs_find(0);
  int rc;
  assert( pVfs!=0 );
  assert( pVfs->xDelete!=0 );
  rc = pVfs->xDelete(pVfs, zFilename, 0);
  if( rc!=SQLITE_OK && rc!=SQLITE_IOERR_DELETE_NOENT ){
    iotestError(p, "cannot delete file \"%s\"\n", zFilename);
  }
}

/*
** Open a database.  Return a pointer to that database connection,
** or if something goes wrong, return a NULL pointer.
**
** If the database does not previously exist, then do all the necessary
** setup as proscribed by the IOTester configuration.
*/
sqlite3 *iotestOpen(IOTester *p, const char *zDbFilename){
  sqlite3 *db;
  int rc;

  rc = sqlite3_open(zDbFilename, &db);
  if( rc!=SQLITE_OK ){
    iotestError(p, "cannot open database \"%s\"\n", zDbFilename);
    sqlite3_close(db);
    return 0;
  }
  if( iotestQueryInt(p, db, -1, "PRAGMA page_count")==0 ){
    iotestRun(p, db, "PRAGMA page_size=%d", p->pgsz);
    iotestRun(p, db, "PRAGMA journal_mode=%s", p->zJMode);
    iotestRun(p, db, "PRAGMA mmap_size=%lld", p->szMMap);
    if( p->isExclusive ){
      iotestRun(p, db, "PRAGMA locking_mode=EXCLUSIVE;");
    }
    iotestRun(p, db, "PRAGMA cache_size=2;");
  }
  return db;
}


/*********************************** Test Modules ****************************
*/
void iotestBasic1(IOTester *p){
  sqlite3 *db = 0;
  p->zTestModule = "basic1";
  iotestBeginTest(p, 1);
  iotestDeleteFile(p, "basic1.db");
  if( p->nFault ) return;
  iotestBeginTest(p, 2);
  db = iotestOpen(p, "basic1.db");
  if( p->nFault ) goto basic1_exit;
  iotestBeginTest(p, 3);
  iotestRun(p, db, "CREATE TABLE t1(a,b,c);");
  if( p->nFault ) goto basic1_exit;
  iotestBeginTest(p, 4);
  iotestRun(p, db, "DROP TABLE t1;");

basic1_exit:
  sqlite3_close(db);
  iotestDeleteFile(p, "basic1.db");
}

/********************** Out-Of-Band System Interactions **********************
**
** Any customizations that need to be made to port this test program to
** new platforms are made below this point.
**
** These functions access the filesystem directly or make other system
** interactions that are non-portable.  Modify these routines as necessary
** to work on alternative operating systems.
*/
#include <stdio.h>

/*
** Start a new test case.  (This is under the "Out-of-band" section because
** of its optional use of printf().)
*/
void iotestBeginTest(IOTester *p, int iTN){
  p->iTestNum = iTN;
  p->nTest++;
  if( p->eVerbosity>=2 ) printf("%s-%d\n", p->zTestModule, iTN);
}

/*********************************** Main ************************************
**
** The main test driver.
*/
int main(int argc, char **argv){
  IOTester x;
  int i;
  static const int aPgsz[] = {
     512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
  };
  static const char *azJMode[] = {
     "delete", "truncate", "persist", "wal"
  };
  static const sqlite3_int64 aszMMap[] = {
     0, 2097152, 2147483648
  };
  memset(&x, 0, sizeof(x));
  for(i=1; i<argc; i++){
    const char *z = argv[i];
    if( z[0]=='-' && z[1]=='-' && z[2]!=0 ) z++;
    if( strcmp(z,"-v")==0 ){
      x.eVerbosity++;
    }else if( strcmp(z, "-vv")==0 ){
      x.eVerbosity += 2;
    }else if (strcmp(z, "-vfs")==0 && i<argc-1 ){
      int rc;
      sqlite3_vfs *pNew = sqlite3_vfs_find(argv[++i]);
      if( pNew==0 ){
        iotestError(&x, "No such VFS: \"%s\"\n", argv[i]);
        break;
      }
      rc = sqlite3_vfs_register(pNew, 1);
      if( rc!=SQLITE_OK ){
        iotestError(&x, "Unable to register VFS \"%s\" - result code %d\n",
                     argv[i], rc);
        break;
      }
    }else{
      iotestError(&x, "unknown option: \"%s\"\n", argv[i]);
      break;
    }
  }
  for(i=0; x.nFault==0 && i<COUNT(aPgsz)*COUNT(azJMode)*COUNT(aszMMap)*2; i++){
    int j,k;
    j = i;
    k = j % COUNT(aPgsz);
    x.pgsz = aPgsz[k];
    j /= COUNT(aPgsz);
    k = j % COUNT(azJMode);
    x.zJMode = azJMode[k];
    j /= COUNT(azJMode);
    k = j % COUNT(aszMMap);
    x.szMMap = aszMMap[k];
    j /= COUNT(aszMMap);
    k = j % 2;
    x.isExclusive = k;
    j /= 2;

    if( x.eVerbosity>=1 ){
      printf("pgsz=%d journal_mode=%s mmap_size=%lld exclusive=%d\n",
             x.pgsz, x.zJMode, x.szMMap, x.isExclusive);
    }

    iotestBasic1(&x);
    if( x.nFault ) break;
  }

  /* Report results */
  printf("%d tests and %d errors\n", x.nTest, x.nFault);
  if( x.nOOM ) printf("%d out-of-memory faults\n", x.nOOM);
  if( x.errTxt ){
    char *zMsg = sqlite3_str_finish(x.errTxt);
    if( zMsg ){
      printf("%s", zMsg);
      sqlite3_free(zMsg);
    }
  }
  return x.nFault>0; 
}
