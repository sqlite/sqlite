/*
** 2011 March 16
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
** This file contains code implements a VFS shim that writes diagnostic
** output for each VFS call, similar to "strace".
**
** USAGE:
**
** This source file exports a single symbol which is the name of a
** function:
**
**   int vfstrace_register(
**     const char *zTraceName,         // Name of the newly constructed VFS
**     const char *zOldVfsName,        // Name of the underlying VFS
**     int (*xOut)(const char*,void*), // Output routine.  ex: fputs
**     void *pOutArg,                  // 2nd argument to xOut.  ex: stderr
**     int makeDefault                 // Make the new VFS the default
**   );
**
** Applications that want to trace their VFS usage must provide a callback
** function with this prototype:
**
**   int traceOutput(const char *zMessage, void *pAppData);
**
** This function will "output" the trace messages, where "output" can
** mean different things to different applications.  The traceOutput function
** for the command-line shell (see shell.c) is "fputs" from the standard
** library, which means that all trace output is written on the stream
** specified by the second argument.  In the case of the command-line shell
** the second argument is stderr.  Other applications might choose to output
** trace information to a file, over a socket, or write it into a buffer.
**
** The vfstrace_register() function creates a new "shim" VFS named by
** the zTraceName parameter.  A "shim" VFS is an SQLite backend that does
** not really perform the duties of a true backend, but simply filters or
** interprets VFS calls before passing them off to another VFS which does
** the actual work.  In this case the other VFS - the one that does the
** real work - is identified by the second parameter, zOldVfsName.  If
** the 2nd parameter is NULL then the default VFS is used.  The common
** case is for the 2nd parameter to be NULL.
**
** The third and fourth parameters are the pointer to the output function
** and the second argument to the output function.  For the SQLite
** command-line shell, when the -vfstrace option is used, these parameters
** are fputs and stderr, respectively.
**
** The fifth argument is true (non-zero) to cause the newly created VFS
** to become the default VFS.  The common case is for the fifth parameter
** to be true.
**
** The call to vfstrace_register() simply creates the shim VFS that does
** tracing.  The application must also arrange to use the new VFS for
** all database connections that are created and for which tracing is 
** desired.  This can be done by specifying the trace VFS using URI filename
** notation, or by specifying the trace VFS as the 4th parameter to
** sqlite3_open_v2() or by making the trace VFS be the default (by setting
** the 5th parameter of vfstrace_register() to 1).
**
**
** ENABLING VFSTRACE IN A COMMAND-LINE SHELL
**
** The SQLite command line shell implemented by the shell.c source file
** can be used with this module.  To compile in -vfstrace support, first
** gather this file (test_vfstrace.c), the shell source file (shell.c),
** and the SQLite amalgamation source files (sqlite3.c, sqlite3.h) into
** the working directory.  Then compile using a command like the following:
**
**    gcc -o sqlite3 -Os -I. -DSQLITE_ENABLE_VFSTRACE \
**        -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_FTS3 -DSQLITE_ENABLE_RTREE \
**        -DHAVE_READLINE -DHAVE_USLEEP=1 \
**        shell.c test_vfstrace.c sqlite3.c -ldl -lreadline -lncurses
**
** The gcc command above works on Linux and provides (in addition to the
** -vfstrace option) support for FTS3 and FTS4, RTREE, and command-line
** editing using the readline library.  The command-line shell does not
** use threads so we added -DSQLITE_THREADSAFE=0 just to make the code
** run a little faster.   For compiling on a Mac, you'll probably need
** to omit the -DHAVE_READLINE, the -lreadline, and the -lncurses options.
** The compilation could be simplified to just this:
**
**    gcc -DSQLITE_ENABLE_VFSTRACE \
**         shell.c test_vfstrace.c sqlite3.c -ldl -lpthread
**
** In this second example, all unnecessary options have been removed
** Note that since the code is now threadsafe, we had to add the -lpthread
** option to pull in the pthreads library.
**
** To cross-compile for windows using MinGW, a command like this might
** work:
**
**    /opt/mingw/bin/i386-mingw32msvc-gcc -o sqlite3.exe -Os -I \
**         -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_VFSTRACE \
**         shell.c test_vfstrace.c sqlite3.c
**
** Similar compiler commands will work on different systems.  The key
** invariants are (1) you must have -DSQLITE_ENABLE_VFSTRACE so that
** the shell.c source file will know to include the -vfstrace command-line
** option and (2) you must compile and link the three source files
** shell,c, test_vfstrace.c, and sqlite3.c.
**
** RUNTIME CONTROL OF VFSTRACE OUTPUT
**
** The application can use the "vfstrace" pragma to control which VFS
** APIs are traced.  To disable all output:
**
**    PRAGMA vfstrace('-all');
**
** To enable all output (which is the default setting):
**
**    PRAGMA vfstrace('+all');
**
** Individual APIs can be enabled or disabled by name, with or without
** the initial "x" character.  For example, to set up for tracing lock
** primatives only:
**
**    PRAGMA vfstrace('-all, +Lock,Unlock,ShmLock');
**
** The argument to the vfstrace pragma ignores capitalization and any
** characters other than alphabetics, '+', and '-'.
*/
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"

/*
** An instance of this structure is attached to the each trace VFS to
** provide auxiliary information.
*/
typedef struct vfstrace_info vfstrace_info;
struct vfstrace_info {
  sqlite3_vfs *pRootVfs;              /* The underlying real VFS */
  int (*xOut)(const char*, void*);    /* Send output here */
  unsigned int mTrace;                /* Mask of interfaces to trace */
  u8 bOn;                             /* Tracing on/off */
  void *pOutArg;                      /* First argument to xOut */
  const char *zVfsName;               /* Name of this trace-VFS */
  sqlite3_vfs *pTraceVfs;             /* Pointer back to the trace VFS */
};

/*
** The sqlite3_file object for the trace VFS
*/
typedef struct vfstrace_file vfstrace_file;
struct vfstrace_file {
  sqlite3_file base;        /* Base class.  Must be first */
  vfstrace_info *pInfo;     /* The trace-VFS to which this file belongs */
  const char *zFName;       /* Base name of the file */
  sqlite3_file *pReal;      /* The real underlying file */
};

