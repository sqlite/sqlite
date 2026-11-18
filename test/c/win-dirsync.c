/*
** 2026 November 15
**
** The author disclaims copyright to this source code.  In place of a
** legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
** This file contains a standalone program that verifies the win32 VFS
** directory-sync behavior:
**
**   1) The parent directory of a newly created journal is synced exactly
**      once, on the first xSync() of that file, and not on any later
**      xSync() of the same file.
**   2) xDelete() syncs the parent directory when the syncDir argument is
**      true, and does not when it is false.
**   3) A failing directory open or a failing FlushFileBuffers() is
**      reported as SQLITE_IOERR_DIR_FSYNC, and the failed sync stays
**      pending so that a later xSync() can retry it (both failure
**      modes are followed by a retry check).
**   4) A direct-call robustness test of the no-separator fallback: a bare
**      relative name that reaches winSyncDirectory() resolves to the current
**      working directory.  SQLite core never supplies such a name to
**      xOpen() - xFullPathname() normalizes input first, and the S8 checks
**      cover that supported call sequence.
**   5) When SQLITE_DISABLE_DIRSYNC or SQLITE_NO_SYNC is compiled in,
**      neither path syncs anything.
**   6) A Windows drive-relative name ("C:foo") is first normalized by
**      xFullPathname(), as SQLite requires, and the resulting full path is
**      then passed to xOpen()/xDelete().  winSyncDirectory() only ever sees
**      that full path, so it needs no drive-relative special case.
**
** The calls are counted by replacing CreateFileW and FlushFileBuffers
** through the win32 VFS xSetSystemCall() interface.  A directory handle is
** one that was opened with FILE_FLAG_BACKUP_SEMANTICS.
**
** Build against the amalgamation.  sqlite3.c is a generated file and is
** not checked into this branch; from a SQLite source tree generate it
** first (requires Tcl):
**
**   tclsh tool/mksqlite3c.tcl            # after target_source creates tsrc
**   (or: make sqlite3.c)
**
** then, from the directory holding sqlite3.c and sqlite3.h:
**
**   cl /nologo /O2 dirsyncprobe.c sqlite3.c /Fe:dirsyncprobe.exe
**
** The fork's CI uses a pre-generated amalgamation (sqlite3-patch.c plus
** sqlite3-base.c on the bench branch) instead, so the same compile line
** there reads "sqlite3-patch.c".  The program prints one line per check
** and exits with a non-zero status if any check fails.
*/
#include "sqlite3.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
** With SQLITE_DISABLE_DIRSYNC or SQLITE_NO_SYNC compiled in, neither the
** creation path nor the delete path is expected to sync a directory.
*/
#if defined(SQLITE_DISABLE_DIRSYNC) || defined(SQLITE_NO_SYNC)
# define EXPECT_DIR_FLUSH 0
#else
# define EXPECT_DIR_FLUSH 1
#endif

