/*
** 2014-08-18
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains code to implement the "changeset" command line
** utility for displaying and transforming changesets generated by
** the Sessions extension.
*/
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>


/*
** Show a usage message on stderr then quit.
*/
static void usage(const char *argv0){
  fprintf(stderr, "Usage: %s FILENAME COMMAND ...\n", argv0);
  fprintf(stderr,
    "COMMANDs:\n"
    "  apply DB [OPTIONS]   Apply the changeset to database file DB. OPTIONS:\n"
    "                          -n|--dryrun     Test run. Don't apply changes\n"
    "                          --enablefk      Enable FOREIGN KEY support\n"
    "                          --nosavepoint   \\\n"
    "                          --invert         \\___  Flags passed into\n"
    "                          --ignorenoop     /     changeset_apply_v2()\n"
    "                          --fknoaction    /\n"
    "  concat FILE2 OUT     Concatenate FILENAME and FILE2 into OUT\n"
    "  dump                 Show the complete content of the changeset\n"
    "  invert OUT           Write an inverted changeset into file OUT\n"
    "  sql                  Give a pseudo-SQL rendering of the changeset\n"
  );
  exit(1);
}

/*
** Read the content of a disk file into an in-memory buffer
*/
static void readFile(const char *zFilename, int *pSz, void **ppBuf){
  FILE *f;
  sqlite3_int64 sz;
  void *pBuf;
  f = fopen(zFilename, "rb");
  if( f==0 ){
    fprintf(stderr, "cannot open \"%s\" for reading\n", zFilename);
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  rewind(f);
  pBuf = sqlite3_malloc64( sz ? sz : 1 );
  if( pBuf==0 ){
    fprintf(stderr, "cannot allocate %d to hold content of \"%s\"\n",
            (int)sz, zFilename);
    exit(1);
  }
  if( sz>0 ){
    if( fread(pBuf, (size_t)sz, 1, f)!=1 ){
      fprintf(stderr, "cannot read all %d bytes of \"%s\"\n",
              (int)sz, zFilename);
      exit(1);
    }
    fclose(f);
  }
  *pSz = (int)sz;
  *ppBuf = pBuf;
}

/* Array for converting from half-bytes (nybbles) into ASCII hex
** digits. */
static const char hexdigits[] = {
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' 
};

/*
** Render an sqlite3_value as an SQL string.
*/
static void renderValue(sqlite3_value *pVal){
  switch( sqlite3_value_type(pVal) ){
    case SQLITE_FLOAT: {
      double r1;
      char zBuf[50];
      r1 = sqlite3_value_double(pVal);
      sqlite3_snprintf(sizeof(zBuf), zBuf, "%!.15g", r1);
      printf("%s", zBuf);
      break;
    }
    case SQLITE_INTEGER: {
      printf("%lld", sqlite3_value_int64(pVal));
      break;
    }
    case SQLITE_BLOB: {
      char const *zBlob = sqlite3_value_blob(pVal);
      int nBlob = sqlite3_value_bytes(pVal);
      int i;
      printf("x'");
      for(i=0; i<nBlob; i++){
        putchar(hexdigits[(zBlob[i]>>4)&0x0F]);
        putchar(hexdigits[(zBlob[i])&0x0F]);
      }
      putchar('\'');
      break;
    }
    case SQLITE_TEXT: {
      const unsigned char *zArg = sqlite3_value_text(pVal);
      putchar('\'');
      while( zArg[0] ){
        putchar(zArg[0]);
        if( zArg[0]=='\'' ) putchar(zArg[0]);
        zArg++;
      }
      putchar('\'');
      break;
    }
    default: {
      assert( sqlite3_value_type(pVal)==SQLITE_NULL );
      printf("NULL");
      break;
    }
  }
}

/*
** Number of conflicts seen
*/
static int nConflict = 0;

/*
** The conflict callback
*/
static int conflictCallback(
  void *pCtx,
  int eConflict,
  sqlite3_changeset_iter *pIter
){
  int op, bIndirect, nCol, i;
  const char *zTab;
  unsigned char *abPK;
  const char *zType = "";
  const char *zOp = "";
  const char *zSep = " ";

  nConflict++;
  sqlite3changeset_op(pIter, &zTab, &nCol, &op, &bIndirect);
  sqlite3changeset_pk(pIter, &abPK, 0);
  switch( eConflict ){
    case SQLITE_CHANGESET_DATA:         zType = "DATA";         break;
    case SQLITE_CHANGESET_NOTFOUND:     zType = "NOTFOUND";     break;
    case SQLITE_CHANGESET_CONFLICT:     zType = "PRIMARY KEY";  break;
    case SQLITE_CHANGESET_FOREIGN_KEY:  zType = "FOREIGN KEY";  break;
    case SQLITE_CHANGESET_CONSTRAINT:   zType = "CONSTRAINT";   break;
  }
  switch( op ){
    case SQLITE_UPDATE:     zOp = "UPDATE of";     break;
    case SQLITE_INSERT:     zOp = "INSERT into";   break;
    case SQLITE_DELETE:     zOp = "DELETE from";   break;
  }
  printf("%s conflict on %s table %s with primary key", zType, zOp, zTab);
  for(i=0; i<nCol && abPK; i++){
    sqlite3_value *pVal;
    if( abPK[i]==0 ) continue;
    printf("%s", zSep);
    if( op==SQLITE_INSERT ){
      sqlite3changeset_new(pIter, i, &pVal);
    }else{
      sqlite3changeset_old(pIter, i, &pVal);
    }
    renderValue(pVal);
    zSep = ",";
  }
  printf("\n");
  return SQLITE_CHANGESET_OMIT;
}

int main(int argc, char **argv){
  int sz, rc;
  void *pBuf = 0;
  if( argc<3 ) usage(argv[0]);
  readFile(argv[1], &sz, &pBuf);

  /* changeset FILENAME apply DB
  ** Apply the changeset in FILENAME to the database file DB
  */
  if( strcmp(argv[2],"apply")==0 ){
    sqlite3 *db;
    int bDryRun = 0;
    int bEnableFK = 0;
    const char *zDb = 0;
    int i;
    int applyFlags = 0;
    for(i=3; i<argc; i++){
      const char *zArg = argv[i];
      if( zArg[0]=='-' ){
        if( zArg[1]=='-' && zArg[2]!=0 ) zArg++;
        if( strcmp(zArg, "-n")==0 || strcmp(zArg,"-dryrun")==0 ){
          bDryRun = 1;
          continue;
        }
        if( strcmp(zArg, "-nosavepoint")==0 ){
          applyFlags |= SQLITE_CHANGESETAPPLY_NOSAVEPOINT;
          continue;
        }
        if( strcmp(zArg, "-invert")==0 ){
          applyFlags |= SQLITE_CHANGESETAPPLY_INVERT;
          continue;
        }
        if( strcmp(zArg, "-ignorenoop")==0 ){
          applyFlags |= SQLITE_CHANGESETAPPLY_IGNORENOOP;
          continue;
        }
        if( strcmp(zArg, "-fknoaction")==0 ){
          applyFlags |= SQLITE_CHANGESETAPPLY_FKNOACTION;
          continue;
        }
        if( strcmp(zArg, "-enablefk")==0 ){
          bEnableFK = 1;
          continue;
        }
        fprintf(stderr, "unknown option: \"%s\"\n", argv[i]);
        exit(1);
      }else if( zDb ){
        fprintf(stderr, "unknown argument: \"%s\"\n", argv[i]);
        exit(1);
      }else{
        zDb = zArg;
      }
    }
    rc = sqlite3_open(zDb, &db);
    if( rc!=SQLITE_OK ){
      fprintf(stderr, "unable to open database file \"%s\": %s\n",
              zDb, sqlite3_errmsg(db));
      sqlite3_close(db);
      exit(1);
    }
    if( bEnableFK ){
      sqlite3_exec(db, "PRAGMA foreign_keys=1;", 0, 0, 0);
    }
    sqlite3_exec(db, "BEGIN", 0, 0, 0);
    nConflict = 0;
    if( applyFlags ){
      rc = sqlite3changeset_apply_v2(db, sz, pBuf, 0, conflictCallback, 0,
                                     0, 0, applyFlags);
    }else{
      rc = sqlite3changeset_apply(db, sz, pBuf, 0, conflictCallback, 0);
    }
    if( rc ){
      fprintf(stderr, "sqlite3changeset_apply() returned %d\n", rc);
    }
    if( nConflict || bDryRun ){
      fprintf(stderr, "%d conflicts - no changes applied\n", nConflict);
      sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
    }else if( rc ){
      fprintf(stderr, "sqlite3changeset_apply() returns %d "
                      "- no changes applied\n", rc);
      sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
    }else{
      sqlite3_exec(db, "COMMIT", 0, 0, 0);
    }
    sqlite3_close(db);
  }else

  /* changeset FILENAME concat FILE2 OUT
  ** Add changeset FILE2 onto the end of the changeset in FILENAME
  ** and write the result into OUT.
  */
  if( strcmp(argv[2],"concat")==0 ){
    int szB;
    void *pB;
    int szOut;
    void *pOutBuf;
    FILE *out;
    const char *zOut = argv[4];
    if( argc!=5 ) usage(argv[0]);
    out = fopen(zOut, "wb");
    if( out==0 ){
      fprintf(stderr, "cannot open \"%s\" for writing\n", zOut);
      exit(1);
    }
    readFile(argv[3], &szB, &pB);
    rc = sqlite3changeset_concat(sz, pBuf, szB, pB, &szOut, &pOutBuf);
    if( rc!=SQLITE_OK ){
      fprintf(stderr, "sqlite3changeset_concat() returns %d\n", rc);
    }else if( szOut>0 && fwrite(pOutBuf, szOut, 1, out)!=1 ){
      fprintf(stderr, "unable to write all %d bytes of output to \"%s\"\n",
              szOut, zOut);
    }
    fclose(out);
    sqlite3_free(pOutBuf);
    sqlite3_free(pB);
  }else

  /* changeset FILENAME dump
  ** Show the complete content of the changeset in FILENAME
  */
  if( strcmp(argv[2],"dump")==0 ){
    int cnt = 0;
    int i;
    sqlite3_changeset_iter *pIter;
    rc = sqlite3changeset_start(&pIter, sz, pBuf);
    if( rc!=SQLITE_OK ){
      fprintf(stderr, "sqlite3changeset_start() returns %d\n", rc);
      exit(1);
    }
    while( sqlite3changeset_next(pIter)==SQLITE_ROW ){
      int op, bIndirect, nCol;
      const char *zTab;
      unsigned char *abPK;
      sqlite3changeset_op(pIter, &zTab, &nCol, &op, &bIndirect);
      cnt++;
      printf("%d: %s table=[%s] indirect=%d nColumn=%d\n",
             cnt, op==SQLITE_INSERT ? "INSERT" :
                       op==SQLITE_UPDATE ? "UPDATE" : "DELETE",
             zTab, bIndirect, nCol);
      sqlite3changeset_pk(pIter, &abPK, 0);
      for(i=0; i<nCol; i++){
        sqlite3_value *pVal;
        pVal = 0;
        sqlite3changeset_old(pIter, i, &pVal);
        if( pVal ){
          printf("    old[%d]%s = ", i, abPK[i] ? "pk" : "  ");
          renderValue(pVal);
          printf("\n");
        }
        pVal = 0;
        sqlite3changeset_new(pIter, i, &pVal);
        if( pVal ){
          printf("    new[%d]%s = ", i, abPK[i] ? "pk" : "  ");
          renderValue(pVal);
          printf("\n");
        }
      }
    }
    sqlite3changeset_finalize(pIter);
  }else

  /* changeset FILENAME invert OUT
  ** Invert the changes in FILENAME and writes the result on OUT
  */
  if( strcmp(argv[2],"invert")==0 ){
    FILE *out;
    int szOut = 0;
    void *pOutBuf = 0;
    const char *zOut = argv[3];
    if( argc!=4 ) usage(argv[0]);
    out = fopen(zOut, "wb");
    if( out==0 ){
      fprintf(stderr, "cannot open \"%s\" for writing\n", zOut);
      exit(1);
    }
    rc = sqlite3changeset_invert(sz, pBuf, &szOut, &pOutBuf);
    if( rc!=SQLITE_OK ){
      fprintf(stderr, "sqlite3changeset_invert() returns %d\n", rc);
    }else if( szOut>0 && fwrite(pOutBuf, szOut, 1, out)!=1 ){
      fprintf(stderr, "unable to write all %d bytes of output to \"%s\"\n",
              szOut, zOut);
    }
    fclose(out);
    sqlite3_free(pOutBuf);
  }else

  /* changeset FILE sql
  ** Show the content of the changeset as pseudo-SQL
  */
  if( strcmp(argv[2],"sql")==0 ){
    int cnt = 0;
    char *zPrevTab = 0;
    char *zSQLTabName = 0;
    sqlite3_changeset_iter *pIter = 0;
    rc = sqlite3changeset_start(&pIter, sz, pBuf);
    if( rc!=SQLITE_OK ){
      fprintf(stderr, "sqlite3changeset_start() returns %d\n", rc);
      exit(1);
    }
    printf("BEGIN;\n");
    while( sqlite3changeset_next(pIter)==SQLITE_ROW ){
      int op, bIndirect, nCol;
      const char *zTab;
      sqlite3changeset_op(pIter, &zTab, &nCol, &op, &bIndirect);
      cnt++;
      if( zPrevTab==0 || strcmp(zPrevTab,zTab)!=0 ){
        sqlite3_free(zPrevTab);
        sqlite3_free(zSQLTabName);
        zPrevTab = sqlite3_mprintf("%s", zTab);
        if( !isalnum(zTab[0]) || sqlite3_strglob("*[^a-zA-Z0-9]*",zTab)==0 ){
          zSQLTabName = sqlite3_mprintf("\"%w\"", zTab);
        }else{
          zSQLTabName = sqlite3_mprintf("%s", zTab);
        }
        printf("/****** Changes for table %s ***************/\n", zSQLTabName);
      }
      switch( op ){
        case SQLITE_DELETE: {
          unsigned char *abPK;
          int i;
          const char *zSep = " ";
          sqlite3changeset_pk(pIter, &abPK, 0);
          printf("/* %d */ DELETE FROM %s WHERE", cnt, zSQLTabName);
          for(i=0; i<nCol; i++){
            sqlite3_value *pVal;
            if( abPK[i]==0 ) continue;
            printf("%sc%d=", zSep, i+1);
            zSep = " AND ";
            sqlite3changeset_old(pIter, i, &pVal);
            renderValue(pVal);
          }
          printf(";\n");
          break;
        }
        case SQLITE_UPDATE: {
          unsigned char *abPK;
          int i;
          const char *zSep = " ";
          sqlite3changeset_pk(pIter, &abPK, 0);
          printf("/* %d */ UPDATE %s SET", cnt, zSQLTabName);
          for(i=0; i<nCol; i++){
            sqlite3_value *pVal = 0;
            sqlite3changeset_new(pIter, i, &pVal);
            if( pVal ){
              printf("%sc%d=", zSep, i+1);
              zSep = ", ";
              renderValue(pVal);
            }
          }
          printf(" WHERE");
          zSep = " ";
          for(i=0; i<nCol; i++){
            sqlite3_value *pVal;
            if( abPK[i]==0 ) continue;
            printf("%sc%d=", zSep, i+1);
            zSep = " AND ";
            sqlite3changeset_old(pIter, i, &pVal);
            renderValue(pVal);
          }
          printf(";\n");
          break;
        }
        case SQLITE_INSERT: {
          int i;
          printf("/* %d */ INSERT INTO %s VALUES", cnt, zSQLTabName);
          for(i=0; i<nCol; i++){
            sqlite3_value *pVal;
            printf("%c", i==0 ? '(' : ',');
            sqlite3changeset_new(pIter, i, &pVal);
            renderValue(pVal);
          }
          printf(");\n");
          break;
        }
      }
    }
    printf("COMMIT;\n");
    sqlite3changeset_finalize(pIter);
    sqlite3_free(zPrevTab);
    sqlite3_free(zSQLTabName);
  }else

  /* If nothing else matches, show the usage comment */
  usage(argv[0]);
  sqlite3_free(pBuf);
  return 0; 
}
