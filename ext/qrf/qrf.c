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
** Implementation of the Result-Format or "qrf" utility library for SQLite.
** See the qrf.md documentation for additional information.
*/
#ifndef SQLITE_QRF_H
#include "qrf.h"
#endif
#include <string.h>
#include <ctype.h>
#include <assert.h>

typedef sqlite3_int64 i64;

/* A single line in the EQP output */
typedef struct qrfEQPGraphRow qrfEQPGraphRow;
struct qrfEQPGraphRow {
  int iEqpId;            /* ID for this row */
  int iParentId;         /* ID of the parent row */
  qrfEQPGraphRow *pNext; /* Next row in sequence */
  char zText[1];         /* Text to display for this row */
};

/* All EQP output is collected into an instance of the following */
typedef struct qrfEQPGraph qrfEQPGraph;
struct qrfEQPGraph {
  qrfEQPGraphRow *pRow;  /* Linked list of all rows of the EQP output */
  qrfEQPGraphRow *pLast; /* Last element of the pRow list */
  char zPrefix[100];     /* Graph prefix */
};

/*
** Private state information.  Subject to change from one release to the
** next.
*/
typedef struct Qrf Qrf;
struct Qrf {
  sqlite3_stmt *pStmt;        /* The statement whose output is to be rendered */
  sqlite3 *db;                /* The corresponding database connection */
  sqlite3_stmt *pJTrans;      /* JSONB to JSON translator statement */
  char **pzErr;               /* Write error message here, if not NULL */
  sqlite3_str *pOut;          /* Accumulated output */
  int iErr;                   /* Error code */
  int nCol;                   /* Number of output columns */
  int expMode;                /* Original sqlite3_stmt_isexplain() plus 1 */
  union {
    struct {                  /* Content for QRF_STYLE_Line */
      int mxColWth;             /* Maximum display width of any column */
      const char **azCol;       /* Names of output columns (MODE_Line) */
    } sLine;
    qrfEQPGraph *pGraph;      /* EQP graph (Eqp, Stats, and StatsEst) */
    struct {                  /* Content for QRF_STYLE_Explain */
      int nIndent;              /* Slots allocated for aiIndent */
      int iIndent;              /* Current slot */
      int *aiIndent;            /* Indentation for each opcode */
    } sExpln;
  } u;
  sqlite3_int64 nRow;         /* Number of rows handled so far */
  int *actualWidth;           /* Actual width of each column */
  sqlite3_qrf_spec spec;      /* Copy of the original spec */
};

/*
** Set an error code and error message.
*/
static void qrfError(
  Qrf *p,                /* Query result state */
  int iCode,             /* Error code */
  const char *zFormat,   /* Message format (or NULL) */
  ...
){
  p->iErr = iCode;
  if( p->pzErr!=0 ){
    sqlite3_free(*p->pzErr);
    *p->pzErr = 0;
    if( zFormat ){
      va_list ap;
      va_start(ap, zFormat);
      *p->pzErr = sqlite3_vmprintf(zFormat, ap);
      va_end(ap);
    }
  }
}

/*
** Out-of-memory error.
*/
static void qrfOom(Qrf *p){
  qrfError(p, SQLITE_NOMEM, "out of memory");
}



/*
** Add a new entry to the EXPLAIN QUERY PLAN data
*/
static void qrfEqpAppend(Qrf *p, int iEqpId, int p2, const char *zText){
  qrfEQPGraphRow *pNew;
  sqlite3_int64 nText;
  if( zText==0 ) return;
  if( p->u.pGraph==0 ){
    p->u.pGraph = sqlite3_malloc64( sizeof(qrfEQPGraph) );
    if( p->u.pGraph==0 ){
      qrfOom(p);
      return;
    }
    memset(p->u.pGraph, 0, sizeof(qrfEQPGraph) );
  }
  nText = strlen(zText);
  pNew = sqlite3_malloc64( sizeof(*pNew) + nText );
  if( pNew==0 ){
    qrfOom(p);
    return;
  }
  pNew->iEqpId = iEqpId;
  pNew->iParentId = p2;
  memcpy(pNew->zText, zText, nText+1);
  pNew->pNext = 0;
  if( p->u.pGraph->pLast ){
    p->u.pGraph->pLast->pNext = pNew;
  }else{
    p->u.pGraph->pRow = pNew;
  }
  p->u.pGraph->pLast = pNew;
}

/*
** Free and reset the EXPLAIN QUERY PLAN data that has been collected
** in p->u.pGraph.
*/
static void qrfEqpReset(Qrf *p){
  qrfEQPGraphRow *pRow, *pNext;
  if( p->u.pGraph ){
    for(pRow = p->u.pGraph->pRow; pRow; pRow = pNext){
      pNext = pRow->pNext;
      sqlite3_free(pRow);
    }
    sqlite3_free(p->u.pGraph);
    p->u.pGraph = 0;
  }
}

/* Return the next EXPLAIN QUERY PLAN line with iEqpId that occurs after
** pOld, or return the first such line if pOld is NULL
*/
static qrfEQPGraphRow *qrfEqpNextRow(Qrf *p, int iEqpId, qrfEQPGraphRow *pOld){
  qrfEQPGraphRow *pRow = pOld ? pOld->pNext : p->u.pGraph->pRow;
  while( pRow && pRow->iParentId!=iEqpId ) pRow = pRow->pNext;
  return pRow;
}

/* Render a single level of the graph that has iEqpId as its parent.  Called
** recursively to render sublevels.
*/
static void qrfEqpRenderLevel(Qrf *p, int iEqpId){
  qrfEQPGraphRow *pRow, *pNext;
  i64 n = strlen(p->u.pGraph->zPrefix);
  char *z;
  for(pRow = qrfEqpNextRow(p, iEqpId, 0); pRow; pRow = pNext){
    pNext = qrfEqpNextRow(p, iEqpId, pRow);
    z = pRow->zText;
    sqlite3_str_appendf(p->pOut, "%s%s%s\n", p->u.pGraph->zPrefix,
                            pNext ? "|--" : "`--", z);
    if( n<(i64)sizeof(p->u.pGraph->zPrefix)-7 ){
      memcpy(&p->u.pGraph->zPrefix[n], pNext ? "|  " : "   ", 4);
      qrfEqpRenderLevel(p, pRow->iEqpId);
      p->u.pGraph->zPrefix[n] = 0;
    }
  }
}

/*
** Display and reset the EXPLAIN QUERY PLAN data
*/
static void qrfEqpRender(Qrf *p, i64 nCycle){
  qrfEQPGraphRow *pRow;
  if( p->u.pGraph!=0 && (pRow = p->u.pGraph->pRow)!=0 ){
    if( pRow->zText[0]=='-' ){
      if( pRow->pNext==0 ){
        qrfEqpReset(p);
        return;
      }
      sqlite3_str_appendf(p->pOut, "%s\n", pRow->zText+3);
      p->u.pGraph->pRow = pRow->pNext;
      sqlite3_free(pRow);
    }else if( nCycle>0 ){
      sqlite3_str_appendf(p->pOut, "QUERY PLAN (cycles=%lld [100%%])\n",nCycle);
    }else{
      sqlite3_str_appendall(p->pOut, "QUERY PLAN\n");
    }
    p->u.pGraph->zPrefix[0] = 0;
    qrfEqpRenderLevel(p, 0);
    qrfEqpReset(p);
  }
}

#ifdef SQLITE_ENABLE_STMT_SCANSTATUS
/*
** Helper function for qrfExpStats().
**
*/
static int qrfStatsHeight(sqlite3_stmt *p, int iEntry){
  int iPid = 0;
  int ret = 1;
  sqlite3_stmt_scanstatus_v2(p, iEntry,
      SQLITE_SCANSTAT_SELECTID, SQLITE_SCANSTAT_COMPLEX, (void*)&iPid
  );
  while( iPid!=0 ){
    int ii;
    for(ii=0; 1; ii++){
      int iId;
      int res;
      res = sqlite3_stmt_scanstatus_v2(p, ii,
          SQLITE_SCANSTAT_SELECTID, SQLITE_SCANSTAT_COMPLEX, (void*)&iId
      );
      if( res ) break;
      if( iId==iPid ){
        sqlite3_stmt_scanstatus_v2(p, ii,
            SQLITE_SCANSTAT_PARENTID, SQLITE_SCANSTAT_COMPLEX, (void*)&iPid
        );
      }
    }
    ret++;
  }
  return ret;
}
#endif /* SQLITE_ENABLE_STMT_SCANSTATUS */


