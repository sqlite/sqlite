/*
** 2026-01-05
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
** This file implements a VFS shim that writes a timestamp and other tracing
** information into reserved space at the end of each page of the database
** file.  The additional data is written as the page is added to the WAL
** file for databases in WAL mode, or as the database file itself is modified
** in rollback modes.
**
** COMPILING
**
** To build this extension as a separately loaded shared library or
** DLL, use compiler command-lines similar to the following:
**
**   (linux)    gcc -fPIC -shared tmstmpvfs.c -o tmstmpvfs.so
**   (mac)      clang -fPIC -dynamiclib tmstmpvfs.c -o tmstmpvfs.dylib
**   (windows)  cl tmstmpvfs.c -link -dll -out:tmstmpvfs.dll
**
** You may want to add additional compiler options, of course,
** according to the needs of your project.
**
** LOADING
**
** To load this extension as a shared library, you first have to
** bring up a dummy SQLite database connection to use as the argument
** to the sqlite3_load_extension() API call.  Then you invoke the
** sqlite3_load_extension() API and shutdown the dummy database
** connection.  All subsequent database connections that are opened
** will include this extension.  For example:
**
**     sqlite3 *db;
**     sqlite3_open(":memory:", &db);
**     sqlite3_load_extension(db, "./tmstmpvfs");
**     sqlite3_close(db);
**
** Tmstmpvfs is a VFS Shim. When loaded, "tmstmpvfs" becomes the new
** default VFS and it uses the prior default VFS as the next VFS
** down in the stack.  This is normally what you want.  However, in
** complex situations where multiple VFS shims are being loaded,
** it might be important to ensure that tmstmpvfs is loaded in the
** correct order so that it sequences itself into the default VFS
** Shim stack in the right order.
**
** USING
**
** Open database connections using the sqlite3_open() or 
** sqlite3_open_v2() interfaces, as normal.  Ordinary database files
** (without a timestamp) will operate normally.
**
** Timestamping only works on databases that have a reserve-bytes
** value of exactly 16.  The default value for reserve-bytes is 0.
** Hence, newly created database files will omit the timestamp by
** default.  To create a database that includes a timestamp, change
** the reserve-bytes value to 16 by running:
**
**    int n = 16;
**    sqlite3_file_control(db, 0, SQLITE_FCNTL_RESERVE_BYTES, &n);
**
** If you do this immediately after creating a new database file,
** before anything else has been written into the file, then that
** might be all that you need to do.  Otherwise, the API call
** above should be followed by:
**
**    sqlite3_exec(db, "VACUUM", 0, 0, 0);
**
** It never hurts to run the VACUUM, even if you don't need it.
**
** From the CLI, use the ".filectrl reserve_bytes 16" command, 
** followed by "VACUUM;".
**
** Note that SQLite allows the number of reserve-bytes to be
** increased but not decreased.  So if a database file already
** has a reserve-bytes value greater than 16, there is no way to
** activate timestamping on that database, other than to dump
** and restore the database file.  Note also that other extensions
** might also make use of the reserve-bytes.  Timestamping will
** be incompatible with those other extensions.
**
** IMPLEMENTATION NOTES
**
** The timestamp information is stored in the last 16 bytes of each page.
** This module only operates if the "bytes of reserved space on each page"
** value at offset 20 the SQLite database header is exactly 16.  If
** the reserved-space value is not 16, this module is a no-op.
**
** The timestamp layout is as follows:
**
**   bytes 0,1     Zero.  Reserved for future expansion
**   bytes 2-7     Milliseconds since the Unix Epoch
**   bytes 8-11    WAL frame number
**   bytes 12      0: WAL write  1: WAL txn  2: rollback write
**   bytes 13-15   Lower 24 bits of Salt-1
**
** For transactions that occur in rollback mode, bytes 6-15 are all
** zero.
**
** LOGGING
**
** Every open VFS creates a log file.  The Database connection and the
** WAL connection are both logged to the same file.  If the name of the
** database file is BASE then the logfile is named something like:
**
**     BASE-tmstmp-UNIQUEID
**
** Where UNIQUEID is a character string that is unique to that particular
** connection.  The log consists of 16-byte records.  Each record consists
** of five unsigned integers:
**
**      1   1   6    4   4   <---  bytes
**     op  a1  ts   a2  a3
**
** The meanings of the a1-a3 values depend on op.  ts is the timestamp
** in milliseconds since 1970-01-01.  (6 bytes is sufficient for timestamps
** for almost 9000 years.) Opcodes are defined by the ELOG_* #defines
** below.
*/
#if defined(SQLITE_AMALGAMATION) && !defined(SQLITE_TMSTMPVFS_STATIC)
# define SQLITE_TMSTMPVFS_STATIC
#endif
#ifdef SQLITE_TMSTMPVFS_STATIC
# include "sqlite3.h"
#else
# include "sqlite3ext.h"
  SQLITE_EXTENSION_INIT1
