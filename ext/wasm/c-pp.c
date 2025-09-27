/*
** 2022-11-12:
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**  * May you do good and not evil.
**  * May you find forgiveness for yourself and forgive others.
**  * May you share freely, never taking more than you give.
**
************************************************************************
**
** The C-minus Preprocessor: a truly minimal C-like preprocessor.
** Why? Because C preprocessors _can_ process non-C code but generally make
** quite a mess of it. The purpose of this application is an extremely
** minimal preprocessor with only the most basic functionality of a C
** preprocessor, namely:
**
** - Limited `#if`, where its one argument is a macro name which
**   resolves to true if it's defined, false if it's not. Likewise,
**   `#ifnot` is the inverse. Includes `#else` and `#elif` and
**   `#elifnot`. Such chains are terminated with `#endif`.
**
** - `#define` accepts one or more arguments, the names of
**   macros. Each one is implicitly true.
**
** - `#undef` undefine one or more macros.
**
** - `#error` treats the rest of the line as a fatal error message.
**
** - `#include` treats its argument as a filename token (NOT quoted,
**   though support for quoting may be added later). Some effort is
**   made to prevent recursive inclusion, but that support is both
**   somewhat fragile and possibly completely unnecessary.
**
** - `#pragma` is in place for adding "meta-commands", but it does not
**   yet have any concrete list of documented commands.
**
** - `#savepoint` takes one argument: begin, commit, rollback. Each
**    corresponds to the similarly-named SQLite savepoint feature.
**    (What we're calling "commit" is called "release" in savepoint
**    terminology.)
**
** - `#stderr` outputs its file name, line number, and the remainder
**   of that line to stderr.
**
** - `#//` acts as a single-line comment, noting that there must be no
**   space after the `//` part because `//` is (despite appearances)
**   parsed like a keyword.
**
** The "#" above is symbolic. The keyword delimiter is configurable
** and defaults to "##". Define CMPP_DEFAULT_DELIM to a string when
** compiling to define the default at build-time.
**
** This preprocessor does no expansion of content except within the
** bounds of its `#keywords`.
**
** Design note: this code makes use of sqlite3. Though not _strictly_
** needed in order to implement it, this tool was specifically created
** for use with the sqlite3 project's own JavaScript code, so there's
** no reason not to make use of it to do some of the heavy lifting. It
** does not require any cutting-edge sqlite3 features and should be
** usable with any version which supports `WITHOUT ROWID`.
**
** Author(s):
**
** - Stephan Beal <https://wanderinghorse.net/home/stephan/>
*/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>

#include "sqlite3.h"

#if defined(_WIN32) || defined(WIN32)
#  include <io.h>
#  include <fcntl.h>
#  ifndef access
#    define access(f,m) _access((f),(m))
#  endif
#else
#  include <unistd.h>
#endif

#ifndef CMPP_DEFAULT_DELIM
#define CMPP_DEFAULT_DELIM "##"
#endif

#ifndef CMPP_ATSIGN
#define CMPP_ATSIGN (unsigned char)'@'
#endif

#if 1
#  define CMPP_NORETURN __attribute__((noreturn))
#else
#  define CMPP_NORETURN
#endif

/* Fatally exits the app with the given printf-style message. */
static CMPP_NORETURN void fatalv(char const *zFmt, va_list);
static CMPP_NORETURN void fatal(char const *zFmt, ...);

/** Proxy for free(), for symmetry with cmpp_realloc(). */
static void cmpp_free(void *p);
/** A realloc() proxy which dies fatally on allocation error. */
static void * cmpp_realloc(void * p, unsigned n);
#if 0
/** A malloc() proxy which dies fatally on allocation error. */
static void * cmpp_malloc(unsigned n);
#endif

static void check__oom2(void const *p, char const *zFile, int line){
  if(!p) fatal("Alloc failed at %s:%d", zFile, line);
}
#define check__oom(P) check__oom2((P), __FILE__, __LINE__)

/*
** If p is stdin or stderr then this is a no-op, else it is a
** proxy for fclose(). This is a no-op if p is NULL.
*/
static void FILE_close(FILE *p);
/*
** Works like fopen() but accepts the special name "-" to mean either
** stdin (if zMode indicates a real-only mode) or stdout. Fails
** fatally on error.
*/
static FILE * FILE_open(char const *zName, const char * zMode);
/*
** Reads the entire contents of the given file, allocating it in a
** buffer which gets assigned to `*pOut`. `*nOut` gets assigned the
** length of the output buffer. Fails fatally on error.
*/
static void FILE_slurp(FILE *pFile, unsigned char **pOut,
                       unsigned * nOut);

/*
** Intended to be passed an sqlite3 result code. If it's non-0
** then it emits a fatal error message which contains both the
** given string and the sqlite3_errmsg() from the application's
** database instance.
*/
static void db_affirm_rc(int rc, const char * zMsg);

/*
** Proxy for sqlite3_str_finish() which fails fatally if that
** routine returns NULL.
*/
static char * db_str_finish(sqlite3_str *s, int * n);
/*
** Proxy for sqlite3_str_new() which fails fatally if that
** routine returns NULL.
*/
static sqlite3_str * db_str_new(void);

/* Proxy for sqlite3_finalize(). */
static void db_finalize(sqlite3_stmt *pStmt);
/*
** Proxy for sqlite3_step() which fails fatally if the result
** is anything other than SQLITE_ROW or SQLITE_DONE.
*/
static int db_step(sqlite3_stmt *pStmt);
/*
** Proxy for sqlite3_bind_int() which fails fatally on error.
*/
static void db_bind_int(sqlite3_stmt *pStmt, int col, int val);
/*
** Proxy for sqlite3_bind_null() which fails fatally on error.
*/
static void db_bind_null(sqlite3_stmt *pStmt, int col);
/*
** Proxy for sqlite3_bind_text() which fails fatally on error.
*/
static void db_bind_text(sqlite3_stmt *pStmt, int col, const char * zStr);
/*
** Proxy for sqlite3_bind_text() which fails fatally on error.
*/
static void db_bind_textn(sqlite3_stmt *pStmt, int col, const char * zStr, int len);
#if 0
/*
** Proxy for sqlite3_bind_text() which fails fatally on error. It uses
** sqlite3_str_vappendf() so supports all of its formatting options.
*/
static void db_bind_textv(sqlite3_stmt *pStmt, int col, const char * zFmt, ...);
#endif
/*
** Proxy for sqlite3_free(), to be passed any memory which is allocated
** by sqlite3_malloc().
*/
static void db_free(void *m);
/*
** Adds the given `#define` macro name to the list of macros, ignoring
** any duplicates. Fails fatally on error.
*/
static void db_define_add(const char * zKey);
/*
** Returns true if the given key is already in the `#define` list,
** else false. Fails fatally on db error.
*/
//static
int db_define_has(const char * zName);

/*
** Returns true if the given key is already in the `#define` list, and
** it has a truthy value (is not empty and not equal to '0'), else
** false. Fails fatally on db error.
**
** nName is the length of zName, or <0 to use strlen() to figure
** it out.
*/
static int db_define_get_bool(const char * zName, int nName);

/*
** Searches for a define where (k GLOB zName). If one is found, a copy
** of it is assigned to *zVal (the caller must eventually db_free()
** it)), *nVal (if nVal is not NULL) is assigned its strlen, and
** returns non-0. If no match is found, 0 is returned and neither
** *zVal nor *nVal are modified. If more than one result matches, a
** fatal error is triggered.
**
** It is legal for *zVal to be NULL (and *nVal to be 0) if it returns
** non-0. That just means that the key was defined with no value part.
*/
static int db_define_get(const char * zName, int nName, char **zVal, unsigned int *nVal);

/*
** Removes the given `#define` macro name from the list of
** macros. Fails fatally on error.
*/
static void db_define_rm(const char * zKey);
/*
** Adds the given filename to the list of being-`#include`d files,
** using the given source file name and line number of error reporting
** purposes. If recursion is later detected.
*/
static void db_including_add(const char * zKey, const char * zSrc, int srcLine);
/*
** Adds the given dir to the list of includes. They are checked in the
** order they are added.
*/
static void db_include_dir_add(const char * zKey);
/*
** Returns a resolved path of PREFIX+'/'+zKey, where PREFIX is one of
** the `#include` dirs (db_include_dir_add()). If no file match is
** found, NULL is returned. Memory must eventually be passed to
** db_free() to free it.
*/
static char * db_include_search(const char * zKey);
/*
** Removes the given key from the `#include` list.
*/
static void db_include_rm(const char * zKey);
/*
** A proxy for sqlite3_prepare() which fails fatally on error.
*/
static void db_prepare(sqlite3_stmt **pStmt, const char * zSql, ...);

/*
** Opens the given file and processes its contents as c-pp, sending
** all output to the global c-pp output channel. Fails fatally on
** error.
*/
static void cmpp_process_file(const char * zName);

/*
 ** If the first nKey bytes of zKey contain a chEq character then:
 **
 ** - Assigns *nVal to the strlen() of the part after that (may be 0).
 ** - If zEq is not NULL, it is set to the position of the chEq.
 ** - Returns the part one byte after that chEq.
 **
 ** Else returns NULL and does not modify *len or *zEq.
 **
 ** If nKey is negative then strlen() is used to calculate it.
 */
static char const * cmpp_val_part(char const *zKey,
                                  int nKey,
                                  char chEq,
                                  unsigned * nVal,
                                  char const **zEq);