/*
** Generate ".scanstatus est" style of EQP output.
*/
static void qrfEqpStats(Qrf *p){
#ifndef SQLITE_ENABLE_STMT_SCANSTATUS
  qrfError(p, SQLITE_ERROR, "not available in this build");
#else
  static const int f = SQLITE_SCANSTAT_COMPLEX;
  sqlite3_stmt *pS = p->pStmt;
  int i = 0;
  i64 nTotal = 0;
  int nWidth = 0;
  sqlite3_str *pLine = sqlite3_str_new(p->db);
  sqlite3_str *pStats = sqlite3_str_new(p->db);
  qrfEqpReset(p);

  for(i=0; 1; i++){
    const char *z = 0;
    int n = 0;
    if( sqlite3_stmt_scanstatus_v2(pS,i,SQLITE_SCANSTAT_EXPLAIN,f,(void*)&z) ){
      break;
    }
    n = (int)strlen(z) + qrfStatsHeight(pS,i)*3;
    if( n>nWidth ) nWidth = n;
  }
  nWidth += 4;

  sqlite3_stmt_scanstatus_v2(pS,-1, SQLITE_SCANSTAT_NCYCLE, f, (void*)&nTotal);
  for(i=0; 1; i++){
    i64 nLoop = 0;
    i64 nRow = 0;
    i64 nCycle = 0;
    int iId = 0;
    int iPid = 0;
    const char *zo = 0;
    const char *zName = 0;
    double rEst = 0.0;

    if( sqlite3_stmt_scanstatus_v2(pS,i,SQLITE_SCANSTAT_EXPLAIN,f,(void*)&zo) ){
      break;
    }
    sqlite3_stmt_scanstatus_v2(pS,i, SQLITE_SCANSTAT_EST,f,(void*)&rEst);
    sqlite3_stmt_scanstatus_v2(pS,i, SQLITE_SCANSTAT_NLOOP,f,(void*)&nLoop);
    sqlite3_stmt_scanstatus_v2(pS,i, SQLITE_SCANSTAT_NVISIT,f,(void*)&nRow);
    sqlite3_stmt_scanstatus_v2(pS,i, SQLITE_SCANSTAT_NCYCLE,f,(void*)&nCycle);
    sqlite3_stmt_scanstatus_v2(pS,i, SQLITE_SCANSTAT_SELECTID,f,(void*)&iId);
    sqlite3_stmt_scanstatus_v2(pS,i, SQLITE_SCANSTAT_PARENTID,f,(void*)&iPid);
    sqlite3_stmt_scanstatus_v2(pS,i, SQLITE_SCANSTAT_NAME,f,(void*)&zName);

    if( nCycle>=0 || nLoop>=0 || nRow>=0 ){
      const char *zSp = "";
      double rpl;
      sqlite3_str_reset(pStats);
      if( nCycle>=0 && nTotal>0 ){
        sqlite3_str_appendf(pStats, "cycles=%lld [%d%%]",
            nCycle, ((nCycle*100)+nTotal/2) / nTotal
        );
        zSp = " ";
      }
      if( nLoop>=0 ){
        sqlite3_str_appendf(pStats, "%sloops=%lld", zSp, nLoop);
        zSp = " ";
      }
      if( nRow>=0 ){
        sqlite3_str_appendf(pStats, "%srows=%lld", zSp, nRow);
        zSp = " ";
      }

      if( p->spec.eStyle==QRF_STYLE_StatsEst ){
        rpl = (double)nRow / (double)nLoop;
        sqlite3_str_appendf(pStats, "%srpl=%.1f est=%.1f", zSp, rpl, rEst);
      }

      sqlite3_str_appendf(pLine,
          "% *s (%s)", -1*(nWidth-qrfStatsHeight(pS,i)*3), zo,
          sqlite3_str_value(pStats)
      );
      sqlite3_str_reset(pStats);
      qrfEqpAppend(p, iId, iPid, sqlite3_str_value(pLine));
      sqlite3_str_reset(pLine);
    }else{
      qrfEqpAppend(p, iId, iPid, zo);
    }
  }
  sqlite3_free(sqlite3_str_finish(pLine));
  sqlite3_free(sqlite3_str_finish(pStats));
#endif
}


/*
** Reset the prepared statement.
*/
static void qrfResetStmt(Qrf *p){
  int rc = sqlite3_reset(p->pStmt);
  if( rc!=SQLITE_OK && p->iErr==SQLITE_OK ){
    qrfError(p, rc, "%s", sqlite3_errmsg(p->db));
  }
}

/*
** If xWrite is defined, send all content of pOut to xWrite and
** reset pOut.
*/
static void qrfWrite(Qrf *p){
  int n;
  if( p->spec.xWrite && (n = sqlite3_str_length(p->pOut))>0 ){
    int rc = p->spec.xWrite(p->spec.pWriteArg,
                 sqlite3_str_value(p->pOut),
                 (sqlite3_int64)n);
    sqlite3_str_reset(p->pOut);
    if( rc ){
      qrfError(p, rc, "Failed to write %d bytes of output", n);
    }
  }
}

/* Lookup table to estimate the number of columns consumed by a Unicode
** character.
*/
static const struct {
  unsigned char w;    /* Width of the character in columns */
  int iFirst;         /* First character in a span having this width */
} aQrfUWidth[] = {
   /* {1, 0x00000}, */
  {0, 0x00300},  {1, 0x00370},  {0, 0x00483},  {1, 0x00487},  {0, 0x00488},
  {1, 0x0048a},  {0, 0x00591},  {1, 0x005be},  {0, 0x005bf},  {1, 0x005c0},
  {0, 0x005c1},  {1, 0x005c3},  {0, 0x005c4},  {1, 0x005c6},  {0, 0x005c7},
  {1, 0x005c8},  {0, 0x00600},  {1, 0x00604},  {0, 0x00610},  {1, 0x00616},
  {0, 0x0064b},  {1, 0x0065f},  {0, 0x00670},  {1, 0x00671},  {0, 0x006d6},
  {1, 0x006e5},  {0, 0x006e7},  {1, 0x006e9},  {0, 0x006ea},  {1, 0x006ee},
  {0, 0x0070f},  {1, 0x00710},  {0, 0x00711},  {1, 0x00712},  {0, 0x00730},
  {1, 0x0074b},  {0, 0x007a6},  {1, 0x007b1},  {0, 0x007eb},  {1, 0x007f4},
  {0, 0x00901},  {1, 0x00903},  {0, 0x0093c},  {1, 0x0093d},  {0, 0x00941},
  {1, 0x00949},  {0, 0x0094d},  {1, 0x0094e},  {0, 0x00951},  {1, 0x00955},
  {0, 0x00962},  {1, 0x00964},  {0, 0x00981},  {1, 0x00982},  {0, 0x009bc},
  {1, 0x009bd},  {0, 0x009c1},  {1, 0x009c5},  {0, 0x009cd},  {1, 0x009ce},
  {0, 0x009e2},  {1, 0x009e4},  {0, 0x00a01},  {1, 0x00a03},  {0, 0x00a3c},
  {1, 0x00a3d},  {0, 0x00a41},  {1, 0x00a43},  {0, 0x00a47},  {1, 0x00a49},
  {0, 0x00a4b},  {1, 0x00a4e},  {0, 0x00a70},  {1, 0x00a72},  {0, 0x00a81},
  {1, 0x00a83},  {0, 0x00abc},  {1, 0x00abd},  {0, 0x00ac1},  {1, 0x00ac6},
  {0, 0x00ac7},  {1, 0x00ac9},  {0, 0x00acd},  {1, 0x00ace},  {0, 0x00ae2},
  {1, 0x00ae4},  {0, 0x00b01},  {1, 0x00b02},  {0, 0x00b3c},  {1, 0x00b3d},
  {0, 0x00b3f},  {1, 0x00b40},  {0, 0x00b41},  {1, 0x00b44},  {0, 0x00b4d},
  {1, 0x00b4e},  {0, 0x00b56},  {1, 0x00b57},  {0, 0x00b82},  {1, 0x00b83},
  {0, 0x00bc0},  {1, 0x00bc1},  {0, 0x00bcd},  {1, 0x00bce},  {0, 0x00c3e},
  {1, 0x00c41},  {0, 0x00c46},  {1, 0x00c49},  {0, 0x00c4a},  {1, 0x00c4e},
  {0, 0x00c55},  {1, 0x00c57},  {0, 0x00cbc},  {1, 0x00cbd},  {0, 0x00cbf},
  {1, 0x00cc0},  {0, 0x00cc6},  {1, 0x00cc7},  {0, 0x00ccc},  {1, 0x00cce},
  {0, 0x00ce2},  {1, 0x00ce4},  {0, 0x00d41},  {1, 0x00d44},  {0, 0x00d4d},
  {1, 0x00d4e},  {0, 0x00dca},  {1, 0x00dcb},  {0, 0x00dd2},  {1, 0x00dd5},
  {0, 0x00dd6},  {1, 0x00dd7},  {0, 0x00e31},  {1, 0x00e32},  {0, 0x00e34},
  {1, 0x00e3b},  {0, 0x00e47},  {1, 0x00e4f},  {0, 0x00eb1},  {1, 0x00eb2},
  {0, 0x00eb4},  {1, 0x00eba},  {0, 0x00ebb},  {1, 0x00ebd},  {0, 0x00ec8},
  {1, 0x00ece},  {0, 0x00f18},  {1, 0x00f1a},  {0, 0x00f35},  {1, 0x00f36},
  {0, 0x00f37},  {1, 0x00f38},  {0, 0x00f39},  {1, 0x00f3a},  {0, 0x00f71},
  {1, 0x00f7f},  {0, 0x00f80},  {1, 0x00f85},  {0, 0x00f86},  {1, 0x00f88},
  {0, 0x00f90},  {1, 0x00f98},  {0, 0x00f99},  {1, 0x00fbd},  {0, 0x00fc6},
  {1, 0x00fc7},  {0, 0x0102d},  {1, 0x01031},  {0, 0x01032},  {1, 0x01033},
  {0, 0x01036},  {1, 0x01038},  {0, 0x01039},  {1, 0x0103a},  {0, 0x01058},
  {1, 0x0105a},  {2, 0x01100},  {0, 0x01160},  {1, 0x01200},  {0, 0x0135f},
  {1, 0x01360},  {0, 0x01712},  {1, 0x01715},  {0, 0x01732},  {1, 0x01735},
  {0, 0x01752},  {1, 0x01754},  {0, 0x01772},  {1, 0x01774},  {0, 0x017b4},
  {1, 0x017b6},  {0, 0x017b7},  {1, 0x017be},  {0, 0x017c6},  {1, 0x017c7},
  {0, 0x017c9},  {1, 0x017d4},  {0, 0x017dd},  {1, 0x017de},  {0, 0x0180b},
  {1, 0x0180e},  {0, 0x018a9},  {1, 0x018aa},  {0, 0x01920},  {1, 0x01923},
  {0, 0x01927},  {1, 0x01929},  {0, 0x01932},  {1, 0x01933},  {0, 0x01939},
  {1, 0x0193c},  {0, 0x01a17},  {1, 0x01a19},  {0, 0x01b00},  {1, 0x01b04},
  {0, 0x01b34},  {1, 0x01b35},  {0, 0x01b36},  {1, 0x01b3b},  {0, 0x01b3c},
  {1, 0x01b3d},  {0, 0x01b42},  {1, 0x01b43},  {0, 0x01b6b},  {1, 0x01b74},
  {0, 0x01dc0},  {1, 0x01dcb},  {0, 0x01dfe},  {1, 0x01e00},  {0, 0x0200b},
  {1, 0x02010},  {0, 0x0202a},  {1, 0x0202f},  {0, 0x02060},  {1, 0x02064},
  {0, 0x0206a},  {1, 0x02070},  {0, 0x020d0},  {1, 0x020f0},  {2, 0x02329},
  {1, 0x0232b},  {2, 0x02e80},  {0, 0x0302a},  {2, 0x03030},  {1, 0x0303f},
  {2, 0x03040},  {0, 0x03099},  {2, 0x0309b},  {1, 0x0a4d0},  {0, 0x0a806},
  {1, 0x0a807},  {0, 0x0a80b},  {1, 0x0a80c},  {0, 0x0a825},  {1, 0x0a827},
  {2, 0x0ac00},  {1, 0x0d7a4},  {2, 0x0f900},  {1, 0x0fb00},  {0, 0x0fb1e},
  {1, 0x0fb1f},  {0, 0x0fe00},  {2, 0x0fe10},  {1, 0x0fe1a},  {0, 0x0fe20},
  {1, 0x0fe24},  {2, 0x0fe30},  {1, 0x0fe70},  {0, 0x0feff},  {2, 0x0ff00},
  {1, 0x0ff61},  {2, 0x0ffe0},  {1, 0x0ffe7},  {0, 0x0fff9},  {1, 0x0fffc},
  {0, 0x10a01},  {1, 0x10a04},  {0, 0x10a05},  {1, 0x10a07},  {0, 0x10a0c},
  {1, 0x10a10},  {0, 0x10a38},  {1, 0x10a3b},  {0, 0x10a3f},  {1, 0x10a40},
  {0, 0x1d167},  {1, 0x1d16a},  {0, 0x1d173},  {1, 0x1d183},  {0, 0x1d185},
  {1, 0x1d18c},  {0, 0x1d1aa},  {1, 0x1d1ae},  {0, 0x1d242},  {1, 0x1d245},
  {2, 0x20000},  {1, 0x2fffe},  {2, 0x30000},  {1, 0x3fffe},  {0, 0xe0001},
  {1, 0xe0002},  {0, 0xe0020},  {1, 0xe0080},  {0, 0xe0100},  {1, 0xe01f0}
};