#endif
#include <string.h>
#include <assert.h>
#include <stdio.h>

/*
** Forward declaration of objects used by this utility
*/
typedef struct sqlite3_vfs TmstmpVfs;
typedef struct TmstmpFile TmstmpFile;

/*
** Bytes of reserved space used by this extension
*/
#define TMSTMP_RESERVE 16

/*
** The magic number used to identify TmstmpFile objects
*/
#define TMSTMP_MAGIC 0x2a87b72d

/*
** Useful datatype abbreviations
*/
#if !defined(SQLITE_AMALGAMATION)
  typedef unsigned char u8;
  typedef unsigned int u32;
#endif

/*
** Current process id
*/
#if defined(_WIN32)
#  include <windows.h>
#  define GETPID (u32)GetCurrentProcessId()
#else
#  include <unistd.h>
#  define GETPID (u32)getpid()
#endif

/* Access to a lower-level VFS that (might) implement dynamic loading,
** access to randomness, etc.
*/
#define ORIGVFS(p)  ((sqlite3_vfs*)((p)->pAppData))
#define ORIGFILE(p) ((sqlite3_file*)(((TmstmpFile*)(p))+1))

/* An open WAL file */
struct TmstmpFile {
  sqlite3_file base;     /* IO methods */
  u32 uMagic;            /* Magic number for sanity checking */
  u32 salt1;             /* Last WAL salt-1 value */
  u32 iFrame;            /* Last WAL frame number */
  u32 pgno;              /* Current page number */
  u32 pgsz;              /* Size of each page, in bytes */
  u8 isWal;              /* True if this is a WAL file */
  u8 isDb;               /* True if this is a DB file */
  u8 isCommit;           /* Last WAL frame header was a transaction commit */
  u8 hasCorrectReserve;  /* File has the correct reserve size */
  u8 inCkpt;             /* True if in a checkpoint */
  FILE *log;             /* Write logging records on this file.  DB only */
  TmstmpFile *pPartner;  /* DB->WAL or WAL->DB mapping */
  sqlite3_int64 iOfst;   /* Offset of last WAL frame header */
  sqlite3_vfs *pSubVfs;  /* Underlying VFS */
};

/*
** Event log opcodes
*/
#define ELOG_OPEN_DB      0x01
#define ELOG_OPEN_WAL     0x02
#define ELOG_WAL_PAGE     0x03
#define ELOG_DB_PAGE      0x04
#define ELOG_CKPT_START   0x05
#define ELOG_CKPT_PAGE    0x06
#define ELOG_CKPT_DONE    0x07
#define ELOG_WAL_RESET    0x08
#define ELOG_CLOSE_WAL    0x0e
#define ELOG_CLOSE_DB     0x0f

/*
** Methods for TmstmpFile
*/
static int tmstmpClose(sqlite3_file*);
static int tmstmpRead(sqlite3_file*, void*, int iAmt, sqlite3_int64 iOfst);
static int tmstmpWrite(sqlite3_file*,const void*,int iAmt, sqlite3_int64 iOfst);
static int tmstmpTruncate(sqlite3_file*, sqlite3_int64 size);
static int tmstmpSync(sqlite3_file*, int flags);
static int tmstmpFileSize(sqlite3_file*, sqlite3_int64 *pSize);
static int tmstmpLock(sqlite3_file*, int);
static int tmstmpUnlock(sqlite3_file*, int);
static int tmstmpCheckReservedLock(sqlite3_file*, int *pResOut);
static int tmstmpFileControl(sqlite3_file*, int op, void *pArg);
static int tmstmpSectorSize(sqlite3_file*);
static int tmstmpDeviceCharacteristics(sqlite3_file*);
static int tmstmpShmMap(sqlite3_file*, int iPg, int pgsz, int, void volatile**);
static int tmstmpShmLock(sqlite3_file*, int offset, int n, int flags);
static void tmstmpShmBarrier(sqlite3_file*);
static int tmstmpShmUnmap(sqlite3_file*, int deleteFlag);
static int tmstmpFetch(sqlite3_file*, sqlite3_int64 iOfst, int iAmt, void **pp);
static int tmstmpUnfetch(sqlite3_file*, sqlite3_int64 iOfst, void *p);

