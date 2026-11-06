/*
** 2026
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
** A small standalone smoke test for the Windows VFS layer, with an
** emphasis on the legacy Windows code paths.
**
** The restored Windows 9x/ME and NT4/2000 code paths in os_win.c are
** selected at run time by osIsNT().  On any Windows release from XP
** onward osIsNT() always returns true, so the ANSI (A-suffixed) branches
** are dead code there and are never exercised by the normal test suite.
**
** os_win.c exports the process-global sqlite3_os_type variable when it is
** built with SQLITE_TEST, and the comment there notes that the test fixture
** "can manually set this value to 1 to emulate Win98 behavior".  That is
** the switch used here.  SQLITE_WIN32_GETVERSIONEX=1 is also required,
** because on modern SDKs GetVersionEx() is deprecated and SQLITE_WIN32_
** GETVERSIONEX therefore defaults to 0, which makes osIsNT() the constant
** (1) and bypasses sqlite3_os_type entirely.
**
** Build the normal (NT) variant:
**
**     cl -DSQLITE_OS_WIN=1 -DSQLITE_THREADSAFE=1 -I. -Isrc \
**        test\testopen.c sqlite3.c /link /MACHINE:x86
**
** Build the variant that can reach the legacy Win9x ANSI branches:
**
**     cl -DSQLITE_OS_WIN=1 -DSQLITE_THREADSAFE=1 -DSQLITE_TEST \
**        -DSQLITE_WIN32_GETVERSIONEX=1 -I. -Isrc \
**        test\testopen.c sqlite3.c /link /MACHINE:x86
**
** Run:
**
**     testopen.exe [--ansi] [--wal] <database-file>
**
** Options:
**
**     --ansi    Emulate Win98 by setting sqlite3_os_type=1 before the
**               first database access.  Requires SQLITE_TEST.
**     --wal     Also switch the database into WAL mode
*/
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "sqlite3.h"

#if defined(SQLITE_TEST)
/*
** Exported by os_win.c under SQLITE_TEST.  0 = unknown, 1 = Win9x,
** 2 = WinNT.  Setting it to 1 before the first database access makes
** osIsNT() false and therefore routes all VFS work through the ANSI
** (A-suffixed) Windows APIs.
*/
extern volatile long sqlite3_os_type;
#endif

static int nFail = 0;

static void fail(const char *zWhat, int rc, const char *zErr){
  printf("FAIL: %s: rc=%d %s\n", zWhat, rc, zErr?zErr:"");
  fflush(stdout);
  nFail++;
}

static const char *exName(DWORD c){
  switch(c){
    case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_OVERFLOW:          return "FLT_OVERFLOW";
    default:                              return "OTHER";
  }
}

/*
** Structured exception filter.  Without this an access violation inside
** the VFS simply kills the process and reports nothing useful, which is
** exactly how the GetFileSizeEx/SetFilePointerEx calling-convention bug
** first showed up.
*/
static int WINAPI sehFilter(EXCEPTION_POINTERS *ep){
  DWORD code = ep->ExceptionRecord->ExceptionCode;
  printf("\n*** EXCEPTION %s (0x%08lX) at addr=%p", exName(code),
         (unsigned long)code, ep->ExceptionRecord->ExceptionAddress);
  if( code==EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters>=2 ){
    printf(" %s of addr=%p",
           ep->ExceptionRecord->ExceptionInformation[0]?"WRITE":"READ",
           (void*)ep->ExceptionRecord->ExceptionInformation[1]);
  }
  printf("\n");
  fflush(stdout);
  nFail++;
  return EXCEPTION_EXECUTE_HANDLER;
}

static int countCb(void *pNotUsed, int nArg, char **azArg, char **azCol){
  int i;
  (void)pNotUsed;
  for(i=0; i<nArg; i++){
    printf("  %s = %s\n", azCol[i], azArg[i]?azArg[i]:"NULL");
  }
  return 0;
}

