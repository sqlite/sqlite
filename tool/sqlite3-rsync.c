/*
** 2024-09-10
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
** This is a utility program that makes a copy of a live SQLite database
** using a bandwidth-efficient protocol, similar to "rsync".
*/
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include "sqlite3.h"

static const char zUsage[] =
  "sqlite3-rsync ORIGIN REPLICA ?OPTIONS?\n"
  "\n"
  "One of ORIGIN or REPLICA is a pathname to a database on the local\n"
  "machine and the other is of the form \"USER@HOST:PATH\" describing\n"
  "a database on a remote machine.  This utility makes REPLICA into a\n"
  "copy of ORIGIN\n"
  "\n"
  "OPTIONS:\n"
  "\n"
  "   --exe PATH    Name of the sqlite3-rsync program on the remote side\n"
  "   --help        Show this help screen\n"
  "   --ssh PATH    Name of the SSH program used to reach the remote side\n"
  "   -v            Verbose.  Multiple v's for increasing output\n"
;

typedef unsigned char u8;

/* Context for the run */
typedef struct SQLiteRsync SQLiteRsync;
struct SQLiteRsync {
  const char *zOrigin;     /* Name of the origin */
  const char *zReplica;    /* Name of the replica */
  FILE *pOut;              /* Transmit to the other side */
  FILE *pIn;               /* Receive from the other side */
  sqlite3 *db;             /* Database connection */
  int nErr;                /* Number of errors encountered */
  u8 eVerbose;             /* Bigger for more output.  0 means none. */
  u8 bCommCheck;           /* True to debug the communication protocol */
  u8 isRemote;             /* On the remote side of a connection */
  u8 isReplica;            /* True if running on the replica side */
  u8 iProtocol;            /* Protocol version number */
  sqlite3_uint64 nOut;     /* Bytes transmitted */
  sqlite3_uint64 nIn;      /* Bytes received */
  unsigned int nPage;      /* Total number of pages in the database */
  unsigned int szPage;     /* Database page size */
  unsigned int nHashSent;  /* Hashes sent (replica to origin) */
  unsigned int nPageSent;  /* Page contents sent (origin to replica) */
};

/* The version number of the protocol.  Sent in the *_BEGIN message
** to verify that both sides speak the same dialect.
*/
#define PROTOCOL_VERSION  1


/* Magic numbers to identify particular messages sent over the wire.
*/
#define ORIGIN_BEGIN    0x41     /* Initial message */
#define ORIGIN_END      0x42     /* Time to quit */
#define ORIGIN_ERROR    0x43     /* Error message from the remote */
#define ORIGIN_PAGE     0x44     /* New page data */
#define ORIGIN_TXN      0x45     /* Transaction commit */
#define ORIGIN_MSG      0x46     /* Informational message */

#define REPLICA_BEGIN   0x61     /* Welcome message */
#define REPLICA_ERROR   0x62     /* Error.  Report and quit. */
#define REPLICA_END     0x63     /* Replica wants to stop */
#define REPLICA_HASH    0x64     /* One or more pages hashes to report */
#define REPLICA_READY   0x65     /* Read to receive page content */
#define REPLICA_MSG     0x66     /* Informational message */

/* From here onward, fgets() is redirected to the console_io library. */
# define fgets(b,n,f) fGetsUtf8(b,n,f)
/*
 * Define macros for emitting output text in various ways:
 *  sputz(s, z)      => emit 0-terminated string z to given stream s
 *  sputf(s, f, ...) => emit varargs per format f to given stream s
 *  oputz(z)         => emit 0-terminated string z to default stream
 *  oputf(f, ...)    => emit varargs per format f to default stream
 *  eputz(z)         => emit 0-terminated string z to error stream
 *  eputf(f, ...)    => emit varargs per format f to error stream
 *  oputb(b, n)      => emit char buffer b[0..n-1] to default stream
 *
 * Note that the default stream is whatever has been last set via:
 *   setOutputStream(FILE *pf)
 * This is normally the stream that CLI normal output goes to.
 * For the stand-alone CLI, it is stdout with no .output redirect.
 *
 * The ?putz(z) forms are for unformatted strings.
 */
# define sputz(s,z) fPutsUtf8(z,s)
# define sputf fPrintfUtf8
# define oputz(z) oPutsUtf8(z)
# define oputf oPrintfUtf8
# define eputz(z) ePutsUtf8(z)
# define eputf ePrintfUtf8
# define oputb(buf,na) oPutbUtf8(buf,na)

/****************************************************************************
** Beginning of the popen2() implementation copied from Fossil  *************
****************************************************************************/
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
/*
** Print a fatal error and quit.
*/
static void win32_fatal_error(const char *zMsg){
  eputz("%s\n", zMsg);
  exit(1);
}
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

#define SQLITE_INTERNAL_LINKAGE static
#define CONSIO_SET_ERROR_STREAM
#include "ext/consio/console_io.c"
#define fflush(s) fFlushBuffer(s) /* must be defined AFTER console_io.c */

/*
** The following macros are used to cast pointers to integers and
** integers to pointers.  The way you do this varies from one compiler
** to the next, so we have developed the following set of #if statements
** to generate appropriate macros for a wide range of compilers.
**
** The correct "ANSI" way to do this is to use the intptr_t type.
** Unfortunately, that typedef is not available on all compilers, or
** if it is available, it requires an #include of specific headers
** that vary from one machine to the next.
**
** This code is copied out of SQLite.
*/
#if defined(__PTRDIFF_TYPE__)  /* This case should work for GCC */
# define INT_TO_PTR(X)  ((void*)(__PTRDIFF_TYPE__)(X))
# define PTR_TO_INT(X)  ((int)(__PTRDIFF_TYPE__)(X))
#elif !defined(__GNUC__)       /* Works for compilers other than LLVM */
# define INT_TO_PTR(X)  ((void*)&((char*)0)[X])
# define PTR_TO_INT(X)  ((int)(((char*)X)-(char*)0))
#elif defined(HAVE_STDINT_H)   /* Use this case if we have ANSI headers */
# define INT_TO_PTR(X)  ((void*)(intptr_t)(X))
# define PTR_TO_INT(X)  ((int)(intptr_t)(X))
#else                          /* Generates a warning - but it always works */
# define INT_TO_PTR(X)  ((void*)(X))
# define PTR_TO_INT(X)  ((int)(X))
#endif