/*
** Methods for TmstmpVfs
*/
static int tmstmpOpen(sqlite3_vfs*, const char *, sqlite3_file*, int , int *);
static int tmstmpDelete(sqlite3_vfs*, const char *zName, int syncDir);
static int tmstmpAccess(sqlite3_vfs*, const char *zName, int flags, int *);
static int tmstmpFullPathname(sqlite3_vfs*, const char *zName, int, char *zOut);
static void *tmstmpDlOpen(sqlite3_vfs*, const char *zFilename);
static void tmstmpDlError(sqlite3_vfs*, int nByte, char *zErrMsg);
static void (*tmstmpDlSym(sqlite3_vfs *pVfs, void *p, const char*zSym))(void);
static void tmstmpDlClose(sqlite3_vfs*, void*);
static int tmstmpRandomness(sqlite3_vfs*, int nByte, char *zOut);
static int tmstmpSleep(sqlite3_vfs*, int microseconds);
static int tmstmpCurrentTime(sqlite3_vfs*, double*);
static int tmstmpGetLastError(sqlite3_vfs*, int, char *);
static int tmstmpCurrentTimeInt64(sqlite3_vfs*, sqlite3_int64*);
static int tmstmpSetSystemCall(sqlite3_vfs*, const char*,sqlite3_syscall_ptr);
static sqlite3_syscall_ptr tmstmpGetSystemCall(sqlite3_vfs*, const char *z);
static const char *tmstmpNextSystemCall(sqlite3_vfs*, const char *zName);

static sqlite3_vfs tmstmp_vfs = {
  3,                            /* iVersion (set when registered) */
  0,                            /* szOsFile (set when registered) */
  1024,                         /* mxPathname */
  0,                            /* pNext */
  "tmstmpvfs",                  /* zName */
  0,                            /* pAppData (set when registered) */ 
  tmstmpOpen,                   /* xOpen */
  tmstmpDelete,                 /* xDelete */
  tmstmpAccess,                 /* xAccess */
  tmstmpFullPathname,           /* xFullPathname */
  tmstmpDlOpen,                 /* xDlOpen */
  tmstmpDlError,                /* xDlError */
  tmstmpDlSym,                  /* xDlSym */
  tmstmpDlClose,                /* xDlClose */
  tmstmpRandomness,             /* xRandomness */
  tmstmpSleep,                  /* xSleep */
  tmstmpCurrentTime,            /* xCurrentTime */
  tmstmpGetLastError,           /* xGetLastError */
  tmstmpCurrentTimeInt64,       /* xCurrentTimeInt64 */
  tmstmpSetSystemCall,          /* xSetSystemCall */
  tmstmpGetSystemCall,          /* xGetSystemCall */
  tmstmpNextSystemCall          /* xNextSystemCall */
};

static const sqlite3_io_methods tmstmp_io_methods = {
  3,                            /* iVersion */
  tmstmpClose,                  /* xClose */
  tmstmpRead,                   /* xRead */
  tmstmpWrite,                  /* xWrite */
  tmstmpTruncate,               /* xTruncate */
  tmstmpSync,                   /* xSync */
  tmstmpFileSize,               /* xFileSize */
  tmstmpLock,                   /* xLock */
  tmstmpUnlock,                 /* xUnlock */
  tmstmpCheckReservedLock,      /* xCheckReservedLock */
  tmstmpFileControl,            /* xFileControl */
  tmstmpSectorSize,             /* xSectorSize */
  tmstmpDeviceCharacteristics,  /* xDeviceCharacteristics */
  tmstmpShmMap,                 /* xShmMap */
  tmstmpShmLock,                /* xShmLock */
  tmstmpShmBarrier,             /* xShmBarrier */
  tmstmpShmUnmap,               /* xShmUnmap */
  tmstmpFetch,                  /* xFetch */
  tmstmpUnfetch                 /* xUnfetch */
};