/*
** Returns the number newline characters between the given starting
** point and inclusive ending point. Results are undefined if zFrom is
** greater than zTo.
*/
static unsigned count_lines(unsigned char const * zFrom,
                            unsigned char const *zTo);

/*
** Wrapper around a FILE handle.
*/
struct FileWrapper {
  /* File's name. */
  char const *zName;
  /* FILE handle. */
  FILE * pFile;
  /* Where FileWrapper_slurp() stores the file's contents. */
  unsigned char * zContent;
  /* Size of this->zContent, as set by FileWrapper_slurp(). */
  unsigned nContent;
};
typedef struct FileWrapper FileWrapper;
#define FileWrapper_empty_m {0,0,0,0}
static const FileWrapper FileWrapper_empty = FileWrapper_empty_m;

/* Proxy for FILE_close(). */
static void FileWrapper_close(FileWrapper * p);
/* Proxy for FILE_open(). */
static void FileWrapper_open(FileWrapper * p, const char * zName, const char *zMode);
/* Proxy for FILE_slurp(). */
static void FileWrapper_slurp(FileWrapper * p);
/*
** If p->zContent ends in \n or \r\n, that part is replaced with 0 and
** p->nContent is adjusted. Returns true if it chomps, else false.
*/
int FileWrapper_chomp(FileWrapper * p);

/*
** Outputs a printf()-formatted message to stderr.
*/
static void g_stderr(char const *zFmt, ...);
/*
** Outputs a printf()-formatted message to stderr.
*/
static void g_stderrv(char const *zFmt, va_list);
#define g_debug(lvl,pfexpr)                                          \
  if(lvl<=g.flags.doDebug) g_stderr("%s @ %s():%d: ",g.zArgv0,__func__,__LINE__); \
  if(lvl<=g.flags.doDebug) g_stderr pfexpr

void cmpp_free(void *p){
  sqlite3_free(p);
}

void * cmpp_realloc(void * p, unsigned n){
  void * const rc = sqlite3_realloc(p, n);
  if(!rc) fatal("realloc(P,%u) failed", n);
  return rc;
}

#if 0
void * cmpp_malloc(unsigned n){
  void * const rc = sqlite3_alloc(n);
  if(!rc) fatal("malloc(%u) failed", n);
  return rc;
}
#endif

FILE * FILE_open(char const *zName, const char * zMode){
  FILE * p;
  if('-'==zName[0] && 0==zName[1]){
    p = strstr(zMode,"w") ? stdout : stdin;
  }else{
    p = fopen(zName, zMode);
    if(!p) fatal("Cannot open file [%s] with mode [%s]", zName, zMode);
  }
  return p;
}

void FILE_close(FILE *p){
  if(p && p!=stdout && p!=stderr){
    fclose(p);
  }
}

void FILE_slurp(FILE *pFile, unsigned char **pOut,
                unsigned * nOut){
  unsigned char zBuf[1024 * 8];
  unsigned char * pDest = 0;
  unsigned nAlloc = 0;
  unsigned nOff = 0;
  /* Note that this needs to be able to work on non-seekable streams,
  ** thus we read in chunks instead of doing a single alloc and
  ** filling it in one go. */
  while( !feof(pFile) ){
    size_t const n = fread(zBuf, 1, sizeof(zBuf), pFile);
    if(n>0){
      if(nAlloc < nOff + n + 1){
        nAlloc = nOff + n + 1;
        pDest = cmpp_realloc(pDest, nAlloc);
      }
      memcpy(pDest + nOff, zBuf, n);
      nOff += n;
    }
  }
  if(pDest) pDest[nOff] = 0;
  *pOut = pDest;
  *nOut = nOff;
}

void FileWrapper_close(FileWrapper * p){
  if(p->pFile) FILE_close(p->pFile);
  if(p->zContent) cmpp_free(p->zContent);
  *p = FileWrapper_empty;
}

void FileWrapper_open(FileWrapper * p, const char * zName,
                      const char * zMode){
  FileWrapper_close(p);
  p->pFile = FILE_open(zName, zMode);
  p->zName = zName;
}

void FileWrapper_slurp(FileWrapper * p){
  assert(!p->zContent);
  assert(p->pFile);
  FILE_slurp(p->pFile, &p->zContent, &p->nContent);
}

int FileWrapper_chomp(FileWrapper * p){
  if( p->nContent && '\n'==p->zContent[p->nContent-1] ){
    p->zContent[--p->nContent] = 0;
    if( p->nContent && '\r'==p->zContent[p->nContent-1] ){
      p->zContent[--p->nContent] = 0;
    }
    return 1;
  }
  return 0;
}

unsigned count_lines(unsigned char const * zFrom, unsigned char const *zTo){
  unsigned ln = 0;
  unsigned char const *zPos = zFrom;
  assert(zFrom && zTo);
  assert(zFrom <= zTo);
  for(; zPos < zTo; ++zPos){
    switch(*zPos){
      case (unsigned)'\n': ++ln; break;
      default: break;
    }
  }
  return ln;
}

enum CmppParseState {
TS_Start = 1,
TS_If,
TS_IfPassed,
TS_Else,
TS_Error
};
typedef enum CmppParseState CmppParseState;
enum CmppTokenType {
TT_Invalid = 0,
TT_Comment,
TT_Define,
TT_Elif,
TT_ElifNot,
TT_Else,
TT_EndIf,
TT_Error,
TT_If,
TT_IfNot,
TT_Include,
TT_Line,
TT_Pragma,
TT_Savepoint,
TT_Stderr,
TT_Undef
};
typedef enum CmppTokenType CmppTokenType;

struct CmppToken {
  CmppTokenType ttype;
  /* Line number of this token in the source file. */
  unsigned lineNo;
  /* Start of the token. */
  unsigned char const * zBegin;
  /* One-past-the-end byte of the token. */
  unsigned char const * zEnd;
};
typedef struct CmppToken CmppToken;
#define CmppToken_empty_m {TT_Invalid,0,0,0}
static const CmppToken CmppToken_empty = CmppToken_empty_m;

/*
** CmppLevel represents one "level" of tokenization, starting at the
** top of the main input, incrementing once for each level of `#if`,
** and decrementing for each `#endif`.
*/
typedef struct CmppLevel CmppLevel;
struct CmppLevel {
  unsigned short flags;
  /*
  ** Used for controlling which parts of an if/elif/...endif chain
  ** should get output.
  */
  unsigned short skipLevel;
  /* The token which started this level (an 'if' or 'ifnot'). */
  CmppToken token;
  CmppParseState pstate;
};
#define CmppLevel_empty_m {0U,0U,CmppToken_empty_m,TS_Start}
static const CmppLevel CmppLevel_empty = CmppLevel_empty_m;
enum CmppLevel_Flags {
/* Max depth of nested `#if` constructs in a single tokenizer. */
CmppLevel_Max = 10,
/* Max number of keyword arguments. */
CmppArgs_Max = 10,
/* Flag indicating that output for a CmpLevel should be elided. */
CmppLevel_F_ELIDE = 0x01,
/*
** Mask of CmppLevel::flags which are inherited when CmppLevel_push()
** is used.
*/
CmppLevel_F_INHERIT_MASK = 0x01
};

typedef struct CmppTokenizer CmppTokenizer;
typedef struct CmppKeyword CmppKeyword;
typedef void (*cmpp_keyword_f)(CmppKeyword const * pKw, CmppTokenizer * t);
struct CmppKeyword {
  const char *zName;
  unsigned nName;
  int bTokenize;
  CmppTokenType ttype;
  cmpp_keyword_f xCall;
};

static CmppKeyword const * CmppKeyword_search(const char *zName);
static void cmpp_process_keyword(CmppTokenizer * const t);

/*
** Tokenizer for c-pp input files.
*/
struct CmppTokenizer {
  const char * zName;            /* Input (file) name for error reporting */
  unsigned const char * zBegin;  /* start of input */
  unsigned const char * zEnd;    /* one-after-the-end of input */
  unsigned const char * zAnchor; /* start of input or end point of
                                    previous token */
  unsigned const char * zPos;    /* current position */
  unsigned int lineNo;           /* line # of current pos */
  CmppParseState pstate;
  CmppToken token;               /* current token result */
  struct {
    unsigned ndx;
    CmppLevel stack[CmppLevel_Max];
  } level;
  /* Args for use in cmpp_keyword_f() impls. */
  struct {
    CmppKeyword const * pKw;
    int argc;
    const unsigned char * argv[CmppArgs_Max];
    unsigned char lineBuf[1024];
  } args;
};
#define CT_level(t) (t)->level.stack[(t)->level.ndx]
#define CT_pstate(t) CT_level(t).pstate
#define CT_skipLevel(t) CT_level(t).skipLevel
#define CLvl_skip(lvl) ((lvl)->skipLevel || ((lvl)->flags & CmppLevel_F_ELIDE))
#define CT_skip(t) CLvl_skip(&CT_level(t))
#define CmppTokenizer_empty_m {               \
    .zName=0, .zBegin=0, .zEnd=0, .zAnchor=0, \
    .zPos=0,                                  \
    .lineNo=1U,                               \
    .pstate = TS_Start,                       \
    .token = CmppToken_empty_m,               \
    .level = {0U,{CmppLevel_empty_m}},        \
    .args = {0,0,{0},{0}}                     \
  }
static const CmppTokenizer CmppTokenizer_empty = CmppTokenizer_empty_m;

static void cmpp_t_out(CmppTokenizer * t, void const *z, unsigned int n);
/*static void cmpp_t_outf(CmppTokenizer * t, char const *zFmt, ...);*/