/* Register SQL functions provided by ext/misc/sha1.c */
extern int sqlite3_sha_init(
  sqlite3 *db,
  char **pzErrMsg,
  const sqlite3_api_routines *pApi
);

#ifdef _WIN32
/*
** On windows, create a child process and specify the stdin, stdout,
** and stderr channels for that process to use.
**
** Return the number of errors.
*/
static int win32_create_child_process(
  wchar_t *zCmd,       /* The command that the child process will run */
  HANDLE hIn,          /* Standard input */
  HANDLE hOut,         /* Standard output */
  HANDLE hErr,         /* Standard error */
  DWORD *pChildPid     /* OUT: Child process handle */
){
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  BOOL rc;

  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  SetHandleInformation(hIn, HANDLE_FLAG_INHERIT, TRUE);
  si.hStdInput  = hIn;
  SetHandleInformation(hOut, HANDLE_FLAG_INHERIT, TRUE);
  si.hStdOutput = hOut;
  SetHandleInformation(hErr, HANDLE_FLAG_INHERIT, TRUE);
  si.hStdError  = hErr;
  rc = CreateProcessW(
     NULL,  /* Application Name */
     zCmd,  /* Command-line */
     NULL,  /* Process attributes */
     NULL,  /* Thread attributes */
     TRUE,  /* Inherit Handles */
     0,     /* Create flags  */
     NULL,  /* Environment */
     NULL,  /* Current directory */
     &si,   /* Startup Info */
     &pi    /* Process Info */
  );
  if( rc ){
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
    *pChildPid = pi.dwProcessId;
  }else{
    win32_fatal_error("cannot create child process");
  }
  return rc!=0;
}
void *win32_utf8_to_unicode(const char *zUtf8){
  int nByte = MultiByteToWideChar(CP_UTF8, 0, zUtf8, -1, 0, 0);
  wchar_t *zUnicode = malloc( nByte*2 );
  MultiByteToWideChar(CP_UTF8, 0, zUtf8, -1, zUnicode, nByte);
  return zUnicode;
}
#endif

/*
** Create a child process running shell command "zCmd".  *ppOut is
** a FILE that becomes the standard input of the child process.
** (The caller writes to *ppOut in order to send text to the child.)
** *ppIn is stdout from the child process.  (The caller
** reads from *ppIn in order to receive input from the child.)
** Note that *ppIn is an unbuffered file descriptor, not a FILE.
** The process ID of the child is written into *pChildPid.
**
** Return the number of errors.
*/
static int popen2(
  const char *zCmd,      /* Command to run in the child process */
  FILE **ppIn,           /* Read from child using this file descriptor */
  FILE **ppOut,          /* Write to child using this file descriptor */
  int *pChildPid,        /* PID of the child process */
  int bDirect            /* 0: run zCmd as a shell cmd.  1: run directly */
){
#ifdef _WIN32
  HANDLE hStdinRd, hStdinWr, hStdoutRd, hStdoutWr, hStderr;
  SECURITY_ATTRIBUTES saAttr;
  DWORD childPid = 0;
  int fd;

  saAttr.nLength = sizeof(saAttr);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;
  hStderr = GetStdHandle(STD_ERROR_HANDLE);
  if( !CreatePipe(&hStdoutRd, &hStdoutWr, &saAttr, 4096) ){
    win32_fatal_error("cannot create pipe for stdout");
  }
  SetHandleInformation( hStdoutRd, HANDLE_FLAG_INHERIT, FALSE);

  if( !CreatePipe(&hStdinRd, &hStdinWr, &saAttr, 4096) ){
    win32_fatal_error("cannot create pipe for stdin");
  }
  SetHandleInformation( hStdinWr, HANDLE_FLAG_INHERIT, FALSE);

  win32_create_child_process(win32_utf8_to_unicode(zCmd),
                             hStdinRd, hStdoutWr, hStderr,&childPid);
  *pChildPid = childPid;
  fd = _open_osfhandle(PTR_TO_INT(hStdoutRd), 0);
  *ppIn = fdopen(fd, "r");
  fd = _open_osfhandle(PTR_TO_INT(hStdinWr), 0);
  *ppOut = _fdopen(fd, "w");
  CloseHandle(hStdinRd);
  CloseHandle(hStdoutWr);
  return 0;
#else
  int pin[2], pout[2];
  *ppIn = 0;
  *ppOut = 0;
  *pChildPid = 0;

  if( pipe(pin)<0 ){
    return 1;
  }
  if( pipe(pout)<0 ){
    close(pin[0]);
    close(pin[1]);
    return 1;
  }
  *pChildPid = fork();
  if( *pChildPid<0 ){
    close(pin[0]);
    close(pin[1]);
    close(pout[0]);
    close(pout[1]);
    *pChildPid = 0;
    return 1;
  }
  signal(SIGPIPE,SIG_IGN);
  if( *pChildPid==0 ){
    int fd;
    /* This is the child process */
    close(0);
    fd = dup(pout[0]);
    if( fd!=0 ) {
      eputz("popen2() failed to open file descriptor 0\n");
      exit(1);
    }
    close(pout[0]);
    close(pout[1]);
    close(1);
    fd = dup(pin[1]);
    if( fd!=1 ){
      eputz("popen() failed to open file descriptor 1\n");
      exit(1);
    }
    close(pin[0]);
    close(pin[1]);
    if( bDirect ){
      execl(zCmd, zCmd, (char*)0);
    }else{
      execl("/bin/sh", "/bin/sh", "-c", zCmd, (char*)0);
    }
    return 1;
  }else{
    /* This is the parent process */
    close(pin[1]);
    *ppIn = fdopen(pin[0], "r");
    close(pout[0]);
    *ppOut = fdopen(pout[1], "w");
    return 0;
  }
#endif
}

/*
** Close the connection to a child process previously created using
** popen2().
*/
static void pclose2(FILE *pIn, FILE *pOut, int childPid){
#ifdef _WIN32
  /* Not implemented, yet */
  fclose(pIn);
  fclose(pOut);
#else
  fclose(pIn);
  fclose(pOut);
  while( waitpid(0, 0, WNOHANG)>0 ) {}
#endif
}
/*****************************************************************************
** End of the popen2() implementation copied from Fossil *********************
*****************************************************************************/