/*
** Bit values for vfstrace_info.mTrace.
*/
#define VTR_CLOSE           0x00000001
#define VTR_READ            0x00000002
#define VTR_WRITE           0x00000004
#define VTR_TRUNC           0x00000008
#define VTR_SYNC            0x00000010
#define VTR_FSIZE           0x00000020
#define VTR_LOCK            0x00000040
#define VTR_UNLOCK          0x00000080
#define VTR_CRL             0x00000100
#define VTR_FCTRL           0x00000200
#define VTR_SECSZ           0x00000400
#define VTR_DEVCHAR         0x00000800
#define VTR_SHMLOCK         0x00001000
#define VTR_SHMMAP          0x00002000
#define VTR_SHMBAR          0x00004000
#define VTR_SHMUNMAP        0x00008000
#define VTR_OPEN            0x00010000
#define VTR_DELETE          0x00020000
#define VTR_ACCESS          0x00040000
#define VTR_FULLPATH        0x00080000
#define VTR_DLOPEN          0x00100000
#define VTR_DLERR           0x00200000
#define VTR_DLSYM           0x00400000
#define VTR_DLCLOSE         0x00800000
#define VTR_RAND            0x01000000
#define VTR_SLEEP           0x02000000
#define VTR_CURTIME         0x04000000
#define VTR_LASTERR         0x08000000

/*
** Method declarations for vfstrace_file.
*/
static int vfstraceClose(sqlite3_file*);
static int vfstraceRead(sqlite3_file*, void*, int iAmt, sqlite3_int64 iOfst);
static int vfstraceWrite(sqlite3_file*,const void*,int iAmt, sqlite3_int64);
static int vfstraceTruncate(sqlite3_file*, sqlite3_int64 size);
static int vfstraceSync(sqlite3_file*, int flags);
static int vfstraceFileSize(sqlite3_file*, sqlite3_int64 *pSize);
static int vfstraceLock(sqlite3_file*, int);
static int vfstraceUnlock(sqlite3_file*, int);
static int vfstraceCheckReservedLock(sqlite3_file*, int *);
static int vfstraceFileControl(sqlite3_file*, int op, void *pArg);
static int vfstraceSectorSize(sqlite3_file*);
static int vfstraceDeviceCharacteristics(sqlite3_file*);
static int vfstraceShmLock(sqlite3_file*,int,int,int);
static int vfstraceShmMap(sqlite3_file*,int,int,int, void volatile **);
static void vfstraceShmBarrier(sqlite3_file*);
static int vfstraceShmUnmap(sqlite3_file*,int);

/*
** Method declarations for vfstrace_vfs.
*/
static int vfstraceOpen(sqlite3_vfs*, const char *, sqlite3_file*, int , int *);
static int vfstraceDelete(sqlite3_vfs*, const char *zName, int syncDir);
static int vfstraceAccess(sqlite3_vfs*, const char *zName, int flags, int *);
static int vfstraceFullPathname(sqlite3_vfs*, const char *zName, int, char *);
static void *vfstraceDlOpen(sqlite3_vfs*, const char *zFilename);
static void vfstraceDlError(sqlite3_vfs*, int nByte, char *zErrMsg);
static void (*vfstraceDlSym(sqlite3_vfs*,void*, const char *zSymbol))(void);
static void vfstraceDlClose(sqlite3_vfs*, void*);
static int vfstraceRandomness(sqlite3_vfs*, int nByte, char *zOut);
static int vfstraceSleep(sqlite3_vfs*, int microseconds);
static int vfstraceCurrentTime(sqlite3_vfs*, double*);
static int vfstraceGetLastError(sqlite3_vfs*, int, char*);
static int vfstraceCurrentTimeInt64(sqlite3_vfs*, sqlite3_int64*);
static int vfstraceSetSystemCall(sqlite3_vfs*,const char*, sqlite3_syscall_ptr);
static sqlite3_syscall_ptr vfstraceGetSystemCall(sqlite3_vfs*, const char *);
static const char *vfstraceNextSystemCall(sqlite3_vfs*, const char *zName);

/*
** Return a pointer to the tail of the pathname.  Examples:
**
**     /home/drh/xyzzy.txt -> xyzzy.txt
**     xyzzy.txt           -> xyzzy.txt
*/
static const char *fileTail(const char *z){
  size_t i;
  if( z==0 ) return 0;
  i = strlen(z)-1;
  while( i>0 && z[i-1]!='/' ){ i--; }
  return &z[i];
}

/*
** Send trace output defined by zFormat and subsequent arguments.
*/
static void vfstrace_printf(
  vfstrace_info *pInfo,
  const char *zFormat,
  ...
){
  va_list ap;
  char *zMsg;
  if( pInfo->bOn ){
    va_start(ap, zFormat);
    zMsg = sqlite3_vmprintf(zFormat, ap);
    va_end(ap);
    pInfo->xOut(zMsg, pInfo->pOutArg);
    sqlite3_free(zMsg);
  }
}