/*
** Pushes a new level into the given tokenizer. Fails fatally if
** it's too deep.
*/
static void CmppLevel_push(CmppTokenizer * const t);
/*
** Pops a level from the tokenizer. Fails fatally if the top
** level is popped.
*/
static void CmppLevel_pop(CmppTokenizer * const t);
/*
** Returns the current level object.
*/
static CmppLevel * CmppLevel_get(CmppTokenizer * const t);

#define CMPPLEVEL_GET
/*
** Policies for how to handle undefined @tokens@ when performing
** content filtering.
*/
enum AtPolicy {
  AT_invalid = -1,
  /** Turn off @foo@ parsing. */
  AT_OFF = 0,
  /** Retain undefined @foo@ - emit it as-is. */
  AT_RETAIN,
  /** Elide undefined @foo@. */
  AT_ELIDE,
  /** Error for undefined @foo@. */
  AT_ERROR,
  AT_DEFAULT = AT_ERROR
};
typedef enum AtPolicy AtPolicy;

static AtPolicy AtPolicy_fromStr(char const *z, int bEnforce){
  if( 0==strcmp(z, "retain") ) return AT_RETAIN;
  if( 0==strcmp(z, "elide") ) return AT_ELIDE;
  if( 0==strcmp(z, "error") ) return AT_ERROR;
  if( 0==strcmp(z, "off") ) return AT_OFF;
  if( bEnforce ){
    fatal("Invalid @ policy value: %s. "
          "Try one of retain|elide|error|off.", z);
  }
  return AT_invalid;
}

/*
** Global app state singleton.
*/
static struct Global {
  /* main()'s argv[0]. */
  const char * zArgv0;
  /* App's db instance. */
  sqlite3 * db;
  /* Current tokenizer (for error reporting purposes). */
  CmppTokenizer const * tok;
  /* Output channel. */
  FileWrapper out;
  struct {
    /*
    ** Bytes of the keyword delimiter/prefix. Owned
    ** elsewhere.
    */
    const char * z;
    /* Byte length of this->zDelim. */
    unsigned short n;
    /*
    ** The @token@ delimiter.
    **
    ** Potential TODO is replace this with a pair of opener/closer
    ** strings, e.g. "{{" and "}}".
    */
    const unsigned char chAt;
  } delim;
  struct {
#define GStmt_map(E)               \
    E(defIns,"INSERT OR REPLACE INTO def(k,v) VALUES(?,?)") \
    E(defDel,"DELETE FROM def WHERE k GLOB ?")    \
    E(defHas,"SELECT 1 FROM def WHERE k GLOB ?")      \
    E(defGet,"SELECT k,v FROM def WHERE k GLOB ?")        \
    E(defGetBool, \
    "SELECT 1 FROM def WHERE k = ?1"                   \
    " AND v IS NOT NULL"                               \
    " AND '0'!=v AND ''!=v")                           \
    E(inclIns,"INSERT OR FAIL INTO incl(file,srcFile,srcLine) VALUES(?,?,?)") \
    E(inclDel,"DELETE FROM incl WHERE file=?") \
    E(inclHas,"SELECT 1 FROM incl WHERE file=?") \
    E(inclPathAdd,"INSERT OR FAIL INTO inclpath(seq,dir) VALUES(?,?)") \
    E(inclSearch, \
    "SELECT ?1 fn WHERE fileExists(fn) " \
    "UNION ALL SELECT * FROM (" \
    "SELECT replace(dir||'/'||?1, '//','/') AS fn "            \
    "FROM inclpath WHERE fileExists(fn) ORDER BY seq"             \
    ")")

#define E(N,S) sqlite3_stmt * N;
    GStmt_map(E)
#undef E
  } stmt;
  struct {
    FILE * pFile;
    int expandSql;
  } sqlTrace;
  struct {
    AtPolicy atPolicy;
    /* If true, enables certain debugging output. */
    char doDebug;
    /* If true, chomp() files read via -Fx=file. */
    char chompF;
  } flags;
} g = {
  .zArgv0 = "?",
  .db = 0,
  .tok = 0,
  .out = FileWrapper_empty_m,
  .delim = {
    .z = CMPP_DEFAULT_DELIM,
    .n = (unsigned short) sizeof(CMPP_DEFAULT_DELIM)-1,
    .chAt = '@'
  },
  .stmt = {
    .defIns =   0,
    .defDel = 0,
    .defHas = 0,
    .defGet = 0,
    .defGetBool = 0,
    .inclIns =   0,
    .inclDel =   0,
    .inclHas =   0,
    .inclPathAdd =   0,
    .inclSearch =   0
  },
  .sqlTrace = {
    .pFile = 0,
    .expandSql = 0
  },
  .flags = {
    .atPolicy = AT_OFF,
    .doDebug = 0,
    .chompF = 0
  }
};

/** Distinct IDs for each g.stmt member. */
enum GStmt_e {
  GStmt_e_none = 0,
#define E(N,S) GStmt_e_ ## N,
  GStmt_map(E)
#undef E
};

/*
** Returns the g.stmt.X corresponding to `which`, initializing it if
** needed. It does not return NULL - it fails fatally on error.
*/
static sqlite3_stmt * g_stmt(enum GStmt_e which){
  sqlite3_stmt ** q = 0;
  char const * zSql = 0;
  switch(which){
    case GStmt_e_none:
      fatal("GStmt_e_none is not a valid statement handle");
      return NULL;
#define E(N,S) case GStmt_e_ ## N: zSql = S; q = &g.stmt.N; break;
    GStmt_map(E)
#undef E
  }
  assert( q );
  assert( zSql && *zSql );
  if( !*q ){
    db_prepare(q, "%s", zSql);
    assert( *q );
  }
  return *q;
}

#if 0
/*
** Outputs a printf()-formatted message to c-pp's global output
** channel.
*/
static void g_outf(char const *zFmt, ...);
void g_outf(char const *zFmt, ...){
  va_list va;
  va_start(va, zFmt);
  vfprintf(g.out.pFile, zFmt, va);
  va_end(va);
}
#endif

/* Outputs n bytes from z to c-pp's global output channel. */
static void g_out(void const *z, unsigned int n);
void g_out(void const *z, unsigned int n){
  if(1!=fwrite(z, n, 1, g.out.pFile)){
    int const err = errno;
    fatal("fwrite() output failed with errno #%d", err);
  }
}

void g_stderrv(char const *zFmt, va_list va){
  vfprintf(stderr, zFmt, va);
}

void g_stderr(char const *zFmt, ...){
  va_list va;
  va_start(va, zFmt);
  g_stderrv(zFmt, va);
  va_end(va);
}

/*
** Emits n bytes of z if CT_skip(t) is false.
*/
void cmpp_t_out(CmppTokenizer * t, void const *z, unsigned int n){
  g_debug(3,("CT_skipLevel() ?= %d\n",CT_skipLevel(t)));
  g_debug(3,("CT_skip() ?= %d\n",CT_skip(t)));
  if(!CT_skip(t)) g_out(z, n);
}

void CmppLevel_push(CmppTokenizer * const t){
  CmppLevel * pPrev;
  CmppLevel * p;
  if(t->level.ndx+1 == (unsigned)CmppLevel_Max){
    fatal("%sif nesting level is too deep. Max=%d\n",
          g.delim.z, CmppLevel_Max);
  }
  pPrev = &CT_level(t);
  g_debug(3,("push from tokenizer level=%u flags=%04x\n", t->level.ndx, pPrev->flags));
  p = &t->level.stack[++t->level.ndx];
  *p = CmppLevel_empty;
  p->token = t->token;
  p->flags = (CmppLevel_F_INHERIT_MASK & pPrev->flags);
  if(CLvl_skip(pPrev)) p->flags |= CmppLevel_F_ELIDE;
  g_debug(3,("push to tokenizer level=%u flags=%04x\n", t->level.ndx, p->flags));
}

void CmppLevel_pop(CmppTokenizer * const t){
  if(!t->level.ndx){
    fatal("Internal error: CmppLevel_pop() at the top of the stack");
  }
  g_debug(3,("pop from tokenizer level=%u, flags=%04x skipLevel?=%d\n", t->level.ndx,
             t->level.stack[t->level.ndx].flags, CT_skipLevel(t)));
  g_debug(3,("CT_skipLevel() ?= %d\n",CT_skipLevel(t)));
  g_debug(3,("CT_skip() ?= %d\n",CT_skip(t)));
  t->level.stack[t->level.ndx--] = CmppLevel_empty;
  g_debug(3,("pop to tokenizer level=%u, flags=%04x\n", t->level.ndx,
             t->level.stack[t->level.ndx].flags));
  g_debug(3,("CT_skipLevel() ?= %d\n",CT_skipLevel(t)));
  g_debug(3,("CT_skip() ?= %d\n",CT_skip(t)));
}

CmppLevel * CmppLevel_get(CmppTokenizer * const t){
  return &t->level.stack[t->level.ndx];
}


void db_affirm_rc(int rc, const char * zMsg){
  if(rc){
    fatal("Db error #%d %s: %s", rc, zMsg,
          sqlite3_errmsg(g.db));
  }
}

void db_finalize(sqlite3_stmt *pStmt){
  sqlite3_finalize(pStmt);
}

int db_step(sqlite3_stmt *pStmt){
  int const rc = sqlite3_step(pStmt);
  if(SQLITE_ROW!=rc && SQLITE_DONE!=rc){
    db_affirm_rc(rc, "from db_step()");
  }
  return rc;
}