/*****************************************************************************
** Beginning of the append_escaped_arg() routine, adapted from the Fossil   **
** subroutine nameed blob_append_escaped_arg()                              **
*****************************************************************************/
/*
** ASCII (for reference):
**    x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xa  xb  xc  xd  xe  xf
** 0x ^`  ^a  ^b  ^c  ^d  ^e  ^f  ^g  \b  \t  \n  ()  \f  \r  ^n  ^o
** 1x ^p  ^q  ^r  ^s  ^t  ^u  ^v  ^w  ^x  ^y  ^z  ^{  ^|  ^}  ^~  ^
** 2x ()  !   "   #   $   %   &   '   (   )   *   +   ,   -   .   /
** 3x 0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ?
** 4x @   A   B   C   D   E   F   G   H   I   J   K   L   M   N   O
** 5x P   Q   R   S   T   U   V   W   X   Y   Z   [   \   ]   ^   _
** 6x `   a   b   c   d   e   f   g   h   i   j   k   l   m   n   o
** 7x p   q   r   s   t   u   v   w   x   y   z   {   |   }   ~   ^_
*/

/*
** Meanings for bytes in a filename:
**
**    0      Ordinary character.  No encoding required
**    1      Needs to be escaped
**    2      Illegal character.  Do not allow in a filename
**    3      First byte of a 2-byte UTF-8
**    4      First byte of a 3-byte UTF-8
**    5      First byte of a 4-byte UTF-8
*/
static const char aSafeChar[256] = {
#ifdef _WIN32
/* Windows
** Prohibit:  all control characters, including tab, \r and \n.
** Escape:    (space) " # $ % & ' ( ) * ; < > ? [ ] ^ ` { | }
*/
/*  x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xa  xb  xc  xd  xe  xf  */
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, /* 0x */
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, /* 1x */
     1,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0, /* 2x */
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  0,  1,  1, /* 3x */
     1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 4x */
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  1,  1,  0, /* 5x */
     1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 6x */
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  0,  1, /* 7x */
#else
/* Unix
** Prohibit:  all control characters, including tab, \r and \n
** Escape:    (space) ! " # $ % & ' ( ) * ; < > ? [ \ ] ^ ` { | }
*/
/*  x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xa  xb  xc  xd  xe  xf  */
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, /* 0x */
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, /* 1x */
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0, /* 2x */
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  0,  1,  1, /* 3x */
     1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 4x */
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  0, /* 5x */
     1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 6x */
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  0,  1, /* 7x */
#endif
    /* all bytes 0x80 through 0xbf are unescaped, being secondary
    ** bytes to UTF8 characters.  Bytes 0xc0 through 0xff are the
    ** first byte of a UTF8 character and do get escaped */
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, /* 8x */
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, /* 9x */
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, /* ax */
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, /* bx */
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3, /* cx */
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3, /* dx */
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4, /* ex */
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5  /* fx */
};

/*
** pStr is a shell command under construction.  This routine safely
** appends filename argument zIn.  It returns 0 on success or non-zero
** on any error.
**
** The argument is escaped if it contains white space or other characters
** that need to be escaped for the shell.  If zIn contains characters
** that cannot be safely escaped, then throw a fatal error.
**
** If the isFilename argument is true, then the argument is expected
** to be a filename.  As shell commands commonly have command-line
** options that begin with "-" and since we do not want an attacker
** to be able to invoke these switches using filenames that begin
** with "-", if zIn begins with "-", prepend an additional "./"
** (or ".\\" on Windows).
*/
int append_escaped_arg(sqlite3_str *pStr, const char *zIn, int isFilename){
  int i;
  unsigned char c;
  int needEscape = 0;
  int n = sqlite3_str_length(pStr);
  char *z = sqlite3_str_value(pStr);

  /* Look for illegal byte-sequences and byte-sequences that require
  ** escaping.  No control-characters are allowed.  All spaces and
  ** non-ASCII unicode characters and some punctuation characters require
  ** escaping. */
  for(i=0; (c = (unsigned char)zIn[i])!=0; i++){
    if( aSafeChar[c] ){
      unsigned char x = aSafeChar[c];
      needEscape = 1;
      if( x==2 ){
        /* Bad ASCII character */
        return 1;
      }else if( x>2 ){
        if( (zIn[i+1]&0xc0)!=0x80
         || (x>=4 && (zIn[i+2]&0xc0)!=0x80)
         || (x==5 && (zIn[i+3]&0xc0)!=0x80)
        ){
          /* Bad UTF8 character */
          return 1;
        }
        i += x-2;
      }
    }
  }

  /* Separate from the previous argument by a space */
  if( n>0 && !isspace(z[n-1]) ){
    sqlite3_str_appendchar(pStr, 1, ' ');
  }

  /* Check for characters that need quoting */
  if( !needEscape ){
    if( isFilename && zIn[0]=='-' ){
      sqlite3_str_appendchar(pStr, 1, '.');
#if defined(_WIN32)
      sqlite3_str_appendchar(pStr, 1, '\\');
#else
      sqlite3_str_appendchar(pStr, 1, '/');
#endif
    }
    sqlite3_str_appendall(pStr, zIn);
  }else{
#if defined(_WIN32)
    /* Quoting strategy for windows:
    ** Put the entire name inside of "...".  Any " characters within
    ** the name get doubled.
    */
    sqlite3_str_appendchar(pStr, 1, '"');
    if( isFilename && zIn[0]=='-' ){
      sqlite3_str_appendchar(pStr, 1, '.');
      sqlite3_str_appendchar(pStr, 1, '\\');
    }else if( zIn[0]=='/' ){
      sqlite3_str_appendchar(pStr, 1, '.');
    }
    for(i=0; (c = (unsigned char)zIn[i])!=0; i++){
      sqlite3_str_appendchar(pStr, 1, (char)c);
      if( c=='"' ) sqlite3_str_appendchar(pStr, 1, '"');
      if( c=='\\' ) sqlite3_str_appendchar(pStr, 1, '\\');
      if( c=='%' && isFilename ) sqlite3_str_append(pStr, "%cd:~,%", 7);
    }
    sqlite3_str_appendchar(pStr, 1, '"');
#else
    /* Quoting strategy for unix:
    ** If the name does not contain ', then surround the whole thing
    ** with '...'.   If there is one or more ' characters within the
    ** name, then put \ before each special character.
    */
    if( strchr(zIn,'\'') ){
      if( isFilename && zIn[0]=='-' ){
        sqlite3_str_appendchar(pStr, 1, '.');
        sqlite3_str_appendchar(pStr, 1, '/');
      }
      for(i=0; (c = (unsigned char)zIn[i])!=0; i++){
        if( aSafeChar[c] && aSafeChar[c]!=2 ){
          sqlite3_str_appendchar(pStr, 1, '\\');
        }
        sqlite3_str_appendchar(pStr, 1, (char)c);
      }
    }else{
      sqlite3_str_appendchar(pStr, 1, '\'');
      if( isFilename && zIn[0]=='-' ){
        sqlite3_str_appendchar(pStr, 1, '.');
        sqlite3_str_appendchar(pStr, 1, '/');
      }
      sqlite3_str_appendall(pStr, zIn);
      sqlite3_str_appendchar(pStr, 1, '\'');
    }
#endif
  }
  return 0;
}
/*****************************************************************************
** End of the append_escaped_arg() routine, adapted from the Fossil         **
*****************************************************************************/