/*
** Try to convert an error code into a symbolic name for that error code.
*/
static const char *vfstrace_errcode_name(int rc ){
  const char *zVal = 0;
  switch( rc ){
    case SQLITE_OK:                 zVal = "SQLITE_OK";                 break;
    case SQLITE_INTERNAL:           zVal = "SQLITE_INTERNAL";           break;
    case SQLITE_ERROR:              zVal = "SQLITE_ERROR";              break;
    case SQLITE_PERM:               zVal = "SQLITE_PERM";               break;
    case SQLITE_ABORT:              zVal = "SQLITE_ABORT";              break;
    case SQLITE_BUSY:               zVal = "SQLITE_BUSY";               break;
    case SQLITE_LOCKED:             zVal = "SQLITE_LOCKED";             break;
    case SQLITE_NOMEM:              zVal = "SQLITE_NOMEM";              break;
    case SQLITE_READONLY:           zVal = "SQLITE_READONLY";           break;
    case SQLITE_INTERRUPT:          zVal = "SQLITE_INTERRUPT";          break;
    case SQLITE_IOERR:              zVal = "SQLITE_IOERR";              break;
    case SQLITE_CORRUPT:            zVal = "SQLITE_CORRUPT";            break;
    case SQLITE_NOTFOUND:           zVal = "SQLITE_NOTFOUND";           break;
    case SQLITE_FULL:               zVal = "SQLITE_FULL";               break;
    case SQLITE_CANTOPEN:           zVal = "SQLITE_CANTOPEN";           break;
    case SQLITE_PROTOCOL:           zVal = "SQLITE_PROTOCOL";           break;
    case SQLITE_EMPTY:              zVal = "SQLITE_EMPTY";              break;
    case SQLITE_SCHEMA:             zVal = "SQLITE_SCHEMA";             break;
    case SQLITE_TOOBIG:             zVal = "SQLITE_TOOBIG";             break;
    case SQLITE_CONSTRAINT:         zVal = "SQLITE_CONSTRAINT";         break;
    case SQLITE_MISMATCH:           zVal = "SQLITE_MISMATCH";           break;
    case SQLITE_MISUSE:             zVal = "SQLITE_MISUSE";             break;
    case SQLITE_NOLFS:              zVal = "SQLITE_NOLFS";              break;
    case SQLITE_IOERR_READ:         zVal = "SQLITE_IOERR_READ";         break;
    case SQLITE_IOERR_SHORT_READ:   zVal = "SQLITE_IOERR_SHORT_READ";   break;
    case SQLITE_IOERR_WRITE:        zVal = "SQLITE_IOERR_WRITE";        break;
    case SQLITE_IOERR_FSYNC:        zVal = "SQLITE_IOERR_FSYNC";        break;
    case SQLITE_IOERR_DIR_FSYNC:    zVal = "SQLITE_IOERR_DIR_FSYNC";    break;
    case SQLITE_IOERR_TRUNCATE:     zVal = "SQLITE_IOERR_TRUNCATE";     break;
    case SQLITE_IOERR_FSTAT:        zVal = "SQLITE_IOERR_FSTAT";        break;
    case SQLITE_IOERR_UNLOCK:       zVal = "SQLITE_IOERR_UNLOCK";       break;
    case SQLITE_IOERR_RDLOCK:       zVal = "SQLITE_IOERR_RDLOCK";       break;
    case SQLITE_IOERR_DELETE:       zVal = "SQLITE_IOERR_DELETE";       break;
    case SQLITE_IOERR_BLOCKED:      zVal = "SQLITE_IOERR_BLOCKED";      break;
    case SQLITE_IOERR_NOMEM:        zVal = "SQLITE_IOERR_NOMEM";        break;
    case SQLITE_IOERR_ACCESS:       zVal = "SQLITE_IOERR_ACCESS";       break;
    case SQLITE_IOERR_CHECKRESERVEDLOCK:
                               zVal = "SQLITE_IOERR_CHECKRESERVEDLOCK"; break;
    case SQLITE_IOERR_LOCK:         zVal = "SQLITE_IOERR_LOCK";         break;
    case SQLITE_IOERR_CLOSE:        zVal = "SQLITE_IOERR_CLOSE";        break;
    case SQLITE_IOERR_DIR_CLOSE:    zVal = "SQLITE_IOERR_DIR_CLOSE";    break;
    case SQLITE_IOERR_SHMOPEN:      zVal = "SQLITE_IOERR_SHMOPEN";      break;
    case SQLITE_IOERR_SHMSIZE:      zVal = "SQLITE_IOERR_SHMSIZE";      break;
    case SQLITE_IOERR_SHMLOCK:      zVal = "SQLITE_IOERR_SHMLOCK";      break;
    case SQLITE_IOERR_SHMMAP:       zVal = "SQLITE_IOERR_SHMMAP";       break;
    case SQLITE_IOERR_SEEK:         zVal = "SQLITE_IOERR_SEEK";         break;
    case SQLITE_IOERR_GETTEMPPATH:  zVal = "SQLITE_IOERR_GETTEMPPATH";  break;
    case SQLITE_IOERR_CONVPATH:     zVal = "SQLITE_IOERR_CONVPATH";     break;
    case SQLITE_READONLY_DBMOVED:   zVal = "SQLITE_READONLY_DBMOVED";   break;
    case SQLITE_LOCKED_SHAREDCACHE: zVal = "SQLITE_LOCKED_SHAREDCACHE"; break;
    case SQLITE_BUSY_RECOVERY:      zVal = "SQLITE_BUSY_RECOVERY";      break;
    case SQLITE_CANTOPEN_NOTEMPDIR: zVal = "SQLITE_CANTOPEN_NOTEMPDIR"; break;
  }
  return zVal;
}

/*
** Convert value rc into a string and print it using zFormat.  zFormat
** should have exactly one %s
*/
static void vfstrace_print_errcode(
  vfstrace_info *pInfo,
  const char *zFormat,
  int rc
){
  const char *zVal;
  char zBuf[50];
  zVal = vfstrace_errcode_name(rc);
  if( zVal==0 ){
    zVal = vfstrace_errcode_name(rc&0xff);
    if( zVal ){
      sqlite3_snprintf(sizeof(zBuf), zBuf, "%s | 0x%x", zVal, rc&0xffff00);
    }else{
      sqlite3_snprintf(sizeof(zBuf), zBuf, "%d (0x%x)", rc, rc);
    }
    zVal = zBuf;
  }
  vfstrace_printf(pInfo, zFormat, zVal);
}

/*
** Append to a buffer.
*/
static void strappend(char *z, int *pI, const char *zAppend){
  int i = *pI;
  while( zAppend[0] ){ z[i++] = *(zAppend++); }
  z[i] = 0;
  *pI = i;
}

/*
** Turn tracing output on or off according to mMask.
*/
static void vfstraceOnOff(vfstrace_info *pInfo, unsigned int mMask){
  pInfo->bOn = (pInfo->mTrace & mMask)!=0;
}

/*
** Close an vfstrace-file.
*/
static int vfstraceClose(sqlite3_file *pFile){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_CLOSE);
  vfstrace_printf(pInfo, "%s.xClose(%s)", pInfo->zVfsName, p->zFName);
  rc = p->pReal->pMethods->xClose(p->pReal);
  vfstrace_print_errcode(pInfo, " -> %s\n", rc);
  if( rc==SQLITE_OK ){
    sqlite3_free((void*)p->base.pMethods);
    p->base.pMethods = 0;
  }
  return rc;
}

/*
** Read data from an vfstrace-file.
*/
static int vfstraceRead(
  sqlite3_file *pFile, 
  void *zBuf, 
  int iAmt, 
  sqlite_int64 iOfst
){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_READ);
  vfstrace_printf(pInfo, "%s.xRead(%s,n=%d,ofst=%lld)",
                  pInfo->zVfsName, p->zFName, iAmt, iOfst);
  rc = p->pReal->pMethods->xRead(p->pReal, zBuf, iAmt, iOfst);
  vfstrace_print_errcode(pInfo, " -> %s\n", rc);
  return rc;
}

/*
** Write data to an vfstrace-file.
*/
static int vfstraceWrite(
  sqlite3_file *pFile, 
  const void *zBuf, 
  int iAmt, 
  sqlite_int64 iOfst
){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_WRITE);
  vfstrace_printf(pInfo, "%s.xWrite(%s,n=%d,ofst=%lld)",
                  pInfo->zVfsName, p->zFName, iAmt, iOfst);
  rc = p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
  vfstrace_print_errcode(pInfo, " -> %s\n", rc);
  return rc;
}

