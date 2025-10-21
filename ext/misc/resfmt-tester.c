/*
** 2025-10-20
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** A simple command-line program for testing the Result-Format or "resfmt"
** utility library for SQLite.
*/
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "sqlite3.h"
#include "resfmt.h"

#define COUNT(X)  (sizeof(X)/sizeof(X[0]))

/* Report out-of-memory and die if the argument is NULL */
static void checkOOM(void *p){
  if( p==0 ){
    fprintf(stdout, "out-of-memory\n");
    exit(1);
  }
}

/* A block of memory */
typedef struct memblock memblock;
struct memblock {
  memblock *pNext;
};

/* List of all memory to be freed */
static memblock *pToFree = 0;

/* Free all memory at exit */
static void tempFreeAll(void){
  while( pToFree ){
    memblock *pNext = pToFree->pNext;
    sqlite3_free(pToFree);
    pToFree = pNext;
  }
}

/* Allocate memory that will be freed all at once by freeall() */
static void *tempMalloc(size_t n){
  memblock *p;
  if( n>0x10000000 ) checkOOM(0);
  p = sqlite3_malloc64( n+sizeof(memblock) );
  checkOOM(p);
  p->pNext = pToFree;
  pToFree = p;
  return (void*)&pToFree[1];
}

/* Make a copy of a string using tempMalloc() */
static char *tempStrdup(char *zIn){
  size_t n;
  char *z;
  if( zIn==0 ) zIn = "";
  n  = strlen(zIn);
  if( n>0x10000000 ) checkOOM(0);
  z = tempMalloc( n+1 );
  checkOOM(z);
  memcpy(z, zIn, n+1);
  return z;
}

/* Function used for writing to the console */
ssize_t testWriter(void *pContext, const unsigned char *p, size_t n){
  return fwrite(p,1,n,stdout);
}