/*
** Write a 6-byte millisecond timestamp into aOut[]
*/
static void tmstmpPutTS(TmstmpFile *p, unsigned char *aOut){
  sqlite3_uint64 tm = 0;
  p->pSubVfs->xCurrentTimeInt64(p->pSubVfs, &tm);
  aOut[0] = (tm>>40)&0xff;
  aOut[1] = (tm>>32)&0xff;
  aOut[2] = (tm>>24)&0xff;
  aOut[3] = (tm>>16)&0xff;
  aOut[4] = (tm>>8)&0xff;
  aOut[5] = tm&0xff;
}

/*
** Read a 32-bit big-endian unsigned integer and return it.
*/
static u32 tmstmpGetU32(const unsigned char *a){
  return (a[0]<<24) + (a[1]<<16) + (a[2]<<8) + a[3];
}

/* Write a 32-bit integer as big-ending into a[]
*/
static void tmstmpPutU32(u32 v, unsigned char *a){
  a[0] = (v>>24) & 0xff;
  a[1] = (v>>16) & 0xff;
  a[2] = (v>>8) & 0xff;
  a[3] = v & 0xff;
}


/*
** Write a record onto the event log
*/
static void tmstmpEvent(
  TmstmpFile *p,
  u8 op,
  u8 a1,
  u32 a2,
  u32 a3
){
  unsigned char a[16];
  if( p->isWal ){
    p = p->pPartner;
    assert( p!=0 );
    assert( p->isDb );
  }
  if( p->log==0 ) return;
  a[0] = op;
  a[1] = a1;
  tmstmpPutTS(p, a+2);
  tmstmpPutU32(a2, a+8);
  tmstmpPutU32(a3, a+12);
  (void)fwrite(a, sizeof(a), 1, p->log);
}

/*
** Close a connection
*/
static int tmstmpClose(sqlite3_file *pFile){
  TmstmpFile *p = (TmstmpFile *)pFile;
  if( p->hasCorrectReserve ){
    tmstmpEvent(p, p->isDb ? ELOG_CLOSE_DB : ELOG_CLOSE_WAL, 0, 0, 0);
  }
  if( p->log ) fclose(p->log);
  if( p->pPartner ){
    assert( p->pPartner->pPartner==p );
    p->pPartner->pPartner = 0;
    p->pPartner = 0;
  }
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xClose(pFile);
}

/*
** Read bytes from a file
*/
static int tmstmpRead(
  sqlite3_file *pFile, 
  void *zBuf, 
  int iAmt, 
  sqlite_int64 iOfst
){
  int rc;
  TmstmpFile *p = (TmstmpFile*)pFile;
  pFile = ORIGFILE(pFile);
  rc = pFile->pMethods->xRead(pFile, zBuf, iAmt, iOfst);
  if( rc!=SQLITE_OK ) return rc;
  if( p->isDb
   && iOfst==0
   && iAmt>=100
  ){
    const unsigned char *a = (unsigned char*)zBuf;
    p->hasCorrectReserve = (a[20]==TMSTMP_RESERVE);
    p->pgsz = (a[16]<<8) + a[17];
    if( p->pgsz==1 ) p->pgsz = 65536;
    if( p->pPartner ){
      p->pPartner->hasCorrectReserve = p->hasCorrectReserve;
      p->pPartner->pgsz = p->pgsz;
    }
  }
  return rc;
}