/*
** Return an estimate of the width, in columns, for the single Unicode
** character c.  For normal characters, the answer is always 1.  But the
** estimate might be 0 or 2 for zero-width and double-width characters.
**
** Different display devices display unicode using different widths.  So
** it is impossible to know that true display width with 100% accuracy.
** Inaccuracies in the width estimates might cause columns to be misaligned.
** Unfortunately, there is nothing we can do about that.
*/
int sqlite3_qrf_wcwidth(int c){
  int iFirst, iLast;

  /* Fast path for common characters */
  if( c<=0x300 ) return 1;

  /* The general case */
  iFirst = 0;
  iLast = sizeof(aQrfUWidth)/sizeof(aQrfUWidth[0]) - 1;
  while( iFirst<iLast-1 ){
    int iMid = (iFirst+iLast)/2;
    int cMid = aQrfUWidth[iMid].iFirst;
    if( cMid < c ){
      iFirst = iMid;
    }else if( cMid > c ){
      iLast = iMid - 1;
    }else{
      return aQrfUWidth[iMid].w;
    }
  }
  if( aQrfUWidth[iLast].iFirst > c ) return aQrfUWidth[iFirst].w;
  return aQrfUWidth[iLast].w;
}

/*
** Compute the value and length of a multi-byte UTF-8 character that
** begins at z[0]. Return the length.  Write the Unicode value into *pU.
**
** This routine only works for *multi-byte* UTF-8 characters.  It does
** not attempt to detect illegal characters.
*/
int sqlite3_qrf_decode_utf8(const unsigned char *z, int *pU){
  if( (z[0] & 0xe0)==0xc0 && (z[1] & 0xc0)==0x80 ){
    *pU = ((z[0] & 0x1f)<<6) | (z[1] & 0x3f);
    return 2;
  }
  if( (z[0] & 0xf0)==0xe0 && (z[1] & 0xc0)==0x80 && (z[2] & 0xc0)==0x80 ){
    *pU = ((z[0] & 0x0f)<<12) | ((z[1] & 0x3f)<<6) | (z[2] & 0x3f);
    return 3;
  }
  if( (z[0] & 0xf8)==0xf0 && (z[1] & 0xc0)==0x80 && (z[2] & 0xc0)==0x80
   && (z[3] & 0xc0)==0x80
  ){
    *pU = ((z[0] & 0x0f)<<18) | ((z[1] & 0x3f)<<12) | ((z[2] & 0x3f))<<6
                              | (z[3] & 0x3f);
    return 4;
  }
  *pU = 0;
  return 1;
}

/*
** Check to see if z[] is a valid VT100 escape.  If it is, then
** return the number of bytes in the escape sequence.  Return 0 if
** z[] is not a VT100 escape.
**
** This routine assumes that z[0] is \033 (ESC).
*/
static int qrfIsVt100(const unsigned char *z){
  int i;
  if( z[1]!='[' ) return 0;
  i = 2;
  while( z[i]>=0x30 && z[i]<=0x3f ){ i++; }
  while( z[i]>=0x20 && z[i]<=0x2f ){ i++; }
  if( z[i]<0x40 || z[i]>0x7e ) return 0;
  return i+1;
}

/*
** Return the length of a string in display characters.
** Multibyte UTF8 characters count as a single character
** for single-width characters, or as two characters for
** double-width characters.
*/
static int qrfDisplayLength(const char *zIn){
  const unsigned char *z = (const unsigned char*)zIn;
  int n = 0;
  while( *z ){
    if( z[0]<' ' ){
      int k;
      if( z[0]=='\033' && (k = qrfIsVt100(z))>0 ){
        z += k;
      }else{
        z++;
      }
    }else if( (0x80&z[0])==0 ){
      n++;
      z++;
    }else{
      int u = 0;
      int len = sqlite3_qrf_decode_utf8(z, &u);
      z += len;
      n += sqlite3_qrf_wcwidth(u);
    }
  }
  return n;
}

/*
** Return the display width of the longest line of text
** in the (possibly) multi-line input string zIn[0..nByte].
** zIn[] is not necessarily zero-terminated.  Take
** into account tab characters, zero- and double-width
** characters, CR and NL, and VT100 escape codes.
**
** Write the number of newlines into *pnNL.  So, *pnNL will
** return 0 if everything fits on one line, or positive it
** it will need to be split.
*/
static int qrfDisplayWidth(const char *zIn, sqlite3_int64 nByte, int *pnNL){
  const unsigned char *z = (const unsigned char*)zIn;
  const unsigned char *zEnd = &z[nByte];
  int mx = 0;
  int n = 0;
  int nNL = 0;
  while( z<zEnd ){
    if( z[0]<' ' ){
      int k;
      if( z[0]=='\033' && (k = qrfIsVt100(z))>0 ){
        z += k;
      }else{
        if( z[0]=='\t' ){
          n = (n+8)&~7;
        }else if( z[0]=='\n' || z[0]=='\r' ){
          nNL++;
          if( n>mx ) mx = n;
          n = 0;
        }
        z++;
      }
    }else if( (0x80&z[0])==0 ){
      n++;
      z++;
    }else{
      int u = 0;
      int len = sqlite3_qrf_decode_utf8(z, &u);
      z += len;
      n += sqlite3_qrf_wcwidth(u);
    }
  }
  if( mx>n ) n = mx;
  if( pnNL ) *pnNL = nNL;
  return n;
}

/*
** Escape the input string if it is needed and in accordance with
** eEsc, which is either QRF_ESC_Ascii or QRF_ESC_Symbol.
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
static void qrfEscape(
  int eEsc,            /* QRF_ESC_Ascii or QRF_ESC_Symbol */
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
  if( eEsc==QRF_ESC_Symbol ) nCtrl *= 2;
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
    if( eEsc==QRF_ESC_Symbol ){
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
static const char qrfCsvQuote[] = {
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
** Encode text appropriately and append it to pOut.
*/
static void qrfEncodeText(Qrf *p, sqlite3_str *pOut, const char *zTxt){
  int iStart = sqlite3_str_length(pOut);
  switch( p->spec.eText ){
    case QRF_TEXT_Sql: {
      if( p->spec.eEsc==QRF_ESC_Off ){
        sqlite3_str_appendf(pOut, "%Q", zTxt);
      }else{
        sqlite3_str_appendf(pOut, "%#Q", zTxt);
      }
      break;
    }
    case QRF_TEXT_Csv: {
      unsigned int i;
      for(i=0; zTxt[i]; i++){
        if( qrfCsvQuote[((const unsigned char*)zTxt)[i]] ){
          i = 0;
          break;
        }
      }
      if( i==0 || strstr(zTxt, p->spec.zColumnSep)!=0 ){
        sqlite3_str_appendf(pOut, "\"%w\"", zTxt);
      }else{
        sqlite3_str_appendall(pOut, zTxt);
      }
      break;
    }
    case QRF_TEXT_Html: {
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
          sqlite3_str_append(pOut, (const char*)z, i);
        }
        switch( z[i] ){
          case '>':   sqlite3_str_append(pOut, "&lt;", 4);   break;
          case '&':   sqlite3_str_append(pOut, "&amp;", 5);  break;
          case '<':   sqlite3_str_append(pOut, "&lt;", 4);   break;
          case '"':   sqlite3_str_append(pOut, "&quot;", 6); break;
          case '\'':  sqlite3_str_append(pOut, "&#39;", 5);  break;
          default:    i--;
        }
        z += i + 1;
      }
      break;
    }
    case QRF_TEXT_Tcl:
    case QRF_TEXT_Json: {
      const unsigned char *z = (const unsigned char*)zTxt;
      sqlite3_str_append(pOut, "\"", 1);
      while( *z ){
        unsigned int i;
        for(i=0; z[i]>=0x20 && z[i]!='\\' && z[i]!='"'; i++){}
        if( i>0 ){
          sqlite3_str_append(pOut, (const char*)z, i);
        }
        if( z[i]==0 ) break;
        switch( z[i] ){
          case '"':   sqlite3_str_append(pOut, "\\\"", 2);  break;
          case '\\':  sqlite3_str_append(pOut, "\\\\", 2);  break;
          case '\b':  sqlite3_str_append(pOut, "\\b", 2);   break;
          case '\f':  sqlite3_str_append(pOut, "\\f", 2);   break;
          case '\n':  sqlite3_str_append(pOut, "\\n", 2);   break;
          case '\r':  sqlite3_str_append(pOut, "\\r", 2);   break;
          case '\t':  sqlite3_str_append(pOut, "\\t", 2);   break;
          default: {
            if( p->spec.eText==QRF_TEXT_Json ){
              sqlite3_str_appendf(pOut, "\\u%04x", z[i]);
            }else{
              sqlite3_str_appendf(pOut, "\\%03o", z[i]);
            }
            break;
          }
        }
        z += i + 1;
      }
      sqlite3_str_append(pOut, "\"", 1);
      break;
    }
    default: {
      sqlite3_str_appendall(pOut, zTxt);
      break;
    }
  }
  if( p->spec.eEsc!=QRF_ESC_Off ){
    qrfEscape(p->spec.eEsc, pOut, iStart);
  }
}