static sqlite3_str * db_str_new(void){
  sqlite3_str * rc = sqlite3_str_new(g.db);
  if(!rc) fatal("Alloc failed for sqlite3_str_new()");
  return rc;
}

static char * db_str_finish(sqlite3_str *s, int * n){
  int const rc = sqlite3_str_errcode(s);
  if(rc) fatal("Error #%d from sqlite3_str_errcode()", rc);
  if(n) *n = sqlite3_str_length(s);
  char * z = sqlite3_str_finish(s);
  if(!z) fatal("Alloc failed for sqlite3_str_new()");
  return z;
}

void db_prepare(sqlite3_stmt **pStmt, const char * zSql, ...){
  int rc;
  sqlite3_str * str = db_str_new();
  char * z = 0;
  int n = 0;
  va_list va;
  if(!str) fatal("sqlite3_str_new() failed");
  va_start(va, zSql);
  sqlite3_str_vappendf(str, zSql, va);
  va_end(va);
  rc = sqlite3_str_errcode(str);
  if(rc) fatal("sqlite3_str_errcode() = %d", rc);
  z = db_str_finish(str, &n);
  rc = sqlite3_prepare_v2(g.db, z, n, pStmt, 0);
  if(rc) fatal("Error #%d (%s) preparing: %s",
               rc, sqlite3_errmsg(g.db), z);
  sqlite3_free(z);
}

void db_bind_int(sqlite3_stmt *pStmt, int col, int val){
  db_affirm_rc(sqlite3_bind_int(pStmt, col, val),
               "from db_bind_int()");
}

void db_bind_null(sqlite3_stmt *pStmt, int col){
  db_affirm_rc(sqlite3_bind_null(pStmt, col),
               "from db_bind_null()");
}

void db_bind_textn(sqlite3_stmt *pStmt, int col,
                   const char * zStr, int n){
  db_affirm_rc(
    (zStr && n)
    ? sqlite3_bind_text(pStmt, col, zStr, n, SQLITE_TRANSIENT)
    : sqlite3_bind_null(pStmt, col),
    "from db_bind_textn()"
  );
}

void db_bind_text(sqlite3_stmt *pStmt, int col,
                  const char * zStr){
  db_bind_textn(pStmt, col, zStr, -1);
}

#if 0
void db_bind_textv(sqlite3_stmt *pStmt, int col,
                   const char * zFmt, ...){
  int rc;
  sqlite3_str * str = db_str_new();
  int n = 0;
  char * z;
  va_list va;
  va_start(va,zFmt);
  sqlite3_str_vappendf(str, zFmt, va);
  va_end(va);
  z = db_str_finish(str, &n);
  rc = sqlite3_bind_text(pStmt, col, z, n, sqlite3_free);
  db_affirm_rc(rc,"from db_bind_textv()");
}
#endif

void db_free(void *m){
  sqlite3_free(m);
}

void db_define_add(const char * zKey){
  int rc;
  char const * zEq = 0;
  unsigned nVal = 0;
  char const * zVal = cmpp_val_part(zKey, -1, '=', &nVal, &zEq);
  sqlite3_stmt * const q = g_stmt(GStmt_e_defIns);
  assert( q );
  //g_stderr("zKey=%s\nzVal=%s\nzEq=%s\n", zKey, zVal, zEq);
  if( zEq ){
    db_bind_textn(q, 1, zKey, (zEq-zKey));
  }else{
    db_bind_text(q, 1, zKey);
  }
  if( zEq ){
    if( zVal && nVal ){
      db_bind_text(q, 2, zVal);
    }else{
      db_bind_null(q, 2);
    }
  }else{
    db_bind_int(q, 2, 1);
  }
  rc = db_step(q);
  if(SQLITE_DONE != rc){
    db_affirm_rc(rc, "Stepping INSERT on def");
  }
  g_debug(2,("define: %s%s%s\n",
             zKey,
             zVal ? " with value " : "",
             zVal ? zVal : ""));
  sqlite3_reset(q);
}

static void db_define_add_file(const char * zKey){
  int rc;
  char const * zEq = 0;
  unsigned nVal = 0;
  char const * zVal = cmpp_val_part(zKey, -1, '=', &nVal, &zEq);
  if( !zVal || !nVal ){
    fatal("Invalid filename: %s", zKey);
  }
  sqlite3_stmt * q = 0;
  FileWrapper fw = FileWrapper_empty;
  FileWrapper_open(&fw, zVal, "r");
  FileWrapper_slurp(&fw);
  q = g_stmt(GStmt_e_defIns);
  //g_stderr("zKey=%s\nzVal=%s\nzEq=%s\n", zKey, zVal, zEq);
  db_bind_textn(q, 1, zKey, (zEq-zKey));
  if( g.flags.chompF ){
    FileWrapper_chomp(&fw);
  }
  if( fw.nContent ){
    db_affirm_rc(
      sqlite3_bind_text(q, 2,
                        (char const *)fw.zContent,
                        (int)fw.nContent, sqlite3_free),
      "binding file content");
    fw.zContent = 0 /* transfered ownership */;
    fw.nContent = 0;
  }else{
    db_affirm_rc( sqlite3_bind_null(q, 2),
                  "binding empty file content");
  }
  FileWrapper_close(&fw);
  rc = db_step(q);
  if(SQLITE_DONE != rc){
    db_affirm_rc(rc, "stepping INSERT on def");
  }
  g_debug(2,("define: %s%s%s\n",
             zKey,
             zVal ? " with value " : "",
             zVal ? zVal : ""));
  sqlite3_clear_bindings(q);
  sqlite3_reset(q);
}

int db_define_has(const char * zName){
  int rc;
  sqlite3_stmt * const q = g_stmt(GStmt_e_defHas);
  db_bind_text(q, 1, zName);
  rc = db_step(q);
  if(SQLITE_ROW == rc){
    rc = 1;
  }else{
    assert(SQLITE_DONE==rc);
    rc = 0;
  }
  g_debug(1,("defined [%s] ?= %d\n",zName, rc));
  sqlite3_clear_bindings(q);
  sqlite3_reset(q);
  return rc;
}

int db_define_get_bool(const char * zName, int nName){
  sqlite3_stmt * const q = g_stmt(GStmt_e_defGetBool);
  int rc = 0;
  if( nName<0 ) nName=(int)strlen(zName);
  db_bind_textn(q, 1, zName, nName);
  rc = db_step(q);
  if(SQLITE_ROW == rc){
    if( SQLITE_ROW==sqlite3_step(q) ){
      fatal("Key is ambiguous: %s", zName);
    }
    rc = 1;
  }else{
    assert(SQLITE_DONE==rc);
    rc = 0;
  }
  sqlite3_clear_bindings(q);
  sqlite3_reset(q);
  return rc;
}

int db_define_get(const char * zName, int nName,
                  char **zVal, unsigned int *nVal){
  sqlite3_stmt * q = g_stmt(GStmt_e_defGet);
  if( nName<0 ) nName=(int)strlen(zName);
  db_bind_textn(q, 1, zName, nName);
  int n = 0;
  int rc = db_step(q);
  if(SQLITE_ROW == rc){
    const unsigned char * z = sqlite3_column_text(q, 1);
    n = sqlite3_column_bytes(q,1);
    if( nVal ) *nVal = (unsigned)n;
    *zVal = sqlite3_mprintf("%.*s", n, z);
    if( n && z ) check__oom(*zVal);
    if( SQLITE_ROW==sqlite3_step(q) ){
      db_free(*zVal);
      *zVal = 0;
      fatal("Key is ambiguous: %.*s\n",
            nName, zName);
    }
    rc = 1;
  }else{
    assert(SQLITE_DONE==rc);
    rc = 0;
  }
  g_debug(1,("define [%.*s] ?= %d %.*s\n",
             nName, zName, rc,
             *zVal ? n : 0,
             *zVal ? *zVal : "<NULL>"));
  sqlite3_clear_bindings(q);
  sqlite3_reset(q);
  return rc;
}

void db_define_rm(const char * zKey){
  int rc;
  int n = 0;
  sqlite3_stmt * const q = g_stmt(GStmt_e_defDel);
  db_bind_text(q, 1, zKey);
  rc = db_step(q);
  if(SQLITE_DONE != rc){
    db_affirm_rc(rc, "Stepping DELETE on def");
  }
  g_debug(2,("undefine: %.*s\n",n, zKey));
  sqlite3_clear_bindings(q);
  sqlite3_reset(q);
}

void db_including_add(const char * zKey, const char * zSrc, int srcLine){
  int rc;
  sqlite3_stmt * const q = g_stmt(GStmt_e_inclIns);
  db_bind_text(q, 1, zKey);
  db_bind_text(q, 2, zSrc);
  db_bind_int(q, 3, srcLine);
  rc = db_step(q);
  if(SQLITE_DONE != rc){
    db_affirm_rc(rc, "Stepping INSERT on incl");
  }
  g_debug(2,("is-including-file add [%s] from [%s]:%d\n", zKey, zSrc, srcLine));
  sqlite3_clear_bindings(q);
  sqlite3_reset(q);
}

void db_include_rm(const char * zKey){
  int rc;
  sqlite3_stmt * const q = g_stmt(GStmt_e_inclDel);
  db_bind_text(q, 1, zKey);
  rc = db_step(q);
  if(SQLITE_DONE != rc){
    db_affirm_rc(rc, "Stepping DELETE on incl");
  }
  g_debug(2,("inclpath rm [%s]\n", zKey));
  sqlite3_clear_bindings(q);
  sqlite3_reset(q);
}