/*
** Truncate an vfstrace-file.
*/
static int vfstraceTruncate(sqlite3_file *pFile, sqlite_int64 size){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_TRUNC);
  vfstrace_printf(pInfo, "%s.xTruncate(%s,%lld)", pInfo->zVfsName, p->zFName,
                  size);
  rc = p->pReal->pMethods->xTruncate(p->pReal, size);
  vfstrace_printf(pInfo, " -> %d\n", rc);
  return rc;
}

/*
** Sync an vfstrace-file.
*/
static int vfstraceSync(sqlite3_file *pFile, int flags){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  int i;
  char zBuf[100];
  memcpy(zBuf, "|0", 3);
  i = 0;
  if( flags & SQLITE_SYNC_FULL )        strappend(zBuf, &i, "|FULL");
  else if( flags & SQLITE_SYNC_NORMAL ) strappend(zBuf, &i, "|NORMAL");
  if( flags & SQLITE_SYNC_DATAONLY )    strappend(zBuf, &i, "|DATAONLY");
  if( flags & ~(SQLITE_SYNC_FULL|SQLITE_SYNC_DATAONLY) ){
    sqlite3_snprintf(sizeof(zBuf)-i, &zBuf[i], "|0x%x", flags);
  }
  vfstraceOnOff(pInfo, VTR_SYNC);
  vfstrace_printf(pInfo, "%s.xSync(%s,%s)", pInfo->zVfsName, p->zFName,
                  &zBuf[1]);
  rc = p->pReal->pMethods->xSync(p->pReal, flags);
  vfstrace_printf(pInfo, " -> %d\n", rc);
  return rc;
}

/*
** Return the current file-size of an vfstrace-file.
*/
static int vfstraceFileSize(sqlite3_file *pFile, sqlite_int64 *pSize){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_FSIZE);
  vfstrace_printf(pInfo, "%s.xFileSize(%s)", pInfo->zVfsName, p->zFName);
  rc = p->pReal->pMethods->xFileSize(p->pReal, pSize);
  vfstrace_print_errcode(pInfo, " -> %s,", rc);
  vfstrace_printf(pInfo, " size=%lld\n", *pSize);
  return rc;
}

/*
** Return the name of a lock.
*/
static const char *lockName(int eLock){
  const char *azLockNames[] = {
     "NONE", "SHARED", "RESERVED", "PENDING", "EXCLUSIVE"
  };
  if( eLock<0 || eLock>=(int)(sizeof(azLockNames)/sizeof(azLockNames[0])) ){
    return "???";
  }else{
    return azLockNames[eLock];
  }
}

/*
** Lock an vfstrace-file.
*/
static int vfstraceLock(sqlite3_file *pFile, int eLock){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_LOCK);
  vfstrace_printf(pInfo, "%s.xLock(%s,%s)", pInfo->zVfsName, p->zFName,
                  lockName(eLock));
  rc = p->pReal->pMethods->xLock(p->pReal, eLock);
  vfstrace_print_errcode(pInfo, " -> %s\n", rc);
  return rc;
}

/*
** Unlock an vfstrace-file.
*/
static int vfstraceUnlock(sqlite3_file *pFile, int eLock){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_UNLOCK);
  vfstrace_printf(pInfo, "%s.xUnlock(%s,%s)", pInfo->zVfsName, p->zFName,
                  lockName(eLock));
  rc = p->pReal->pMethods->xUnlock(p->pReal, eLock);
  vfstrace_print_errcode(pInfo, " -> %s\n", rc);
  return rc;
}

/*
** Check if another file-handle holds a RESERVED lock on an vfstrace-file.
*/
static int vfstraceCheckReservedLock(sqlite3_file *pFile, int *pResOut){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_CRL);
  vfstrace_printf(pInfo, "%s.xCheckReservedLock(%s,%d)", 
                  pInfo->zVfsName, p->zFName);
  rc = p->pReal->pMethods->xCheckReservedLock(p->pReal, pResOut);
  vfstrace_print_errcode(pInfo, " -> %s", rc);
  vfstrace_printf(pInfo, ", out=%d\n", *pResOut);
  return rc;
}