typedef HANDLE (WINAPI *PFNCREATEFILEW)(
  LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
typedef BOOL (WINAPI *PFNFLUSHFILEBUFFERS)(HANDLE);

static sqlite3_vfs *winVfs = 0;
static PFNCREATEFILEW realCreateFileW = 0;
static PFNFLUSHFILEBUFFERS realFlushFileBuffers = 0;

#define MAXDIRH 128
static HANDLE dirHandle[MAXDIRH];
static int nDirHandle = 0;
static int nDirOpen = 0;       /* Directory handles opened */
static int nDirFlush = 0;      /* FlushFileBuffers() calls on directory handles */
static int nFileFlush = 0;     /* FlushFileBuffers() calls on other handles */
static int failNextDirOpen = 0;
static int failNextDirFlush = 0;

static void resetCounters(void){
  nDirHandle = 0;
  nDirOpen = 0;
  nDirFlush = 0;
  nFileFlush = 0;
}

static HANDLE WINAPI probeCreateFileW(
  LPCWSTR zName,
  DWORD dwDesiredAccess,
  DWORD dwShareMode,
  LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  DWORD dwCreationDisposition,
  DWORD dwFlagsAndAttributes,
  HANDLE hTemplateFile
){
  int isDir = (dwFlagsAndAttributes & FILE_FLAG_BACKUP_SEMANTICS)!=0;
  HANDLE h;
  if( isDir ){
    nDirOpen++;
    if( failNextDirOpen ){
      failNextDirOpen = 0;
      SetLastError(ERROR_ACCESS_DENIED);
      return INVALID_HANDLE_VALUE;
    }
  }
  h = realCreateFileW(zName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                      dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
  if( isDir && h!=INVALID_HANDLE_VALUE && nDirHandle<MAXDIRH ){
    dirHandle[nDirHandle++] = h;
  }
  return h;
}

static BOOL WINAPI probeFlushFileBuffers(HANDLE h){
  int i;
  for(i=0; i<nDirHandle; i++){
    if( dirHandle[i]==h ){
      nDirFlush++;
      if( failNextDirFlush ){
        failNextDirFlush = 0;
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
      }
      return realFlushFileBuffers(h);
    }
  }
  nFileFlush++;
  return realFlushFileBuffers(h);
}

static int nFail = 0;

static void check(int cond, const char *zMsg){
  printf("%s %s\n", cond ? "ok  " : "FAIL", zMsg);
  fflush(stdout);
  if( !cond ) nFail++;
}

static void absName(const char *zRel, char *zOut, int nOut){
  DWORD n = GetCurrentDirectoryA((DWORD)nOut, zOut);
  if( n==0 || n>=(DWORD)nOut-2 ){
    memset(zOut, 0, (size_t)nOut);
    return;
  }
  if( zOut[n-1]!='\\' ){
    zOut[n++] = '\\';
    zOut[n] = 0;
  }
  strncat(zOut, zRel, (size_t)(nOut-n-1));
}

static sqlite3_file *openJournal(const char *zName){
  sqlite3_file *pFile;
  int rc;
  /* xOpen() expects a pointer to a caller-allocated sqlite3_file structure
  ** that is at least szOsFile bytes long.  Passing &pFile here (a
  ** sqlite3_file**) would make winOpen() overwrite the pointer variable
  ** with the whole winFile structure. */
  pFile = (sqlite3_file*)calloc(1, (size_t)winVfs->szOsFile);
  if( pFile==0 ){
    printf("FAIL out of memory\n");
    fflush(stdout);
    nFail++;
    return 0;
  }
  rc = winVfs->xOpen(winVfs, zName, pFile,
      SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_MAIN_JOURNAL, 0);
  if( rc!=SQLITE_OK ){
    printf("FAIL cannot open %s: %s\n", zName, sqlite3_errstr(rc));
    fflush(stdout);
    nFail++;
    free(pFile);
    return 0;
  }
  return pFile;
}

int main(int argc, char **argv){
  char zAbs1[600], zAbs2[600], zAbs3[600];
  sqlite3_file *pFile;
  int rc;

  (void)argc;
  (void)argv;

  winVfs = sqlite3_vfs_find("win32");
  if( winVfs==0 ){
    printf("FAIL no win32 VFS\n");
    return 1;
  }
  realCreateFileW = (PFNCREATEFILEW)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                   "CreateFileW");
  realFlushFileBuffers = (PFNFLUSHFILEBUFFERS)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                   "FlushFileBuffers");
  if( realCreateFileW==0 || realFlushFileBuffers==0 ){
    printf("FAIL GetProcAddress failed\n");
    return 1;
  }
  if( winVfs->xSetSystemCall(winVfs, "CreateFileW",
        (sqlite3_syscall_ptr)probeCreateFileW)!=SQLITE_OK ){
    printf("FAIL cannot override CreateFileW\n");
    return 1;
  }
  if( winVfs->xSetSystemCall(winVfs, "FlushFileBuffers",
        (sqlite3_syscall_ptr)probeFlushFileBuffers)!=SQLITE_OK ){
    printf("FAIL cannot override FlushFileBuffers\n");
    return 1;
  }

  absName("dirsync-probe1-journal", zAbs1, sizeof(zAbs1));
  absName("dirsync-probe2-journal", zAbs2, sizeof(zAbs2));
  absName("dirsync-probe3-journal", zAbs3, sizeof(zAbs3));

  /* S1: the first xSync() of a new journal syncs the parent directory. */
  resetCounters();
  pFile = openJournal(zAbs1);
  if( pFile ){
    rc = pFile->pMethods->xSync(pFile, SQLITE_SYNC_NORMAL);
    check( rc==SQLITE_OK, "S1.1 first xSync() of a new journal succeeds" );
    check( nDirFlush==EXPECT_DIR_FLUSH,
           "S1.2 the parent directory is synced by the first xSync()" );
    /* S2: a later xSync() of the same file must not sync it again. */
    rc = pFile->pMethods->xSync(pFile, SQLITE_SYNC_NORMAL);
    check( rc==SQLITE_OK, "S2.1 a second xSync() of the same journal succeeds" );
    check( nDirFlush==EXPECT_DIR_FLUSH,
           "S2.2 the parent directory is not synced again" );
    pFile->pMethods->xClose(pFile);
    free(pFile);
    pFile = 0;
  }

  /* S3: xDelete() with syncDir=1 syncs the parent directory. */
  resetCounters();
  rc = winVfs->xDelete(winVfs, zAbs1, 1);
  check( rc==SQLITE_OK, "S3.1 xDelete() with syncDir=1 succeeds" );
  check( nDirFlush==EXPECT_DIR_FLUSH,
         "S3.2 xDelete() with syncDir=1 syncs the parent directory" );

  /* S4: xDelete() with syncDir=0 never syncs a directory. */
  pFile = openJournal(zAbs2);
  if( pFile ){
    pFile->pMethods->xClose(pFile);
    free(pFile);
  }
  resetCounters();
  rc = winVfs->xDelete(winVfs, zAbs2, 0);
  check( rc==SQLITE_OK, "S4.1 xDelete() with syncDir=0 succeeds" );
  check( nDirFlush==0,
         "S4.2 xDelete() with syncDir=0 never syncs the parent directory" );

  /* S5: a failing directory open is reported, and the sync stays pending. */
  resetCounters();
  pFile = openJournal(zAbs3);
  if( pFile ){
    failNextDirOpen = 1;
    rc = pFile->pMethods->xSync(pFile, SQLITE_SYNC_NORMAL);
    if( EXPECT_DIR_FLUSH ){
      check( rc==SQLITE_IOERR_DIR_FSYNC,
             "S5.1 a failing directory open is reported as SQLITE_IOERR_DIR_FSYNC" );
      rc = pFile->pMethods->xSync(pFile, SQLITE_SYNC_NORMAL);
      check( rc==SQLITE_OK, "S5.2 the next xSync() retries the failed sync" );
      check( nDirFlush==1,
             "S5.3 the retry syncs the parent directory exactly once" );
    }else{
      check( rc==SQLITE_OK,
             "S5.1 no directory sync is attempted with the option compiled out (flag defined as 0)" );
    }
    pFile->pMethods->xClose(pFile);
    free(pFile);
    pFile = 0;
  }

  /* S6: a failing FlushFileBuffers() is reported as SQLITE_IOERR_DIR_FSYNC. */
  resetCounters();
  pFile = openJournal(zAbs3);
  if( pFile ){
    failNextDirFlush = 1;
    rc = pFile->pMethods->xSync(pFile, SQLITE_SYNC_NORMAL);
    if( EXPECT_DIR_FLUSH ){
      check( rc==SQLITE_IOERR_DIR_FSYNC,
             "S6.1 a failing FlushFileBuffers() is reported as SQLITE_IOERR_DIR_FSYNC" );
      /* The failed flush must leave WINFILE_DIRSYNC set, so the next
      ** xSync() retries it rather than reporting success without the
      ** directory having been synced. */
      rc = pFile->pMethods->xSync(pFile, SQLITE_SYNC_NORMAL);
      check( rc==SQLITE_OK,
             "S6.2 the failed directory flush is retried by the next xSync()" );
      check( nDirFlush==2,
             "S6.3 the retry performs exactly one more directory flush" );
    }else{
      check( rc==SQLITE_OK,
             "S6.1 no directory sync is attempted with the option compiled out (flag defined as 0)" );
    }
    pFile->pMethods->xClose(pFile);
    free(pFile);
    pFile = 0;
  }

  /* S7: a direct-call robustness test for the no-separator fallback in
  ** winSyncDirectory().  This is not a pathname that SQLite core supplies
  ** to xOpen(): the normal input is normalized by xFullPathname(), and the
  ** S8 checks cover that supported call sequence.  The assertions below
  ** only pin what the defensive L"." branch does when a bare relative name
  ** reaches it anyway. */
  resetCounters();
  pFile = openJournal("dirsync-probe-rel-journal");
  if( pFile ){
    rc = pFile->pMethods->xSync(pFile, SQLITE_SYNC_NORMAL);
    check( rc==SQLITE_OK, "S7.1 xSync() of a relative journal name succeeds" );
    check( nDirFlush==EXPECT_DIR_FLUSH,
           "S7.2 the current working directory is synced" );
    pFile->pMethods->xClose(pFile);
    free(pFile);
    pFile = 0;
  }
  resetCounters();
  rc = winVfs->xDelete(winVfs, "dirsync-probe-rel-journal", 1);
  check( rc==SQLITE_OK, "S7.3 xDelete() of a relative name succeeds" );
  check( nDirFlush==EXPECT_DIR_FLUSH,
         "S7.4 the current working directory is synced on delete" );

  /* S8: a Windows drive-relative name ("C:foo") is not a valid xOpen()
  ** input - SQLite requires xFullPathname() to have been applied first,
  ** and a drive-relative path's parent is that drive's current directory,
  ** not necessarily the process working directory.  Normalize it through
  ** the VFS before calling xOpen()/xDelete(), so this exercises the
  ** sequence SQLite actually uses.  No drive-relative special case is
  ** needed in winSyncDirectory() because it only ever sees the full path. */
  {
    char zFull[600];
    int isFull = 0;
    zFull[0] = 0;
    rc = winVfs->xFullPathname(winVfs, "C:dirsync-probe-drive-rel-journal",
                               (int)sizeof(zFull), zFull);
    check( rc==SQLITE_OK,
           "S8.1 xFullPathname() resolves a drive-relative name" );
    if( rc==SQLITE_OK ){
      isFull = strchr(zFull, ':')!=0 && strchr(zFull, '\\')!=0;
      check( isFull, "S8.2 the resolved name is a full path" );
    }
    /* zFull is not a valid string unless xFullPathname() succeeded and
    ** produced a full path.  Do not read or pass it on any other path. */
    if( isFull ){
      resetCounters();
      pFile = openJournal(zFull);
      if( pFile ){
        rc = pFile->pMethods->xSync(pFile, SQLITE_SYNC_NORMAL);
        check( rc==SQLITE_OK,
               "S8.3 xSync() of the resolved journal name succeeds" );
        check( nDirFlush==EXPECT_DIR_FLUSH,
               "S8.4 the parent directory of the resolved name is synced" );
        pFile->pMethods->xClose(pFile);
        free(pFile);
        pFile = 0;
      }
      resetCounters();
      rc = winVfs->xDelete(winVfs, zFull, 1);
      check( rc==SQLITE_OK,
             "S8.5 xDelete() of the resolved name succeeds" );
      check( nDirFlush==EXPECT_DIR_FLUSH,
             "S8.6 the parent directory is synced on delete" );
      winVfs->xDelete(winVfs, zFull, 0);
    }
  }

  /* Clean up. */
  winVfs->xDelete(winVfs, zAbs1, 0);
  winVfs->xDelete(winVfs, zAbs2, 0);
  winVfs->xDelete(winVfs, zAbs3, 0);
  winVfs->xDelete(winVfs, "dirsync-probe-rel-journal", 0);

  printf("%s: %d check(s) failed\n", nFail ? "FAILED" : "PASSED", nFail);
  fflush(stdout);
  return nFail ? 1 : 0;
}