/*
** The current iCol-th column of p->pStmt is known to be a BLOB.  Check
** to see if that BLOB is really a JSONB blob.  If it is, then translate
** it into a text JSON representation and return a pointer to that text JSON.
**
** The memory used to hold the JSON text is managed internally by the
** "p" object and is overwritten and/or deallocated upon the next call
** to this routine (with the same p argument) or when the p object is
** finailized.
*/
static const char *qrfJsonbToJson(Qrf *p, int iCol){
  int nByte;
  const void *pBlob;
  int rc;
  if( p->pJTrans==0 ){
    sqlite3 *db;
    rc = sqlite3_open(":memory:",&db);
    if( rc ){
      sqlite3_close(db);
      return 0;
    }
    rc = sqlite3_prepare_v2(db, "SELECT json(?1)", -1, &p->pJTrans, 0);
    if( rc ){
      sqlite3_finalize(p->pJTrans);
      p->pJTrans = 0;
      sqlite3_close(db);
      return 0;
    }
  }else{
    sqlite3_reset(p->pJTrans);
  }
  nByte = sqlite3_column_bytes(p->pStmt, iCol);
  pBlob = sqlite3_column_blob(p->pStmt, iCol);
  sqlite3_bind_blob(p->pJTrans, 1, (void*)pBlob, nByte, SQLITE_STATIC);
  rc = sqlite3_step(p->pJTrans);
  if( rc==SQLITE_ROW ){
    return (const char*)sqlite3_column_text(p->pJTrans, 0);
  }else{
    return 0;
  }
}

/*
** Render value pVal into pOut
*/
static void qrfRenderValue(Qrf *p, sqlite3_str *pOut, int iCol){
  if( p->spec.xRender ){
    sqlite3_value *pVal;
    char *z;
    pVal = sqlite3_value_dup(sqlite3_column_value(p->pStmt,iCol));
    z = p->spec.xRender(p->spec.pRenderArg, pVal);
    sqlite3_value_free(pVal);
    if( z ){
      sqlite3_str_appendall(pOut, z);
      sqlite3_free(z);
      return;
    }
  }
  switch( sqlite3_column_type(p->pStmt,iCol) ){
    case SQLITE_INTEGER: {
      sqlite3_str_appendf(pOut, "%lld", sqlite3_column_int64(p->pStmt,iCol));
      break;
    }
    case SQLITE_FLOAT: {
      const char *zTxt = (const char*)sqlite3_column_text(p->pStmt,iCol);
      sqlite3_str_appendall(pOut, zTxt);
      break;
    }
    case SQLITE_BLOB: {
      if( p->spec.bTextJsonb==QRF_Yes ){
        const char *zJson = qrfJsonbToJson(p, iCol);
        if( zJson ){
          qrfEncodeText(p, pOut, zJson);
          break;
        }
      }
      switch( p->spec.eBlob ){
        case QRF_BLOB_Hex:
        case QRF_BLOB_Sql: {
          int iStart;
          int nBlob = sqlite3_column_bytes(p->pStmt,iCol);
          int i, j;
          char *zVal;
          const unsigned char *a = sqlite3_column_blob(p->pStmt,iCol);
          if( p->spec.eBlob==QRF_BLOB_Sql ){
            sqlite3_str_append(pOut, "x'", 2);
          }
          iStart = sqlite3_str_length(pOut);
          sqlite3_str_appendchar(pOut, nBlob, ' ');
          sqlite3_str_appendchar(pOut, nBlob, ' ');
          if( p->spec.eBlob==QRF_BLOB_Sql ){
            sqlite3_str_appendchar(pOut, 1, '\'');
          }
          if( sqlite3_str_errcode(pOut) ) return;
          zVal = sqlite3_str_value(pOut);
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
          sqlite3_str_append(pOut, "\"", 1);
          iStart = sqlite3_str_length(pOut);
          for(i=szC; i>0; i--){
            sqlite3_str_appendchar(pOut, nBlob, ' ');
          }
          sqlite3_str_appendchar(pOut, 1, '"');
          if( sqlite3_str_errcode(pOut) ) return;
          zVal = sqlite3_str_value(pOut);
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
          qrfEncodeText(p, pOut, zTxt);
        }
      }
      break;
    }
    case SQLITE_NULL: {
      sqlite3_str_appendall(pOut, p->spec.zNull);
      break;
    }
    case SQLITE_TEXT: {
      const char *zTxt = (const char*)sqlite3_column_text(p->pStmt,iCol);
      qrfEncodeText(p, pOut, zTxt);
      break;
    }
  }
}

/*
** Store string zUtf to pOut as w characters.  If w is negative,
** then right-justify the text.  W is the width in display characters, not
** in bytes.  Double-width unicode characters count as two characters.
** VT100 escape sequences count as zero.  And so forth.
*/
static void qrfWidthPrint(Qrf *p, sqlite3_str *pOut, int w, const char *zUtf){
  const unsigned char *a = (const unsigned char*)zUtf;
  static const int mxW = 10000000;
  unsigned char c;
  int i = 0;
  int n = 0;
  int k;
  int aw;
  if( w<-mxW ){
    w = -mxW;
  }else if( w>mxW ){
    w= mxW;
  }
  aw = w<0 ? -w : w;
  if( zUtf==0 ) zUtf = "";
  while( (c = a[i])!=0 ){
    if( (c&0xc0)==0xc0 ){
      int u;
      int len = sqlite3_qrf_decode_utf8(a+i, &u);
      int x = sqlite3_qrf_wcwidth(u);
      if( x+n>aw ){
        break;
      }
      i += len;
      n += x;
    }else if( c==0x1b && (k = qrfIsVt100(&a[i]))>0 ){
      i += k;       
    }else if( n>=aw ){
      break;
    }else{
      n++;
      i++;
    }
  }
  if( n>=aw ){
    sqlite3_str_append(pOut, zUtf, i);
  }else if( w<0 ){
    if( aw>n ) sqlite3_str_appendchar(pOut, aw-n, ' ');
    sqlite3_str_append(pOut, zUtf, i);
  }else{
    sqlite3_str_append(pOut, zUtf, i);
    if( aw>n ) sqlite3_str_appendchar(pOut, aw-n, ' ');
  }
}

/*
** Data for columnar layout, collected into a single object so
** that it can be more easily passed into subroutines.
*/
typedef struct qrfColData qrfColData;
struct qrfColData {
  Qrf *p;                  /* The QRF instance */
  int nCol;                /* Number of columns in the table */
  unsigned char bMultiRow; /* One or more cells will span multiple lines */
  sqlite3_int64 nRow;      /* Number of rows */
  sqlite3_int64 nAlloc;    /* Number of cells allocated */
  sqlite3_int64 n;         /* Number of cells.  nCol*nRow */
  char **azThis;           /* Cache of pointers to current row */
  char **az;               /* Content of all cells */
  int *aiWth;              /* Width of each cell */
  int *aiCol;              /* Width of each column */
  unsigned char *aAlign;   /* Alignment for each column */
};

/*
** Free all the memory allocates in the qrfColData object
*/
static void qrfColDataFree(qrfColData *p){
  sqlite3_int64 i;
  for(i=0; i<p->n; i++) sqlite3_free(p->az[i]);
  sqlite3_free(p->az);
  sqlite3_free(p->aiWth);
  sqlite3_free(p->azThis);
  memset(p, 0, sizeof(*p));
}

/*
** Allocate space for more cells in the qrfColData object.
** Return non-zero if a memory allocation fails.
*/
static int qrfColDataEnlarge(qrfColData *p){
  char **azData;
  int *aiWth;
  p->nAlloc = 2*p->nAlloc + 10*p->nCol;
  azData = sqlite3_realloc64(p->az, p->nAlloc*sizeof(char*));
  if( azData==0 ){
    qrfOom(p->p);
    qrfColDataFree(p);
    return 1;
  }
  p->az = azData;
  aiWth = sqlite3_realloc64(p->aiWth, p->nAlloc*sizeof(int));
  if( aiWth==0 ){
    qrfOom(p->p);
    qrfColDataFree(p);
    return 1;
  }
  p->aiWth = aiWth;
  return 0;
}

/*
** (*pz)[] is a line of text that is to be displayed the box or table or
** similar tabular formats.  z[] contain newlines or might be too wide
** to fit in the columns so will need to be split into multiple line.
**
** This routine determines:
**
**    *  How many bytes of z[] should be shown on the current line.
**    *  How many character positions those bytes will cover.
**    *  The byte offset to the start of the next line.
*/
static void qrfWrapLine(
  const char *zIn,   /* Input text to be displayed */
  int w,             /* Column width in characters (not bytes) */
  int bWrap,         /* True if we should do word-wrapping */
  int *pnThis,       /* OUT: How many bytes of z[] for the current line */
  int *pnWide,       /* OUT: How wide is the text of this line */
  int *piNext        /* OUT: Offset into z[] to start of the next line */
){
  int i;                 /* Input bytes consumed */
  int k;                 /* Bytes in a VT100 code */
  int n;                 /* Output column number */
  const unsigned char *z = (const unsigned char*)zIn;
  unsigned char c;

  if( zIn[0]==0 ){
    *pnThis = 0;
    *pnWide = 0;
    *piNext = 0;
    return;
  }
  n = 0;
  for(i=0; n<w; i++){
    c = zIn[i];
    if( c>=0xc0 ){
      int u;
      int len = sqlite3_qrf_decode_utf8(&z[i], &u);
      int wcw = sqlite3_qrf_wcwidth(u);
      if( wcw+n>w ) break;
      i += len-1;
      n += wcw;
      continue;
    }
    if( c>=' ' ){
      n++;
      continue;
    }
    if( c==0 || c=='\n' ) break;
    if( c=='\r' && zIn[i+1]=='\n' ){ c = zIn[++i]; break; }
    if( c=='\t' ){
      int wcw = 8 - (n&7);
      if( n+wcw>w ) break;
      n += wcw;
      continue;
    }
    if( c==0x1b && (k = qrfIsVt100(&z[i]))>0 ){
      i += k-1;
    }
  }
  if( c==0 ){
    *pnThis = i;
    *pnWide = n;
    *piNext = i;
    return;
  }
  if( c=='\n' ){
    *pnThis = i;
    *pnWide = n;
    *piNext = i+1;
    return;
  }

  /* If we get this far, that means the current line will end at some
  ** point that is neither a "\n" or a 0x00.  Figure out where that
  ** split should occur
  */
  if( bWrap && z[i]!=0 && !isspace(z[i]) && isalnum(c)==isalnum(z[i]) ){
    /* Perhaps try to back up to a better place to break the line */
    for(k=i-1; k>=i/2; k--){
      if( isspace(z[k]) ) break;
    }
    if( k<i/2 ){
      for(k=i; k>=i/2; k--){
        if( isalnum(z[k-1])!=isalnum(z[k]) && (z[k]&0xc0)!=0x80 ) break;
      }
    }
    if( k>=i/2 ){
      i = k;
      n = qrfDisplayWidth((const char*)z, k, 0);
    }
  }
  *pnThis = i;
  *pnWide = n;
  while( zIn[i]==' ' || zIn[i]=='\t' || zIn[i]=='\r' ){ i++; }
  *piNext = i;
}