static void run(sqlite3 *db, const char *zWhat, const char *zSql){
  char *zErr = 0;
  int rc;
  __try{
    rc = sqlite3_exec(db, zSql, countCb, 0, &zErr);
  }__except(sehFilter(GetExceptionInformation())){
    printf("  (crashed in %s)\n", zWhat);
    return;
  }
  if( rc!=SQLITE_OK ){
    fail(zWhat, rc, zErr);
  }else{
    printf("ok: %s\n", zWhat);
  }
  sqlite3_free(zErr);
  fflush(stdout);
}

int main(int argc, char **argv){
  const char *zDb = 0;
  int bAnsi = 0;
  int bWal = 0;
  sqlite3 *db = 0;
  int rc;
  int i;

  for(i=1; i<argc; i++){
    if( strcmp(argv[i], "--ansi")==0 ){
      bAnsi = 1;
    }else if( strcmp(argv[i], "--wal")==0 ){
      bWal = 1;
    }else{
      zDb = argv[i];
    }
  }
  if( zDb==0 ){
    printf("usage: %s [--ansi] [--wal] <database-file>\n", argv[0]);
    return 1;
  }

  printf("sqlite3 %s (%d-bit)\n", sqlite3_libversion(),
         (int)(sizeof(void*)*8));
  if( bAnsi ){
#if defined(SQLITE_TEST)
    /*
    ** Must be done before anything touches the VFS.  osIsNT() evaluates
    ** ((sqlite3_os_type==2) || sqlite3_win32_is_nt()), and
    ** sqlite3_win32_is_nt() short-circuits on a non-zero sqlite3_os_type,
    ** so a value of 1 makes osIsNT() false.
    */
    sqlite3_os_type = 1;
    printf("mode: Win98 emulation, legacy ANSI code paths (osIsNT()==0)\n");
#else
    printf("mode: --ansi requires the amalgamation to be built with "
           "-DSQLITE_TEST -DSQLITE_WIN32_GETVERSIONEX=1\n");
    return 1;
#endif
  }else{
    printf("mode: default (NT code paths)\n");
  }
  fflush(stdout);

  __try{
    rc = sqlite3_open(zDb, &db);
  }__except(sehFilter(GetExceptionInformation())){
    printf("crashed in sqlite3_open\n");
    return 2;
  }
  printf("sqlite3_open rc=%d\n", rc);
  fflush(stdout);
  if( rc!=SQLITE_OK ){
    fail("sqlite3_open", rc, db?sqlite3_errmsg(db):0);
    sqlite3_close(db);
    return 1;
  }

  run(db, "CREATE TABLE",
      "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT, c BLOB);");
  run(db, "INSERT", "INSERT INTO t(b,c) VALUES('one',x'001122'),"
                    "('two',x'334455'),('three',x'66778899');");
  run(db, "UPDATE", "UPDATE t SET b=b||'-x' WHERE a=2;");
  run(db, "SELECT", "SELECT a,b,hex(c) FROM t ORDER BY a;");
  run(db, "DELETE", "DELETE FROM t WHERE a=3;");
  run(db, "SELECT after DELETE", "SELECT count(*),group_concat(b) FROM t;");
  run(db, "REINDEX", "REINDEX;");
  run(db, "VACUUM", "VACUUM;");
  run(db, "PRAGMA integrity_check", "PRAGMA integrity_check;");

  if( bWal ){
    run(db, "journal_mode=WAL", "PRAGMA journal_mode=WAL;");
    run(db, "INSERT in WAL", "INSERT INTO t(b) VALUES('wal');");
    run(db, "SELECT in WAL", "SELECT count(*) FROM t;");
    run(db, "journal_mode=DELETE", "PRAGMA journal_mode=DELETE;");
  }

  __try{
    rc = sqlite3_close(db);
  }__except(sehFilter(GetExceptionInformation())){
    printf("crashed in sqlite3_close\n");
    return 3;
  }
  printf("sqlite3_close rc=%d\n", rc);
  if( rc!=SQLITE_OK ) fail("sqlite3_close", rc, 0);

  if( nFail==0 ){
    printf("ALL PASSED\n");
    return 0;
  }
  printf("%d FAILURE(S)\n", nFail);
  return 1;
}