char * db_include_search(const char * zKey){
  char * zName = 0;
  sqlite3_stmt * const q = g_stmt(GStmt_e_inclSearch);
  db_bind_text(q, 1, zKey);
  if(SQLITE_ROW==db_step(q)){
    const unsigned char * z = sqlite3_column_text(q, 0);
    zName = z ? sqlite3_mprintf("%s", z) : 0;
    if(!zName) fatal("Alloc failed");
  }
  sqlite3_clear_bindings(q);
  sqlite3_reset(q);
  return zName;
}

static int db_including_has(const char * zName){
  int rc;
  sqlite3_stmt * const q = g_stmt(GStmt_e_inclHas);
  db_bind_text(q, 1, zName);
  rc = db_step(q);
  if(SQLITE_ROW == rc){
    rc = 1;
  }else{
    assert(SQLITE_DONE==rc);
    rc = 0;
  }
  g_debug(2,("inclpath has [%s] = %d\n",zName, rc));
  sqlite3_clear_bindings(q);
  sqlite3_reset(q);
  return rc;
}

#if 0
/*
** Fails fatally if the `#include` list contains the given key.
*/
static void db_including_check(const char * zKey);
void db_including_check(const char * zName){
  if(db_including_has(zName)){
    fatal("Recursive include detected: %s\n", zName);
  }
}
#endif

void db_include_dir_add(const char * zDir){
  static int seq = 0;
  int rc;
  sqlite3_stmt * const q = g_stmt(GStmt_e_inclPathAdd);
  db_bind_int(q, 1, ++seq);
  db_bind_text(q, 2, zDir);
  rc = db_step(q);
  if(SQLITE_DONE != rc){
    db_affirm_rc(rc, "Stepping INSERT on inclpath");
  }
  g_debug(2,("inclpath add #%d: %s\n",seq, zDir));
  sqlite3_clear_bindings(q);
  sqlite3_reset(q);
}

static void cmpp_atexit(void){
#define E(N,S) sqlite3_finalize(g.stmt.N);
  GStmt_map(E)
#undef E
  FileWrapper_close(&g.out);
  if(g.db) sqlite3_close(g.db);
}

/*
** sqlite3 UDF which returns true if its argument refers to an
** accessible file, else false.
*/
static void udf_file_exists(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zName;
  (void)(argc);  /* Unused parameter */
  zName = (const char*)sqlite3_value_text(argv[0]);
  if( zName==0 ) return;
  sqlite3_result_int(context, 0==access(zName, 0));
}

/**
 ** This sqlite3_trace_v2() callback outputs tracing info using
 ** g.sqlTrace.pFile.
*/
static int cmpp__db_sq3TraceV2(unsigned t,void*c,void*p,void*x){
  static unsigned int counter = 0;
  switch(t){
    case SQLITE_TRACE_STMT:{
      FILE * const fp = g.sqlTrace.pFile;
      if( fp ){
        char const * const zSql = (char const *)x;
        char * const zExp = g.sqlTrace.expandSql
          ? sqlite3_expanded_sql((sqlite3_stmt*)p)
          : 0;
        fprintf(fp, "SQL TRACE #%u: %s\n",
                ++counter, zExp ? zExp : zSql);
        sqlite3_free(zExp);
      }
      break;
    }
  }
  return 0;
}

/* Initialize g.db, failing fatally on error. */
static void cmpp_initdb(void){
  int rc;
  char * zErr = 0;
  const char * zSchema =
    "CREATE TABLE def("
    /* ^^^ defines */
      "k TEXT PRIMARY KEY NOT NULL,"
      "v TEXT DEFAULT NULL"
    ") WITHOUT ROWID;"
    "CREATE TABLE incl("
    /* ^^^ files currently being included */
      "file TEXT PRIMARY KEY NOT NULL,"
      "srcFile TEXT DEFAULT NULL,"
      "srcLine INTEGER DEFAULT 0"
    ") WITHOUT ROWID;"
    "CREATE TABLE inclpath("
    /* ^^^ include path */
      "seq INTEGER UNIQUE ON CONFLICT IGNORE, "
      "dir TEXT PRIMARY KEY NOT NULL ON CONFLICT IGNORE"
    ");"
    "BEGIN;"
    ;
  assert(0==g.db);
  if(g.db) return;
  rc = sqlite3_open_v2(":memory:", &g.db, SQLITE_OPEN_READWRITE, 0);
  if(rc) fatal("Error opening :memory: db.");
  sqlite3_trace_v2(g.db, SQLITE_TRACE_STMT, cmpp__db_sq3TraceV2, 0);
  rc = sqlite3_exec(g.db, zSchema, 0, 0, &zErr);
  if(rc) fatal("Error initializing database: %s", zErr);
  rc = sqlite3_create_function(g.db, "fileExists", 1,
                               SQLITE_UTF8|SQLITE_DIRECTONLY, 0,
                               udf_file_exists, 0, 0);
  db_affirm_rc(rc, "UDF registration failed.");
}

/*
** For position zPos, which must be in the half-open range
** [zBegin,zEnd), returns g.delim.n if it is at the start of a line and
** starts with g.delim.z, else returns 0.
*/
static unsigned short cmpp_is_delim(unsigned char const *zBegin,
                                    unsigned char const *zEnd,
                                    unsigned char const *zPos){
  assert(zEnd>zBegin);
  assert(zPos<zEnd);
  assert(zPos>=zBegin);
  if(zPos>zBegin &&
     ('\n'!=*(zPos - 1)
      || ((unsigned)(zEnd - zPos) <= g.delim.n))){
    return 0;
  }else if(0==memcmp(zPos, g.delim.z, g.delim.n)){
    return g.delim.n;
  }else{
    return 0;
  }
}

static char const * cmpp_val_part(char const *zKey,
                                  int nKey,
                                  char chEq,
                                  unsigned * len,
                                  char const **zEq){
  const char *zPos = zKey;
  if( nKey<0 ) nKey = (int)strlen(zKey);
  const char * const zEnd = zPos + nKey;
  for( ; *zPos && zPos<zEnd ; ++zPos) {
    if( chEq==*zPos ){
      if( zEq ) *zEq = zPos;
      ++zPos;
      *len = (unsigned)(zEnd - zPos);
      return zPos;
    }
  }
  return 0;
}

static void cmpp_t_out_expand(CmppTokenizer * const t,
                              unsigned char const * zFrom,
                              unsigned int n){
  unsigned char const *zLeft = zFrom;
  unsigned char const * const zEnd = zFrom + n;
  unsigned char const *z = AT_OFF==g.flags.atPolicy ? zEnd : zLeft;
  unsigned char const chEol = (unsigned char)'\n';
  int state = 0 /* 0==looking for opening @
                ** 1==looking for closing @ */;
#define tflush \
  if(zLeft<z){ cmpp_t_out(t, zLeft, (z-zLeft)); } zLeft = z
  for( ; z<zEnd; ++z ){
    zLeft = z;
    for( ;z<zEnd; ++z ){
      if( chEol==*z ){
        state = 0;
        ++z /*ensure that we flush the EOL now*/;
        tflush;
        --z/*And make sure that zLeft does the right thing*/;
        break;
      }
      if( g.delim.chAt==*z ){
        if( 0==state ){
          tflush;
          state = 1;
        }else{ /* process chAt...chAt */
          char *zVal = 0;
          unsigned int nVal = 0;
          assert( 1==state );
          assert( g.delim.chAt==*zLeft );
          assert( zLeft<z );
          if( z==zLeft+1 ){
            tflush;
          }else{
            char const *zKey = (char const*)zLeft+1;
            int const nKey = (z-zLeft-1);
            if( db_define_get(zKey, nKey, &zVal, &nVal) ){
              if( nVal ){
                cmpp_t_out(t, (unsigned char const*)zVal, nVal);
              }else{
                /* Elide it */
              }
              zLeft = z+1/*skip closing g.delim.chAt*/;
              db_free(zVal);
            }else{
              assert( !zVal );
              /* No match. Emit it as-is. */
              switch( g.flags.atPolicy ){
                case AT_RETAIN:
                  tflush;
                  break;
                case AT_ERROR:
                  fatal("Undefined key: %c%.*s%c",
                        g.delim.chAt, nKey, zKey, g.delim.chAt );
                  break;
                case AT_ELIDE:
                  zLeft = z+1;
                  break;
                case AT_invalid:
                case AT_OFF:
                  fatal("Unhandled g.flags.atPolicy #%d!",
                        g.flags.atPolicy);
                  break;
              }
            }
            state = 0;
          }
        }/* process chAt...chAt */
      }/*g.delim.chAt*/
    }/*per-line loop*/
  }/*outer loop*/
  tflush;
#undef tflush
  return;
}

