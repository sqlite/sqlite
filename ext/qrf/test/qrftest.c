/*
** 2025-12-01
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
** A test harness for QRF.
*/
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <ctype.h>
#include "sqlite3.h"
#include "qrf.h"

/* Integer types */
typedef sqlite3_int64 i64;
typedef unsigned char u8;

/*
** State object for the test
*/
typedef struct QrfTest QrfTest;
struct QrfTest {
  int nErr;                  /* Number of errors */
  int nTest;                 /* Number of test cases */
  sqlite3 *db;               /* Database connection used for tests */
  const char *zFilename;     /* Input filename */
  i64 nLine;                 /* Line number of last line of input read */
  FILE *in;                  /* Input file stream */
  sqlite3_str *pExpected;    /* Expected results */
  char *zResult;             /* Results written here */
  sqlite3_str *pResult;      /* Or here */
  sqlite3_str *pSql;         /* Accumulated SQL script */
  sqlite3_qrf_spec spec;     /* Output format spec */
};

/*
** Report OOM and die
*/
static void qrfTestOom(void){
  printf("Out of memory\n");
  exit(1);
}

/*
** Change a string value in spec.
*/
static void qrfTestSetStr(char **pz, const char *z){
  size_t n = strlen(z);
  if( (*pz)!=0 ) sqlite3_free(*pz);
  if( z==0 || strcmp(z,"<NULL>")==0 ){
    *pz = 0;
  }else{
    *pz = sqlite3_malloc64( n+1 );
    if( (*pz)==0 ) qrfTestOom();
    memcpy(*pz, z, n);
    (*pz)[n] = 0;
  }
}

/*
** Free all resources held by p->spec.
*/
static void qrfTestResetSpec(QrfTest *p){
  sqlite3_free(p->spec.aWidth);
  sqlite3_free(p->spec.aAlign);
  sqlite3_free(p->spec.zColumnSep);
  sqlite3_free(p->spec.zRowSep);
  sqlite3_free(p->spec.zTableName);
  sqlite3_free(p->spec.zNull);
  memset(&p->spec, 0, sizeof(p->spec));
  p->spec.iVersion = 1;
}

/*
** Free all memory resources held by p.
*/
static void qrfTestReset(QrfTest *p){
  if( p->in ){ fclose(p->in); p->in = 0; }
  if( p->db ){ sqlite3_close(p->db); p->db = 0; }
  if( p->pExpected ){ sqlite3_str_free(p->pExpected); p->pExpected = 0; }
  if( p->zResult ){ sqlite3_free(p->zResult); p->zResult = 0; }
  if( p->pResult ){ sqlite3_str_free(p->pResult); p->pResult = 0; }
  qrfTestResetSpec(p);
}

/*
** Report an error
*/
static void qrfTestError(QrfTest *p, const char *zFormat, ...){
  va_list ap;
  sqlite3_str *pErr;
  va_start(ap, zFormat);
  pErr = sqlite3_str_new(p->db);
  sqlite3_str_vappendf(pErr, zFormat, ap);
  va_end(ap);
  printf("%s:%d: %s\n", p->zFilename, (int)p->nLine, sqlite3_str_value(pErr));
  sqlite3_str_free(pErr);
  p->nErr++;
}

/*
** Return a pointer to the next token in the input, or NULL if there
** are no more tokens.   Leave *pz pointing to the first character past
** the end of the token.
*/
static const char *nextToken(char **pz, int eMode){
  char *z = *pz;
  char *zReturn = 0;
  while( isspace(z[0]) ) z++;
  if( eMode!=1 && z[0]=='*' && z[1]=='/' ) return 0;
  zReturn = z;
  if( z[0]==0 ) return 0;
  while( z[0] && !isspace(z[0]) ){ z++; }
  if( z[0] ){
    z[0] = 0;
    z++;
  }
  *pz = z;
  return zReturn;
}

/* Arrays of names that map symbol names into numeric constants. */

