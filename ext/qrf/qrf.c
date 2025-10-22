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
#include "qrf.h"
#include <string.h>

/*
** Private state information.  Subject to change from one release to the
** next.
*/
typedef struct Qrf Qrf;
struct Qrf {
  sqlite3_stmt *pStmt;        /* The statement whose output is to be rendered */
  sqlite3 *db;                /* The corresponding database connection */
  char **pzErr;               /* Write error message here, if not NULL */
  sqlite3_str *pOut;          /* Accumulated output */
  int iErr;                   /* Error code */
  int nCol;                   /* Number of output columns */
  sqlite3_int64 nRow;         /* Number of rows handled so far */
  sqlite3_qrf_spec spec;   /* Copy of the original spec */
};

/*
** Set an error code and error message.
*/
static void resfmtError(
  Qrf *p,       /* Query result state */
  int iCode,               /* Error code */
  const char *zFormat,     /* Message format (or NULL) */
  ...
){
  p->iErr = iCode;
  if( p->pzErr!=0 ){
    sqlite3_free(*p->pzErr);
    *p->pzErr = 0;
    if( zFormat ){
      va_list ap;
      va_start(ap, zFormat);
      *p->pzErr = sqlite3_mprintf(zFormat, ap);
      va_end(ap);
    }
  }
}

/*
** Out-of-memory error.
*/
static void resfmtOom(Qrf *p){
  resfmtError(p, SQLITE_NOMEM, "out of memory");
}