int main(int argc, char **argv){
  char *zSrc;
  FILE *pSrc;
  sqlite3_str *pBuf;
  sqlite3 *db = 0;
  sqlite3_stmt *pStmt;
  int rc;
  int lineNum = 0;
  int bUseWriter = 1;
  sqlite3_resfmt_spec spec;
  char zLine[1000];

  if( argc<2 ){
    zSrc = "<stdin>";
    pSrc = stdin;
  }else{
    zSrc = argv[1];
    pSrc = fopen(zSrc, "rb");
    if( pSrc==0 ){
      fprintf(stderr, "cannot open \"%s\" for reading\n", zSrc);
      exit(1);
    }
  }
  memset(&spec, 0, sizeof(spec));
  spec.iVersion = 1;
  spec.eFormat = RESFMT_List;
  spec.xWrite = testWriter;
  pBuf = sqlite3_str_new(0);
  rc = sqlite3_open(":memory:", &db);
  if( rc ){
    fprintf(stderr, "unable to open an in-memory database: %s\n",
            sqlite3_errmsg(db));
    exit(1);
  }
  while( fgets(zLine, sizeof(zLine), pSrc) ){
    size_t n = strlen(zLine);
    lineNum++;
    while( n>0 && zLine[n-1]>0 && zLine[n-1]<=' ' ) n--;
    zLine[n] = 0;
    printf("%s\n", zLine);
    if( strncmp(zLine, "--open=", 7)==0 ){
      if( db ) sqlite3_close(db);
      db = 0;
      rc = sqlite3_open(&zLine[7], &db);
      if( rc!=SQLITE_OK ){
        fprintf(stderr, "%s:%d: cannot open \"%s\": %s\n",
                zSrc, lineNum, &zLine[7],
                sqlite3_errmsg(db));
        exit(1);
      }
    }else
    if( strcmp(zLine, "--go")==0 ){
      const char *zSql, *zTail;
      sqlite3_resfmt *pFmt;
      int iErr = 0;
      char *zErr = 0;
      int n;
      if( db==0 ){
        fprintf(stderr, "%s:%d: database not open\n", zSrc, lineNum);
        exit(1);
      }
      zSql = sqlite3_str_value(pBuf);
      pStmt = 0;
      while( zSql[0] ){
        rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, &zTail);
        if( rc || pStmt==0 ){
          fprintf(stderr, "%s:%d: sqlite3_prepare() fails: %s\n",
                  zSrc, lineNum, sqlite3_errmsg(db));
          sqlite3_finalize(pStmt);
          break;
        }
        zSql = sqlite3_sql(pStmt);
        while( isspace(zSql[0]) ) zSql++;
        n = (int)strlen(zSql);
        while( n>0 && isspace(zSql[n-1]) ) n--;
        if( n>0 ){
          char *zOut = 0;
          printf("/* %.*s */\n", n, zSql);
          if( bUseWriter ){
            spec.pzOutput = 0;
            spec.xWrite = testWriter;
          }else{
            spec.pzOutput = &zOut;
            spec.xWrite = 0;
          }
          pFmt = sqlite3_resfmt_begin(pStmt, &spec);
          while( sqlite3_step(pStmt)==SQLITE_ROW ){
            sqlite3_resfmt_row(pFmt);
          }
          rc = sqlite3_resfmt_finish(pFmt, &iErr, &zErr);
          if( !bUseWriter && zOut ){
            fputs(zOut, stdout);
            sqlite3_free(zOut);
          }
          printf("/* rc=%d.  error-code=%d.  error-message=%s */\n",
                 rc, iErr, zErr ? zErr : "NULL");
          sqlite3_free(zErr);
        }
        sqlite3_finalize(pStmt);
        pStmt = 0;
        zSql = zTail;
      }
      sqlite3_str_reset(pBuf);
    }else
    if( strncmp(zLine, "--eFormat=", 10)==0 ){
      const struct { const char *z; int e; } aFmt[] = {
         { "box",      RESFMT_Box,      },
         { "column",   RESFMT_Column,   },
         { "count",    RESFMT_Count,    },
         { "csv",      RESFMT_Csv,      },
         { "eqp",      RESFMT_EQP,      },
         { "explain",  RESFMT_Explain,  },
         { "html",     RESFMT_Html,     },
         { "insert",   RESFMT_Insert,   },
         { "json",     RESFMT_Json,     },
         { "line",     RESFMT_Line,     },
         { "list",     RESFMT_List,     },
         { "markdown", RESFMT_Markdown, },
         { "off",      RESFMT_Off,      },
         { "pretty",   RESFMT_Pretty,   },
         { "table",    RESFMT_Table,    },
         { "tcl",      RESFMT_Tcl,      },
         { "scanexp",  RESFMT_ScanExp,  },
      };
      int i;
      for(i=0; i<COUNT(aFmt); i++){
        if( strcmp(aFmt[i].z,&zLine[10])==0 ){
          spec.eFormat = aFmt[i].e;
          break;
        }
      }
      if( i>=COUNT(aFmt) ){
        sqlite3_str *pMsg = sqlite3_str_new(0);
        for(i=0; i<COUNT(aFmt); i++){
          sqlite3_str_appendf(pMsg, " %s", aFmt[i].z);
        }
        fprintf(stderr, "%s:%d: no such format: \"%s\"\nChoices: %s\n",
                zSrc, lineNum, &zLine[10], sqlite3_str_value(pMsg));
        sqlite3_free(sqlite3_str_finish(pMsg));
      }
    }else
    if( strncmp(zLine, "--eQuote=", 9)==0 ){
      const struct { const char *z; int e; } aQuote[] = {
         { "c",        RESFMT_Q_C       },
         { "csv",      RESFMT_Q_Csv     },
         { "html",     RESFMT_Q_Html    },
         { "json",     RESFMT_Q_Json    },
         { "off",      RESFMT_Q_Off     },
         { "sql",      RESFMT_Q_Sql     },
         { "tcl",      RESFMT_Q_C       },
      };
      int i;
      for(i=0; i<COUNT(aQuote); i++){
        if( strcmp(aQuote[i].z,&zLine[9])==0 ){
          spec.eQuote = aQuote[i].e;
          break;
        }
      }
      if( i>=COUNT(aQuote) ){
        sqlite3_str *pMsg = sqlite3_str_new(0);
        for(i=0; i<COUNT(aQuote); i++){
          sqlite3_str_appendf(pMsg, " %s", aQuote[i].z);
        }
        fprintf(stderr, "%s:%d: no such quoting style: \"%s\"\nChoices: %s\n",
                zSrc, lineNum, &zLine[9], sqlite3_str_value(pMsg));
        sqlite3_free(sqlite3_str_finish(pMsg));
      }
    }else
    if( strncmp(zLine, "--eEscape=", 10)==0 ){
      const struct { const char *z; int e; } aEscape[] = {
         { "ascii",     RESFMT_E_Ascii   },
         { "off",       RESFMT_E_Off     },
         { "symbol",    RESFMT_E_Symbol  },
      };
      int i;
      for(i=0; i<COUNT(aEscape); i++){
        if( strcmp(aEscape[i].z,&zLine[10])==0 ){
          spec.eEscape = aEscape[i].e;
          break;
        }
      }
      if( i>=COUNT(aEscape) ){
        sqlite3_str *pMsg = sqlite3_str_new(0);
        for(i=0; i<COUNT(aEscape); i++){
          sqlite3_str_appendf(pMsg, " %s", aEscape[i].z);
        }
        fprintf(stderr, "%s:%d: no such escape mode: \"%s\"\nChoices: %s\n",
                zSrc, lineNum, &zLine[10], sqlite3_str_value(pMsg));
        sqlite3_free(sqlite3_str_finish(pMsg));
      }
    }else
    if( strncmp(zLine, "--bShowCNames=", 14)==0 ){
      spec.bShowCNames = atoi(&zLine[14])!=0;
    }else
    if( strncmp(zLine, "--zNull=", 8)==0 ){
      spec.zNull = tempStrdup(&zLine[8]);
    }else
    if( strncmp(zLine, "--zColumnSep=", 13)==0 ){
      spec.zColumnSep = tempStrdup(&zLine[13]);
    }else
    if( strncmp(zLine, "--zRowSep=", 10)==0 ){
      spec.zRowSep = tempStrdup(&zLine[10]);
    }else
    if( strcmp(zLine, "--exit")==0 ){
      break;
    }else
    if( strncmp(zLine, "--use-writer=",13)==0 ){
      bUseWriter = atoi(&zLine[13])!=0;
    }else
    {
      if( sqlite3_str_length(pBuf) ) sqlite3_str_append(pBuf, "\n", 1);
      sqlite3_str_appendall(pBuf, zLine);
    }
  }
  if( db ) sqlite3_close(db);
  sqlite3_free(sqlite3_str_finish(pBuf));
  tempFreeAll();
  if( pSrc!=stdin ) fclose(pSrc);
  return 0;
}