static const char *azStyle[] = {
  "auto", "box", "column", "count", "csv", "eqp", "explain",
  "html", "insert", "json", "jobject", "line", "list", "markdown",
  "off", "quote", "stats", "statsest", "statsvm", "table", 0
};
static const char *azEsc[] = {
  "auto", "off", "ascii", "symbol", 0
};
static const char *azText[] = {
  "auto", "plain", "sql", "csv", "html", "tcl", "json", 0
};
static const char *azBlob[] = {
  "auto", "text", "sql", "hex", "tcl", "json", "size", 0
};
static const char *azBool[] = {
  "auto", "off", "on", 0
};
static const char *azAlign[] = { "auto", "left", "right", "center", 0 };

/*
** Find the match for zArg, and azChoice[] and return its index.
** If not found, issue an error message and return 0;
*/
static int findChoice(
  QrfTest *p,
  const char *zKey,
  const char *zArg,
  const char *const*azChoice
){
  int i;
  sqlite3_str *pErr;
  if( zArg==0 ){
    qrfTestError(p, "missing argument to \"%s\"", zKey);
    return 0;
  }
  for(i=0; azChoice[i]; i++){
    if( strcmp(zArg,azChoice[i])==0 ) return i;
  }
  pErr = sqlite3_str_new(p->db);
  sqlite3_str_appendf(pErr, "argument to %s should be one of:");
  for(i=0; azChoice[i]; i++){
    sqlite3_str_appendf(pErr, " %s", azChoice[i]);
  }
  qrfTestError(p, "%z", sqlite3_str_finish(pErr));
  return 0;
}

/*
** zLine[] contains text that changes values of p->spec.  Parse that
** line and make appropriate changes.
**
** Return 0 if zLine[] ends with and end-of-comment.  Return 1 if the
** spec definition is to continue.
*/
static int qrfTestParseSpec(QrfTest *p, char *zLine){
  const char *zToken;
  while( (zToken = nextToken(&zLine,1))!=0 ){
    if( strcmp(zToken,"*/")==0 ) return 0;
    if( strcmp(zToken,"eStyle")==0 ){
      p->spec.eStyle = findChoice(p, zToken, nextToken(&zLine,0), azStyle);
    }else
    if( strcmp(zToken,"eEsc")==0 ){
      p->spec.eEsc = findChoice(p, zToken, nextToken(&zLine,0), azEsc);
    }else
    if( strcmp(zToken,"eText")==0 ){
      p->spec.eText = findChoice(p, zToken, nextToken(&zLine,0), azText);
    }else
    if( strcmp(zToken,"eTitle")==0 ){
      p->spec.eTitle = findChoice(p, zToken, nextToken(&zLine,0), azText);
    }else
    if( strcmp(zToken,"eBlob")==0 ){
      p->spec.eBlob = findChoice(p, zToken, nextToken(&zLine,0), azBlob);
    }else
    if( strcmp(zToken,"bTitles")==0 ){
      p->spec.bTitles = findChoice(p, zToken, nextToken(&zLine,0),azBool);
    }else
    if( strcmp(zToken,"bWordWrap")==0 ){
      p->spec.bWordWrap = findChoice(p, zToken, nextToken(&zLine,0),azBool);
    }else
    if( strcmp(zToken,"bTextJsonb")==0 ){
      p->spec.bTextJsonb = findChoice(p, zToken, nextToken(&zLine,0),azBool);
    }else
    if( strcmp(zToken,"eDfltAlign")==0 ){
      p->spec.eDfltAlign = findChoice(p, zToken, nextToken(&zLine,0),azAlign);
    }else
    if( strcmp(zToken,"eTitleAlign")==0 ){
      p->spec.eTitleAlign = findChoice(p, zToken, nextToken(&zLine,0),azAlign);
    }else
    if( strcmp(zToken,"bSplitColumn")==0 ){
      p->spec.bSplitColumn = findChoice(p, zToken, nextToken(&zLine,0),azBool);
    }else
    if( strcmp(zToken,"bBorder")==0 ){
      p->spec.bBorder = findChoice(p, zToken, nextToken(&zLine,0),azBool);
    }else
    if( strcmp(zToken,"zColumnSep")==0 ){
      qrfTestSetStr(&p->spec.zColumnSep, nextToken(&zLine,0));
    }else
    if( strcmp(zToken,"zRowSep")==0 ){
      qrfTestSetStr(&p->spec.zRowSep, nextToken(&zLine,0));
    }else
    if( strcmp(zToken,"zTableName")==0 ){
      qrfTestSetStr(&p->spec.zTableName, nextToken(&zLine,0));
    }else
    if( strcmp(zToken,"zNull")==0 ){
      qrfTestSetStr(&p->spec.zNull, nextToken(&zLine,0));
    }else
    {
      qrfTestError(p, "unknown spec key: \"%s\"", zToken);
    }
  }
  return 1;
}