/*
** File control method. For custom operations on an vfstrace-file.
*/
static int vfstraceFileControl(sqlite3_file *pFile, int op, void *pArg){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  char zBuf[100];
  char zBuf2[100];
  char *zOp;
  char *zRVal = 0;
  vfstraceOnOff(pInfo, VTR_FCTRL);
  switch( op ){
    case SQLITE_FCNTL_LOCKSTATE:           zOp = "LOCKSTATE";           break;
    case SQLITE_GET_LOCKPROXYFILE:         zOp = "GET_LOCKPROXYFILE";   break;
    case SQLITE_SET_LOCKPROXYFILE:         zOp = "SET_LOCKPROXYFILE";   break;
    case SQLITE_LAST_ERRNO:                zOp = "LAST_ERRNO";          break;
    case SQLITE_FCNTL_SIZE_HINT: {
      sqlite3_snprintf(sizeof(zBuf), zBuf, "SIZE_HINT,%lld",
                       *(sqlite3_int64*)pArg);
      zOp = zBuf;
      break;
    }
    case SQLITE_FCNTL_CHUNK_SIZE: {
      sqlite3_snprintf(sizeof(zBuf), zBuf, "CHUNK_SIZE,%d", *(int*)pArg);
      zOp = zBuf;
      break;
    }
    case SQLITE_FCNTL_FILE_POINTER:        zOp = "FILE_POINTER";        break;
    case SQLITE_FCNTL_WIN32_AV_RETRY:      zOp = "WIN32_AV_RETRY";      break;
    case SQLITE_FCNTL_PERSIST_WAL: {
       sqlite3_snprintf(sizeof(zBuf), zBuf, "PERSIST_WAL,%d", *(int*)pArg);
       zOp = zBuf;
       break;
    }
    case SQLITE_FCNTL_OVERWRITE:           zOp = "OVERWRITE";           break;
    case SQLITE_FCNTL_VFSNAME:             zOp = "VFSNAME";             break;
    case SQLITE_FCNTL_POWERSAFE_OVERWRITE: zOp = "POWERSAFE_OVERWRITE"; break;
    case SQLITE_FCNTL_PRAGMA: {
      const char *const* a = (const char*const*)pArg;
      if( a[1] && strcmp(a[1],"vfstrace")==0 && a[2] ){
        const u8 *zArg = (const u8*)a[2];
        if( zArg[0]>='0' && zArg[0]<=9 ){
          pInfo->mTrace = (sqlite3_uint64)strtoll(a[2], 0, 0);
        }else{
          static const struct {
            const char *z;
            unsigned int m;
          } aKw[] = {
            { "all",                   0xffffffff   },
            { "close",                 VTR_CLOSE    },
            { "read",                  VTR_READ     },
            { "write",                 VTR_WRITE    },
            { "truncate",              VTR_TRUNC    },
            { "sync",                  VTR_SYNC     },
            { "filesize",              VTR_FSIZE    },
            { "lock",                  VTR_LOCK     },
            { "unlock",                VTR_UNLOCK   },
            { "checkreservedlock",     VTR_CRL      },
            { "filecontrol",           VTR_FCTRL    },
            { "sectorsize",            VTR_SECSZ    },
            { "devicecharacteristics", VTR_DEVCHAR  },
            { "shmlock",               VTR_SHMLOCK  },
            { "shmmap",                VTR_SHMMAP   },
            { "shmummap",              VTR_SHMUNMAP },
            { "shmbarrier",            VTR_SHMBAR   },
            { "open",                  VTR_OPEN     },
            { "delete",                VTR_DELETE   },
            { "access",                VTR_ACCESS   },
            { "fullpathname",          VTR_FULLPATH },
            { "dlopen",                VTR_DLOPEN   },
            { "dlerror",               VTR_DLERR    },
            { "dlsym",                 VTR_DLSYM    },
            { "dlclose",               VTR_DLCLOSE  },
            { "randomness",            VTR_RAND     },
            { "sleep",                 VTR_SLEEP    },
            { "currenttime",           VTR_CURTIME  },
            { "currenttimeint64",      VTR_CURTIME  },
            { "getlasterror",          VTR_LASTERR  },
          };
          int onOff = 1;
          while( zArg[0] ){
            int jj, n;
            while( zArg[0]!=0 && zArg[0]!='-' && zArg[0]!='+'
                   && !isalpha(zArg[0]) ) zArg++;
            if( zArg[0]==0 ) break;
            if( zArg[0]=='-' ){
              onOff = 0;
              zArg++;
            }else if( zArg[0]=='+' ){
              onOff = 1;
              zArg++;
            }
            while( !isalpha(zArg[0]) ){
              if( zArg[0]==0 ) break;
              zArg++;
            }
            if( zArg[0]=='x' && isalpha(zArg[1]) ) zArg++;
            for(n=0; isalpha(zArg[n]); n++){}
            for(jj=0; jj<(int)(sizeof(aKw)/sizeof(aKw[0])); jj++){
              if( sqlite3_strnicmp(aKw[jj].z,(const char*)zArg,n)==0 ){
                if( onOff ){
                  pInfo->mTrace |= aKw[jj].m;
                }else{
                  pInfo->mTrace &= ~aKw[jj].m;
                }
                break;
              }
            }
            zArg += n;
          }
        }
      }
      sqlite3_snprintf(sizeof(zBuf), zBuf, "PRAGMA,[%s,%s]",a[1],a[2]);
      zOp = zBuf;
      break;
    }
    case SQLITE_FCNTL_BUSYHANDLER:         zOp = "BUSYHANDLER";         break;
    case SQLITE_FCNTL_TEMPFILENAME:        zOp = "TEMPFILENAME";        break;
    case SQLITE_FCNTL_MMAP_SIZE: {
      sqlite3_int64 iMMap = *(sqlite3_int64*)pArg;
      sqlite3_snprintf(sizeof(zBuf), zBuf, "MMAP_SIZE,%lld",iMMap);
      zOp = zBuf;
      break;
    }
    case SQLITE_FCNTL_TRACE:               zOp = "TRACE";               break;
    case SQLITE_FCNTL_HAS_MOVED:           zOp = "HAS_MOVED";           break;
    case SQLITE_FCNTL_SYNC:                zOp = "SYNC";                break;
    case SQLITE_FCNTL_COMMIT_PHASETWO:     zOp = "COMMIT_PHASETWO";     break;
    case SQLITE_FCNTL_WIN32_SET_HANDLE:    zOp = "WIN32_SET_HANDLE";    break;
    case SQLITE_FCNTL_WAL_BLOCK:           zOp = "WAL_BLOCK";           break;
    case SQLITE_FCNTL_ZIPVFS:              zOp = "ZIPVFS";              break; 
    case SQLITE_FCNTL_RBU:                 zOp = "RBU";                 break;
    case SQLITE_FCNTL_VFS_POINTER:         zOp = "VFS_POINTER";         break;
    case SQLITE_FCNTL_JOURNAL_POINTER:     zOp = "JOURNAL_POINTER";     break;
    case SQLITE_FCNTL_WIN32_GET_HANDLE:    zOp = "WIN32_GET_HANDLE";    break;
    case SQLITE_FCNTL_PDB:                 zOp = "PDB";                 break;
    case SQLITE_FCNTL_BEGIN_ATOMIC_WRITE:  zOp = "BEGIN_ATOMIC_WRITE";  break;
    case SQLITE_FCNTL_COMMIT_ATOMIC_WRITE: zOp = "COMMIT_ATOMIC_WRITE"; break;
    case SQLITE_FCNTL_ROLLBACK_ATOMIC_WRITE: {
       zOp = "ROLLBACK_ATOMIC_WRITE";
       break;
    }
    case SQLITE_FCNTL_LOCK_TIMEOUT: {
       sqlite3_snprintf(sizeof(zBuf), zBuf, "LOCK_TIMEOUT,%d", *(int*)pArg);
       zOp = zBuf;
       break;
    }
    case SQLITE_FCNTL_DATA_VERSION:        zOp = "DATA_VERSION";        break;
    case SQLITE_FCNTL_SIZE_LIMIT:          zOp = "SIZE_LIMIT";          break;
    case SQLITE_FCNTL_CKPT_DONE:           zOp = "CKPT_DONE";           break;
    case SQLITE_FCNTL_RESERVE_BYTES:       zOp = "RESERVED_BYTES";      break;
    case SQLITE_FCNTL_CKPT_START:          zOp = "CKPT_START";          break;
    case SQLITE_FCNTL_EXTERNAL_READER:     zOp = "EXTERNAL_READER";     break;
    case SQLITE_FCNTL_CKSM_FILE:           zOp = "CKSM_FILE";           break;
    case SQLITE_FCNTL_RESET_CACHE:         zOp = "RESET_CACHE";         break;
    case 0xca093fa0:                       zOp = "DB_UNCHANGED";        break;
    default: {
      sqlite3_snprintf(sizeof zBuf, zBuf, "%d", op);
      zOp = zBuf;
      break;
    }
  }
  vfstrace_printf(pInfo, "%s.xFileControl(%s,%s)",
                  pInfo->zVfsName, p->zFName, zOp);
  rc = p->pReal->pMethods->xFileControl(p->pReal, op, pArg);
  if( rc==SQLITE_OK ){
    switch( op ){
      case SQLITE_FCNTL_VFSNAME: {
        *(char**)pArg = sqlite3_mprintf("vfstrace.%s/%z",
                                    pInfo->zVfsName, *(char**)pArg);
        zRVal = *(char**)pArg;
        break;
      }
      case SQLITE_FCNTL_MMAP_SIZE: {
        sqlite3_snprintf(sizeof(zBuf2), zBuf2, "%lld", *(sqlite3_int64*)pArg);
        zRVal = zBuf2;
        break;
      }
      case SQLITE_FCNTL_HAS_MOVED:
      case SQLITE_FCNTL_PERSIST_WAL: {
        sqlite3_snprintf(sizeof(zBuf2), zBuf2, "%d", *(int*)pArg);
        zRVal = zBuf2;
        break;
      }
      case SQLITE_FCNTL_PRAGMA:
      case SQLITE_FCNTL_TEMPFILENAME: {
        zRVal = *(char**)pArg;
        break;
      }
    }
  }
  if( zRVal ){
    vfstrace_print_errcode(pInfo, " -> %s", rc);
    vfstrace_printf(pInfo, ", %s\n", zRVal);
  }else{
    vfstrace_print_errcode(pInfo, " -> %s\n", rc);
  }
  return rc;
}