/*
** Scans t to the next keyword line, emitting all input before that
** which is _not_ a keyword line unless it's elided due to being
** inside a block which elides its content. Returns 0 if no keyword
** line was found, in which case the end of the input has been
** reached, else returns a truthy value and sets up t's state for use
** with cmpp_process_keyword(), which should then be called.
*/
static int cmpp_next_keyword_line(CmppTokenizer * const t){
  unsigned char const * zStart;
  unsigned char const * z;
  CmppToken * const tok = &t->token;
  unsigned short isDelim = 0;

  assert(t->zBegin);
  assert(t->zEnd > t->zBegin);
  if(!t->zPos) t->zPos = t->zBegin;
  t->zAnchor = t->zPos;
  zStart = z = t->zPos;
  *tok = CmppToken_empty;
  while(z<t->zEnd
        && 0==(isDelim = cmpp_is_delim(t->zBegin, t->zEnd, z))){
    ++z;
  }
  if(z>zStart){
    /* We passed up content */
    //cmpp_t_out(t, zStart, (unsigned)(z - zStart));
    cmpp_t_out_expand(t, zStart, (unsigned)(z - zStart));
  }
  assert(isDelim==0 || isDelim==g.delim.n);
  tok->lineNo = t->lineNo += count_lines(zStart, z);
  if(isDelim){
    /* Handle backslash-escaped newlines */
    int isEsc = 0, atEol = 0;
    tok->zBegin = z+isDelim;
    for( ++z ; z<t->zEnd && 0==atEol; ++z ){
      switch((int)*z){
        case (int)'\\':
          isEsc = 0==isEsc; break;
        case (int)'\n':
          atEol = 0==isEsc;
          isEsc = 0;
          ++t->lineNo;
          break;
        default:
          break;
      }
    }
    tok->zEnd = atEol ? z-1 : z;
    /* Strip leading spaces */
    while(tok->zBegin < tok->zEnd && isspace((char)(*tok->zBegin))){
      ++tok->zBegin;
    }
    tok->ttype = TT_Line;
    g_debug(2,("Keyword @ line %u: [[[%.*s]]]\n",
               tok->lineNo,
               (int)(tok->zEnd-tok->zBegin), tok->zBegin));
  }
  t->zPos = z;
  if(isDelim){
    /* Split t->token into arguments for the line's keyword */
    int i, argc = 0, prevChar = 0;
    const unsigned tokLen = (unsigned)(tok->zEnd - tok->zBegin);
    unsigned char * zKwd;
    unsigned char * zEsc;
    unsigned char * zz;

    assert(TT_Line==tok->ttype);
    if((unsigned)sizeof(t->args.lineBuf) < tokLen + 1){
      fatal("Keyword line #%u is unreasonably long: %.*s",
            tok->lineNo, tokLen, tok->zBegin);
    }else if(!tokLen){
      fatal("Line #%u has no keyword after delimiter", tok->lineNo);
    }
    g_debug(2,("token @ line %u len=%u [[[%.*s]]]\n",
               tok->lineNo, tokLen, tokLen, tok->zBegin));
    zKwd = &t->args.lineBuf[0];
    memcpy(zKwd, tok->zBegin, tokLen);
    memset(zKwd + tokLen, 0, sizeof(t->args.lineBuf) - tokLen);
    for( zEsc = 0, zz = zKwd; *zz; ++zz ){
      /* Convert backslash-escaped newlines to whitespace */
      switch((int)*zz){
        case (int)'\\':
          if(zEsc) zEsc = 0;
          else zEsc = zz;
          break;
        case (int)'\n':
          assert(zEsc && "Should not have an unescaped newline?");
          if(zEsc==zz-1){
            *zEsc = (unsigned char)' ';
            /* FIXME?: memmove() lnBuf content one byte to the left here
            ** to collapse backslash and newline into a single
            ** byte. Also consider collapsing all leading space on the
            ** next line. */
          }
          zEsc = 0;
          *zz = (unsigned char)' ';
          break;
        default:
          zEsc = 0;
          break;
      }
    }
    t->args.argv[argc++] = zKwd;
    for( zz = zKwd; *zz; ++zz ){
      if(isspace(*zz)){
        *zz = 0;
        break;
      }
    }
    t->args.pKw = CmppKeyword_search((char const *)zKwd);
    if(!t->args.pKw){
      fatal("Unknown keyword '%s' at line %u\n", (char const *)zKwd,
            tok->lineNo);
    }
    for( ++zz ; *zz && isspace(*zz); ++zz ){}
    if(t->args.pKw->bTokenize){
      for( ; *zz; prevChar = *zz, ++zz ){
        /* Split string into word-shaped tokens.
        ** TODO ?= quoted strings, for the sake of the
        ** #error keyword. */
        if(isspace(*zz)){
          assert(zz!=zKwd && "Leading space was stripped earlier.");
          *zz = 0;
        }else{
          if(argc == (int)CmppArgs_Max){
            fatal("Too many arguments @ line %u: %.*s",
                  tok->lineNo, tokLen, tok->zBegin);
          }else if(zz>zKwd && !prevChar){
            t->args.argv[argc++] = zz;
          }
        }
      }
    }else{
      /* Treat rest of line as one token */
      if(*zz) t->args.argv[argc++] = zz;
    }
    tok->ttype = t->args.pKw->ttype;
    if(g.flags.doDebug>1){
      for(i = 0; i < argc; ++i){
        g_debug(0,("line %u arg #%d=%s\n",
                   tok->lineNo, i,
                   (char const *)t->args.argv[i]));
      }
    }
    t->args.argc = argc;
  }else{
    t->args.pKw = 0;
    t->args.argc = 0;
  }
  return isDelim;
}

static void cmpp_kwd__err_prefix(CmppKeyword const * pKw, CmppTokenizer *t,
                                 char const *zPrefix){
  g_stderr("%s%s%s @ %s line %u: ",
           zPrefix ? zPrefix : "",
           zPrefix ? ": " : "",
           pKw->zName, t->zName, t->token.lineNo);
}

/* Internal error reporting helper for cmpp_keyword_f() impls. */
static CMPP_NORETURN void cmpp_kwd__misuse(CmppKeyword const * pKw,
                                           CmppTokenizer *t,
                                           char const *zFmt, ...){
  va_list va;
  cmpp_kwd__err_prefix(pKw, t, "Fatal error");
  va_start(va, zFmt);
  fatalv(zFmt, va);
  va_end(va);
}

/* No-op cmpp_keyword_f() impl. */
static void cmpp_kwd_noop(CmppKeyword const * pKw, CmppTokenizer *t){
  (void)pKw;
  (void)t;
}

/* #error impl. */
static void cmpp_kwd_error(CmppKeyword const * pKw, CmppTokenizer *t){
  if(CT_skip(t)) return;
  else{
    assert(t->args.argc < 3);
    const char *zBegin = t->args.argc>1
      ? (const char *)t->args.argv[1] : 0;
    cmpp_kwd__err_prefix(pKw, t, NULL);
    fatal("%s", zBegin ? zBegin : "(no additional info)");
  }
}

/* Impl. for #define, #undef */
static void cmpp_kwd_define(CmppKeyword const * pKw, CmppTokenizer *t){
  if(CT_skip(t)) return;
  if(t->args.argc<2){
    cmpp_kwd__misuse(pKw, t, "Expecting one or more arguments");
  }else{
    int i = 1;
    void (*func)(const char *) = TT_Define==pKw->ttype
      ? db_define_add : db_define_rm;
    for( ; i < t->args.argc; ++i){
      func( (char const *)t->args.argv[i] );
    }
  }
}

/* Impl. for #if, #ifnot, #elif, #elifnot. */
static void cmpp_kwd_if(CmppKeyword const * pKw, CmppTokenizer *t){
  int buul = 0;
  CmppParseState tmpState = TS_Start;
  if(t->args.argc!=2){
    cmpp_kwd__misuse(pKw, t, "Expecting exactly 1 argument");
  }
  /*g_debug(0,("%s %s level %u pstate=%d\n", pKw->zName,
             (char const *)t->args.argv[1],
             t->level.ndx, (int)CT_pstate(t)));*/
  switch(pKw->ttype){
    case TT_Elif:
    case TT_ElifNot:
      switch(CT_pstate(t)){
        case TS_If: break;
        case TS_IfPassed: CT_level(t).flags |= CmppLevel_F_ELIDE; return;
        default: goto misuse;
      }
      break;
    case TT_If:
    case TT_IfNot:
      CmppLevel_push(t);
      break;
    default:
      cmpp_kwd__misuse(pKw, t, "Unexpected keyword token type");
      break;
  }
  char const * const zKey = (char const *)t->args.argv[1];
  char const * zEq = 0;
  unsigned nValPart = 0;
  char const * zValPart = cmpp_val_part(zKey, -1, '=', &nValPart, &zEq);
  if( zValPart ){
    unsigned nVal = 0;
    char * zVal = 0;
    buul = db_define_get(zKey, (zEq-zKey), &zVal, &nVal);
    if( nVal ){
      /* FIXME? do this with a query */
      g_debug(1,("if get-define %.*s=%.*s zValPart=%s\n",
                 (zEq-zKey), zKey,
                 nVal, zVal, zValPart));
      buul = 0==sqlite3_strglob(zValPart,zVal);
    }
    db_free(zVal);
  }else{
    buul = db_define_get_bool(zKey, -1);
  }
  if(TT_IfNot==pKw->ttype || TT_ElifNot==pKw->ttype) buul = !buul;
  if(buul){
    CT_pstate(t) = tmpState = TS_IfPassed;
    CT_skipLevel(t) = 0;
  }else{
    CT_pstate(t) = TS_If /* also for TT_IfNot, TT_Elif, TT_ElifNot */;
    CT_skipLevel(t) = 1;
    g_debug(3,("setting CT_skipLevel = 1 @ level %d\n", t->level.ndx));
  }
  if(TT_If==pKw->ttype || TT_IfNot==pKw->ttype){
    unsigned const lvlIf = t->level.ndx;
    CmppToken const lvlToken = CT_level(t).token;
    while(cmpp_next_keyword_line(t)){
      cmpp_process_keyword(t);
      if(lvlIf > t->level.ndx){
        assert(TT_EndIf == t->token.ttype);
        break;
      }
#if 0
      if(TS_IfPassed==tmpState){
        tmpState = TS_Start;
        t->level.stack[lvlIf].flags |= CmppLevel_F_ELIDE;
        g_debug(1,("Setting ELIDE for TS_IfPassed @ lv %d (lvlIf=%d)\n", t->level.ndx, lvlIf));
      }
#endif
    }
    if(lvlIf <= t->level.ndx){
      cmpp_kwd__err_prefix(pKw, t, NULL);
      fatal("Input ended inside an unterminated %sif "
            "opened at [%s] line %u",
            g.delim.z, t->zName, lvlToken.lineNo);
    }
  }
  return;
  misuse:
  cmpp_kwd__misuse(pKw, t, "'%s' used out of context",
                   pKw->zName);
}