/*
** Print a markdown or table-style row separator using ascii-art
*/
static void qrfRowSeparator(sqlite3_str *pOut, qrfColData *p, char cSep){
  int i;
  if( p->nCol>0 ){
    sqlite3_str_append(pOut, &cSep, 1);
    sqlite3_str_appendchar(pOut, p->aiCol[0]+2, '-');
    for(i=1; i<p->nCol; i++){
      sqlite3_str_append(pOut, &cSep, 1);
      sqlite3_str_appendchar(pOut, p->aiCol[i]+2, '-');
    }
    sqlite3_str_append(pOut, &cSep, 1);
  }
  sqlite3_str_append(pOut, "\n", 1);
}

/*
** UTF8 box-drawing characters.  Imagine box lines like this:
**
**           1
**           |
**       4 --+-- 2
**           |
**           3
**
** Each box characters has between 2 and 4 of the lines leading from
** the center.  The characters are here identified by the numbers of
** their corresponding lines.
*/
#define BOX_24   "\342\224\200"  /* U+2500 --- */
#define BOX_13   "\342\224\202"  /* U+2502  |  */
#define BOX_23   "\342\224\214"  /* U+250c  ,- */
#define BOX_34   "\342\224\220"  /* U+2510 -,  */
#define BOX_12   "\342\224\224"  /* U+2514  '- */
#define BOX_14   "\342\224\230"  /* U+2518 -'  */
#define BOX_123  "\342\224\234"  /* U+251c  |- */
#define BOX_134  "\342\224\244"  /* U+2524 -|  */
#define BOX_234  "\342\224\254"  /* U+252c -,- */
#define BOX_124  "\342\224\264"  /* U+2534 -'- */
#define BOX_1234 "\342\224\274"  /* U+253c -|- */

/* Draw horizontal line N characters long using unicode box
** characters
*/
static void qrfBoxLine(sqlite3_str *pOut, int N){
  const char zDash[] =
      BOX_24 BOX_24 BOX_24 BOX_24 BOX_24 BOX_24 BOX_24 BOX_24 BOX_24 BOX_24
      BOX_24 BOX_24 BOX_24 BOX_24 BOX_24 BOX_24 BOX_24 BOX_24 BOX_24 BOX_24;
  const int nDash = sizeof(zDash) - 1;
  N *= 3;
  while( N>nDash ){
    sqlite3_str_append(pOut, zDash, nDash);
    N -= nDash;
  }
  sqlite3_str_append(pOut, zDash, N);
}

/*
** Draw a horizontal separator for a QRF_STYLE_Box table.
*/
static void qrfBoxSeparator(
  sqlite3_str *pOut,
  qrfColData *p,
  const char *zSep1,
  const char *zSep2,
  const char *zSep3
){
  int i;
  if( p->nCol>0 ){
    sqlite3_str_appendall(pOut, zSep1);
    qrfBoxLine(pOut, p->aiCol[0]+2);
    for(i=1; i<p->nCol; i++){
      sqlite3_str_appendall(pOut, zSep2);
      qrfBoxLine(pOut, p->aiCol[i]+2);
    }
    sqlite3_str_appendall(pOut, zSep3);
  }
  sqlite3_str_append(pOut, "\n", 1);
}

/*
** Load into pData the default alignment for the body of a table.
*/
static void qrfLoadAlignment(qrfColData *pData, Qrf *p){
  sqlite3_int64 i;
  memset(pData->aAlign, p->spec.eDfltAlign, pData->nCol);
  for(i=0; i<pData->nCol; i++){
    if( i<p->spec.nAlign ){
      unsigned char ax = p->spec.aAlign[i];
      if( (ax & QRF_ALIGN_HMASK)!=0 ){
        pData->aAlign[i] = (ax & QRF_ALIGN_HMASK) |
                            (pData->aAlign[i] & QRF_ALIGN_VMASK);
      }
    }else if( i<p->spec.nWidth ){
      if( p->spec.aWidth[i]<0 ){
         pData->aAlign[i] = QRF_ALIGN_Right |
                               (pData->aAlign[i] & QRF_ALIGN_VMASK);
      }
    }else{
      break;
    }
  }
}

/*
** Columnar modes require that the entire query be evaluated first, with
** results written into memory, so that we can compute appropriate column
** widths.
*/
static void qrfColumnar(Qrf *p){
  sqlite3_int64 i, j;                     /* Loop counters */
  const char *colSep = 0;                 /* Column separator text */
  const char *rowSep = 0;                 /* Row terminator text */
  const char *rowStart = 0;               /* Row start text */
  int szColSep, szRowSep, szRowStart;     /* Size in bytes of previous 3 */
  int rc;                                 /* Result code */
  int nColumn = p->nCol;                  /* Number of columns */
  int bWW;                                /* True to do word-wrap */
  sqlite3_str *pStr;                      /* Temporary rendering */
  qrfColData data;                        /* Columnar layout data */

  rc = sqlite3_step(p->pStmt);
  if( rc!=SQLITE_ROW || nColumn==0 ){
    return;   /* No output */
  }

  /* Initialize the data container */
  memset(&data, 0, sizeof(data));
  data.nCol = p->nCol;
  data.azThis = sqlite3_malloc64( nColumn*(sizeof(char*) + sizeof(int) + 1) );
  if( data.azThis==0 ){
    qrfOom(p);
    return;
  }
  data.aiCol = (int*)&data.azThis[nColumn];
  data.aAlign = (unsigned char*)&data.aiCol[nColumn];
  qrfColDataEnlarge(&data);
  if( p->iErr ){
    qrfColDataFree(&data);
    return;
  }

  /* Load the column header names and all cell content into data */
  if( p->spec.bColumnNames==QRF_Yes ){
    unsigned char saved_eText = p->spec.eText;
    p->spec.eText = p->spec.eTitle;
    for(i=0; i<nColumn; i++){
      const char *z = (const char*)sqlite3_column_name(p->pStmt,i);
      int nNL = 0;
      int n;
      pStr = sqlite3_str_new(p->db);
      qrfEncodeText(p, pStr, z ? z : "");
      n = sqlite3_str_length(pStr);
      z = data.az[data.n] = sqlite3_str_finish(pStr);
      data.aiWth[data.n] = qrfDisplayWidth(z, n, &nNL);
      data.n++;
      if( nNL ) data.bMultiRow = 1;
    }
    p->spec.eText = saved_eText;
    p->nRow++;
  }
  do{
    if( data.n+nColumn > data.nAlloc ){
      qrfColDataEnlarge(&data);
      if( p->iErr ) return;
    }
    for(i=0; i<nColumn; i++){
      char *z;
      int nNL = 0;
      int n;
      pStr = sqlite3_str_new(p->db);
      qrfRenderValue(p, pStr, i);
      n = sqlite3_str_length(pStr);
      z = data.az[data.n] = sqlite3_str_finish(pStr);
      data.aiWth[data.n] = qrfDisplayWidth(z, n, &nNL);
      data.n++;
      if( nNL ) data.bMultiRow = 1;
    }
    p->nRow++;
  }while( sqlite3_step(p->pStmt)==SQLITE_ROW && p->iErr==SQLITE_OK );
  if( p->iErr ){
    qrfColDataFree(&data);
    return;
  }

  /* Compute the width and alignment of every column */
  if( p->spec.bColumnNames==QRF_No ){
    qrfLoadAlignment(&data, p);
  }else if( p->spec.eTitleAlign==QRF_Auto ){
    memset(data.aAlign, QRF_ALIGN_Center, nColumn);
  }else{
    memset(data.aAlign, p->spec.eTitleAlign, nColumn);
  }

  for(i=0; i<nColumn; i++){
    int w = 0;
    if( i<p->spec.nWidth ){
      w = p->spec.aWidth[i];
      if( w==(-32768) ){
        w = 0;
        if( p->spec.nAlign>i && (p->spec.aAlign[i] & QRF_ALIGN_HMASK)==0 ){
          data.aAlign[i] |= QRF_ALIGN_Right;
        }
      }else if( w<0 ){
        w = -w;
        if( p->spec.nAlign>i && (p->spec.aAlign[i] & QRF_ALIGN_HMASK)==0 ){
          data.aAlign[i] |= QRF_ALIGN_Right;
        }
      }
    }
    if( w==0 ){
      for(j=i; j<data.n; j+=nColumn){
        if( data.aiWth[j] > w ){
          w = data.aiWth[j];
          if( p->spec.mxColWidth>0 && w>p->spec.mxColWidth ){
            w = p->spec.mxColWidth;
            data.bMultiRow = 1;
            break;
          }
        }
      }
    }else if( data.bMultiRow==0 ){
      for(j=i; j<data.n; j+=nColumn){
        if( data.aiWth[j] > w ){
          data.bMultiRow = 1;
          break;
        }
      }
    }
    data.aiCol[i] = w;
  }

  /* TBD: Narrow columns so that the total is less than p->spec.mxTotalWidth */

  /* Draw the line across the top of the table.  Also initialize
  ** the row boundary and column separator texts. */
  switch( p->spec.eStyle ){
    case QRF_STYLE_Box:
      rowStart = BOX_13 " ";
      colSep = " " BOX_13 " ";
      rowSep = " " BOX_13 "\n";
      qrfBoxSeparator(p->pOut, &data, BOX_23, BOX_234, BOX_34);
      break;
    case QRF_STYLE_Table:
      rowStart = "| ";
      colSep = " | ";
      rowSep = " |\n";
      qrfRowSeparator(p->pOut, &data, '+');
      break;
    case QRF_STYLE_Column:
      rowStart = "";
      colSep = "  ";
      rowSep = "\n";
      break;
    case QRF_STYLE_Markdown:
      rowStart = "| ";
      colSep = " | ";
      rowSep = " |\n";
      break;
  }
  szRowStart = (int)strlen(rowStart);
  szRowSep = (int)strlen(rowSep);
  szColSep = (int)strlen(colSep);

  bWW = (p->spec.bWordWrap==QRF_Yes && data.bMultiRow);
  for(i=0; i<data.n; i+=nColumn){
    int bMore;

    /* Draw a single row of the table.  This might be the title line
    ** (if there is a title line) or a row in the body of the table.
    ** The column number will be j.  The row number is i/nColumn.
    */
    for(j=0; j<nColumn; j++){ data.azThis[j] = data.az[i+j]; }
    do{
      sqlite3_str_append(p->pOut, rowStart, szRowStart);
      bMore = 0;
      for(j=0; j<nColumn; j++){
        int nThis = 0;
        int nWide = 0;
        int iNext = 0;
        int nWS;
        qrfWrapLine(data.azThis[j], data.aiCol[j], bWW, &nThis, &nWide, &iNext);
        nWS = data.aiCol[j] - nWide;
        if( (data.aAlign[j] & QRF_ALIGN_HMASK)==QRF_ALIGN_Center ){
          /* Center the text */
          sqlite3_str_appendchar(p->pOut, nWS/2, ' ');
          sqlite3_str_append(p->pOut, data.azThis[j], nThis);
          sqlite3_str_appendchar(p->pOut, nWS - nWS/2, ' ');
        }else if( (data.aAlign[j] & QRF_ALIGN_HMASK)==QRF_ALIGN_Right){
          /* Right justify the text */
          sqlite3_str_appendchar(p->pOut, nWS, ' ');
          sqlite3_str_append(p->pOut, data.azThis[j], nThis);
        }else{
          /* Left justify the next */
          sqlite3_str_append(p->pOut, data.azThis[j], nThis);
          sqlite3_str_appendchar(p->pOut, nWS, ' ');
        }
        data.azThis[j] += iNext;
        if( data.azThis[j][0]!=0 ) bMore = 1;
        if( j<nColumn-1 ){
          sqlite3_str_append(p->pOut, colSep, szColSep);
        }else{
          sqlite3_str_append(p->pOut, rowSep, szRowSep);
        }
      }
    }while( bMore );

    /* Draw either (1) the separator between the title line and the body
    ** of the table, or (2) separators between individual rows of the table
    ** body.  isTitleDataSeparator will be true if we are doing (1).
    */
    if( (i==0 || data.bMultiRow) && i+nColumn<data.n ){
      int isTitleDataSeparator = (i==0 && p->spec.bColumnNames==QRF_Yes);
      if( isTitleDataSeparator ){
        qrfLoadAlignment(&data, p);
      }
      switch( p->spec.eStyle ){
        case QRF_STYLE_Table: {
          if( isTitleDataSeparator || data.bMultiRow ){
            qrfRowSeparator(p->pOut, &data, '+');
          }
          break;
        }
        case QRF_STYLE_Box: {
          if( isTitleDataSeparator || data.bMultiRow ){
            qrfBoxSeparator(p->pOut, &data, BOX_123, BOX_1234, BOX_134);
          }
          break;
        }
        case QRF_STYLE_Markdown: {
          if( isTitleDataSeparator ){
            qrfRowSeparator(p->pOut, &data, '|');
          }
          break;
        }
        case QRF_STYLE_Column: {
          if( isTitleDataSeparator ){
            for(j=0; j<nColumn; j++){
              sqlite3_str_appendchar(p->pOut, data.aiCol[j], '-');
              if( j<nColumn-1 ){
                sqlite3_str_append(p->pOut, colSep, szColSep);
              }else{
                sqlite3_str_append(p->pOut, rowSep, szRowSep);
              }
            }
          }else if( data.bMultiRow ){
            sqlite3_str_append(p->pOut, "\n", 1);
          }
          break;
        }
      }
    }
  }

  /* Draw the line across the bottom of the table */
  switch( p->spec.eStyle ){
    case QRF_STYLE_Box:
      qrfBoxSeparator(p->pOut, &data, BOX_12, BOX_124, BOX_14);
      break;
    case QRF_STYLE_Table:
      qrfRowSeparator(p->pOut, &data, '+');
      break;
  }
  qrfWrite(p);

  qrfColDataFree(&data);
  return;
}