/*
** Return the sector-size in bytes for an vfstrace-file.
*/
static int vfstraceSectorSize(sqlite3_file *pFile){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_SECSZ);
  vfstrace_printf(pInfo, "%s.xSectorSize(%s)", pInfo->zVfsName, p->zFName);
  rc = p->pReal->pMethods->xSectorSize(p->pReal);
  vfstrace_printf(pInfo, " -> %d\n", rc);
  return rc;
}

/*
** Return the device characteristic flags supported by an vfstrace-file.
*/
static int vfstraceDeviceCharacteristics(sqlite3_file *pFile){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_DEVCHAR);
  vfstrace_printf(pInfo, "%s.xDeviceCharacteristics(%s)",
                  pInfo->zVfsName, p->zFName);
  rc = p->pReal->pMethods->xDeviceCharacteristics(p->pReal);
  vfstrace_printf(pInfo, " -> 0x%08x\n", rc);
  return rc;
}

/*
** Shared-memory operations.
*/
static int vfstraceShmLock(sqlite3_file *pFile, int ofst, int n, int flags){
  static const char *azLockName[] = {
     "WRITE",
     "CKPT",
     "RECOVER",
     "READ0",
     "READ1",
     "READ2",
     "READ3",
     "READ4",
  };
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  char zLck[100];
  int i = 0;
  vfstraceOnOff(pInfo, VTR_SHMLOCK);
  memcpy(zLck, "|0", 3);
  if( flags & SQLITE_SHM_UNLOCK )    strappend(zLck, &i, "|UNLOCK");
  if( flags & SQLITE_SHM_LOCK )      strappend(zLck, &i, "|LOCK");
  if( flags & SQLITE_SHM_SHARED )    strappend(zLck, &i, "|SHARED");
  if( flags & SQLITE_SHM_EXCLUSIVE ) strappend(zLck, &i, "|EXCLUSIVE");
  if( flags & ~(0xf) ){
     sqlite3_snprintf(sizeof(zLck)-i, &zLck[i], "|0x%x", flags);
  }
  if( ofst>=0 && ofst<(int)(sizeof(azLockName)/sizeof(azLockName[0])) ){
    vfstrace_printf(pInfo, "%s.xShmLock(%s,ofst=%d(%s),n=%d,%s)",
                  pInfo->zVfsName, p->zFName, ofst, azLockName[ofst],
                  n, &zLck[1]);
  }else{
    vfstrace_printf(pInfo, "%s.xShmLock(%s,ofst=5d,n=%d,%s)",
                  pInfo->zVfsName, p->zFName, ofst,
                  n, &zLck[1]);
  }
  rc = p->pReal->pMethods->xShmLock(p->pReal, ofst, n, flags);
  vfstrace_print_errcode(pInfo, " -> %s\n", rc);
  return rc;
}
static int vfstraceShmMap(
  sqlite3_file *pFile, 
  int iRegion, 
  int szRegion, 
  int isWrite, 
  void volatile **pp
){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_SHMMAP);
  vfstrace_printf(pInfo, "%s.xShmMap(%s,iRegion=%d,szRegion=%d,isWrite=%d,*)",
                  pInfo->zVfsName, p->zFName, iRegion, szRegion, isWrite);
  rc = p->pReal->pMethods->xShmMap(p->pReal, iRegion, szRegion, isWrite, pp);
  vfstrace_print_errcode(pInfo, " -> %s\n", rc);
  return rc;
}
static void vfstraceShmBarrier(sqlite3_file *pFile){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  vfstraceOnOff(pInfo, VTR_SHMBAR);
  vfstrace_printf(pInfo, "%s.xShmBarrier(%s)\n", pInfo->zVfsName, p->zFName);
  p->pReal->pMethods->xShmBarrier(p->pReal);
}
static int vfstraceShmUnmap(sqlite3_file *pFile, int delFlag){
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = p->pInfo;
  int rc;
  vfstraceOnOff(pInfo, VTR_SHMUNMAP);
  vfstrace_printf(pInfo, "%s.xShmUnmap(%s,delFlag=%d)",
                  pInfo->zVfsName, p->zFName, delFlag);
  rc = p->pReal->pMethods->xShmUnmap(p->pReal, delFlag);
  vfstrace_print_errcode(pInfo, " -> %s\n", rc);
  return rc;
}