/*
** Write data to a tmstmp-file.
*/
static int tmstmpWrite(
  sqlite3_file *pFile,
  const void *zBuf,
  int iAmt,
  sqlite_int64 iOfst
){
  TmstmpFile *p = (TmstmpFile*)pFile;
  sqlite3_file *pSub = ORIGFILE(pFile);
  if( !p->hasCorrectReserve ){
    /* The database does not have the correct reserve size.  No-op */
  }else if( p->isWal ){
    /* Writing into a WAL file */
    if( iAmt==24 ){
      /* A frame header */
      u32 x = 0;
      p->iFrame = (iOfst - 32)/(p->pgsz+24);
      p->pgno = tmstmpGetU32((const u8*)zBuf);
      p->salt1 = tmstmpGetU32(((const u8*)zBuf)+8);
      memcpy(&x, ((const u8*)zBuf)+4, 4);
      p->isCommit = (x!=0);
      p->iOfst = iOfst;
    }else if( iAmt>=512 && iOfst==p->iOfst+24 ){
      unsigned char *s = (unsigned char*)zBuf+iAmt-TMSTMP_RESERVE;
      memset(s, 0, TMSTMP_RESERVE);
      tmstmpPutTS(p, s+2);
      tmstmpPutU32(p->iFrame, s+8);
      tmstmpPutU32(p->salt1, s+12);
      s[12] = p->isCommit ? 1 : 0;
      tmstmpEvent(p, ELOG_WAL_PAGE, s[12], p->pgno, p->iFrame);
    }else if( iAmt==32 && iOfst==0 ){
      u32 salt1 = tmstmpGetU32(((const u8*)zBuf)+16);
      tmstmpEvent(p, ELOG_WAL_RESET, 0, 0, salt1);
    }
  }else if( p->inCkpt ){
    assert( p->pgsz>0 );
    tmstmpEvent(p, ELOG_CKPT_PAGE, 0, iOfst/p->pgsz, 0);
  }else if( p->pPartner==0 ){
    /* Writing into a database in rollback mode */
    unsigned char *s = (unsigned char*)zBuf+iAmt-TMSTMP_RESERVE;
    memset(s, 0, TMSTMP_RESERVE);
    tmstmpPutTS(p, s+2);
    s[12] = 2;
    assert( p->pgsz>0 );
    tmstmpEvent(p, ELOG_DB_PAGE, 0, (u32)(iOfst/p->pgsz), 0);
  }
  return pSub->pMethods->xWrite(pSub,zBuf,iAmt,iOfst);
}

/*
** Truncate a tmstmp-file.
*/
static int tmstmpTruncate(sqlite3_file *pFile, sqlite_int64 size){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xTruncate(pFile, size);
}

/*
** Sync a tmstmp-file.
*/
static int tmstmpSync(sqlite3_file *pFile, int flags){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xSync(pFile, flags);
}

/*
** Return the current file-size of a tmstmp-file.
*/
static int tmstmpFileSize(sqlite3_file *pFile, sqlite_int64 *pSize){
  TmstmpFile *p = (TmstmpFile *)pFile;
  pFile = ORIGFILE(p);
  return pFile->pMethods->xFileSize(pFile, pSize);
}

/*
** Lock a tmstmp-file.
*/
static int tmstmpLock(sqlite3_file *pFile, int eLock){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xLock(pFile, eLock);
}

/*
** Unlock a tmstmp-file.
*/
static int tmstmpUnlock(sqlite3_file *pFile, int eLock){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xUnlock(pFile, eLock);
}

/*
** Check if another file-handle holds a RESERVED lock on a tmstmp-file.
*/
static int tmstmpCheckReservedLock(sqlite3_file *pFile, int *pResOut){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xCheckReservedLock(pFile, pResOut);
}

/*
** File control method. For custom operations on a tmstmp-file.
*/
static int tmstmpFileControl(sqlite3_file *pFile, int op, void *pArg){
  int rc;
  TmstmpFile *p = (TmstmpFile*)pFile;
  pFile = ORIGFILE(pFile);
  rc = pFile->pMethods->xFileControl(pFile, op, pArg);
  if( rc==SQLITE_OK ){
    switch( op ){
      case SQLITE_FCNTL_VFSNAME: {
        if( p->hasCorrectReserve ){
          *(char**)pArg = sqlite3_mprintf("tmstmp/%z", *(char**)pArg);
        }
        break;
      }
      case SQLITE_FCNTL_CKPT_START: {
        p->inCkpt = 1;
        assert( p->isDb );
        assert( p->pPartner!=0 );
        p->pPartner->inCkpt = 1;
        if( p->hasCorrectReserve ){
          tmstmpEvent(p, ELOG_CKPT_START, 0, 0, 0);
        }
        break;
      }
      case SQLITE_FCNTL_CKPT_DONE: {
        p->inCkpt = 0;
        assert( p->isDb );
        assert( p->pPartner!=0 );
        p->pPartner->inCkpt = 0;
        if( p->hasCorrectReserve ){
          tmstmpEvent(p, ELOG_CKPT_DONE, 0, 0, 0);
        }
        break;
      }
    }
  }
  return rc;
}

/*
** Return the sector-size in bytes for a tmstmp-file.
*/
static int tmstmpSectorSize(sqlite3_file *pFile){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xSectorSize(pFile);
}