/*
** Parameter azArray points to a zero-terminated array of strings. zStr
** points to a single nul-terminated string. Return non-zero if zStr
** is equal, according to strcmp(), to any of the strings in the array.
** Otherwise, return zero.
*/
static int qrfStringInArray(const char *zStr, const char **azArray){
  int i;
  if( zStr==0 ) return 0;
  for(i=0; azArray[i]; i++){
    if( 0==strcmp(zStr, azArray[i]) ) return 1;
  }
  return 0;
}

/*
** Print out an EXPLAIN with indentation.  This is a two-pass algorithm.
**
** On the first pass, we compute aiIndent[iOp] which is the amount of
** indentation to apply to the iOp-th opcode.  The output actually occurs
** on the second pass.
**
** The indenting rules are:
**
**     * For each "Next", "Prev", "VNext" or "VPrev" instruction, indent
**       all opcodes that occur between the p2 jump destination and the opcode
**       itself by 2 spaces.
**
**     * Do the previous for "Return" instructions for when P2 is positive.
**       See tag-20220407a in wherecode.c and vdbe.c.
**
**     * For each "Goto", if the jump destination is earlier in the program
**       and ends on one of:
**          Yield  SeekGt  SeekLt  RowSetRead  Rewind
**       or if the P1 parameter is one instead of zero,
**       then indent all opcodes between the earlier instruction
**       and "Goto" by 2 spaces.
*/
static void qrfExplain(Qrf *p){
  int *abYield = 0;     /* abYield[iOp] is rue if opcode iOp is an OP_Yield */
  int *aiIndent = 0;    /* Indent the iOp-th opcode by aiIndent[iOp] */
  i64 nAlloc = 0;       /* Allocated size of aiIndent[], abYield */
  int nIndent = 0;      /* Number of entries in aiIndent[] */
  int iOp;              /* Opcode number */
  int i;                /* Column loop counter */

  const char *azNext[] = { "Next", "Prev", "VPrev", "VNext", "SorterNext",
                           "Return", 0 };
  const char *azYield[] = { "Yield", "SeekLT", "SeekGT", "RowSetRead",
                            "Rewind", 0 };
  const char *azGoto[] = { "Goto", 0 };

  /* The caller guarantees that the leftmost 4 columns of the statement
  ** passed to this function are equivalent to the leftmost 4 columns
  ** of EXPLAIN statement output. In practice the statement may be
  ** an EXPLAIN, or it may be a query on the bytecode() virtual table.  */
  assert( sqlite3_column_count(p->pStmt)>=4 );
  assert( 0==sqlite3_stricmp( sqlite3_column_name(p->pStmt, 0), "addr" ) );
  assert( 0==sqlite3_stricmp( sqlite3_column_name(p->pStmt, 1), "opcode" ) );
  assert( 0==sqlite3_stricmp( sqlite3_column_name(p->pStmt, 2), "p1" ) );
  assert( 0==sqlite3_stricmp( sqlite3_column_name(p->pStmt, 3), "p2" ) );

  for(iOp=0; SQLITE_ROW==sqlite3_step(p->pStmt); iOp++){
    int iAddr = sqlite3_column_int(p->pStmt, 0);
    const char *zOp = (const char*)sqlite3_column_text(p->pStmt, 1);
    int p1 = sqlite3_column_int(p->pStmt, 2);
    int p2 = sqlite3_column_int(p->pStmt, 3);

    /* Assuming that p2 is an instruction address, set variable p2op to the
    ** index of that instruction in the aiIndent[] array. p2 and p2op may be
    ** different if the current instruction is part of a sub-program generated
    ** by an SQL trigger or foreign key.  */
    int p2op = (p2 + (iOp-iAddr));

    /* Grow the aiIndent array as required */
    if( iOp>=nAlloc ){
      nAlloc += 100;
      aiIndent = (int*)sqlite3_realloc64(aiIndent, nAlloc*sizeof(int));
      abYield = (int*)sqlite3_realloc64(abYield, nAlloc*sizeof(int));
      if( aiIndent==0 || abYield==0 ){
        qrfOom(p);
        sqlite3_free(aiIndent);
        sqlite3_free(abYield);
        return;
      }
    }

    abYield[iOp] = qrfStringInArray(zOp, azYield);
    aiIndent[iOp] = 0;
    nIndent = iOp+1;
    if( qrfStringInArray(zOp, azNext) && p2op>0 ){
      for(i=p2op; i<iOp; i++) aiIndent[i] += 2;
    }
    if( qrfStringInArray(zOp, azGoto) && p2op<iOp && (abYield[p2op] || p1) ){
      for(i=p2op; i<iOp; i++) aiIndent[i] += 2;
    }
  }
  sqlite3_free(abYield);

  /* Second pass.  Actually generate output */
  sqlite3_reset(p->pStmt);
  if( p->iErr==SQLITE_OK ){
    static const int aExplainWidth[] = {4,       13, 4, 4, 4, 13, 2, 13};
    static const int aExplainMap[] =   {0,       1,  2, 3, 4, 5,  6, 7 };
    static const int aScanExpWidth[] = {4,15, 6, 13, 4, 4, 4, 13, 2, 13};
    static const int aScanExpMap[] =   {0, 9, 8, 1,  2, 3, 4, 5,  6, 7 };
    const int *aWidth = aExplainWidth;
    const int *aMap = aExplainMap;
    int nWidth = sizeof(aExplainWidth)/sizeof(int);
    int iIndent = 1;
    int nArg = p->nCol;
    if( p->spec.eStyle==QRF_STYLE_StatsVm ){
      aWidth = aScanExpWidth;
      aMap = aScanExpMap;
      nWidth = sizeof(aScanExpWidth)/sizeof(int);
      iIndent = 3;
    }
    if( nArg>nWidth ) nArg = nWidth;

    for(iOp=0; sqlite3_step(p->pStmt)==SQLITE_ROW; iOp++){
      /* If this is the first row seen, print out the headers */
      if( iOp==0 ){
        for(i=0; i<nArg; i++){
          const char *zCol = sqlite3_column_name(p->pStmt, aMap[i]);
          qrfWidthPrint(p,p->pOut, aWidth[i], zCol);
          if( i==nArg-1 ){
            sqlite3_str_append(p->pOut, "\n", 1);
          }else{
            sqlite3_str_append(p->pOut, "  ", 2);
          }
        }
        for(i=0; i<nArg; i++){
          sqlite3_str_appendf(p->pOut, "%.*c", aWidth[i], '-');
          if( i==nArg-1 ){
            sqlite3_str_append(p->pOut, "\n", 1);
          }else{
            sqlite3_str_append(p->pOut, "  ", 2);
          }
        }
      }
  
      for(i=0; i<nArg; i++){
        const char *zSep = "  ";
        int w = aWidth[i];
        const char *zVal = (const char*)sqlite3_column_text(p->pStmt, aMap[i]);
        int len;
        if( i==nArg-1 ) w = 0;
        if( zVal==0 ) zVal = "";
        len = qrfDisplayLength(zVal);
        if( len>w ){
          w = len;
          zSep = " ";
        }
        if( i==iIndent && aiIndent && iOp<nIndent ){
          sqlite3_str_appendchar(p->pOut, aiIndent[iOp], ' ');
        }
        qrfWidthPrint(p, p->pOut, w, zVal);
        if( i==nArg-1 ){
          sqlite3_str_append(p->pOut, "\n", 1);
        }else{
          sqlite3_str_appendall(p->pOut, zSep);
        }
      }
      p->nRow++;
    }
    qrfWrite(p);
  }
  sqlite3_free(aiIndent);
}