/*
** Open an vfstrace file handle.
*/
static int vfstraceOpen(
  sqlite3_vfs *pVfs,
  const char *zName,
  sqlite3_file *pFile,
  int flags,
  int *pOutFlags
){
  int rc;
  vfstrace_file *p = (vfstrace_file *)pFile;
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  p->pInfo = pInfo;
  p->zFName = zName ? fileTail(zName) : "<temp>";
  p->pReal = (sqlite3_file *)&p[1];
  rc = pRoot->xOpen(pRoot, zName, p->pReal, flags, pOutFlags);
  vfstraceOnOff(pInfo, VTR_OPEN);
  vfstrace_printf(pInfo, "%s.xOpen(%s,flags=0x%x)",
                  pInfo->zVfsName, p->zFName, flags);
  if( p->pReal->pMethods ){
    sqlite3_io_methods *pNew = sqlite3_malloc( sizeof(*pNew) );
    const sqlite3_io_methods *pSub = p->pReal->pMethods;
    memset(pNew, 0, sizeof(*pNew));
    pNew->iVersion = pSub->iVersion;
    pNew->xClose = vfstraceClose;
    pNew->xRead = vfstraceRead;
    pNew->xWrite = vfstraceWrite;
    pNew->xTruncate = vfstraceTruncate;
    pNew->xSync = vfstraceSync;
    pNew->xFileSize = vfstraceFileSize;
    pNew->xLock = vfstraceLock;
    pNew->xUnlock = vfstraceUnlock;
    pNew->xCheckReservedLock = vfstraceCheckReservedLock;
    pNew->xFileControl = vfstraceFileControl;
    pNew->xSectorSize = vfstraceSectorSize;
    pNew->xDeviceCharacteristics = vfstraceDeviceCharacteristics;
    if( pNew->iVersion>=2 ){
      pNew->xShmMap = pSub->xShmMap ? vfstraceShmMap : 0;
      pNew->xShmLock = pSub->xShmLock ? vfstraceShmLock : 0;
      pNew->xShmBarrier = pSub->xShmBarrier ? vfstraceShmBarrier : 0;
      pNew->xShmUnmap = pSub->xShmUnmap ? vfstraceShmUnmap : 0;
    }
    pFile->pMethods = pNew;
  }
  vfstrace_print_errcode(pInfo, " -> %s", rc);
  if( pOutFlags ){
    vfstrace_printf(pInfo, ", outFlags=0x%x\n", *pOutFlags);
  }else{
    vfstrace_printf(pInfo, "\n");
  }
  return rc;
}

/*
** Delete the file located at zPath. If the dirSync argument is true,
** ensure the file-system modifications are synced to disk before
** returning.
*/
static int vfstraceDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  int rc;
  vfstraceOnOff(pInfo, VTR_DELETE);
  vfstrace_printf(pInfo, "%s.xDelete(\"%s\",%d)",
                  pInfo->zVfsName, zPath, dirSync);
  rc = pRoot->xDelete(pRoot, zPath, dirSync);
  vfstrace_print_errcode(pInfo, " -> %s\n", rc);
  return rc;
}

/*
** Test for access permissions. Return true if the requested permission
** is available, or false otherwise.
*/
static int vfstraceAccess(
  sqlite3_vfs *pVfs, 
  const char *zPath, 
  int flags, 
  int *pResOut
){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  int rc;
  vfstraceOnOff(pInfo, VTR_ACCESS);
  vfstrace_printf(pInfo, "%s.xAccess(\"%s\",%d)",
                  pInfo->zVfsName, zPath, flags);
  rc = pRoot->xAccess(pRoot, zPath, flags, pResOut);
  vfstrace_print_errcode(pInfo, " -> %s", rc);
  vfstrace_printf(pInfo, ", out=%d\n", *pResOut);
  return rc;
}

/*
** Populate buffer zOut with the full canonical pathname corresponding
** to the pathname in zPath. zOut is guaranteed to point to a buffer
** of at least (DEVSYM_MAX_PATHNAME+1) bytes.
*/
static int vfstraceFullPathname(
  sqlite3_vfs *pVfs, 
  const char *zPath, 
  int nOut, 
  char *zOut
){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  int rc;
  vfstraceOnOff(pInfo, VTR_FULLPATH);
  vfstrace_printf(pInfo, "%s.xFullPathname(\"%s\")",
                  pInfo->zVfsName, zPath);
  rc = pRoot->xFullPathname(pRoot, zPath, nOut, zOut);
  vfstrace_print_errcode(pInfo, " -> %s", rc);
  vfstrace_printf(pInfo, ", out=\"%.*s\"\n", nOut, zOut);
  return rc;
}

/*
** Open the dynamic library located at zPath and return a handle.
*/
static void *vfstraceDlOpen(sqlite3_vfs *pVfs, const char *zPath){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  vfstraceOnOff(pInfo, VTR_DLOPEN);
  vfstrace_printf(pInfo, "%s.xDlOpen(\"%s\")\n", pInfo->zVfsName, zPath);
  return pRoot->xDlOpen(pRoot, zPath);
}

/*
** Populate the buffer zErrMsg (size nByte bytes) with a human readable
** utf-8 string describing the most recent error encountered associated 
** with dynamic libraries.
*/
static void vfstraceDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  vfstraceOnOff(pInfo, VTR_DLERR);
  vfstrace_printf(pInfo, "%s.xDlError(%d)", pInfo->zVfsName, nByte);
  pRoot->xDlError(pRoot, nByte, zErrMsg);
  vfstrace_printf(pInfo, " -> \"%s\"", zErrMsg);
}

/*
** Return a pointer to the symbol zSymbol in the dynamic library pHandle.
*/
static void (*vfstraceDlSym(sqlite3_vfs *pVfs,void *p,const char *zSym))(void){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  vfstrace_printf(pInfo, "%s.xDlSym(\"%s\")\n", pInfo->zVfsName, zSym);
  return pRoot->xDlSym(pRoot, p, zSym);
}

/*
** Close the dynamic library handle pHandle.
*/
static void vfstraceDlClose(sqlite3_vfs *pVfs, void *pHandle){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  vfstraceOnOff(pInfo, VTR_DLCLOSE);
  vfstrace_printf(pInfo, "%s.xDlOpen()\n", pInfo->zVfsName);
  pRoot->xDlClose(pRoot, pHandle);
}

/*
** Populate the buffer pointed to by zBufOut with nByte bytes of 
** random data.
*/
static int vfstraceRandomness(sqlite3_vfs *pVfs, int nByte, char *zBufOut){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  vfstraceOnOff(pInfo, VTR_RAND);
  vfstrace_printf(pInfo, "%s.xRandomness(%d)\n", pInfo->zVfsName, nByte);
  return pRoot->xRandomness(pRoot, nByte, zBufOut);
}

/*
** Sleep for nMicro microseconds. Return the number of microseconds 
** actually slept.
*/
static int vfstraceSleep(sqlite3_vfs *pVfs, int nMicro){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  vfstraceOnOff(pInfo, VTR_SLEEP);
  vfstrace_printf(pInfo, "%s.xSleep(%d)\n", pInfo->zVfsName, nMicro);
  return pRoot->xSleep(pRoot, nMicro);
}