/*
** Return the tail of a file pathname.  The tail is the last component
** of the path.  For example, the tail of "/a/b/c.d" is "c.d".
*/
const char *file_tail(const char *z){
  const char *zTail = z;
  if( !zTail ) return 0;
  while( z[0] ){
    if( z[0]=='/' ) zTail = &z[1];
    z++;
  }
  return zTail;
}


/* Read a single big-endian 32-bit unsigned integer from the input
** stream.  Return 0 on success and 1 if there are any errors.
*/
static int readUint32(SQLiteRsync *p, unsigned int *pU){
  unsigned char buf[4];
  if( fread(buf, sizeof(buf), 1, p->pIn)==1 ){
    *pU = (buf[0]<<24) | (buf[1]<<16) | (buf[2]<<8) | buf[3];
    p->nIn += 4;
    return 0;
  }else{
    p->nErr++;
    return 1;
  }
}

/* Write a single big-endian 32-bit unsigned integer to the output stream.
** Return 0 on success and 1 if there are any errors.
*/
static int writeUint32(SQLiteRsync *p, unsigned int x){
  unsigned char buf[4];
  buf[3] = x & 0xff;
  x >>= 8;
  buf[2] = x & 0xff;
  x >>= 8;
  buf[1] = x & 0xff;
  x >>= 8;
  buf[0] = x;
  if( fwrite(buf, sizeof(buf), 1, p->pOut)!=1 ){
    p->nErr++;
    return 1;
  }
  p->nOut += 4;
  return 0;
}

/* Read a single byte from the wire.
*/
int readByte(SQLiteRsync *p){
  int c = fgetc(p->pIn);
  if( c!=EOF ) p->nIn++;
  return c;
}

/* Write a single byte into the wire.
*/
void writeByte(SQLiteRsync *p, int c){
  fputc(c, p->pOut);
  p->nOut++;
}

/* Read a power of two encoded as a single byte.
*/
int readPow2(SQLiteRsync *p){
  int x = readByte(p);
  if( x>=32 ){
    p->nErr++;
    return 0;
  }
  return 1<<x;
}

/* Write a power-of-two value onto the wire as a single byte.
*/
void writePow2(SQLiteRsync *p, int c){
  int n;
  if( c<0 || (c&(c-1))!=0 ){
    p->nErr++;
  }
  for(n=0; c>1; n++){ c /= 2; }
  writeByte(p, n);
}

/* Read an array of bytes from the wire.
*/
void readBytes(SQLiteRsync *p, int nByte, void *pData){
  if( fread(pData, 1, nByte, p->pIn)==nByte ){
    p->nIn += nByte;
  }else{
    p->nErr++;
  }
}

/* Write an array of bytes onto the wire.
*/
void writeBytes(SQLiteRsync *p, int nByte, const void *pData){
  if( fwrite(pData, 1, nByte, p->pOut)==nByte ){
    p->nOut += nByte;
  }else{
    p->nErr++;
  }
}

/* Report an error.
**
** If this happens on the remote side, we send back a *_ERROR
** message.  On the local side, the error message goes to stderr.
*/
static void reportError(SQLiteRsync *p, const char *zFormat, ...){
  va_list ap;
  char *zMsg;
  unsigned int nMsg;
  va_start(ap, zFormat);
  zMsg = sqlite3_vmprintf(zFormat, ap);
  va_end(ap);
  nMsg = zMsg ? (unsigned int)strlen(zMsg) : 0;
  if( p->isRemote ){
    if( p->isReplica ){
      putc(REPLICA_ERROR, p->pOut);
    }else{
      putc(ORIGIN_ERROR, p->pOut);
    }
    writeUint32(p, nMsg);
    writeBytes(p, nMsg, zMsg);
    fflush(p->pOut);
  }else{
    eputf("%s\n", zMsg);
  }
  sqlite3_free(zMsg);
  p->nErr++;
}

/* Send an informational message.
**
** If this happens on the remote side, we send back a *_MSG
** message.  On the local side, the message goes to stdout.
*/
static void infoMsg(SQLiteRsync *p, const char *zFormat, ...){
  va_list ap;
  char *zMsg;
  unsigned int nMsg;
  va_start(ap, zFormat);
  zMsg = sqlite3_vmprintf(zFormat, ap);
  va_end(ap);
  nMsg = zMsg ? (unsigned int)strlen(zMsg) : 0;
  if( p->isRemote ){
    if( p->isReplica ){
      putc(REPLICA_MSG, p->pOut);
    }else{
      putc(ORIGIN_MSG, p->pOut);
    }
    writeUint32(p, nMsg);
    writeBytes(p, nMsg, zMsg);
    fflush(p->pOut);
  }else{
    oputf("%s\n", zMsg);
  }
  sqlite3_free(zMsg);
}

/* Receive and report an error message coming from the other side.
*/
static void readAndDisplayMessage(SQLiteRsync *p, int c){
  unsigned int n = 0;
  char *zMsg;
  const char *zPrefix;
  if( c==ORIGIN_ERROR || c==REPLICA_ERROR ){
    zPrefix = "ERROR: ";
    p->nErr++;
  }else{
    zPrefix = "";
  }
  readUint32(p, &n);
  if( n==0 ){
    eputz("ERROR: unknown (possibly out-of-memory)\n");
  }else{
    zMsg = sqlite3_malloc64( n+1 );
    if( zMsg==0 ){
      eputz("ERROR: out-of-memory\n");
      return;
    }
    memset(zMsg, 0, n+1);
    readBytes(p, n, zMsg);
    eputf("%s%s\n", zPrefix, zMsg);
    sqlite3_free(zMsg);
  }
}