/* Impl. for #else. */
static void cmpp_kwd_else(CmppKeyword const * pKw, CmppTokenizer *t){
  if(t->args.argc>1){
    cmpp_kwd__misuse(pKw, t, "Expecting no arguments");
  }
  switch(CT_pstate(t)){
    case TS_IfPassed: CT_skipLevel(t) = 1; break;
    case TS_If: CT_skipLevel(t) = 0; break;
    default:
      cmpp_kwd__misuse(pKw, t, "'%s' with no matching 'if'",
                      pKw->zName);
  }
  /*g_debug(0,("else flags=0x%02x skipLevel=%u\n",
    CT_level(t).flags, CT_level(t).skipLevel));*/
  CT_pstate(t) = TS_Else;
}

/* Impl. for #endif. */
static void cmpp_kwd_endif(CmppKeyword const * pKw, CmppTokenizer *t){
  /* Maintenance reminder: we ignore all arguments after the endif
  ** to allow for constructs like:
  **
  ** #endif // foo
  **
  ** in a manner which does not require a specific comment style */
  switch(CT_pstate(t)){
    case TS_Else:
    case TS_If:
    case TS_IfPassed:
      break;
    default:
      cmpp_kwd__misuse(pKw, t, "'%s' with no matching 'if'",
                       pKw->zName);
  }
  CmppLevel_pop(t);
}

/* Impl. for #include. */
static void cmpp_kwd_include(CmppKeyword const * pKw, CmppTokenizer *t){
  char const * zFile;
  char * zResolved;
  if(CT_skip(t)) return;
  else if(t->args.argc!=2){
    cmpp_kwd__misuse(pKw, t, "Expecting exactly 1 filename argument");
  }
  zFile = (const char *)t->args.argv[1];
  if(db_including_has(zFile)){
    /* Note that different spellings of the same filename
    ** will elude this check, but that seems okay, as different
    ** spellings means that we're not re-running the exact same
    ** invocation. We might want some other form of multi-include
    ** protection, rather than this, however. There may well be
    ** sensible uses for recursion. */
    cmpp_kwd__err_prefix(pKw, t, NULL);
    fatal("Recursive include of file: %s", zFile);
  }
  zResolved = db_include_search(zFile);
  if(zResolved){
    db_including_add(zFile, t->zName, t->token.lineNo);
    cmpp_process_file(zResolved);
    db_include_rm(zFile);
    db_free(zResolved);
  }else{
    cmpp_kwd__err_prefix(pKw, t, NULL);
    fatal("file not found: %s", zFile);
  }
}

/* Impl. for #pragma. */
static void cmpp_kwd_pragma(CmppKeyword const * pKw, CmppTokenizer *t){
  const char * zArg;
  if(CT_skip(t)) return;
  else if(t->args.argc<2){
    cmpp_kwd__misuse(pKw, t, "Expecting an argument");
  }
  zArg = (const char *)t->args.argv[1];
#define M(X) 0==strcmp(zArg,X)
  if(M("defines")){
    sqlite3_stmt * q = 0;
    db_prepare(&q, "SELECT k FROM def ORDER BY k");
    g_stderr("cmpp defines:\n");
    while(SQLITE_ROW==db_step(q)){
      const char * z = (const char *)sqlite3_column_text(q, 0);
      int const n = sqlite3_column_bytes(q, 0);
      g_stderr("\t%.*s\n", n, z);
    }
    db_finalize(q);
  }else if(M("@")){
    if(t->args.argc>2){
      g.flags.atPolicy =
        AtPolicy_fromStr((char const *)t->args.argv[2], 1);
    }else{
      g.flags.atPolicy = AT_DEFAULT;
    }
  }else if(M("no-@")){
    g.flags.atPolicy = AT_OFF;
  }else if(M("chomp-F")){
    g.flags.chompF = 1;
  }else if(M("no-chomp-F")){
    g.flags.chompF = 0;
  }else{
    cmpp_kwd__misuse(pKw, t, "Unknown pragma: %s", zArg);
  }
#undef M
}

/* Impl. for #savepoint. */
static void cmpp_kwd_savepoint(CmppKeyword const * pKw, CmppTokenizer *t){
  const char * zArg;
  if(CT_skip(t)) return;
  else if(t->args.argc!=2){
    cmpp_kwd__misuse(pKw, t, "Expecting one argument");
  }
  zArg = (const char *)t->args.argv[1];
#define SP_NAME " cmpp /*" __FILE__ "*/;"
#define M(X) 0==strcmp(zArg,X)
  if(M("begin")){
    db_affirm_rc(sqlite3_exec(g.db, "SAVEPOINT" SP_NAME, 0, 0, 0),
                 "Starting a savepoint");
  }else if(M("rollback")){
    db_affirm_rc(
      sqlite3_exec(g.db,
                   "ROLLBACK TO SAVEPOINT" SP_NAME
                   "RELEASE SAVEPOINT" SP_NAME,
                   0, 0, 0
      ), "Rolling back a savepoint"
    );
  }else if(M("commit")){
    db_affirm_rc(
      sqlite3_exec(g.db,
                   "RELEASE" SP_NAME,
                   0, 0, 0
      ), "Committing a savepoint"
    );
  }else{
    cmpp_kwd__misuse(pKw, t, "Unknown savepoint option: %s", zArg);
  }
#undef SP_NAME
#undef M
}

/* #stder impl. */
static void cmpp_kwd_stderr(CmppKeyword const * pKw, CmppTokenizer *t){
  if(CT_skip(t)) return;
  else{
    const char *zBegin = t->args.argc>1
      ? (const char *)t->args.argv[1] : 0;
    if(zBegin){
      g_stderr("%s:%u: %s\n", t->zName, t->token.lineNo, zBegin);
    }else{
      g_stderr("%s:%u: (no %.*s%s argument)\n",
               t->zName, t->token.lineNo,
               g.delim.n, g.delim.z, pKw->zName);
    }
  }
}

#if 0
/* Impl. for dummy placeholder. */
static void cmpp_kwd_todo(CmppKeyword const * pKw, CmppTokenizer *t){
  (void)t;
  g_debug(0,("TODO: keyword handler for %s\n", pKw->zName));
}
#endif

CmppKeyword aKeywords[] = {
/* Keep these sorted by zName */
  {"//", 2, 0, TT_Comment, cmpp_kwd_noop},
  {"define", 6, 1, TT_Define, cmpp_kwd_define},
  {"elif", 4, 1, TT_Elif, cmpp_kwd_if},
  {"elifnot", 7, 1, TT_ElifNot, cmpp_kwd_if},
  {"else", 4, 1, TT_Else, cmpp_kwd_else},
  {"endif", 5, 0, TT_EndIf, cmpp_kwd_endif},
  {"error", 4, 0, TT_Error, cmpp_kwd_error},
  {"if", 2, 1, TT_If, cmpp_kwd_if},
  {"ifnot", 5, 1, TT_IfNot, cmpp_kwd_if},
  {"include", 7, 0, TT_Include, cmpp_kwd_include},
  {"pragma", 6, 1, TT_Pragma, cmpp_kwd_pragma},
  {"savepoint", 9, 1, TT_Savepoint, cmpp_kwd_savepoint},
  {"stderr", 6, 0, TT_Stderr, cmpp_kwd_stderr},
  {"undef", 5, 1, TT_Undef, cmpp_kwd_define},
  {0,0,TT_Invalid, 0}
};

static int cmp_CmppKeyword(const void *p1, const void *p2){
  char const * zName = (const char *)p1;
  CmppKeyword const * kw = (CmppKeyword const *)p2;
  return strcmp(zName, kw->zName);
}

CmppKeyword const * CmppKeyword_search(const char *zName){
  return (CmppKeyword const *)bsearch(zName, &aKeywords[0],
                                      sizeof(aKeywords)/sizeof(aKeywords[0]) - 1,
                                      sizeof(aKeywords[0]),
                                      cmp_CmppKeyword);
}

void cmpp_process_keyword(CmppTokenizer * const t){
  assert(t->args.pKw);
  assert(t->args.argc);
  t->args.pKw->xCall(t->args.pKw, t);
  t->args.pKw = 0;
  t->args.argc = 0;
}

void cmpp_process_file(const char * zName){
  FileWrapper fw = FileWrapper_empty;
  CmppTokenizer const * const oldTok = g.tok;
  CmppTokenizer ct = CmppTokenizer_empty;
  g.tok = &ct;
  FileWrapper_open(&fw, zName, "r");
  FileWrapper_slurp(&fw);
  g_debug(1,("Read %u byte(s) from [%s]\n", fw.nContent, fw.zName));
  if( fw.zContent ){
    ct.zName = zName;
    ct.zBegin = fw.zContent;
    ct.zEnd = fw.zContent + fw.nContent;
    while(cmpp_next_keyword_line(&ct)){
      cmpp_process_keyword(&ct);
    }
  }
  FileWrapper_close(&fw);
  if(0!=ct.level.ndx){
    CmppLevel * const lv = CmppLevel_get(&ct);
    fatal("Input ended inside an unterminated nested construct"
          "opened at [%s] line %u", zName, lv->token.lineNo);
  }
  g.tok = oldTok;
}


