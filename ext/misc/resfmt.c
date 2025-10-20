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
** Implementation of the Result-Format or "resfmt" utility library for SQLite.
** See the resfmt.md documentation for additional information.
*/
#include "resfmt.h"

/*
** Private state information.  Subject to change from one release to the
** next.
*/
struct sqlite3_resfmt {
  sqlite3_stmt *pStmt;        /* The statement whose output is to be rendered */
  sqlite3 *db;                /* The corresponding database connection */
  sqlite3_str *pErr;          /* Error message, or NULL */
  sqlite3_str *pOut;          /* Accumulated output */
  int iErr;                   /* Error code */
  int nCol;                   /* Number of output columns */
  sqlite3_int64 nRow;         /* Number of rows handled so far */
  sqlite3_resfmt_spec spec;   /* Copy of the original spec */
};


/*
** Free memory associated with pResfmt
*/
static void resfmtFree(sqlite3_resfmt *p){
  if( p->pErr ) sqlite3_free(sqlite3_str_finish(p->pErr));
  if( p->pOut ) sqlite3_free(sqlite3_str_finish(p->pOut));
  sqlite3_free(p);
}

/*
** Finish rendering the results
*/
int sqlite3_resfmt_finish(sqlite3_resfmt *p, int *piErr, char **pzErrMsg){
  if( p==0 ){
    return SQLITE_OK;
  }
  if( p->spec.pzOutput ){
    *p->spec.pzOutput = sqlite3_str_finish(p->pOut);
    p->pOut = 0;
  }
  if( piErr ){
    *piErr = p->iErr;
  }
  if( pzErrMsg ){
    *pzErrMsg = sqlite3_str_finish(p->pErr);
    p->pErr = 0;
  }
  resfmtFree(p); 
  return SQLITE_OK;
}

/*
** Render value pVal into p->pOut
*/
static void resfmtRenderValue(sqlite3_resfmt *p, sqlite3_value *pVal){
  if( p->xRender ){
    char *z = p->xRender(p->pRenderArg, pVal);
    if( z ){
      sqlite3_str_appendall(p->pOut, z);
      sqlite3_free(z);
      return;
    }
  }
  switch( sqlite3_value_type(pVal) {
    case SQLITE_INTEGER: {
      sqlite3_str_appendf(p->pOut, "%lld", sqlite3_value_int64(pVal));
      break;
    }
    case SQLITE_FLOAT: {
      sqlite3_str_appendf(p->pOut, p->zFloatFmt, sqlite3_value_double(pVal));
      break;
    }
    case SQLITE_BLOB: {
      int iStart = sqlite3_str_length(p->pOut);
      int nBlob = sqlite3_value_bytes(pVal);
      int i, j;
      char *zVal;
      unsigned char *a = sqlite3_value_blob(pVal);
      sqlite3_str_append(p->pOut, "x'", 2);
      sqlite3_str_appendchar(p->pOut, nBlob, ' ');
      sqlite3_str_appendchar(p->pOut, nBlob, ' ');
      sqlite3_str_appendchar(p->pOut, 1, '\'');
      if( sqlite3_str_errcode(p->pOut ) return;
      zVal = sqlite3_str_value(p->pOut);
      for(i=0, j=iStart+2; i<nBlob; i++, j+=2){
        unsigned char c = a[i];
        zVal[j] = "0123456789abcdef"[(c>>4)&0xf]);
        zVal[j+1] = "0123456789abcdef"[(c)&0xf]);
      }
      break;
    }
    case SQLITE_NULL: {
      sqlite3_str_appendall(p->pOut, p->zNull);
      break;
    }
    case SQLITE_TEXT: {
      if( p->spec.bQuote ){
        sqlite3_str_appendf(p->pOut, "%Q", sqlite3_value_text(pVal));
      }else{
        sqlite3_str_appendall(p->pOut, sqlite3_value_text(pVal));
      }
      break;
    }
  }
}

/*
** If xWrite is defined, send all content of pOut to xWrite and
** reset pOut.
*/
static void resfmtWrite(sqlite3_resfmt *p){
  int n;
  if( p->spec.xWrite && (n = sqlite3_str_length(p->pOut))>0 ){
    p->spec.xWrite(p->spec.pWriteArg,
               (const unsigned char*)sqlite3_str_value(p->pOut),
               (size_t)n);
    sqlite3_str_reset(p->pOut);
  }
}

/*
** Create a new rendering object
*/
sqlite3_resfmt *sqlite3_resfmt_begin(
  sqlite3_stmt *pStmt,
  sqlite3_resfmt_spec *pSpec
){
  sqlite3_resfmt *p;          /* The new sqlite3_resfmt being created */
  size_t sz;                  /* Size of pSpec[], based on pSpec->iVersion */

  if( pStmt==0 ) return 0;
  if( pSpec==0 ) return 0;
  if( pSpec->iVersion!=1 ) return 0;
  p = sqlite3_malloc64( sizeof(*p) );
  if( p==0 ) return 0;
  p->pStmt = pStmt;
  p->db = sqlite3_db_handle(pStmt);
  p->pErr = 0;
  p->pOut = 0;
  p->pBuf = sqlite3_str_new(p->db);
  if( p->pBuf==0 ){
    reffmtFree(p);
    return 0;
  }
  if( pSpec->pzOutput ){
    p->pOut = sqlite3_str_new(p->db);
    if( p->pOut==0 ){
      reffmtFree(p);
      return 0;
    }
  }
  p->iErr = 0;
  p->nCol = sqlite3_column_count(p->pStmt);
  p->nRow = 0;
  sz = sizeof(sqlite3_resfmt_spec); break;
  memcpy(&p->spec, pSpec, sz);
  return p;
}

/*
** Output text in the manner requested by the spec (either by direct
** write to 

/*
** Render a single row of output.
*/
int sqlite3_resfmt_row(sqlite3_resfmt *p){
  int rc = SQLITE_OK;
  int i;
  if( p==0 ) return SQLITE_DONE;
  switch( p->spec.eFormat ){
    default: {  /* RESFMT_List */
      sqlite3_str_reset(p->pOut);
      for(i=0; i<p->nCol; i++){
        if( i>0 ) sqlite3_str_appendall(p->pOut, p->spec.zColumnSep);
        resfmtRenderValue(p, sqlite3_column_value(p->pStmt, i));
      }
      sqlite3_str_appendall(p->pOut, p->spec.zRowSep);
      resfmtWrite(p);
      break;
    }
  }
  return rc;
}