/* Construct a new prepared statement.  Report an error and return NULL
** if anything goes wrong.
*/
static sqlite3_stmt *prepareStmtVA(
  SQLiteRsync *p,
  char *zFormat,
  va_list ap
){
  sqlite3_stmt *pStmt = 0;
  char *zSql;
  char *zToFree = 0;
  int rc;

  if( strchr(zFormat,'%') ){
    zSql = sqlite3_vmprintf(zFormat, ap);
    if( zSql==0 ){
      reportError(p, "out-of-memory");
      return 0;
    }else{
      zToFree = zSql;
    }
  }else{
    zSql = zFormat;
  }
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
  if( rc || pStmt==0 ){
    reportError(p, "unable to prepare SQL [%s]: %s", zSql,
                sqlite3_errmsg(p->db));
    sqlite3_finalize(pStmt);
    pStmt = 0;
  }
  if( zToFree ) sqlite3_free(zToFree);
  return pStmt;
}
static sqlite3_stmt *prepareStmt(
  SQLiteRsync *p,
  char *zFormat,
  ...
){
  sqlite3_stmt *pStmt;
  va_list ap;
  va_start(ap, zFormat);
  pStmt = prepareStmtVA(p, zFormat, ap);
  va_end(ap);
  return pStmt;
}

/* Run a single SQL statement
*/
static void runSql(SQLiteRsync *p, char *zSql, ...){
  sqlite3_stmt *pStmt;
  va_list ap;

  va_start(ap, zSql);
  pStmt = prepareStmtVA(p, zSql, ap);
  va_end(ap);
  if( pStmt ){
    int rc = sqlite3_step(pStmt);
    if( rc==SQLITE_ROW ) rc = sqlite3_step(pStmt);
    if( rc!=SQLITE_OK && rc!=SQLITE_DONE ){
      reportError(p, "SQL statement [%s] failed: %s", zSql,
                  sqlite3_errmsg(p->db));
    }
    sqlite3_finalize(pStmt);
  }
}

/* Run an SQL statement that returns a single unsigned 32-bit integer result
*/
static int runSqlReturnUInt(
  SQLiteRsync *p,
  unsigned int *pRes,
  char *zSql,
  ...
){
  sqlite3_stmt *pStmt;
  int res = 0;
  va_list ap;

  va_start(ap, zSql);
  pStmt = prepareStmtVA(p, zSql, ap);
  va_end(ap);
  if( pStmt==0 ){
    res = 1;
  }else{
    int rc = sqlite3_step(pStmt);
    if( rc==SQLITE_ROW ){
      *pRes = (unsigned int)(sqlite3_column_int64(pStmt, 0)&0xffffffff);
    }else{
      reportError(p, "SQL statement [%s] failed: %s", zSql,
                  sqlite3_errmsg(p->db));
      res = 1;
    }
    sqlite3_finalize(pStmt);
  }
  return res;
}

/* Run an SQL statement that returns a single TEXT value that is no more
** than 99 bytes in length.
*/
static int runSqlReturnText(
  SQLiteRsync *p,
  char *pRes,
  char *zSql,
  ...
){
  sqlite3_stmt *pStmt;
  int res = 0;
  va_list ap;

  va_start(ap, zSql);
  pStmt = prepareStmtVA(p, zSql, ap);
  va_end(ap);
  pRes[0] = 0;
  if( pStmt==0 ){
    res = 1;
  }else{
    int rc = sqlite3_step(pStmt);
    if( rc==SQLITE_ROW ){
      const unsigned char *a = sqlite3_column_text(pStmt, 0);
      int n;
      if( a==0 ){
        pRes[0] = 0;
      }else{
        n = sqlite3_column_bytes(pStmt, 0);
        if( n>99 ) n = 99;
        memcpy(pRes, a, n);
        pRes[n] = 0;
      }
    }else{
      reportError(p, "SQL statement [%s] failed: %s", zSql,
                  sqlite3_errmsg(p->db));
      res = 1;
    }
    sqlite3_finalize(pStmt);
  }
  return res;
}

/* Close the database connection associated with p
*/
static void closeDb(SQLiteRsync *p){
  if( p->db ){
    sqlite3_close(p->db);
    p->db = 0;
  }
}