void fatalv(char const *zFmt, va_list va){
  fflush(stdout);
  fputc('\n', stderr);
  if( g.tok ){
    fprintf(stderr,"%s: @%s:%d: ",
            g.zArgv0,
            (g.tok->zName && 0==strcmp("-",g.tok->zName))
            ? "<stdin>"
            : g.tok->zName,
            g.tok->lineNo);
  }
  if(zFmt && *zFmt){
    vfprintf(stderr, zFmt, va);
  }
  fputc('\n', stderr);
  fflush(stderr);
  exit(1);
}

void fatal(char const *zFmt, ...){
  va_list va;
  va_start(va, zFmt);
  fatalv(zFmt, va);
  va_end(va);
}

#undef CT_level
#undef CT_pstate
#undef CT_skipLevel
#undef CT_skip
#undef CLvl_skip

static void usage(int isErr){
  FILE * const fOut = isErr ? stderr : stdout;
  fprintf(fOut, "Usage: %s [flags] [infile...]\n", g.zArgv0);
  fprintf(fOut,
          "Flags and filenames may be in any order and "
          "they are processed in that order.\n"
          "\nFlags:\n");
#define GAP "     "
#define arg(F,D) fprintf(fOut,"\n  %s\n" GAP "%s\n",F, D)
  arg("-o|--outfile FILE","Send output to FILE (default=- (stdout)).\n"
      GAP "Because arguments are processed in order, this should\n"
      GAP "normally be given before -f.");
  arg("-f|--file FILE","Read input from FILE (default=- (stdin)).\n"
      "     All non-flag arguments are assumed to be the input files.");
  arg("-DXYZ[=value]","Define XYZ to the given value (default=1).");
  arg("-UXYZ","Undefine all defines matching glob XYZ.");
  arg("-IXYZ","Add dir XYZ to the " CMPP_DEFAULT_DELIM "include path.");
  arg("-FXYZ=filename",
      "Define XYZ to the raw contents of the given file.\n"
      GAP "The file is not processed as by " CMPP_DEFAULT_DELIM"include\n"
      GAP "Maybe it should be. Or maybe we need a new flag for that.");
  arg("-d|--delimiter VALUE", "Set keyword delimiter to VALUE "
      "(default=" CMPP_DEFAULT_DELIM ").");
  arg("--@-policy retain|elide|error|off",
      "Specifies how to handle @tokens@ (default=off).\n"
      GAP "off    = do not look for @tokens@\n"
      GAP "retain = parse @tokens@ and retain any undefined ones\n"
      GAP "elide  = parse @tokens@ and elide any undefined ones\n"
      GAP "error  = parse @tokens@ and error for any undefined ones"
  );
  arg("-@", "Equivalent to --@-policy=error.");
  arg("-no-@", "Equivalent to --@-policy=off (the default).");
  arg("--sql-trace", "Send a trace of all SQL to stderr.");
  arg("--sql-trace-x",
      "Like --sql-trace but expand all bound values in the SQL.");
  arg("--no-sql-trace", "Disable SQL tracing (default).");
  arg("--chomp-F", "One trailing newline is trimmed from files "
      "read via -FXYZ=filename.");
  arg("--no-chomp-F", "Disable --chomp-F (default).");
#undef arg
#undef GAP
  fputs("\n",fOut);
}

static void get_flag_val(int argc, char const * const * argv, int * ndx,
                    char const **zVal){
  char const * zEq = strchr(argv[*ndx], '=');
  if( zEq ){
    *zVal = zEq+1;
    return;
  }
  if(*ndx+1>=argc){
    fatal("Missing value for flag '%s'", argv[*ndx]);
  }
  *zVal = argv[++*ndx];
}

static int arg_is_flag( char const *zFlag, char const *zArg,
                        char const **zValIfEqX ){
  *zValIfEqX = 0;
  if( 0==strcmp(zFlag, zArg) ) return 1;
  char const * z = strchr(zArg,'=');
  if( z && z>zArg ){
    /* compare the part before the '=' */
    if( 0==strncmp(zFlag, zArg, z-zArg) ){
      *zValIfEqX = z+1;
      return 1;
    }
  }
  return 0;
}

int main(int argc, char const * const * argv){
  int rc = 0;
  int inclCount = 0;
  int nFile = 0;
  int ndxTrace = 0;
  int expandMode = 0;
  char const * zVal = 0;
#define ARGVAL if( !zVal ) get_flag_val(argc, argv, &i, &zVal)
#define M(X) arg_is_flag(X, zArg, &zVal)
#define ISFLAG(X) else if(M(X))
#define ISFLAG2(X,Y) else if(M(X) || M(Y))
#define NOVAL if( zVal ) fatal("Unexpected value for %s", zArg)

  g.zArgv0 = argv[0];
#define DOIT if(doIt)
  for(int doIt = 0; doIt<2; ++doIt){
    /**
       Loop through the flags twice. The first time we just validate
       and look for --help/-?. The second time we process the flags.
       This approach allows us to easily chain multiple files and
       flags:

       ./c-pp -Dfoo -o foo x.y -Ufoo -Dbar -o bar x.y
    */
    DOIT{
      atexit(cmpp_atexit);
      if( 1==ndxTrace ){
        /* Ensure that we start with tracing in the early stage if
           --sql-trace is the first arg, in order to log schema
           setup. */
        g.sqlTrace.pFile = stderr;
        g.sqlTrace.expandSql = expandMode;
      }
      cmpp_initdb();
    }
    for(int i = 1; i < argc; ++i){
      int negate = 0;
      char const * zArg = argv[i];
      //g_stderr("i=%d zArg=%s\n", i, zArg);
      zVal = 0;
      while('-'==*zArg) ++zArg;
      if(zArg==argv[i]/*not a flag*/){
        zVal = zArg;
        goto do_infile;
      }
      if( 0==strncmp(zArg,"no-",3) ){
        zArg += 3;
        negate = 1;
      }
      ISFLAG2("?","help"){
        NOVAL;
        usage(0);
        goto end;
      }else if('D'==*zArg){
        ++zArg;
        if(!*zArg) fatal("Missing key for -D");
        DOIT {
          db_define_add(zArg);
        }
      }else if('F'==*zArg){
        ++zArg;
        if(!*zArg) fatal("Missing key for -F");
        DOIT {
          db_define_add_file(zArg);
        }
      }else if('U'==*zArg){
        ++zArg;
        if(!*zArg) fatal("Missing key for -U");
        DOIT {
          db_define_rm(zArg);
        }
      }else if('I'==*zArg){
        ++zArg;
        if(!*zArg) fatal("Missing directory for -I");
        DOIT {
          db_include_dir_add(zArg);
          ++inclCount;
        }
      }
      ISFLAG2("o","outfile"){
        ARGVAL;
        DOIT {
          FileWrapper_open(&g.out, zVal, "w");
        }
      }
      ISFLAG2("f","file"){
        ARGVAL;
      do_infile:
        DOIT {
          ++nFile;
          if(!g.out.pFile) FileWrapper_open(&g.out, "-", "w");
          if(!inclCount){
            db_include_dir_add(".");
            ++inclCount;
          }
          cmpp_process_file(zVal);
        }
      }
      ISFLAG("@"){
        NOVAL;
        DOIT {
          assert( AT_DEFAULT!=AT_OFF );
          g.flags.atPolicy = negate ? AT_OFF : AT_DEFAULT;
        }
      }
      ISFLAG("@-policy"){
        AtPolicy aup;
        ARGVAL;
        aup = AtPolicy_fromStr(zVal, 1);
        DOIT {
          g.flags.atPolicy = aup;
        }
      }
      ISFLAG("debug"){
        NOVAL;
        g.flags.doDebug += negate ? -1 : 1;
      }
      ISFLAG("sql-trace"){
        NOVAL;
        /* Needs to be set before the start of the second pass, when
           the db is inited. */
        g.sqlTrace.expandSql = 0;
        DOIT {
          g.sqlTrace.pFile = negate ? (FILE*)0 : stderr;
        }else if( !ndxTrace && !negate ){
          ndxTrace = i;
          expandMode = 0;
        }
      }
      ISFLAG("sql-trace-x"){
        NOVAL;
        g.sqlTrace.expandSql = 1;
        DOIT {
          g.sqlTrace.pFile = negate ? (FILE*)0 : stderr;
        }else if( !ndxTrace && !negate ){
          ndxTrace = i;
          expandMode = 1;
        }
      }
      ISFLAG("chomp-F"){
        NOVAL;
        DOIT g.flags.chompF = !negate;
      }
      ISFLAG2("d","delimiter"){
        ARGVAL;
        if( !doIt ){
          g.delim.z = zVal;
          g.delim.n = (unsigned short)strlen(zVal);
          if(!g.delim.n) fatal("Keyword delimiter may not be empty.");
        }
      }
      else{
        fatal("Unhandled flag: %s", argv[i]);
      }
    }
    DOIT {
      if(!nFile){
        if(!g.out.zName) g.out.zName = "-";
        if(!inclCount){
          db_include_dir_add(".");
          ++inclCount;
        }
        FileWrapper_open(&g.out, g.out.zName, "w");
        cmpp_process_file("-");
      }
    }
  }
  end:
  FileWrapper_close(&g.out);
  return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