/*
** Read and run a single test script.
**
** The file is SQL text.  Special C-style comments control the testing.
** Because this description is itself within a C-style comment, the comment
** delimiters are shown as (*...*), with parentheses instead of "/".
**
**  (* spec KEYWORD VALUE ... *)
**
**      Fill out the p->spec field to use for the next test.
**
**  (* result
**  ** EXPECTED
**  *)
**
**      Run QRF and compare results against EXPECTED, with leading "** "
**      removed.
**
*/
static void qrfTestOneFile(QrfTest *p, const char *zFilename){
  int rc;               /* Result code */
  int eMode;            /* 0 = gather SQL.  1 = spec.  2 = gather result */
  char zLine[4000];     /* One line of input */

  p->nLine = 0;
  p->zFilename = zFilename;
  p->in = 0;
  p->zResult = 0;
  p->pResult = 0;
  p->pExpected = 0;
  p->pSql = 0;
  memset(&p->spec, 0, sizeof(p->spec));
  p->spec.iVersion = 1;
  rc = sqlite3_open(":memory:", &p->db);
  if( rc ){
    qrfTestError(p, "cannot open an in-memory database");
    return;
  }
  p->in = fopen(zFilename, "rb");
  if( p->in==0 ){
    qrfTestError(p, "cannot open input file \"%s\"", zFilename);
    qrfTestReset(p);
    return;
  }
  p->pSql = sqlite3_str_new(p->db);
  p->pExpected = sqlite3_str_new(p->db);
  while( fgets(zLine, sizeof(zLine), p->in) ){
    size_t n = strlen(zLine);
    int done = 0;
    p->nLine++;
    if( n==0 ) continue;
    if( zLine[n-1]!='\n' ){
      qrfTestError(p, "input line too long. Max length %d",(int)sizeof(zLine));
      qrfTestReset(p);
      return;
    }
    do{
      done = 1;
      switch( eMode ){
        case 0: {  /* Gathering SQL text */
          if( strncmp(zLine, "/* spec", 7)==0 ){
            memmove(zLine, &zLine[7], n-7);
            n-= 7;
            eMode = 1;
            done = 0;
          }else if( strncmp(zLine, "/* result", 9)==0 ){
            /* qrfTestRunSql(p); */
            sqlite3_str_truncate(p->pExpected, 0);
            eMode = 2;
          }else{
            sqlite3_str_append(p->pSql, zLine, n);
          }
          break;
        }
        case 1: {  /* Processing spec descriptions */
          eMode = qrfTestParseSpec(p, zLine);
          break;
        }
        case 2: {
          if( strncmp(zLine, "*/",2)==0 ){
          }else if( strcmp(zLine, "**\n")==0 ){
            sqlite3_str_append(p->pExpected, "\n", 1);
          }else if( strncmp(zLine, "** ",3)==0 ){
            sqlite3_str_append(p->pExpected, zLine+3, n-3);
          }else{
            qrfTestError(p, "bad result line");
          }
          break;
        }
      } /* End of switch(eMode) */
    }while( !done );
  }
  qrfTestReset(p);
  if( sqlite3_memory_used()>0 ){
    qrfTestError(p, "Memory leak: %lld bytes", sqlite3_memory_used());
  }
  p->nTest++;
}

/*
** Program entry point
*/
int main(int argc, char **argv){
  int i;
  QrfTest x;
  memset(&x, 0, sizeof(x));
  for(i=1; i<argc; i++){
    int nErr = x.nErr;
    qrfTestOneFile(&x, argv[i]);
    if( x.nErr>nErr ){
      printf("%s: %d error%s\n", argv[i], x.nErr-nErr, (x.nErr>nErr+1)?"s":"");
    }
  }
  printf("Test cases: %d   Errors: %d\n", x.nTest, x.nErr);
}