/*
** Do a "scanstatus vm" style EXPLAIN listing on p->pStmt.
**
** p->pStmt is probably not an EXPLAIN query.  Instead, construct a
** new query that is a bytecode() rendering of p->pStmt with extra
** columns for the "scanstatus vm" outputs, and run the results of
** that new query through the normal EXPLAIN formatting.
*/
static void qrfScanStatusVm(Qrf *p){
  sqlite3_stmt *pOrigStmt = p->pStmt;
  sqlite3_stmt *pExplain;
  int rc;
  static const char *zSql =
      "  SELECT addr, opcode, p1, p2, p3, p4, p5, comment, nexec,"
      "   format('% 6s (%.2f%%)',"
      "      CASE WHEN ncycle<100_000 THEN ncycle || ' '"
      "         WHEN ncycle<100_000_000 THEN (ncycle/1_000) || 'K'"
      "         WHEN ncycle<100_000_000_000 THEN (ncycle/1_000_000) || 'M'"
      "         ELSE (ncycle/1000_000_000) || 'G' END,"
      "       ncycle*100.0/(sum(ncycle) OVER ())"
      "   )  AS cycles"
      "   FROM bytecode(?1)";
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &pExplain, 0);
  if( rc ){
    qrfError(p, rc, "%s", sqlite3_errmsg(p->db));
    sqlite3_finalize(pExplain);
    return;
  }
  sqlite3_bind_pointer(pExplain, 1, pOrigStmt, "stmt-pointer", 0);
  p->pStmt = pExplain;
  p->nCol = 10;
  qrfExplain(p);
  sqlite3_finalize(pExplain);
  p->pStmt = pOrigStmt;
}

/*
** Initialize the internal Qrf object.
*/
static void qrfInitialize(
  Qrf *p,                        /* State object to be initialized */
  sqlite3_stmt *pStmt,           /* Query whose output to be formatted */
  const sqlite3_qrf_spec *pSpec, /* Format specification */
  char **pzErr                   /* Write errors here */
){
  size_t sz;                     /* Size of pSpec[], based on pSpec->iVersion */
  memset(p, 0, sizeof(*p));
  p->pzErr = pzErr;
  if( pSpec->iVersion!=1 ){
    qrfError(p, SQLITE_ERROR,
       "unusable sqlite3_qrf_spec.iVersion (%d)",
       pSpec->iVersion);
    return;
  }
  p->pStmt = pStmt;
  p->db = sqlite3_db_handle(pStmt);
  p->pOut = sqlite3_str_new(p->db);
  if( p->pOut==0 ){
    qrfOom(p);
    return;
  }
  p->iErr = 0;
  p->nCol = sqlite3_column_count(p->pStmt);
  p->nRow = 0;
  sz = sizeof(sqlite3_qrf_spec);
  memcpy(&p->spec, pSpec, sz);
  if( p->spec.zNull==0 ) p->spec.zNull = "";
qrf_reinit:
  switch( p->spec.eStyle ){
    case QRF_Auto: {
      switch( sqlite3_stmt_isexplain(pStmt) ){
        case 0:  p->spec.eStyle = QRF_STYLE_Box;      break;
        case 1:  p->spec.eStyle = QRF_STYLE_Explain;  break;
        default: p->spec.eStyle = QRF_STYLE_Eqp;      break;
      }
      goto qrf_reinit;
    }
    case QRF_STYLE_List: {
      if( p->spec.zColumnSep==0 ) p->spec.zColumnSep = "|";
      if( p->spec.zRowSep==0 ) p->spec.zRowSep = "\n";
      break;
    }
    case QRF_STYLE_JsonLine:
    case QRF_STYLE_Json: {
      p->spec.eText = QRF_TEXT_Json;
      p->spec.eBlob = QRF_BLOB_Json;
      p->spec.zNull = "null";
      break;
    }
    case QRF_STYLE_Html: {
      p->spec.eText = QRF_TEXT_Html;
      p->spec.zNull = "null";
      break;
    }
    case QRF_STYLE_Insert: {
      p->spec.eText = QRF_TEXT_Sql;
      p->spec.eBlob = QRF_BLOB_Sql;
      p->spec.zNull = "NULL";
      if( p->spec.zTableName==0 || p->spec.zTableName[0]==0 ){
        p->spec.zTableName = "tab";
      }
      break;
    }
    case QRF_STYLE_Csv: {
      p->spec.eStyle = QRF_STYLE_List;
      p->spec.eText = QRF_TEXT_Csv;
      p->spec.eBlob = QRF_BLOB_Tcl;
      p->spec.zColumnSep = ",";
      p->spec.zRowSep = "\r\n";
      break;
    }
    case QRF_STYLE_Quote: {
      p->spec.eText = QRF_TEXT_Sql;
      p->spec.eBlob = QRF_BLOB_Sql;
      p->spec.zNull = "NULL";
      p->spec.zColumnSep = ",";
      p->spec.zRowSep = "\n";
      break;
    }
    case QRF_STYLE_Eqp: {
      int expMode = sqlite3_stmt_isexplain(p->pStmt);
      if( expMode!=2 ){
        sqlite3_stmt_explain(p->pStmt, 2);
        p->expMode = expMode+1;
      }
      break;
    }
    case QRF_STYLE_Explain: {
      int expMode = sqlite3_stmt_isexplain(p->pStmt);
      if( expMode!=1 ){
        sqlite3_stmt_explain(p->pStmt, 1);
        p->expMode = expMode+1;
      }
      break;
    }
  }
  if( p->spec.eEsc==QRF_Auto ){
    p->spec.eEsc = QRF_ESC_Ascii;
  }
  if( p->spec.eText==QRF_Auto ){
    p->spec.eText = QRF_TEXT_Off;
  }
  if( p->spec.eTitle==QRF_Auto ){
    switch( p->spec.eStyle ){
      case QRF_STYLE_Box:
      case QRF_STYLE_Column:
      case QRF_STYLE_Table:
        p->spec.eTitle = QRF_TEXT_Off;
        break;
      default:
        p->spec.eTitle = p->spec.eText;
        break;
    }
  }
  if( p->spec.eBlob==QRF_Auto ){
    switch( p->spec.eText ){
      case QRF_TEXT_Sql:  p->spec.eBlob = QRF_BLOB_Sql;  break;
      case QRF_TEXT_Csv:  p->spec.eBlob = QRF_BLOB_Tcl;  break;
      case QRF_TEXT_Tcl:  p->spec.eBlob = QRF_BLOB_Tcl;  break;
      case QRF_TEXT_Json: p->spec.eBlob = QRF_BLOB_Json; break;
      default:            p->spec.eBlob = QRF_BLOB_Text; break;
    }
  }
  if( p->spec.bColumnNames==QRF_Auto ){
    switch( p->spec.eStyle ){
      case QRF_STYLE_Box:
      case QRF_STYLE_Csv:
      case QRF_STYLE_Column:
      case QRF_STYLE_Table:
      case QRF_STYLE_Markdown:
        p->spec.bColumnNames = QRF_Yes;
        break;
      default:
        p->spec.bColumnNames = QRF_No;
        break;
    }
  }
  if( p->spec.bWordWrap==QRF_Auto ){
    p->spec.bWordWrap = QRF_Yes;
  }
  if( p->spec.bTextJsonb==QRF_Auto ){
    p->spec.bTextJsonb = QRF_No;
  }
  if( p->spec.zColumnSep==0 ) p->spec.zColumnSep = ",";
  if( p->spec.zRowSep==0 ) p->spec.zRowSep = "\n";
}

/*
** Attempt to determine if identifier zName needs to be quoted, either
** because it contains non-alphanumeric characters, or because it is an
** SQLite keyword.  Be conservative in this estimate:  When in doubt assume
** that quoting is required.
**
** Return 1 if quoting is required.  Return 0 if no quoting is required.
*/

static int qrf_need_quote(const char *zName){
  int i;
  const unsigned char *z = (const unsigned char*)zName;
  if( z==0 ) return 1;
  if( !isalpha(z[0]) && z[0]!='_' ) return 1;
  for(i=0; z[i]; i++){
    if( !isalnum(z[i]) && z[i]!='_' ) return 1;
  }
  return sqlite3_keyword_check(zName, i)!=0;
}

/*
** Helper function for QRF_STYLE_Json and QRF_STYLE_JsonLine.
** The initial "{" for a JSON object that will contain row content
** has been output.  Now output all the content.
*/
static void qrfOneJsonRow(Qrf *p){
  int i, nItem; 
  for(nItem=i=0; i<p->nCol; i++){
    const char *zCName;
    if( sqlite3_column_type(p->pStmt,i)==SQLITE_NULL ) continue;
    zCName = sqlite3_column_name(p->pStmt, i);
    if( nItem>0 ) sqlite3_str_append(p->pOut, ",", 1);
    nItem++;
    qrfEncodeText(p, p->pOut, zCName);
    sqlite3_str_append(p->pOut, ":", 1);
    qrfRenderValue(p, p->pOut, i);
  }
  qrfWrite(p);
}

