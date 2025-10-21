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
#include <string.h>

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
** Escape the input string if it is needed and in accordance with
** eEscape, which is either RESFMT_E_Ascii or RESFMT_E_Symbol.
**
** Escaping is needed if the string contains any control characters
** other than \t, \n, and \r\n
**
** If no escaping is needed (the common case) then set *ppOut to NULL
** and return 0.  If escaping is needed, write the escaped string into
** memory obtained from sqlite3_malloc64() and make *ppOut point to that
** memory and return 0.  If an error occurs, return non-zero.
**
** The caller is responsible for freeing *ppFree if it is non-NULL in order
** to reclaim memory.
*/
static void resfmtEscape(
  int eEscape,            /* RESFMT_E_Ascii or RESFMT_E_Symbol */
  sqlite3_str *pStr,      /* String to be escaped */
  int iStart              /* Begin escapding on this byte of pStr */
){
  sqlite3_int64 i, j;     /* Loop counters */
  sqlite3_int64 sz;       /* Size of the string prior to escaping */
  sqlite3_int64 nCtrl = 0;/* Number of control characters to escape */
  unsigned char *zIn;     /* Text to be escaped */
  unsigned char c;        /* A single character of the text */
  unsigned char *zOut;    /* Where to write the results */

  /* Find the text to be escaped */
  zIn = (unsigned char*)sqlite3_str_value(pStr);
  if( zIn==0 ) return;
  zIn += iStart;

  /* Count the control characters */
  for(i=0; (c = zIn[i])!=0; i++){
    if( c<=0x1f
     && c!='\t'
     && c!='\n'
     && (c!='\r' || zIn[i+1]!='\n')
    ){
      nCtrl++;
    }
  }
  if( nCtrl==0 ) return;  /* Early out if no control characters */

  /* Make space to hold the escapes.  Copy the original text to the end
  ** of the available space. */
  sz = sqlite3_str_length(pStr) - iStart;
  if( eEscape==RESFMT_E_Symbol ) nCtrl *= 2;
  sqlite3_str_appendchar(pStr, nCtrl, ' ');
  zOut = (unsigned char*)sqlite3_str_value(pStr);
  if( zOut==0 ) return;
  zOut += iStart;
  zIn = zOut + nCtrl;
  memmove(zIn,zOut,sz);

  /* Convert the control characters */
  for(i=j=0; (c = zIn[i])!=0; i++){
    if( c>0x1f
     || c=='\t'
     || c=='\n'
     || (c=='\r' && zIn[i+1]=='\n')
    ){
      continue;
    }
    if( i>0 ){
      memmove(&zOut[j], zIn, i);
      j += i;
    }
    zIn += i+1;
    i = -1;
    if( eEscape==RESFMT_E_Symbol ){
      zOut[j++] = 0xe2;
      zOut[j++] = 0x90;
      zOut[j++] = 0x80+c;
    }else{
      zOut[j++] = '^';
      zOut[j++] = 0x40+c;
    }
  }
}

/*
** Encode text appropriately and append it to p->pOut.
*/
static void resfmtEncodeText(sqlite3_resfmt *p, const char *zTxt){
  int iStart = sqlite3_str_length(p->pOut);
  switch( p->spec.eQuote ){
    case RESFMT_Q_Sql: {
      sqlite3_str_appendf(p->pOut, "%Q", zTxt);
      break;
    }
    default: {
      sqlite3_str_appendall(p->pOut, zTxt);
      break;
    }
  }
  if( p->spec.eEscape!=RESFMT_E_Off ){
    resfmtEscape(p->spec.eEscape, p->pOut, iStart);
  }
}

