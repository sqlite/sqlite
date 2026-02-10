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
** information into 16 byts of reserved space at the end of each page of the
** database file.
**
** The VFS also tries to generate log-files with names of the form:
**
**         $(DATABASE)-tmstmp/$(TIME)-$(PID)-$(ID)
**
** Log files are only generated if directory $(DATABASE)-tmstmp exists.
** The name of each log file is the current ISO8601 time in milliseconds,
** the process ID, and a random 32-bit value (to disambiguate multiple
** connections from the same process) separated by dashes.  The log file
** contains 16-bytes records for various events, such as opening or close
** of the database or WAL file, writes to the WAL file, checkpoints, and
** similar.   The logfile is only generated if the connection attempts to
** modify the database.  There is a separate log file for each open database
** connection.
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
** Another option is to statically link both SQLite and this extension
** into your application.  If both this file and "sqlite3.c" are statically
** linked, and if "sqlite3.c" is compiled with an option like:
**
**       -DSQLITE_EXTRA_INIT=sqlite3_register_tmstmpvfs
**
** Then SQLite will use the tmstmp VFS by default throughout your
** application.
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
** When running the CLI, you can load this extension at invocation by
** adding a command-line option like this:  "--vfs ./tmstmpvfs.so".
** The --vfs option usually specifies the symbolic name of a built-in VFS.
** But if the argument to --vfs is not a built-in VFS but is instead the
** name of a file, the CLI tries to load that file as an extension.  Note
** that the full name of the extension file must be provided, including
** the ".so" or ".dylib" or ".dll" suffix.
**
** An application can see if the tmstmpvfs is being used by examining
** the results from SQLITE_FCNTL_VFSNAME (or the .vfsname command in
** the CLI).  If the answer include "tmstmp", then this VFS is being
** used.
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
** SQLite allows the number of reserve-bytes to be increased, but
** not decreased.  If you want to restore the reserve-bytes to 0
** (to disable tmstmpvfs), the easiest approach is to use VACUUM INTO
** with a URI filename as the argument and include "reserve=0" query
** parameter on the URI.  Example:
**
**     VACUUM INTO  'file:notimestamps.db?reserve=0';
**
** Then switch over to using the new database file.  The reserve=0 query
** parameter only works on SQLite 3.52.0 and later.
**
** IMPLEMENTATION NOTES
**
** The timestamp information is stored in the last 16 bytes of each page.
** This module only operates if the "bytes of reserved space on each page"
** value at offset 20 the SQLite database header is exactly 16.  If
** the reserved-space value is not 16, no timestamp information is added
** to database pages.  Some, but not all, logfile entries will be made
** still, but the size of the logs will be greatly reduced.
**
** The timestamp layout is as follows:
**
**   bytes 0,1     Zero.  Reserved for future expansion
**   bytes 2-7     Milliseconds since the Unix Epoch
**   bytes 8-11    WAL frame number
**   bytes 12      0: WAL write  2: rollback write
**   bytes 13-15   Lower 24 bits of Salt-1
**
** For transactions that occur in rollback mode, only the timestamp
** in bytes 2-7 and byte 12 are non-zero.  Byte 12 is set to 2 for
** rollback writes.
**
** The 16-byte tag is added to each database page when the content
** is written into the database file itself.  This shim does not make
** any changes to the page as it is written to the WAL file, since
** that would mess up the WAL checksum.
**
** LOGGING
**
** An open database connection that attempts to write to the database
** will create a log file if a directory name $(DATABASE)-tmstmp exists.
** The name of the log file is:
**
**         $(TIME)-$(PID)-$(RANDOM)
**
** Where TIME is an ISO 8601 date in milliseconds with no punctuation,
** PID is the process ID, and RANDOM is a 32-bit random number expressed
** as hexadecimal.
**
** The log consists of 16-byte records.  Each record consists of five
** unsigned integers:
**
**      1   1   6    4   4   <---  bytes
**     op  a1  ts   a2  a3
**
** The meanings of the a1-a3 values depend on op.  ts is the timestamp
** in milliseconds since the unix epoch (1970-01-01 00:00:00).
** Opcodes are defined by the ELOG_* #defines below.
**
**   ELOG_OPEN_DB         "Open a connection to the database file"
**                        op = 0x01
**                        a2 = process-ID
**
**   ELOG_OPEN_WAL        "Open a connection to the -wal file"
**                        op = 0x02
**                        a2 = process-ID
**
**   ELOG_WAL_PAGE        "New page added to the WAL file"
**                        op = 0x03
**                        a1 = 1 if last page of a txn.  0 otherwise.
**                        a2 = page number in the DB file
**                        a3 = frame number in the WAL file
**
**   ELOG_DB_PAGE         "Database page updated using rollback mode"
**                        op = 0x04
**                        a2 = page number in the DB file
**
**   ELOG_CKPT_START      "Start of a checkpoint operation"
**                        op = 0x05
**
**   ELOG_CKPT_PAGE       "Page xfer from WAL to database"
**                        op = 0x06
**                        a2 = database page number
**                        a3 = frame number in the WAL file
**
**   ELOG_CKPT_END        "Start of a checkpoint operation"
**                        op = 0x07
**
**   ELOG_WAL_RESET       "WAL file header overwritten"
**                        op = 0x08
**                        a3 = Salt1 value
**
**   ELOG_CLOSE_WAL       "Close the WAL file connection"
**                        op = 0x0e
**
**   ELOG_CLOSE_DB        "Close the DB connection"
**                        op = 0x0f
**
** VIEWING TIMESTAMPS AND LOGS
**
** The command-line utility at tool/showtmlog.c will read and display
** the content of one or more tmstmpvfs.c log files.  If all of the
** log files are stored in directory $(DATABASE)-tmstmp, then you can
** view them all using a command like shown below (with an extra "?"
** inserted on the wildcard to avoid closing the C-language comment
** that contains this text):
**
**    showtmlog $(DATABASE)-tmstmp/?*
**
** The command-line utility at tools/showdb.c can be used to show the
** timestamps on pages of a database file, using a command like this:
**
**    showdb --tmstmp $(DATABASE) pgidx
*
** The command above shows the timestamp and the intended use of every
** pages in the database, in human-readable form.  If you also add
** the --csv option to the command above, then the command generates
** a Comma-Separated-Value (CSV) file as output, which contains a
** decoding of the complete timestamp tag on each page of the database.
** This CVS file can be easily imported into another SQLite database
** using a CLI command like the following:
**
**    .import --csv '|showdb --tmstmp -csv orig.db pgidx' ts_table
**
** In the command above, the database containing the timestamps is
** "orig.db" and the content is imported into a new table named "ts_table".
** The "ts_table" is created automatically, using the column names found
** in the first line of the CSV file.  All columns of the automatically
** created ts_table are of type TEXT.  It might make more sense to
** create the table yourself, using more sensible datatypes, like this:
**
**   CREATE TABLE ts_table (
**     pgno INT,        -- page number
**     tm REAL,         -- seconds since 1970-01-01
**     frame INT,       -- WAL frame number
**     flg INT,         -- flag (tag byte 12)
**     salt INT,        -- WAL salt (tag bytes 13-15)
**     parent INT,      -- Parent page number
**     child INT,       -- Index of this page in its parent
**     ovfl INT,        -- Index of this page on the overflow chain
**     txt TEXT         -- Description of this page
**   );
**
** Then import using:
**
**   .import --csv --skip 1 '|showdb --tmstmp --csv orig.db pgidx' ts_table
**
** Note the addition of the "--skip 1" option on ".import" to bypass the
** first line of the CSV file that contains the column names.
**
** Both programs "showdb" and "showtmlog" can be built by running
** "make showtmlog showdb" from the top-level of a recent SQLite
** source tree.
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
typedef struct TmstmpLog TmstmpLog;

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