/*
** Run the origin-side protocol.
**
** Begin by sending the ORIGIN_BEGIN message with two arguments,
** nPage, and szPage.  Then enter a loop responding to message from
** the replica:
**
**    REPLICA_ERROR  size  text
**
**         Report an error from the replica and quit
**
**    REPLICA_END
**
**         The replica is terminating.  Stop processing now.
**
**    REPLICA_HASH  hash
**
**         The argument is the 20-byte SHA1 hash for the next page
**         page hashes appear in sequential order with no gaps.
**
**    REPLICA_READY
**
**         The replica has sent all the hashes that it intends to send.
**         This side (the origin) can now start responding with page
**         content for pages that do not have a matching hash.
*/
static void originSide(SQLiteRsync *p){
  int rc = 0;
  int c = 0;
  unsigned int nPage = 0;
  unsigned int iPage = 0;
  unsigned int szPg = 0;
  sqlite3_stmt *pCkHash = 0;
  char buf[200];

  p->isReplica = 0;
  if( p->bCommCheck ){
    infoMsg(p, "origin  zOrigin=%Q zReplica=%Q isRemote=%d protocol=%d",
               p->zOrigin, p->zReplica, p->isRemote, PROTOCOL_VERSION);
    writeByte(p, ORIGIN_END);
    fflush(p->pOut);
  }else{
    /* Open the ORIGIN database. */
    rc = sqlite3_open_v2(p->zOrigin, &p->db, SQLITE_OPEN_READWRITE, 0);
    if( rc ){
      reportError(p, "unable to open origin database file \"%s\": %s",
                  sqlite3_errmsg(p->db));
      closeDb(p);
      return;
    }
    sqlite3_sha_init(p->db, 0, 0);
    runSql(p, "BEGIN");
    runSqlReturnText(p, buf, "PRAGMA journal_mode");
    if( sqlite3_stricmp(buf,"wal")!=0 ){
      reportError(p, "Origin database is not in WAL mode");
    }
    runSqlReturnUInt(p, &nPage, "PRAGMA page_count");
    runSqlReturnUInt(p, &szPg, "PRAGMA page_size");

    if( p->nErr==0 ){
      /* Send the ORIGIN_BEGIN message */
      writeByte(p, ORIGIN_BEGIN);
      writeByte(p, PROTOCOL_VERSION);
      writePow2(p, szPg);
      writeUint32(p, nPage);
      fflush(p->pOut);
      p->nPage = nPage;
      p->szPage = szPg;
      p->iProtocol = PROTOCOL_VERSION;
    }
  }

  /* Respond to message from the replica */
  while( p->nErr==0 && (c = readByte(p))!=EOF && c!=REPLICA_END ){
    switch( c ){
      case REPLICA_BEGIN: {
        /* This message is only sent if the replica received an origin-protocol
        ** that is larger than what it knows about.  The replica sends back
        ** a counter-proposal of an earlier protocol which the origin can
        ** accept by resending a new ORIGIN_BEGIN. */
        p->iProtocol = readByte(p);
        writeByte(p, ORIGIN_BEGIN);
        writeByte(p, p->iProtocol);
        writePow2(p, p->szPage);
        writeUint32(p, p->nPage);
        break;
      }
      case REPLICA_MSG:
      case REPLICA_ERROR: {
        readAndDisplayMessage(p, c);
        break;
      }
      case REPLICA_HASH: {
        if( pCkHash==0 ){
          runSql(p, "CREATE TEMP TABLE badHash(pgno INTEGER PRIMARY KEY)");
          pCkHash = prepareStmt(p,
            "INSERT INTO badHash SELECT pgno FROM sqlite_dbpage('main')"
            " WHERE pgno=?1 AND sha1b(data)!=?2"
          );
          if( pCkHash==0 ) break;
        }
        p->nHashSent++;
        iPage++;
        sqlite3_bind_int64(pCkHash, 1, iPage);
        readBytes(p, 20, buf);
        sqlite3_bind_blob(pCkHash, 2, buf, 20, SQLITE_STATIC);
        rc = sqlite3_step(pCkHash);
        if( rc!=SQLITE_DONE ){
          reportError(p, "SQL statement [%s] failed: %s",
                      sqlite3_sql(pCkHash), sqlite3_errmsg(p->db));
        }
        sqlite3_reset(pCkHash);
        break;
      }
      case REPLICA_READY: {
        sqlite3_stmt *pStmt;
        sqlite3_finalize(pCkHash);
        pCkHash = 0;
        pStmt = prepareStmt(p,
               "SELECT pgno, data"
               "  FROM badHash JOIN sqlite_dbpage('main') USING(pgno) "
               "UNION ALL "
               "SELECT pgno, data"
               "  FROM sqlite_dbpage('main')"
               " WHERE pgno>%d",
               iPage);
        if( pStmt==0 ) break;
        while( sqlite3_step(pStmt)==SQLITE_ROW && p->nErr==0 ){
          const void *pContent = sqlite3_column_blob(pStmt, 1);
          writeByte(p, ORIGIN_PAGE);
          writeUint32(p, (unsigned int)sqlite3_column_int64(pStmt, 0));
          writeBytes(p, szPg, pContent);
          p->nPageSent++;
        }
        sqlite3_finalize(pStmt);
        writeByte(p, ORIGIN_TXN);
        writeUint32(p, nPage);
        writeByte(p, ORIGIN_END);
        goto origin_end;
      }
      default: {
        reportError(p, "Origin side received unknown message: 0x%02x", c);
        break;
      }
    }
  }

origin_end:
  if( pCkHash ) sqlite3_finalize(pCkHash);
  closeDb(p);
}