/*
** Render a single row of output.
*/
static void qrfOneSimpleRow(Qrf *p){
  int i;
  switch( p->spec.eStyle ){
    case QRF_STYLE_Off:
    case QRF_STYLE_Count: {
      /* No-op */
      break;
    }
    case QRF_STYLE_Json: {
      if( p->nRow==0 ){
        sqlite3_str_append(p->pOut, "[{", 2);
      }else{
        sqlite3_str_append(p->pOut, "},\n{", 4);
      }
      qrfOneJsonRow(p);
      break;
    }
    case QRF_STYLE_JsonLine: {
      if( p->nRow==0 ){
        sqlite3_str_append(p->pOut, "{", 1);
      }else{
        sqlite3_str_append(p->pOut, "}\n{", 3);
      }
      qrfOneJsonRow(p);
      break;
    }
    case QRF_STYLE_Html: {
      if( p->nRow==0 && p->spec.bColumnNames==QRF_Yes ){
        sqlite3_str_append(p->pOut, "<TR>", 4);
        for(i=0; i<p->nCol; i++){
          const char *zCName = sqlite3_column_name(p->pStmt, i);
          sqlite3_str_append(p->pOut, "\n<TH>", 5);
          qrfEncodeText(p, p->pOut, zCName);
        }
        sqlite3_str_append(p->pOut, "\n</TR>\n", 7);
      }
      sqlite3_str_append(p->pOut, "<TR>", 4);
      for(i=0; i<p->nCol; i++){
        sqlite3_str_append(p->pOut, "\n<TD>", 5);
        qrfRenderValue(p, p->pOut, i);
      }
      sqlite3_str_append(p->pOut, "\n</TR>\n", 7);
      qrfWrite(p);
      break;
    }
    case QRF_STYLE_Insert: {
      if( qrf_need_quote(p->spec.zTableName) ){
        sqlite3_str_appendf(p->pOut,"INSERT INTO \"%w\"",p->spec.zTableName);
      }else{
        sqlite3_str_appendf(p->pOut,"INSERT INTO %s",p->spec.zTableName);
      }
      if( p->spec.bColumnNames==QRF_Yes ){
        for(i=0; i<p->nCol; i++){
          const char *zCName = sqlite3_column_name(p->pStmt, i);
          if( qrf_need_quote(zCName) ){
            sqlite3_str_appendf(p->pOut, "%c\"%w\"",
                                i==0 ? '(' : ',', zCName);
          }else{
            sqlite3_str_appendf(p->pOut, "%c%s",
                                i==0 ? '(' : ',', zCName);
          }
        }
        sqlite3_str_append(p->pOut, ")", 1);
      }
      sqlite3_str_append(p->pOut," VALUES(", 8);
      for(i=0; i<p->nCol; i++){
        if( i>0 ) sqlite3_str_append(p->pOut, ",", 1);
        qrfRenderValue(p, p->pOut, i);
      }
      sqlite3_str_append(p->pOut, ");\n", 3);
      qrfWrite(p);
      break;
    }
    case QRF_STYLE_Line: {
      sqlite3_str *pVal;
      int mxW;
      int bWW;
      if( p->u.sLine.azCol==0 ){
        p->u.sLine.azCol = sqlite3_malloc64( p->nCol*sizeof(char*) );
        if( p->u.sLine.azCol==0 ){
          qrfOom(p);
          break;
        }
        p->u.sLine.mxColWth = 0;
        for(i=0; i<p->nCol; i++){
          int sz;
          p->u.sLine.azCol[i] = sqlite3_column_name(p->pStmt, i);
          if( p->u.sLine.azCol[i]==0 ) p->u.sLine.azCol[i] = "unknown";
          sz = qrfDisplayLength(p->u.sLine.azCol[i]);
          if( sz > p->u.sLine.mxColWth ) p->u.sLine.mxColWth = sz;
        }
      }
      if( p->nRow ) sqlite3_str_append(p->pOut, "\n", 1);
      pVal = sqlite3_str_new(p->db);
      mxW = p->spec.mxTotalWidth ? p->spec.mxTotalWidth : QRF_MAX_WIDTH;
      mxW -= 3 + p->u.sLine.mxColWth;
      bWW = p->spec.bWordWrap==QRF_Yes;
      for(i=0; i<p->nCol; i++){
        const char *zVal;
        int cnt = 0;
        qrfWidthPrint(p, p->pOut, -p->u.sLine.mxColWth, p->u.sLine.azCol[i]);
        sqlite3_str_append(p->pOut, " = ", 3);
        qrfRenderValue(p, pVal, i);
        zVal = sqlite3_str_value(pVal);
        if( zVal==0 ) zVal = "";
        do{
          int nThis, nWide, iNext;
          qrfWrapLine(zVal, mxW, bWW, &nThis, &nWide, &iNext);
          if( cnt ) sqlite3_str_appendchar(p->pOut,p->u.sLine.mxColWth+3,' ');
          cnt++;
          if( p->spec.mxRowHeight>0 && cnt>p->spec.mxRowHeight ){
            zVal = "...";
            nThis = iNext = 3;
          }
          sqlite3_str_append(p->pOut, zVal, nThis);
          sqlite3_str_append(p->pOut, "\n", 1);
          zVal += iNext;
        }while( zVal[0] );
        sqlite3_str_reset(pVal);
      }
      sqlite3_free(sqlite3_str_finish(pVal));
      qrfWrite(p);
      break;
    }
    case QRF_STYLE_Eqp: {
      const char *zEqpLine = (const char*)sqlite3_column_text(p->pStmt,3);
      int iEqpId = sqlite3_column_int(p->pStmt, 0);
      int iParentId = sqlite3_column_int(p->pStmt, 1);
      if( zEqpLine==0 ) zEqpLine = "";
      if( zEqpLine[0]=='-' ) qrfEqpRender(p, 0);
      qrfEqpAppend(p, iEqpId, iParentId, zEqpLine);
      break;
    }
    default: {  /* QRF_STYLE_List */
      if( p->nRow==0 && p->spec.bColumnNames==QRF_Yes ){
        int saved_eText = p->spec.eText;
        p->spec.eText = p->spec.eTitle;
        for(i=0; i<p->nCol; i++){
          const char *zCName = sqlite3_column_name(p->pStmt, i);
          if( i>0 ) sqlite3_str_appendall(p->pOut, p->spec.zColumnSep);
          qrfEncodeText(p, p->pOut, zCName);
        }
        sqlite3_str_appendall(p->pOut, p->spec.zRowSep);
        qrfWrite(p);
        p->spec.eText = saved_eText;
      }
      for(i=0; i<p->nCol; i++){
        if( i>0 ) sqlite3_str_appendall(p->pOut, p->spec.zColumnSep);
        qrfRenderValue(p, p->pOut, i);
      }
      sqlite3_str_appendall(p->pOut, p->spec.zRowSep);
      qrfWrite(p);
      break;
    }
  }
  p->nRow++;
}

/*
** Finish rendering the results
*/
static void qrfFinalize(Qrf *p){
  switch( p->spec.eStyle ){
    case QRF_STYLE_Count: {
      sqlite3_str_appendf(p->pOut, "%lld\n", p->nRow);
      qrfWrite(p);
      break;
    }
    case QRF_STYLE_Json: {
      sqlite3_str_append(p->pOut, "}]\n", 3);
      qrfWrite(p);
      break;
    }
    case QRF_STYLE_JsonLine: {
      sqlite3_str_append(p->pOut, "}\n", 2);
      qrfWrite(p);
      break;
    }
    case QRF_STYLE_Line: {
      if( p->u.sLine.azCol ) sqlite3_free(p->u.sLine.azCol);
      break;
    }
    case QRF_STYLE_Stats:
    case QRF_STYLE_StatsEst:
    case QRF_STYLE_Eqp: {
      qrfEqpRender(p, 0);
      qrfWrite(p);
      break;
    }
  }
  if( p->spec.pzOutput ){
    if( p->spec.pzOutput[0] ){
      sqlite3_int64 n, sz;
      char *zCombined;
      sz = strlen(p->spec.pzOutput[0]);
      n = sqlite3_str_length(p->pOut);
      zCombined = sqlite3_realloc(p->spec.pzOutput[0], sz+n+1);
      if( zCombined==0 ){
        sqlite3_free(p->spec.pzOutput[0]);
        p->spec.pzOutput[0] = 0;
        qrfOom(p);
      }else{
        p->spec.pzOutput[0] = zCombined;
        memcpy(zCombined+sz, sqlite3_str_value(p->pOut), n+1);
      }
      sqlite3_free(sqlite3_str_finish(p->pOut));
    }else{
      p->spec.pzOutput[0] = sqlite3_str_finish(p->pOut);
    }
  }else if( p->pOut ){
    sqlite3_free(sqlite3_str_finish(p->pOut));
  }
  if( p->expMode>0 ){
    sqlite3_stmt_explain(p->pStmt, p->expMode-1);
  }
  if( p->actualWidth ){
    sqlite3_free(p->actualWidth);
  }
  if( p->pJTrans ){
    sqlite3 *db = sqlite3_db_handle(p->pJTrans);
    sqlite3_finalize(p->pJTrans);
    sqlite3_close(db);
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
  qrfInitialize(&qrf, pStmt, pSpec, pzErr);
  switch( qrf.spec.eStyle ){
    case QRF_STYLE_Box:
    case QRF_STYLE_Column:
    case QRF_STYLE_Markdown: 
    case QRF_STYLE_Table: {
      /* Columnar modes require that the entire query be evaluated and the
      ** results stored in memory, so that we can compute column widths */
      qrfColumnar(&qrf);
      break;
    }
    case QRF_STYLE_Explain: {
      qrfExplain(&qrf);
      break;
    }
    case QRF_STYLE_StatsVm: {
      qrfScanStatusVm(&qrf);
      break;
    }
    case QRF_STYLE_Stats:
    case QRF_STYLE_StatsEst: {
      qrfEqpStats(&qrf);
      break;
    }
    default: {
      /* Non-columnar modes where the output can occur after each row
      ** of result is received */
      while( qrf.iErr==SQLITE_OK && sqlite3_step(pStmt)==SQLITE_ROW ){
        qrfOneSimpleRow(&qrf);
      }
      break;
    }
  }
  qrfResetStmt(&qrf);
  qrfFinalize(&qrf);
  return qrf.iErr;
}