/*
** Return the device characteristic flags supported by a tmstmp-file.
*/
static int tmstmpDeviceCharacteristics(sqlite3_file *pFile){
  int devchar = 0;
  pFile = ORIGFILE(pFile);
  devchar = pFile->pMethods->xDeviceCharacteristics(pFile);
  return (devchar & ~SQLITE_IOCAP_SUBPAGE_READ);
}

/* Create a shared memory file mapping */
static int tmstmpShmMap(
  sqlite3_file *pFile,
  int iPg,
  int pgsz,
  int bExtend,
  void volatile **pp
){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xShmMap(pFile,iPg,pgsz,bExtend,pp);
}

/* Perform locking on a shared-memory segment */
static int tmstmpShmLock(sqlite3_file *pFile, int offset, int n, int flags){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xShmLock(pFile,offset,n,flags);
}

/* Memory barrier operation on shared memory */
static void tmstmpShmBarrier(sqlite3_file *pFile){
  pFile = ORIGFILE(pFile);
  pFile->pMethods->xShmBarrier(pFile);
}

/* Unmap a shared memory segment */
static int tmstmpShmUnmap(sqlite3_file *pFile, int deleteFlag){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xShmUnmap(pFile,deleteFlag);
}

/* Fetch a page of a memory-mapped file */
static int tmstmpFetch(
  sqlite3_file *pFile,
  sqlite3_int64 iOfst,
  int iAmt,
  void **pp
){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xFetch(pFile, iOfst, iAmt, pp);
}

/* Release a memory-mapped page */
static int tmstmpUnfetch(sqlite3_file *pFile, sqlite3_int64 iOfst, void *pPage){
  pFile = ORIGFILE(pFile);
  return pFile->pMethods->xUnfetch(pFile, iOfst, pPage);
}


/*
** Open a tmstmp file handle.
*/
static int tmstmpOpen(
  sqlite3_vfs *pVfs,
  const char *zName,
  sqlite3_file *pFile,
  int flags,
  int *pOutFlags
){
  TmstmpFile *p, *pDb;
  sqlite3_file *pSubFile;
  sqlite3_vfs *pSubVfs;
  int rc;
  sqlite3_uint64 r1;
  u32 r2;
  u32 pid;
  char *zLogName;
  
  pSubVfs = ORIGVFS(pVfs);
  if( (flags & (SQLITE_OPEN_MAIN_DB|SQLITE_OPEN_WAL))==0 ){
    /* If the file is not a persistent database or a WAL file, then
    ** bypass the timestamp logic all together */
    return pSubVfs->xOpen(pSubVfs, zName, pFile, flags, pOutFlags);
  }
  if( (flags & SQLITE_OPEN_WAL)!=0 ){
    pDb = (TmstmpFile*)sqlite3_database_file_object(zName);
    if( pDb==0
     || pDb->uMagic!=TMSTMP_MAGIC
     || !pDb->isDb
     || pDb->pPartner!=0
    ){
      return pSubVfs->xOpen(pSubVfs, zName, pFile, flags, pOutFlags);
    }
  }else{
    pDb = 0;
  }
  p = (TmstmpFile*)pFile;
  memset(p, 0, sizeof(*p));
  pSubFile = ORIGFILE(pFile);
  pFile->pMethods = &tmstmp_io_methods;
  p->pSubVfs = pSubVfs;
  p->uMagic = TMSTMP_MAGIC;
  rc = pSubVfs->xOpen(pSubVfs, zName, pSubFile, flags, pOutFlags);
  if( rc ) goto tmstmp_open_done;
  if( pDb!=0 ){
    p->isWal = 1;
    p->pPartner = pDb;
    pDb->pPartner = p;
  }else{
    p->isDb = 1;
    r1 = 0;
    p->pSubVfs->xCurrentTimeInt64(p->pSubVfs, &r1);
    sqlite3_randomness(sizeof(r2), &r2);
    pid = GETPID;
    zLogName = sqlite3_mprintf("%s-%llx%08x%08x.tmstmp", zName, r1, pid, r2);
    if( zLogName ){
      p->log = fopen(zLogName, "wb");
      sqlite3_free(zLogName);
    }
  }
  tmstmpEvent(p, p->isWal ? ELOG_OPEN_WAL : ELOG_OPEN_DB, 0, GETPID, 0);

tmstmp_open_done:
  if( rc ) pFile->pMethods = 0;
  return rc;
}