/* Information for the tmstmp log file. */
struct  TmstmpLog {
  char *zLogname;        /* Log filename */
  FILE *log;             /* Open log file */
  int n;                 /* Bytes of a[] used */
  unsigned char a[16*6]; /* Buffered header for the log */
};

/* An open WAL or DB file */
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
  TmstmpLog *pLog;       /* Log file */
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
  p->pSubVfs->xCurrentTimeInt64(p->pSubVfs, (sqlite3_int64*)&tm);
  tm -= 210866760000000LL;
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

/* Free a TmstmpLog object */
static void tmstmpLogFree(TmstmpLog *pLog){
  if( pLog==0 ) return;
  sqlite3_free(pLog->zLogname);
  sqlite3_free(pLog);
}

/* Flush log content.  Open the file if necessary. Return the
** number of errors. */
static int tmstmpLogFlush(TmstmpFile *p){
  TmstmpLog *pLog = p->pLog;
  assert( pLog!=0 );
  if( pLog->log==0 ){
    pLog->log = fopen(pLog->zLogname, "wb");
    if( pLog->log==0 ){
      tmstmpLogFree(pLog);
      p->pLog = 0;
      return 1;
    }
  }
  (void)fwrite(pLog->a, pLog->n, 1, pLog->log);
  pLog->n = 0;
  return 0;
}