/*
** Run the replica-side protocol.  The protocol is passive in the sense
** that it only response to message from the origin side.
**
**    ORIGIN_BEGIN  idProtocol szPage nPage
**
**         The origin is reporting the protocol version number, the size of
**         each page in the origin database (sent as a single-byte power-of-2),
**         and the number of pages in the origin database.
**         This procedure checks compatibility, and if everything is ok,
**         it starts sending hashes of pages already present back to the origin.
**
**    ORIGIN_ERROR  size text
**
**         Report the received error and quit.
**
**    ORIGIN_PAGE  pgno content
**
**         Update the content of the given page.
**
**    ORIGIN_TXN   pgno
**
**         Close the update transaction.  The total database size is pgno
**         pages.
**
**    ORIGIN_END
**
**         Expect no more transmissions from the origin.
*/
static void replicaSide(SQLiteRsync *p){
  int c;
  sqlite3_stmt *pIns = 0;
  unsigned int szOPage = 0;
  char buf[65536];

  p->isReplica = 1;
  if( p->bCommCheck ){
    infoMsg(p, "replica zOrigin=%Q zReplica=%Q isRemote=%d protocol=%d",
               p->zOrigin, p->zReplica, p->isRemote, PROTOCOL_VERSION);
    writeByte(p, REPLICA_END);
    fflush(p->pOut);
  }

  /* Respond to message from the origin.  The origin will initiate the
  ** the conversation with an ORIGIN_BEGIN message.
  */
  while( p->nErr==0 && (c = readByte(p))!=EOF && c!=ORIGIN_END ){
    switch( c ){
      case ORIGIN_MSG:
      case ORIGIN_ERROR: {
        readAndDisplayMessage(p, c);
        break;
      }
      case ORIGIN_BEGIN: {
        unsigned int nOPage = 0;
        unsigned int nRPage = 0, szRPage = 0;
        int rc = 0;
        sqlite3_stmt *pStmt = 0;

        closeDb(p);
        p->iProtocol = readByte(p);
        szOPage = readPow2(p);
        readUint32(p, &nOPage);
        if( p->nErr ) break;
        if( p->iProtocol>PROTOCOL_VERSION ){
          /* If the protocol version on the origin side is larger, send back
          ** a REPLICA_BEGIN message with the protocol version number of the
          ** replica side.  This gives the origin an opportunity to resend
          ** a new ORIGIN_BEGIN with a reduced protocol version. */
          writeByte(p, REPLICA_BEGIN);
          writeByte(p, PROTOCOL_VERSION);
          break;
        }
        p->nPage = nOPage;
        p->szPage = szOPage;
        rc = sqlite3_open(p->zReplica, &p->db);
        if( rc ){
          reportError(p, "cannot open replica database \"%s\": %s",
                      p->zReplica, sqlite3_errmsg(p->db));
          closeDb(p);
          break;
        }
        sqlite3_sha_init(p->db, 0, 0);
        if( runSqlReturnUInt(p, &nRPage, "PRAGMA page_count") ){
          break;
        }
        if( nRPage==0 ){
          runSql(p, "PRAGMA page_size=%u", szOPage);
          runSql(p, "PRAGMA journal_mode=WAL");
        }
        runSql(p, "BEGIN IMMEDIATE");
        runSqlReturnText(p, buf, "PRAGMA journal_mode");
        if( strcmp(buf, "wal")!=0 ){
          reportError(p, "replica is not in WAL mode");
          break;
        }
        runSqlReturnUInt(p, &nRPage, "PRAGMA page_count");
        runSqlReturnUInt(p, &szRPage, "PRAGMA page_size");
        if( szRPage!=szOPage ){
          reportError(p, "page size mismatch; origin is %d bytes and "
                         "replica is %d bytes", szOPage, szRPage);
          break;
        }

        pStmt = prepareStmt(p,
                   "SELECT sha1b(data) FROM sqlite_dbpage"
                   " WHERE pgno<=min(%d,%d)"
                   " ORDER BY pgno", nRPage, nOPage);
        while( sqlite3_step(pStmt)==SQLITE_ROW && p->nErr==0 ){
          const unsigned char *a = sqlite3_column_blob(pStmt, 0);
          writeByte(p, REPLICA_HASH);
          writeBytes(p, 20, a);
          p->nHashSent++;
        }
        sqlite3_finalize(pStmt);
        writeByte(p, REPLICA_READY);
        fflush(p->pOut);
        runSql(p, "PRAGMA writable_schema=ON");
        break;
      }
      case ORIGIN_TXN: {
        unsigned int nOPage = 0;
        readUint32(p, &nOPage);
        if( pIns==0 ){
          /* Nothing has changed */
          runSql(p, "COMMIT");
        }else if( p->nErr ){
          runSql(p, "ROLLBACK");
        }else{
          int rc;
          sqlite3_bind_int64(pIns, 1, nOPage);
          sqlite3_bind_null(pIns, 2);
          rc = sqlite3_step(pIns);
          if( rc!=SQLITE_DONE ){
            reportError(p, "SQL statement [%s] failed: %s",
                   sqlite3_sql(pIns), sqlite3_errmsg(p->db));
          }
          sqlite3_reset(pIns);
          runSql(p, "COMMIT");
        }
        break;
      }
      case ORIGIN_PAGE: {
        unsigned int pgno = 0;
        int rc;
        readUint32(p, &pgno);
        if( p->nErr ) break;
        if( pIns==0 ){
          pIns = prepareStmt(p,
            "INSERT INTO sqlite_dbpage(pgno,data,schema) VALUES(?1,?2,'main')"
          );
          if( pIns==0 ) break;
        }
        readBytes(p, szOPage, buf);
        if( p->nErr ) break;
        p->nPageSent++;
        sqlite3_bind_int64(pIns, 1, pgno);
        sqlite3_bind_blob(pIns, 2, buf, szOPage, SQLITE_STATIC);
        rc = sqlite3_step(pIns);
        if( rc!=SQLITE_DONE ){
          reportError(p, "SQL statement [%s] failed: %s",
                 sqlite3_sql(pIns), sqlite3_errmsg(p->db));
        }
        sqlite3_reset(pIns);
        break;
      }
      default: {
        reportError(p, "Replica side received unknown message: 0x%02x", c);
        break;
      }
    }
  }

  if( pIns ) sqlite3_finalize(pIns);
  closeDb(p);
}

/*
** The argument might be -vvv...vv with any number of "v"s.  Return
** the number of "v"s.  Return 0 if the argument is not a -vvv...v.
*/
static int numVs(const char *z){
  int n = 0;
  if( z[0]!='-' ) return 0;
  z++;
  if( z[0]=='-' ) z++;
  while( z[0]=='v' ){ n++; z++; }
  if( z[0]==0 ) return n;
  return 0;
}

/*
** Get the argument to an --option.  Throw an error and die if no argument
** is available.
*/
static const char *cmdline_option_value(int argc, const char * const*argv,
                                        int i){
  if( i==argc ){
    eputf("%s: Error: missing argument to %s\n",
          argv[0], argv[argc-1]);
    exit(1);
  }
  return argv[i];
}


