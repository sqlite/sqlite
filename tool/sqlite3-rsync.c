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
#include "sqlite3.h"

static const char zUsage[] = 
  "sqlite3-rsync ORIGIN REPLICA\n"
  "\n"
  "One of ORIGIN or REPLICA is a pathname to a database on the local\n"
  "machine and the other is of the form \"USER@HOST:PATH\" describing\n"
  "a database on a remote machine.  This utility makes REPLICA into a\n"
  "copy of ORIGIN\n"
;

/* Context for the run */
typedef struct SQLiteRsync SQLiteRsync;
struct SQLiteRsync {
  const char *zOrigin;     /* Name of the origin */
  const char *zReplica;    /* Name of the replica */
  FILE *pOut;              /* Transmit to the other side */
  FILE *pIn;               /* Receive from the other side */
  sqlite3_uint64 nOut;     /* Bytes transmitted */
  sqlite3_uint64 nIn;      /* Bytes received */
  int eVerbose;            /* Bigger for more output.  0 means none. */
  int bCommCheck;          /* True to debug the communication protocol */
};

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
  fprintf(stderr, "%s", zMsg);
  exit(1);
}
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

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
      fprintf(stderr,"popen2() failed to open file descriptor 0");
      exit(1);
    }
    close(pout[0]);
    close(pout[1]);
    close(1);
    fd = dup(pin[1]);
    if( fd!=1 ){
      fprintf(stderr,"popen() failed to open file descriptor 1");
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


/* For Debugging, specifically for --commcheck:
**
** Read a single line of text from p->pIn.  Write this to standard
** output if and only if p->eVerbose>0.
*/
static void echoOneLine(SQLiteRsync *p){
  char zLine[1000];
  if( fgets(zLine, sizeof(zLine), p->pIn) ){
    if( p->eVerbose ) printf("GOT: %s", zLine);
  }
}

/*
** Run the origin-side protocol.
**
**    1.  Send the origin-begin message
**    2.  Receive replica-begin message
**         -  Error check and abort if necessary
**    3.  Receive replica-hash messages
**    4.  BEGIN
**    5.  Send changed pages
**    6.  COMMIT
**    7.  Send origin-end message
*/
static void originSide(SQLiteRsync *p){
  if( p->bCommCheck ){
    fprintf(p->pOut, "sqlite3-rsync origin-begin %s\n", p->zOrigin);
    fflush(p->pOut);
    echoOneLine(p);
    fprintf(p->pOut, "origin-end\n");
    fflush(p->pOut);
    echoOneLine(p);
    return;
  }
}

/*
** Run the replica-side protocol.
**
**    1.  Receive the origin-begin message
**         -  Error check.  If unable to continue, send replica-error and quit
**    2.  BEGIN IMMEDIATE
**    3.  Send replica-begin message
**    4.  Send replica-hash messages
**    5.  Receive changed pages and apply them
**    6.  Receive origin-end message
**    7.  COMMIT
*/
static void replicaSide(SQLiteRsync *p){
  if( p->bCommCheck ){
    echoOneLine(p);
    fprintf(p->pOut, "replica-begin %s\n", p->zReplica);
    fflush(p->pOut);
    echoOneLine(p);
    fprintf(p->pOut, "replica-end\n");
    fflush(p->pOut);
    return;
  }
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
** If (4) is seen, call replicaSide() on stdin and stdout.
*/
int main(int argc, char **argv){
  int isOrigin = 0;
  int isReplica = 0;
  int i;
  SQLiteRsync ctx;
  char *zDiv;
  FILE *pIn = 0;
  FILE *pOut = 0;
  int childPid = 0;
  const char *zSsh = "ssh";
  const char *zExe = argv[0];
  char *zCmd = 0;

  memset(&ctx, 0, sizeof(ctx));
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
    if( strcmp(z, "-v")==0 ){
      ctx.eVerbose++;
      continue;
    }
    if( strcmp(z, "--ssh")==0 ){
      zSsh = argv[++i];
      continue;
    }
    if( strcmp(z, "--exe")==0 ){
      zSsh = argv[++i];
      continue;
    }
    if( strcmp(z, "-help")==0 || strcmp(z, "--help")==0
     || strcmp(z, "-?")==0
    ){
      printf("%s", zUsage);
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
        printf("%s\n", sqlite3_str_value(pStr));
        return 0;
      }
      fprintf(stderr,
         "unknown option: \"%s\". Use --help for more detail.\n", z);
      return 1;
    }
    if( ctx.zOrigin==0 ){
      ctx.zOrigin = z;
    }else if( ctx.zReplica==0 ){
      ctx.zReplica = z;
    }else{
      fprintf(stderr, "Unknown argument: \"%s\"\n", z);
      return 1;
    }
  }
  if( ctx.zOrigin==0 ){
    fprintf(stderr, "missing ORIGIN database filename\n");
    return 1;
  }
  if( isOrigin && isReplica ){
    fprintf(stderr, "bad option combination\n");
    return 1;
  }
  if( (isOrigin || isReplica) && ctx.zReplica!=0 ){
    fprintf(stderr, "Unknown argument: \"%s\"\n", ctx.zReplica);
    return 1;
  }
  if( isOrigin ){
    ctx.pIn = stdin;
    ctx.pOut = stdout;
    originSide(&ctx);
    return 0;
  }
  if( isReplica ){
    ctx.zReplica = ctx.zOrigin;
    ctx.zOrigin = 0;
    ctx.pIn = stdin;
    ctx.pOut = stdout;
    replicaSide(&ctx);
    return 0;
  }
  if( ctx.zReplica==0 ){
    fprintf(stderr, "missing REPLICA database filename\n");
    return 1;
  }
  zDiv = strchr(ctx.zOrigin,':');
  if( zDiv ){
    if( strchr(ctx.zReplica,':')!=0 ){
      fprintf(stderr, 
         "At least one of ORIGIN and REPLICA must be a local database\n"
         "You provided two remote databases.\n");
      return 1;
    }
    /* Remote ORIGIN and local REPLICA */
  }else if( (zDiv = strchr(ctx.zReplica,':'))!=0 ){
    /* Local ORIGIN and remote REPLICA */
    printf("%s\n", zSsh);
  }else{
    /* Local ORIGIN and REPLICA */
    sqlite3_str *pStr = sqlite3_str_new(0);
    append_escaped_arg(pStr, zExe, 1);
    append_escaped_arg(pStr, "--replica", 0);
    append_escaped_arg(pStr, ctx.zReplica, 1);
    zCmd = sqlite3_str_finish(pStr);
    if( ctx.eVerbose ) printf("%s\n", zCmd);
    if( popen2(zCmd, &ctx.pIn, &ctx.pOut, &childPid, 0) ){
      fprintf(stderr, "Could not start auxiliary process: %s\n", zCmd);
      return 1;
    }
    originSide(&ctx);
  }
  sqlite3_free(zCmd);
  if( pIn!=0 && pOut!=0 ){
    pclose2(pIn, pOut, childPid);
  }
  return 0;
}