/*
** All VFS interfaces other than xOpen are passed down into the Sub-VFS.
*/
static int tmstmpDelete(sqlite3_vfs *p, const char *zName, int syncDir){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xDelete(pSub,zName,syncDir);
}
static int tmstmpAccess(sqlite3_vfs *p, const char *zName, int flags, int *pR){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xAccess(pSub,zName,flags,pR);
}
static int tmstmpFullPathname(sqlite3_vfs*p,const char *zName,int n,char *zOut){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xFullPathname(pSub,zName,n,zOut);
}
static void *tmstmpDlOpen(sqlite3_vfs *p, const char *zFilename){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xDlOpen(pSub,zFilename);
}
static void tmstmpDlError(sqlite3_vfs *p, int nByte, char *zErrMsg){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xDlError(pSub,nByte,zErrMsg);
}
static void(*tmstmpDlSym(sqlite3_vfs *p, void *pDl, const char *zSym))(void){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xDlSym(pSub,pDl,zSym);
}
static void tmstmpDlClose(sqlite3_vfs *p, void *pDl){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xDlClose(pSub,pDl);
}
static int tmstmpRandomness(sqlite3_vfs *p, int nByte, char *zOut){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xRandomness(pSub,nByte,zOut);
}
static int tmstmpSleep(sqlite3_vfs *p, int microseconds){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xSleep(pSub,microseconds);
}
static int tmstmpCurrentTime(sqlite3_vfs *p, double *prNow){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xCurrentTime(pSub,prNow);
}
static int tmstmpGetLastError(sqlite3_vfs *p, int a, char *b){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xGetLastError(pSub,a,b);
}
static int tmstmpCurrentTimeInt64(sqlite3_vfs *p, sqlite3_int64 *piNow){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xCurrentTimeInt64(pSub,piNow);
}
static int tmstmpSetSystemCall(sqlite3_vfs *p, const char *zName,
                               sqlite3_syscall_ptr x){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xSetSystemCall(pSub,zName,x);
}
static sqlite3_syscall_ptr tmstmpGetSystemCall(sqlite3_vfs *p, const char *z){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xGetSystemCall(pSub,z);
}
static const char *tmstmpNextSystemCall(sqlite3_vfs *p, const char *zName){
  sqlite3_vfs *pSub = ORIGVFS(p);
  return pSub->xNextSystemCall(pSub,zName);
}

/*
** Register the tmstmp VFS as the default VFS for the system.
*/
static int tmstmpRegisterVfs(void){
  int rc = SQLITE_OK;
  sqlite3_vfs *pOrig = sqlite3_vfs_find(0);
  if( pOrig==0 ) return SQLITE_ERROR;
  if( pOrig==&tmstmp_vfs ) return SQLITE_OK;
  tmstmp_vfs.iVersion = pOrig->iVersion;
  tmstmp_vfs.pAppData = pOrig;
  tmstmp_vfs.szOsFile = pOrig->szOsFile + sizeof(TmstmpFile);
  rc = sqlite3_vfs_register(&tmstmp_vfs, 1);
  return rc;
}

#if defined(SQLITE_TMSTMPVFS_STATIC)
/* This variant of the initializer runs when the extension is
** statically linked.
*/
int sqlite3_register_tmstmpvfs(const char *NotUsed){
  (void)NotUsed;
  return tmstmpRegisterVfs();
}
int sqlite3_unregister_tmstmpvfs(void){
  if( sqlite3_vfs_find("tmstmpvfs") ){
    sqlite3_vfs_unregister(&tmstmp_vfs);
    sqlite3_cancel_auto_extension((void(*)(void))tmstmpRegisterFunc);
  }
  return SQLITE_OK;
}
#endif /* defined(SQLITE_TMSTMPVFS_STATIC */

#if !defined(SQLITE_TMSTMPVFS_STATIC)
/* This variant of the initializer function is used when the
** extension is shared library to be loaded at run-time.
*/
#ifdef _WIN32
__declspec(dllexport)
#endif
/* 
** This routine is called by sqlite3_load_extension() when the
** extension is first loaded.
***/
int sqlite3_tmstmpvfs_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg; /* not used */
  rc = tmstmpRegisterVfs();
  if( rc==SQLITE_OK ) rc = SQLITE_OK_LOAD_PERMANENTLY;
  return rc;
}
#endif /* !defined(SQLITE_TMSTMPVFS_STATIC) */