/*
** Parse command-line arguments.  Dispatch subroutines to do the
** requested work.
**
** Input formats:
**
**  (1)    sqlite3-rsync  FILENAME1  USER@HOST:FILENAME2
**
**  (2)    sqlite3-rsync  USER@HOST:FILENAME1  FILENAME2
**
**  (3)    sqlite3-rsync --origin FILENAME1
**
**  (4)    sqlite3-rsync --replica FILENAME2
**
** The user types (1) or (2).  SSH launches (3) or (4).
**
** If (1) is seen then popen2 is used launch (4) on the remote and
** originSide() is called locally.
**
** If (2) is seen, then popen2() is used to launch (3) on the remote
** and replicaSide() is run locally.
**
** If (3) is seen, call originSide() on stdin and stdout.
**
q** If (4) is seen, call replicaSide() on stdin and stdout.
*/
int main(int argc, char const * const *argv){
  int isOrigin = 0;
  int isReplica = 0;
  int i;
  SQLiteRsync ctx;
  char *zDiv;
  FILE *pIn = 0;
  FILE *pOut = 0;
  int childPid = 0;
  const char *zSsh = "ssh";
  const char *zExe = "sqlite3-rsync";
  char *zCmd = 0;

#define cli_opt_val cmdline_option_value(argc, argv, ++i)
  memset(&ctx, 0, sizeof(ctx));
  setOutputStream(stdout);
  setErrorStream(stderr);
  for(i=1; i<argc; i++){
    const char *z = argv[i];
    if( strcmp(z,"--origin")==0 ){
      isOrigin = 1;
      continue;
    }
    if( strcmp(z,"--replica")==0 ){
      isReplica = 1;
      continue;
    }
    if( numVs(z) ){
      ctx.eVerbose += numVs(z);
      continue;
    }
    if( strcmp(z, "--ssh")==0 ){
      zSsh = cli_opt_val;
      continue;
    }
    if( strcmp(z, "--exe")==0 ){
      zExe = cli_opt_val;
      continue;
    }
    if( strcmp(z, "-help")==0 || strcmp(z, "--help")==0
     || strcmp(z, "-?")==0
    ){
      oputf("%s", zUsage);
      return 0;
    }
    if( z[0]=='-' ){
      if( strcmp(z,"--commcheck")==0 ){  /* DEBUG ONLY */
        /* Run a communication check with the remote side.  Do not attempt
        ** to exchange any database connection */
        ctx.bCommCheck = 1;
        continue;
      }
      if( strcmp(z,"--arg-escape-check")==0 ){  /* DEBUG ONLY */
        /* Test the append_escaped_arg() routine by using it to render a
        ** copy of the input command-line, assuming all arguments except
        ** this one are filenames. */
        sqlite3_str *pStr = sqlite3_str_new(0);
        int k;
        for(k=0; k<argc; k++){
          append_escaped_arg(pStr, argv[k], i!=k);
        }
        oputf("%s\n", sqlite3_str_value(pStr));
        return 0;
      }
      eputf("unknown option: \"%s\". Use --help for more detail.\n", z);
      return 1;
    }
    if( ctx.zOrigin==0 ){
      ctx.zOrigin = z;
    }else if( ctx.zReplica==0 ){
      ctx.zReplica = z;
    }else{
      eputf("Unknown argument: \"%s\"\n", z);
      return 1;
    }
  }
  if( ctx.zOrigin==0 ){
    eputz("missing ORIGIN database filename\n");
    return 1;
  }
  if( ctx.zReplica==0 ){
    eputz("missing REPLICA database filename\n");
    return 1;
  }
  if( isOrigin && isReplica ){
    eputz("bad option combination\n");
    return 1;
  }
  if( isOrigin ){
    ctx.pIn = stdin;
    ctx.pOut = stdout;
    ctx.isRemote = 1;
    originSide(&ctx);
    return 0;
  }
  if( isReplica ){
    ctx.pIn = stdin;
    ctx.pOut = stdout;
    ctx.isRemote = 1;
    replicaSide(&ctx);
    return 0;
  }
  if( ctx.zReplica==0 ){
    eputz("missing REPLICA database filename\n");
    return 1;
  }
  zDiv = strchr(ctx.zOrigin,':');
  if( zDiv ){
    if( strchr(ctx.zReplica,':')!=0 ){
      eputz(
         "At least one of ORIGIN and REPLICA must be a local database\n"
         "You provided two remote databases.\n");
      return 1;
    }
    /* Remote ORIGIN and local REPLICA */
    sqlite3_str *pStr = sqlite3_str_new(0);
    append_escaped_arg(pStr, zSsh, 1);
    sqlite3_str_appendf(pStr, " -e none");
    *(zDiv++) = 0;
    append_escaped_arg(pStr, ctx.zOrigin, 0);
    append_escaped_arg(pStr, zExe, 1);
    append_escaped_arg(pStr, "--origin", 0);
    if( ctx.bCommCheck ){
      append_escaped_arg(pStr, "--commcheck", 0);
      if( ctx.eVerbose==0 ) ctx.eVerbose = 1;
    }
    append_escaped_arg(pStr, zDiv, 1);
    append_escaped_arg(pStr, file_tail(ctx.zReplica), 1);
    zCmd = sqlite3_str_finish(pStr);
    if( ctx.eVerbose ) oputf("%s\n", zCmd);
    if( popen2(zCmd, &ctx.pIn, &ctx.pOut, &childPid, 0) ){
      eputf("Could not start auxiliary process: %s\n", zCmd);
      return 1;
    }
    replicaSide(&ctx);
  }else if( (zDiv = strchr(ctx.zReplica,':'))!=0 ){
    /* Local ORIGIN and remote REPLICA */
    sqlite3_str *pStr = sqlite3_str_new(0);
    append_escaped_arg(pStr, zSsh, 1);
    sqlite3_str_appendf(pStr, " -e none");
    *(zDiv++) = 0;
    append_escaped_arg(pStr, ctx.zReplica, 0);
    append_escaped_arg(pStr, zExe, 1);
    append_escaped_arg(pStr, "--replica", 0);
    if( ctx.bCommCheck ){
      append_escaped_arg(pStr, "--commcheck", 0);
      if( ctx.eVerbose==0 ) ctx.eVerbose = 1;
    }
    append_escaped_arg(pStr, file_tail(ctx.zOrigin), 1);
    append_escaped_arg(pStr, zDiv, 1);
    zCmd = sqlite3_str_finish(pStr);
    if( ctx.eVerbose ) oputf("%s\n", zCmd);
    if( popen2(zCmd, &ctx.pIn, &ctx.pOut, &childPid, 0) ){
      eputf("Could not start auxiliary process: %s\n", zCmd);
      return 1;
    }
    originSide(&ctx);
  }else{
    /* Local ORIGIN and REPLICA */
    sqlite3_str *pStr = sqlite3_str_new(0);
    append_escaped_arg(pStr, argv[0], 1);
    append_escaped_arg(pStr, "--replica", 0);
    if( ctx.bCommCheck ){
      append_escaped_arg(pStr, "--commcheck", 0);
    }
    append_escaped_arg(pStr, ctx.zOrigin, 1);
    append_escaped_arg(pStr, ctx.zReplica, 1);
    zCmd = sqlite3_str_finish(pStr);
    if( ctx.eVerbose ) oputf("%s\n", zCmd);
    if( popen2(zCmd, &ctx.pIn, &ctx.pOut, &childPid, 0) ){
      eputf("Could not start auxiliary process: %s\n", zCmd);
      return 1;
    }
    originSide(&ctx);
  }
  if( ctx.eVerbose ){
    if( ctx.nErr ) oputf("%d errors, ", ctx.nErr);
    oputf("%lld bytes sent, %lld bytes received\n", ctx.nOut, ctx.nIn);
    if( ctx.eVerbose>=2 ){
      oputf("Database is %u pages of %u bytes each.\n",
             ctx.nPage, ctx.szPage);
      oputf("Sent %u hashes, %u page contents\n",
             ctx.nHashSent, ctx.nPageSent);
    }
  }
  sqlite3_free(zCmd);
  if( pIn!=0 && pOut!=0 ){
    pclose2(pIn, pOut, childPid);
  }
  return 0;
}