/*
** Render value pVal into p->pOut
*/
static void resfmtRenderValue(sqlite3_resfmt *p, int iCol){
  if( p->spec.xRender ){
    sqlite3_value *pVal;
    char *z;
    pVal = sqlite3_value_dup(sqlite3_column_value(p->pStmt,iCol));
    z = p->spec.xRender(p->spec.pRenderArg, pVal);
    sqlite3_value_free(pVal);
    if( z ){
      sqlite3_str_appendall(p->pOut, z);
      sqlite3_free(z);
      return;
    }
  }
  switch( sqlite3_column_type(p->pStmt,iCol) ){
    case SQLITE_INTEGER: {
      sqlite3_str_appendf(p->pOut, "%lld", sqlite3_column_int64(p->pStmt,iCol));
      break;
    }
    case SQLITE_FLOAT: {
      if( p->spec.zFloatFmt ){
        double r = sqlite3_column_double(p->pStmt,iCol);
        sqlite3_str_appendf(p->pOut, p->spec.zFloatFmt, r);
      }else{
        const char *zTxt = (const char*)sqlite3_column_text(p->pStmt,iCol);
        sqlite3_str_appendall(p->pOut, zTxt);
      }
      break;
    }
    case SQLITE_BLOB: {
      switch( p->spec.eBlob ){
        case RESFMT_B_Hex:
        case RESFMT_B_Sql: {
          int iStart;
          int nBlob = sqlite3_column_bytes(p->pStmt,iCol);
          int i, j;
          char *zVal;
          const unsigned char *a = sqlite3_column_blob(p->pStmt,iCol);
          if( p->spec.eBlob==RESFMT_B_Sql ){
            sqlite3_str_append(p->pOut, "x'", 2);
          }
          iStart = sqlite3_str_length(p->pOut);
          sqlite3_str_appendchar(p->pOut, nBlob, ' ');
          sqlite3_str_appendchar(p->pOut, nBlob, ' ');
          if( p->spec.eBlob==RESFMT_B_Sql ){
            sqlite3_str_appendchar(p->pOut, 1, '\'');
          }
          if( sqlite3_str_errcode(p->pOut) ) return;
          zVal = sqlite3_str_value(p->pOut);
          for(i=0, j=iStart; i<nBlob; i++, j+=2){
            unsigned char c = a[i];
            zVal[j] = "0123456789abcdef"[(c>>4)&0xf];
            zVal[j+1] = "0123456789abcdef"[(c)&0xf];
          }
          break;
        }
        case RESFMT_B_Tcl:
        case RESFMT_B_Json: {
          int iStart;
          int nBlob = sqlite3_column_bytes(p->pStmt,iCol);
          int i, j;
          char *zVal;
          const unsigned char *a = sqlite3_column_blob(p->pStmt,iCol);
          int szC = p->spec.eBlob==RESFMT_B_Json ? 6 : 4;
          sqlite3_str_append(p->pOut, "\"", 1);
          iStart = sqlite3_str_length(p->pOut);
          for(i=szC; i>0; i--){
            sqlite3_str_appendchar(p->pOut, nBlob, ' ');
          }
          sqlite3_str_appendchar(p->pOut, 1, '"');
          if( sqlite3_str_errcode(p->pOut) ) return;
          zVal = sqlite3_str_value(p->pOut);
          for(i=0, j=iStart; i<nBlob; i++, j+=szC){
            unsigned char c = a[i];
            zVal[j] = '\\';
            if( szC==4 ){
              zVal[j+1] = '0' + ((c>>6)&3);
              zVal[j+2] = '0' + ((c>>3)&7);
              zVal[j+3] = '0' + (c&7);
            }else{
              zVal[j+1] = 'u';
              zVal[j+2] = '0';
              zVal[j+3] = '0';
              zVal[j+4] = "0123456789abcdef"[(c>>4)&0xf];
              zVal[j+5] = "0123456789abcdef"[(c)&0xf];
            }
          }
          break;
        }
        default: {
          const char *zTxt = (const char*)sqlite3_column_text(p->pStmt,iCol);
          resfmtEncodeText(p, zTxt);
        }
      }
      break;
    }
    case SQLITE_NULL: {
      sqlite3_str_appendall(p->pOut, p->spec.zNull);
      break;
    }
    case SQLITE_TEXT: {
      const char *zTxt = (const char*)sqlite3_column_text(p->pStmt,iCol);
      resfmtEncodeText(p, zTxt);
      break;
    }
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
  p->pOut = sqlite3_str_new(p->db);
  if( p->pOut==0 ){
    resfmtFree(p);
    return 0;
  }
  p->iErr = 0;
  p->nCol = sqlite3_column_count(p->pStmt);
  p->nRow = 0;
  sz = sizeof(sqlite3_resfmt_spec);
  memcpy(&p->spec, pSpec, sz);
  if( p->spec.zNull==0 ) p->spec.zNull = "";
  if( p->spec.eBlob==RESFMT_B_Auto ){
    switch( p->spec.eQuote ){
      case RESFMT_Q_Sql:  p->spec.eBlob = RESFMT_B_Sql;  break;
      case RESFMT_Q_Csv:  p->spec.eBlob = RESFMT_B_Tcl;  break;
      case RESFMT_Q_Tcl:  p->spec.eBlob = RESFMT_B_Tcl;  break;
      case RESFMT_Q_Json: p->spec.eBlob = RESFMT_B_Json; break;
      default:            p->spec.eBlob = RESFMT_B_Text; break;
    }
  }
  switch( p->spec.eFormat ){
    case RESFMT_List: {
      if( p->spec.zColumnSep==0 ) p->spec.zColumnSep = "|";
      if( p->spec.zRowSep==0 ) p->spec.zRowSep = "\n";
      break;
    }
  }
  return p;
}

/*
** Render a single row of output.
*/
int sqlite3_resfmt_row(sqlite3_resfmt *p){
  int rc = SQLITE_OK;
  int i;
  if( p==0 ) return SQLITE_DONE;
  switch( p->spec.eFormat ){
    case RESFMT_Off:
    case RESFMT_Count: {
      /* No-op */
      break;
    }
    default: {  /* RESFMT_List */
      if( p->nRow==0 && p->spec.bShowCNames ){
        for(i=0; i<p->nCol; i++){
          const char *zCName = sqlite3_column_name(p->pStmt, i);
          if( i>0 ) sqlite3_str_appendall(p->pOut, p->spec.zColumnSep);
          resfmtEncodeText(p, zCName);
        }
        sqlite3_str_appendall(p->pOut, p->spec.zRowSep);
        resfmtWrite(p);
      }
      for(i=0; i<p->nCol; i++){
        if( i>0 ) sqlite3_str_appendall(p->pOut, p->spec.zColumnSep);
        resfmtRenderValue(p, i);
      }
      sqlite3_str_appendall(p->pOut, p->spec.zRowSep);
      resfmtWrite(p);
      break;
    }
  }
  p->nRow++;
  return rc;
}

/*
** Finish rendering the results
*/
int sqlite3_resfmt_finish(sqlite3_resfmt *p, int *piErr, char **pzErrMsg){
  if( p==0 ){
    return SQLITE_OK;
  }
  switch( p->spec.eFormat ){
    case RESFMT_Count: {
      sqlite3_str_appendf(p->pOut, "%lld\n", p->nRow);
      resfmtWrite(p);
      break;
    }
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