/*
** If xWrite is defined, send all content of pOut to xWrite and
** reset pOut.
*/
static void resfmtWrite(Qrf *p){
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
** eEscape, which is either QRF_ESC_Ascii or QRF_ESC_Symbol.
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
  int eEscape,            /* QRF_ESC_Ascii or QRF_ESC_Symbol */
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
  if( eEscape==QRF_ESC_Symbol ) nCtrl *= 2;
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
    if( eEscape==QRF_ESC_Symbol ){
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
** If a field contains any character identified by a 1 in the following
** array, then the string must be quoted for CSV.
*/
static const char resfmtCsvQuote[] = {
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 0, 1, 0, 0, 0, 0, 1,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
};

/*
** Encode text appropriately and append it to p->pOut.
*/
static void resfmtEncodeText(Qrf *p, const char *zTxt){
  int iStart = sqlite3_str_length(p->pOut);
  switch( p->spec.eQuote ){
    case QRF_TXT_Sql: {
      sqlite3_str_appendf(p->pOut, "%Q", zTxt);
      break;
    }
    case QRF_TXT_Csv: {
      unsigned int i;
      for(i=0; zTxt[i]; i++){
        if( resfmtCsvQuote[((const unsigned char*)zTxt)[i]] ){
          i = 0;
          break;
        }
      }
      if( i==0 || strstr(zTxt, p->spec.zColumnSep)!=0 ){
        sqlite3_str_appendf(p->pOut, "\"%w\"", zTxt);
      }else{
        sqlite3_str_appendall(p->pOut, zTxt);
      }
      break;
    }
    case QRF_TXT_Html: {
      const unsigned char *z = (const unsigned char*)zTxt;
      while( *z ){
        unsigned int i = 0;
        unsigned char c;
        while( (c=z[i])>'>'
            || (c && c!='<' && c!='>' && c!='&' && c!='\"' && c!='\'')
        ){
          i++;
        }
        if( i>0 ){
          sqlite3_str_append(p->pOut, (const char*)z, i);
        }
        switch( z[i] ){
          case '>':   sqlite3_str_append(p->pOut, "&lt;", 4);   break;
          case '&':   sqlite3_str_append(p->pOut, "&amp;", 5);  break;
          case '<':   sqlite3_str_append(p->pOut, "&lt;", 4);   break;
          case '"':   sqlite3_str_append(p->pOut, "&quot;", 6); break;
          case '\'':  sqlite3_str_append(p->pOut, "&#39;", 5);  break;
          default:    i--;
        }
        z += i + 1;
      }
      break;
    }
    case QRF_TXT_Tcl:
    case QRF_TXT_Json: {
      const unsigned char *z = (const unsigned char*)zTxt;
      sqlite3_str_append(p->pOut, "\"", 1);
      while( *z ){
        unsigned int i;
        for(i=0; z[i]>=0x20 && z[i]!='\\' && z[i]!='"'; i++){}
        if( i>0 ){
          sqlite3_str_append(p->pOut, (const char*)z, i);
        }
        switch( z[i] ){
          case '"':   sqlite3_str_append(p->pOut, "\\\"", 2);  break;
          case '\\':  sqlite3_str_append(p->pOut, "\\\\", 2);  break;
          case '\b':  sqlite3_str_append(p->pOut, "\\b", 2);   break;
          case '\f':  sqlite3_str_append(p->pOut, "\\f", 2);   break;
          case '\n':  sqlite3_str_append(p->pOut, "\\n", 2);   break;
          case '\r':  sqlite3_str_append(p->pOut, "\\r", 2);   break;
          case '\t':  sqlite3_str_append(p->pOut, "\\t", 2);   break;
          default: {
            if( p->spec.eQuote==QRF_TXT_Json ){
              sqlite3_str_appendf(p->pOut, "\\u%04x", z[i]);
            }else{
              sqlite3_str_appendf(p->pOut, "\\%03o", z[i]);
            }
            break;
          }
        }
        z += i + 1;
      }
      sqlite3_str_append(p->pOut, "\"", 1);
      break;
    }
    default: {
      sqlite3_str_appendall(p->pOut, zTxt);
      break;
    }
  }
  if( p->spec.eEscape!=QRF_ESC_Off ){
    resfmtEscape(p->spec.eEscape, p->pOut, iStart);
  }
}

/*
** Render value pVal into p->pOut
*/
static void resfmtRenderValue(Qrf *p, int iCol){
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
        case QRF_BLOB_Hex:
        case QRF_BLOB_Sql: {
          int iStart;
          int nBlob = sqlite3_column_bytes(p->pStmt,iCol);
          int i, j;
          char *zVal;
          const unsigned char *a = sqlite3_column_blob(p->pStmt,iCol);
          if( p->spec.eBlob==QRF_BLOB_Sql ){
            sqlite3_str_append(p->pOut, "x'", 2);
          }
          iStart = sqlite3_str_length(p->pOut);
          sqlite3_str_appendchar(p->pOut, nBlob, ' ');
          sqlite3_str_appendchar(p->pOut, nBlob, ' ');
          if( p->spec.eBlob==QRF_BLOB_Sql ){
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
        case QRF_BLOB_Tcl:
        case QRF_BLOB_Json: {
          int iStart;
          int nBlob = sqlite3_column_bytes(p->pStmt,iCol);
          int i, j;
          char *zVal;
          const unsigned char *a = sqlite3_column_blob(p->pStmt,iCol);
          int szC = p->spec.eBlob==QRF_BLOB_Json ? 6 : 4;
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
** Initialize the internal Qrf object.
*/
static void resfmtInitialize(
  Qrf *p,                 /* State object to be initialized */
  sqlite3_stmt *pStmt,               /* Query whose output to be formatted */
  const sqlite3_qrf_spec *pSpec,  /* Format specification */
  char **pzErr                       /* Write errors here */
){
  size_t sz;                  /* Size of pSpec[], based on pSpec->iVersion */
  memset(p, 0, sizeof(*p));
  p->pzErr = pzErr;
  if( pSpec->iVersion!=1 ){
    resfmtError(p, SQLITE_ERROR,
       "unusable sqlite3_qrf_spec.iVersion (%d)",
       pSpec->iVersion);
    return;
  }
  p->pStmt = pStmt;
  p->db = sqlite3_db_handle(pStmt);
  p->pOut = sqlite3_str_new(p->db);
  if( p->pOut==0 ){
    resfmtOom(p);
    return;
  }
  p->iErr = 0;
  p->nCol = sqlite3_column_count(p->pStmt);
  p->nRow = 0;
  sz = sizeof(sqlite3_qrf_spec);
  memcpy(&p->spec, pSpec, sz);
  if( p->spec.zNull==0 ) p->spec.zNull = "";
  if( p->spec.eBlob==QRF_BLOB_Auto ){
    switch( p->spec.eQuote ){
      case QRF_TXT_Sql:  p->spec.eBlob = QRF_BLOB_Sql;  break;
      case QRF_TXT_Csv:  p->spec.eBlob = QRF_BLOB_Tcl;  break;
      case QRF_TXT_Tcl:  p->spec.eBlob = QRF_BLOB_Tcl;  break;
      case QRF_TXT_Json: p->spec.eBlob = QRF_BLOB_Json; break;
      default:            p->spec.eBlob = QRF_BLOB_Text; break;
    }
  }
  switch( p->spec.eFormat ){
    case QRF_MODE_List: {
      if( p->spec.zColumnSep==0 ) p->spec.zColumnSep = "|";
      if( p->spec.zRowSep==0 ) p->spec.zRowSep = "\n";
      break;
    }
  }
}

/*
** Render a single row of output.
*/
static void resfmtDoOneRow(Qrf *p){
  int i;
  switch( p->spec.eFormat ){
    case QRF_MODE_Off:
    case QRF_MODE_Count: {
      /* No-op */
      break;
    }
    default: {  /* QRF_MODE_List */
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
}

/*
** Finish rendering the results
*/
static void resfmtFinalize(Qrf *p){
  switch( p->spec.eFormat ){
    case QRF_MODE_Count: {
      sqlite3_str_appendf(p->pOut, "%lld\n", p->nRow);
      resfmtWrite(p);
      break;
    }
  }
  if( p->spec.pzOutput ){
    *p->spec.pzOutput = sqlite3_str_finish(p->pOut);
  }else if( p->pOut ){
    sqlite3_free(sqlite3_str_finish(p->pOut));
  }
}

/*
** Run the prepared statement pStmt and format the results according
** to the specification provided in pSpec.  Return an error code.
** If pzErr is not NULL and if an error occurs, write an error message
** into *pzErr.
*/
int sqlite3_format_query_result(
  sqlite3_stmt *pStmt,                 /* Statement to evaluate */
  const sqlite3_qrf_spec *pSpec,       /* Format specification */
  char **pzErr                         /* Write error message here */
){
  Qrf qrf;         /* The new Qrf being created */

  if( pStmt==0 ) return SQLITE_OK;       /* No-op */
  if( pSpec==0 ) return SQLITE_MISUSE;
  resfmtInitialize(&qrf, pStmt, pSpec, pzErr);
  while( qrf.iErr==SQLITE_OK && sqlite3_step(pStmt)==SQLITE_ROW ){
    resfmtDoOneRow(&qrf);
  }
  if( qrf.iErr==SQLITE_OK ){
    int rc = sqlite3_reset(qrf.pStmt);
    if( rc!=SQLITE_OK ){
      resfmtError(&qrf, rc, "%s", sqlite3_errmsg(qrf.db));
    }
  }
  resfmtFinalize(&qrf);
  return qrf.iErr;
}