/*
** Return the current time as a Julian Day number in *pTimeOut.
*/
static int vfstraceCurrentTime(sqlite3_vfs *pVfs, double *pTimeOut){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  int rc;
  vfstraceOnOff(pInfo, VTR_CURTIME);
  vfstrace_printf(pInfo, "%s.xCurrentTime()", pInfo->zVfsName);
  rc = pRoot->xCurrentTime(pRoot, pTimeOut);
  vfstrace_printf(pInfo, " -> %.17g\n", *pTimeOut);
  return rc;
}
static int vfstraceCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *pTimeOut){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  int rc;
  vfstraceOnOff(pInfo, VTR_CURTIME);
  vfstrace_printf(pInfo, "%s.xCurrentTimeInt64()", pInfo->zVfsName);
  rc = pRoot->xCurrentTimeInt64(pRoot, pTimeOut);
  vfstrace_printf(pInfo, " -> %lld\n", *pTimeOut);
  return rc;
}

/*
** Return the most recent error code and message
*/
static int vfstraceGetLastError(sqlite3_vfs *pVfs, int nErr, char *zErr){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  int rc;
  vfstraceOnOff(pInfo, VTR_LASTERR);
  vfstrace_printf(pInfo, "%s.xGetLastError(%d,zBuf)", pInfo->zVfsName, nErr);
  if( nErr ) zErr[0] = 0;
  rc = pRoot->xGetLastError(pRoot, nErr, zErr);
  vfstrace_printf(pInfo, " -> zBuf[] = \"%s\", rc = %d\n", nErr?zErr:"", rc);
  return rc;
}

/*
** Override system calls.
*/
static int vfstraceSetSystemCall(
  sqlite3_vfs *pVfs,
  const char *zName,
  sqlite3_syscall_ptr pFunc
){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  return pRoot->xSetSystemCall(pRoot, zName, pFunc);
}
static sqlite3_syscall_ptr vfstraceGetSystemCall(
  sqlite3_vfs *pVfs,
  const char *zName
){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  return pRoot->xGetSystemCall(pRoot, zName);
}
static const char *vfstraceNextSystemCall(sqlite3_vfs *pVfs, const char *zName){
  vfstrace_info *pInfo = (vfstrace_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  return pRoot->xNextSystemCall(pRoot, zName);
}


/*
** Clients invoke this routine to construct a new trace-vfs shim.
**
** Return SQLITE_OK on success.  
**
** SQLITE_NOMEM is returned in the case of a memory allocation error.
** SQLITE_NOTFOUND is returned if zOldVfsName does not exist.
*/
int vfstrace_register(
   const char *zTraceName,           /* Name of the newly constructed VFS */
   const char *zOldVfsName,          /* Name of the underlying VFS */
   int (*xOut)(const char*,void*),   /* Output routine.  ex: fputs */
   void *pOutArg,                    /* 2nd argument to xOut.  ex: stderr */
   int makeDefault                   /* True to make the new VFS the default */
){
  sqlite3_vfs *pNew;
  sqlite3_vfs *pRoot;
  vfstrace_info *pInfo;
  size_t nName;
  size_t nByte;

  pRoot = sqlite3_vfs_find(zOldVfsName);
  if( pRoot==0 ) return SQLITE_NOTFOUND;
  nName = strlen(zTraceName);
  nByte = sizeof(*pNew) + sizeof(*pInfo) + nName + 1;
  pNew = sqlite3_malloc64( nByte );
  if( pNew==0 ) return SQLITE_NOMEM;
  memset(pNew, 0, nByte);
  pInfo = (vfstrace_info*)&pNew[1];
  pNew->iVersion = pRoot->iVersion;
  pNew->szOsFile = pRoot->szOsFile + sizeof(vfstrace_file);
  pNew->mxPathname = pRoot->mxPathname;
  pNew->zName = (char*)&pInfo[1];
  memcpy((char*)&pInfo[1], zTraceName, nName+1);
  pNew->pAppData = pInfo;
  pNew->xOpen = vfstraceOpen;
  pNew->xDelete = vfstraceDelete;
  pNew->xAccess = vfstraceAccess;
  pNew->xFullPathname = vfstraceFullPathname;
  pNew->xDlOpen = pRoot->xDlOpen==0 ? 0 : vfstraceDlOpen;
  pNew->xDlError = pRoot->xDlError==0 ? 0 : vfstraceDlError;
  pNew->xDlSym = pRoot->xDlSym==0 ? 0 : vfstraceDlSym;
  pNew->xDlClose = pRoot->xDlClose==0 ? 0 : vfstraceDlClose;
  pNew->xRandomness = vfstraceRandomness;
  pNew->xSleep = vfstraceSleep;
  pNew->xCurrentTime = vfstraceCurrentTime;
  pNew->xGetLastError = pRoot->xGetLastError==0 ? 0 : vfstraceGetLastError;
  if( pNew->iVersion>=2 ){
    pNew->xCurrentTimeInt64 = pRoot->xCurrentTimeInt64==0 ? 0 :
                                   vfstraceCurrentTimeInt64;
    if( pNew->iVersion>=3 ){
      pNew->xSetSystemCall = pRoot->xSetSystemCall==0 ? 0 : 
                                   vfstraceSetSystemCall;
      pNew->xGetSystemCall = pRoot->xGetSystemCall==0 ? 0 : 
                                   vfstraceGetSystemCall;
      pNew->xNextSystemCall = pRoot->xNextSystemCall==0 ? 0 : 
                                   vfstraceNextSystemCall;
    }
  }
  pInfo->pRootVfs = pRoot;
  pInfo->xOut = xOut;
  pInfo->pOutArg = pOutArg;
  pInfo->zVfsName = pNew->zName;
  pInfo->pTraceVfs = pNew;
  pInfo->mTrace = 0xffffffff;
  pInfo->bOn = 1;
  vfstrace_printf(pInfo, "%s.enabled_for(\"%s\")\n",
       pInfo->zVfsName, pRoot->zName);
  return sqlite3_vfs_register(pNew, makeDefault);
}

/*
** Look for the named VFS.  If it is a TRACEVFS, then unregister it
** and delete it.
*/
void vfstrace_unregister(const char *zTraceName){
  sqlite3_vfs *pVfs = sqlite3_vfs_find(zTraceName);
  if( pVfs==0 ) return;
  if( pVfs->xOpen!=vfstraceOpen ) return;
  sqlite3_vfs_unregister(pVfs);
  sqlite3_free(pVfs);
}