/*
** Write a record onto the event log
*/
static void tmstmpEvent(
  TmstmpFile *p,
  u8 op,
  u8 a1,
  u32 a2,
  u32 a3,
  u8 *pTS
){
  unsigned char *a;
  TmstmpLog *pLog;
  if( p->isWal ){
    p = p->pPartner;
    assert( p!=0 );
    assert( p->isDb );
  }
  pLog = p->pLog;
  if( pLog==0 ) return;
  if( pLog->n >= (int)sizeof(pLog->a) ){
    if( tmstmpLogFlush(p) ) return;
  }
  a = pLog->a + pLog->n;
  a[0] = op;
  a[1] = a1;
  if( pTS ){
    memcpy(a+2, pTS, 6);
  }else{
    tmstmpPutTS(p, a+2);
  }
  tmstmpPutU32(a2, a+8);
  tmstmpPutU32(a3, a+12);
  pLog->n += 16;
  if( pLog->log || (op>=ELOG_WAL_PAGE && op<=ELOG_WAL_RESET) ){
    (void)tmstmpLogFlush(p);
  }
}

/*
** Close a connection
*/
static int tmstmpClose(sqlite3_file *pFile){
  TmstmpFile *p = (TmstmpFile *)pFile;
  if( p->hasCorrectReserve ){
    tmstmpEvent(p, p->isDb ? ELOG_CLOSE_DB : ELOG_CLOSE_WAL, 0, 0, 0, 0);
  }
  tmstmpLogFree(p->pLog);
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
  if( p->isWal
   && p->inCkpt
   && iAmt>=512 && iAmt<=65535 && (iAmt&(iAmt-1))==0
  ){
    p->pPartner->iFrame = (iOfst-56)/(p->pgsz+24) + 1;
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
      p->iFrame = (iOfst - 32)/(p->pgsz+24)+1;
      p->pgno = tmstmpGetU32((const u8*)zBuf);
      p->salt1 = tmstmpGetU32(((const u8*)zBuf)+16);
      memcpy(&x, ((const u8*)zBuf)+4, 4);
      p->isCommit = (x!=0);
      p->iOfst = iOfst;
    }else if( iAmt>=512 && iOfst==p->iOfst+24 ){
      unsigned char s[TMSTMP_RESERVE];
      memset(s, 0, TMSTMP_RESERVE);
      tmstmpPutTS(p, s+2);
      tmstmpEvent(p, ELOG_WAL_PAGE, p->isCommit, p->pgno, p->iFrame, s+2);
    }else if( iAmt==32 && iOfst==0 ){
      p->salt1 = tmstmpGetU32(((const u8*)zBuf)+16);
      tmstmpEvent(p, ELOG_WAL_RESET, 0, 0, p->salt1, 0);
    }
  }else if( p->inCkpt ){
    unsigned char *s = (unsigned char*)zBuf+iAmt-TMSTMP_RESERVE;
    memset(s, 0, TMSTMP_RESERVE);
    tmstmpPutTS(p, s+2);
    tmstmpPutU32(p->iFrame, s+8);
    tmstmpPutU32(p->pPartner->salt1, s+12);
    assert( p->pgsz>0 );
    tmstmpEvent(p, ELOG_CKPT_PAGE, 0, (iOfst/p->pgsz)+1, p->iFrame, 0);
  }else if( p->pPartner==0 ){
    /* Writing into a database in rollback mode */
    unsigned char *s = (unsigned char*)zBuf+iAmt-TMSTMP_RESERVE;
    memset(s, 0, TMSTMP_RESERVE);
    tmstmpPutTS(p, s+2);
    s[12] = 2;
    assert( p->pgsz>0 );
    tmstmpEvent(p, ELOG_DB_PAGE, 0, (u32)(iOfst/p->pgsz), 0, s+2);
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
  switch( op ){
    case SQLITE_FCNTL_VFSNAME: {
      if( p->hasCorrectReserve && rc==SQLITE_OK ){
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
        tmstmpEvent(p, ELOG_CKPT_START, 0, 0, 0, 0);
      }
      rc = SQLITE_OK;
      break;
    }
    case SQLITE_FCNTL_CKPT_DONE: {
      p->inCkpt = 0;
      assert( p->isDb );
      assert( p->pPartner!=0 );
      p->pPartner->inCkpt = 0;
      if( p->hasCorrectReserve ){
        tmstmpEvent(p, ELOG_CKPT_DONE, 0, 0, 0, 0);
      }
      rc = SQLITE_OK;
      break;
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
    u32 r2;
    u32 pid;
    TmstmpLog *pLog;
    sqlite3_uint64 r1;         /* Milliseconds since 1970-01-01 */
    sqlite3_uint64 days;       /* Days since 1970-01-01 */
    sqlite3_uint64 sod;        /* Start of date specified by r1 */
    sqlite3_uint64 z;          /* Days since 0000-03-01 */
    sqlite3_uint64 era;        /* 400-year era */
    int h;                     /* hour */
    int m;                     /* minute */
    int s;                     /* second */
    int f;                     /* millisecond */
    int Y;                     /* year */
    int M;                     /* month */
    int D;                     /* day */
    int y;                     /* year assuming March is first month */
    unsigned int doe;          /* day of 400-year era */
    unsigned int yoe;          /* year of 400-year era */
    unsigned int doy;          /* day of year */
    unsigned int mp;           /* month with March==0 */
  
    p->isDb = 1;
    r1 = 0;
    pLog = sqlite3_malloc64( sizeof(TmstmpLog) );
    if( pLog==0 ){
      return SQLITE_NOMEM;
    }
    memset(pLog, 0, sizeof(pLog[0]));
    p->pLog = pLog;
    p->pSubVfs->xCurrentTimeInt64(p->pSubVfs, (sqlite3_int64*)&r1);
    r1 -= 210866760000000LL;
    days = r1/86400000;
    sod = (r1%86400000)/1000;
    f = (int)(r1%1000);
  
    h = sod/3600;
    m = (sod%3600)/60;
    s = sod%60;
    z = days + 719468;
    era = z/146097;
    doe = (unsigned)(z - era*146097);
    yoe = (doe - doe/1460 + doe/36524 - doe/146096)/365;
    y = (int)yoe + era*400;
    doy = doe - (365*yoe + yoe/4 - yoe/100);
    mp = (5*doy + 2)/153;
    D = doy - (153*mp + 2)/5 + 1;
    M = mp + (mp<10 ? 3 : -9);
    Y = y + (M <=2);
    sqlite3_randomness(sizeof(r2), &r2);
    pid = GETPID;
    pLog->zLogname = sqlite3_mprintf(
         "%s-tmstmp/%04d%02d%02dT%02d%02d%02d%03d-%08d-%08x",
          zName,       Y,  M,  D,   h,  m,  s,  f, pid,  r2);
  }
  tmstmpEvent(p, p->isWal ? ELOG_OPEN_WAL : ELOG_OPEN_DB, 0, GETPID, 0, 0);

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
  (void)db;       /* not used */
  rc = tmstmpRegisterVfs();
  if( rc==SQLITE_OK ) rc = SQLITE_OK_LOAD_PERMANENTLY;
  return rc;
}
#endif /* !defined(SQLITE_TMSTMPVFS_STATIC) */
