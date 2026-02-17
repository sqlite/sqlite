/*
** 2026 February 13
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
** This file contains utility routines used by btree.c when compiled to
** support BEGIN CONCURRENT.
*/

#include "sqliteInt.h"
#include "vdbeInt.h"
#include "btreeInt.h"

#include <string.h>
#include <assert.h>

#ifndef SQLITE_OMIT_CONCURRENT

/* !SQLITE_OMIT_CONCURRENT
**
** Write the serialized data blob for the value stored in pMem into 
** buf. It is assumed that the caller has allocated sufficient space.
** Return the number of bytes written.
**
** nBuf is the amount of space left in buf[].  The caller is responsible
** for allocating enough space to buf[] to hold the entire field, exclusive
** of the pMem->u.nZero bytes for a MEM_Zero value.
**
** Return the number of bytes actually written into buf[].  The number
** of bytes in the zero-filled tail is included in the return value only
** if those bytes were zeroed in buf[].
*/ 
static u32 bcRecordSerialPut(u8 *buf, Mem *pMem, u32 serial_type){
  u32 len;

  /* Integer and Real */
  if( serial_type<=7 && serial_type>0 ){
    u64 v;
    u32 i;
    if( serial_type==7 ){
      assert( sizeof(v)==sizeof(pMem->u.r) );
      memcpy(&v, &pMem->u.r, sizeof(v));
      swapMixedEndianFloat(v);
    }else{
      v = pMem->u.i;
    }
    len = i = sqlite3SmallTypeSizes[serial_type];
    assert( i>0 );
    do{
      buf[--i] = (u8)(v&0xFF);
      v >>= 8;
    }while( i );
    return len;
  }

  /* String or blob */
  if( serial_type>=12 ){
    assert( pMem->n + ((pMem->flags & MEM_Zero)?pMem->u.nZero:0)
             == (int)sqlite3VdbeSerialTypeLen(serial_type) );
    len = pMem->n;
    if( len>0 ) memcpy(buf, pMem->z, len);
    return len;
  }

  /* NULL or constants 0 or 1 */
  return 0;
}

/* !SQLITE_OMIT_CONCURRENT
**
** Return the serial-type for the value stored in pMem. Before returning,
** set (*pLen) to the size in bytes of the serialized form of the value.
**
** This routine might convert a large MEM_IntReal value into MEM_Real.
*/
static u32 bcRecordSerialType(Mem *pMem, u32 *pLen){
  int flags = pMem->flags;
  u32 n;

  assert( pLen!=0 );
  if( flags&MEM_Null ){
    *pLen = 0;
    return 0;
  }
  if( flags&(MEM_Int|MEM_IntReal) ){
    /* Figure out whether to use 1, 2, 4, 6 or 8 bytes. */
#   define MAX_6BYTE ((((i64)0x00008000)<<32)-1)
    i64 i = pMem->u.i;
    u64 u;
    testcase( flags & MEM_Int );
    testcase( flags & MEM_IntReal );
    if( i<0 ){
      u = ~i;
    }else{
      u = i;
    }
    if( u<=127 ){
      if( (i&1)==i ){
        *pLen = 0;
        return 8+(u32)u;
      }else{
        *pLen = 1;
        return 1;
      }
    }
    if( u<=32767 ){ *pLen = 2; return 2; }
    if( u<=8388607 ){ *pLen = 3; return 3; }
    if( u<=2147483647 ){ *pLen = 4; return 4; }
    if( u<=MAX_6BYTE ){ *pLen = 6; return 5; }
    *pLen = 8;
    if( flags&MEM_IntReal ){
      /* If the value is IntReal and is going to take up 8 bytes to store
      ** as an integer, then we might as well make it an 8-byte floating
      ** point value */
      pMem->u.r = (double)pMem->u.i;
      pMem->flags &= ~MEM_IntReal;
      pMem->flags |= MEM_Real;
      return 7;
    }
    return 6;
  }
  if( flags&MEM_Real ){
    *pLen = 8;
    return 7;
  }
  assert( pMem->db->mallocFailed || flags&(MEM_Str|MEM_Blob) );
  assert( pMem->n>=0 );
  n = (u32)pMem->n;
  if( flags & MEM_Zero ){
    n += pMem->u.nZero;
  }
  *pLen = n;
  return ((n*2) + 12 + ((flags&MEM_Str)!=0));
}


/* !SQLITE_OMIT_CONCURRENT
**
** Serialize the unpacked record in pRec into a buffer obtained from
** sqlite3_malloc(). If successful, (*ppRec) is set to point to the 
** buffer and (*pnRec) to its size in bytes before returning SQLITE_OK.
** Or, if an OOM error occurs, return SQLITE_NOMEM. The final values
** of (*ppRec) and (*pnRec) are undefined in this case.
**
*/
int sqlite3BcSerializeRecord(
  UnpackedRecord *pRec,           /* Record to serialize */
  u8 **ppRec,                     /* OUT: buffer containing serialization */
  int *pnRec                      /* OUT: size of (*ppRec) in bytes */
){
  int ii;
  int nData = 0;
  int nHdr = 0;
  u8 *pOut = 0;
  int iOffHdr = 0;
  int iOffData = 0;

  for(ii=0; ii<pRec->nField; ii++){
    u32 n;
    u32 stype = bcRecordSerialType(&pRec->aMem[ii], &n);
    nData += n;
    nHdr += sqlite3VarintLen(stype);
    pRec->aMem[ii].uTemp = stype;
  }

  if( nHdr<=126 ){
    /* The common case */
    nHdr += 1;
  }else{
    /* Rare case of a really large header */
    int nVarint = sqlite3VarintLen(nHdr);
    nHdr += nVarint;
    if( nVarint<sqlite3VarintLen(nHdr) ) nHdr++;
  }

  pOut = (u8*)sqlite3_malloc(nData+nHdr);
  if( pOut==0 ){
    return SQLITE_NOMEM_BKPT;
  }

  iOffData = nHdr;
  iOffHdr = putVarint32(pOut, nHdr);
  for(ii=0; ii<pRec->nField; ii++){
    u32 stype = pRec->aMem[ii].uTemp;
    iOffHdr += putVarint32(&pOut[iOffHdr], stype);
    iOffData += bcRecordSerialPut(&pOut[iOffData], &pRec->aMem[ii], stype);
  }
  assert( iOffData==(nHdr+nData) );

  *ppRec = pOut;
  *pnRec = iOffData;

  return SQLITE_OK;
}

/* !SQLITE_OMIT_CONCURRENT
**
** Helper function for bcRecordToText(). Return a buffer obtained from
** sqlite3_malloc() containing a nul-terminated string containing the hex 
** form of blob aIn[], size nIn bytes. It is the responsibility of the
** caller to eventually free the buffer using sqlite3_free(). If an OOM
** occurs, NULL may be returned.
*/
static char *hex_encode(const u8 *aIn, int nIn){
  char *zRet = sqlite3_malloc(nIn*2+1);
  static const char aDigit[] = "0123456789ABCDEF";
  int i;
  for(i=0; i<nIn; i++){
    zRet[i*2] = aDigit[ (aIn[i] >> 4) ];
    zRet[i*2+1] = aDigit[ (aIn[i] & 0xF) ];
  }
  return zRet;
}


/* !SQLITE_OMIT_CONCURRENT
**
** Buffer aRec[], which is nRec bytes in size, contains a serialized SQLite
** record. This function decodes the record and returns a nul-terminated
** string containing a human-readable version of the record.
**
** The value returned points to a buffer obtained from sqlite3_malloc(). It
** is the responsibility of the caller to eventually free this buffer using
** sqlite3_free(). If an OOM error occurs, NULL may be returned.
**
** If parameter delta is -ve, a "+" is appended to the text record. If it
** is +ve, a "-" is appended.
*/
static char *bcRecordToText(const u8 *aRec, int nRec, int delta){
  char *zRet = 0;
  const char *zSep = "";
  const u8 *pEndHdr;              /* Points to one byte past record header */
  const u8 *pHdr;                 /* Current point in record header */
  const u8 *pBody;                /* Current point in record data */
  u64 nHdr;                       /* Bytes in record header */
  const char *zDelta = 0;

  if( nRec>0 ){

    pHdr = aRec + sqlite3GetVarint(aRec, &nHdr);
    pBody = pEndHdr = &aRec[nHdr];
    while( pHdr<pEndHdr ){
      u64 iSerialType = 0;
      int nByte = 0;
  
      pHdr += sqlite3GetVarint(pHdr, &iSerialType);
      nByte = sqlite3VdbeSerialTypeLen((u32)iSerialType);
  
      switch( iSerialType ){
        case 0: {  /* Null */
          zRet = sqlite3_mprintf("%z%sNULL", zRet, zSep);
          break;
        }
        case 1: case 2: case 3: case 4: case 5: case 6: {
          i64 iVal = 0;
  
          switch( iSerialType ){
            case 1: 
              iVal = (i64)pBody[0];
              break;
            case 2: 
              iVal = ((i64)pBody[0] << 8) + (i64)pBody[1];
              break;
            case 3: 
              iVal = ((i64)pBody[0]<<16) + ((i64)pBody[1]<<8) + (i64)pBody[2];
              break;
            case 4: 
              iVal = ((i64)pBody[0] << 24) + ((i64)pBody[1] << 16) 
                   + ((i64)pBody[2] << 8) + (i64)pBody[3];
              break;
            case 5: 
              iVal = ((i64)pBody[0] << 40) + ((i64)pBody[1] << 32) 
                   + ((i64)pBody[2] << 24) + ((i64)pBody[3] << 16) 
                   + ((i64)pBody[4] << 8) + (i64)pBody[5];
              break;
            case 6: 
              iVal = ((i64)pBody[0] << 56) + ((i64)pBody[1] << 48) 
                   + ((i64)pBody[2] << 40) + ((i64)pBody[3] << 32) 
                   + ((i64)pBody[4] << 24) + ((i64)pBody[5] << 16) 
                   + ((i64)pBody[6] << 8) + (i64)pBody[7];
              break;
          }
  
          zRet = sqlite3_mprintf("%z%s%lld", zRet, zSep, iVal);
          break;
        }
        case 7: {
          double d;
          u64 i = ((u64)pBody[0] << 56) + ((u64)pBody[1] << 48) 
              + ((u64)pBody[2] << 40) + ((u64)pBody[3] << 32) 
              + ((u64)pBody[4] << 24) + ((u64)pBody[5] << 16) 
              + ((u64)pBody[6] << 8) + (u64)pBody[7];
          memcpy(&d, &i, 8);
          zRet = sqlite3_mprintf("%z%s%f", zRet, zSep, d);
          break;
        }
  
        case 8: {  /* 0 */
          zRet = sqlite3_mprintf("%z%s0", zRet, zSep);
          break;
        }
        case 9: {  /* 1 */
          zRet = sqlite3_mprintf("%z%s1", zRet, zSep);
          break;
        }
  
        default: {
          if( (iSerialType % 2) ){
            /* A text value */
            zRet = sqlite3_mprintf("%z%s%.*Q", zRet, zSep, nByte, pBody);
          }else{  
            /* A blob value */
            char *zHex = hex_encode(pBody, nByte);
            zRet = sqlite3_mprintf("%z%sX'%z'", zRet, zSep, zHex);
          }
          break;
        }
      }
      pBody += nByte;
      zSep = ",";
    }
  }

  zDelta = "";
  if( delta<0 ) zDelta = "+";
  if( delta>0 ) zDelta = "-";
  return sqlite3_mprintf("(%z)%s", zRet, zDelta);
}

/* !SQLITE_OMIT_CONCURRENT
**
** There has just been a conflict between pWrite and pRead on index zIdx, which
** is attached to table zTab. Issue a log message.
*/
void sqlite3BcLogIndexConflict(
  const char *zTab, 
  const char *zIdx, 
  BtWriteIndex *pWrite, 
  BtReadIndex *pRead
){
  sqlite3BeginBenignMalloc();
  {
    char *zMin = bcRecordToText(pRead->aRecMin, pRead->nRecMin, pRead->drc_min);
    char *zMax = bcRecordToText(pRead->aRecMax, pRead->nRecMax, pRead->drc_max);
    char *zKey = bcRecordToText(pWrite->aRec, pWrite->nRec, 0);
    sqlite3_log(SQLITE_OK,
        "cannot commit CONCURRENT transaction - conflict in index %s.%s - "
        "range (%s,%s) conflicts with write to key %s", 
        (zTab ? zTab : "UNKNOWN"), 
        (zIdx ? zIdx : "UNKNOWN"), 
        zMin, zMax, zKey
    );
    sqlite3_free(zMin);
    sqlite3_free(zMax);
    sqlite3_free(zKey);
  }
  sqlite3EndBenignMalloc();
}

/*************************************************************************
** Start of virtual table "sqlite_concurrent" implementation. 
*/
#define CONC_SCHEMA "CREATE TABLE x(root, op, k1, k2, sortem HIDDEN)"
#define CONCURRENT_SORTEM 4

typedef struct ConcTable ConcTable;
typedef struct ConcCursor ConcCursor;
typedef struct ConcRow ConcRow;

struct ConcRow {
  Pgno root;
  const char *zOp;
  char *zK1;
  char *zK2;
  ConcRow *pRowNext;
};

struct ConcCursor {
  sqlite3_vtab_cursor base;       /* Base class.  Must be first */
  ConcRow *pRow;
};

struct ConcTable {
  sqlite3_vtab base;              /* Base class.  Must be first */
  sqlite3 *db;                    /* The database */
};

/* !SQLITE_OMIT_CONCURRENT
**
** Connect to the sqlite_concurrent eponymous table.
*/
static int concConnect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  ConcTable *pTab = 0;
  int rc = SQLITE_OK;
  (void)pAux;
  (void)argc;
  (void)argv;
  (void)pzErr;

  sqlite3_vtab_config(db, SQLITE_VTAB_DIRECTONLY);
  sqlite3_vtab_config(db, SQLITE_VTAB_USES_ALL_SCHEMAS);
  rc = sqlite3_declare_vtab(db, CONC_SCHEMA);
  if( rc==SQLITE_OK ){
    pTab = (ConcTable *)sqlite3_malloc64(sizeof(ConcTable));
    if( pTab==0 ) rc = SQLITE_NOMEM_BKPT;
  }

  assert( rc==SQLITE_OK || pTab==0 );
  if( rc==SQLITE_OK ){
    memset(pTab, 0, sizeof(ConcTable));
    pTab->db = db;
  }

  *ppVtab = (sqlite3_vtab*)pTab;
  return rc;
}

/* !SQLITE_OMIT_CONCURRENT
**
** Disconnect from sqlite_concurrent.
*/
static int concDisconnect(sqlite3_vtab *pVtab){
  sqlite3_free(pVtab);
  return SQLITE_OK;
}

/* !SQLITE_OMIT_CONCURRENT
**
** xBestIndex method for sqlite_concurrent.
*/
static int concBestIndex(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo){
  int ii;
  for(ii=0; ii<pIdxInfo->nConstraint; ii++){
    struct sqlite3_index_constraint *p = &pIdxInfo->aConstraint[ii];
    if( p->iColumn!=CONCURRENT_SORTEM ) continue;
    if( p->op!=SQLITE_INDEX_CONSTRAINT_EQ ) continue;
    if( !p->usable ) return SQLITE_CONSTRAINT;
    pIdxInfo->idxNum = 1;
    pIdxInfo->aConstraintUsage[ii].argvIndex = 1;
    pIdxInfo->aConstraintUsage[ii].omit = 1;
    break;
  }
  return SQLITE_OK;
}


/* !SQLITE_OMIT_CONCURRENT
**
** Open a new cursor for sqlite_concurrent.
*/
static int concOpen(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCursor){
  ConcCursor *pCsr;

  pCsr = (ConcCursor *)sqlite3_malloc64(sizeof(ConcCursor));
  if( pCsr==0 ){
    return SQLITE_NOMEM_BKPT;
  }else{
    memset(pCsr, 0, sizeof(ConcCursor));
  }

  *ppCursor = (sqlite3_vtab_cursor *)pCsr;
  return SQLITE_OK;
}

/* !SQLITE_OMIT_CONCURRENT
**
** Close an sqlite_concurrent cursor.
*/
static int concClose(sqlite3_vtab_cursor *pCursor){
  ConcCursor *pCsr = (ConcCursor *)pCursor;
  ConcRow *pRow = 0;
  ConcRow *pNext = 0;
  for(pRow=pCsr->pRow; pRow; pRow=pNext){
    pNext = pRow->pRowNext;
    sqlite3_free(pRow->zK1);
    sqlite3_free(pRow->zK2);
    sqlite3_free(pRow);
  }
  sqlite3_free(pCsr);
  return SQLITE_OK;
}

/* !SQLITE_OMIT_CONCURRENT
**
** Advance the sqlite_concurrent cursor to the next row.
*/
static int concNext(sqlite3_vtab_cursor *pCursor){
  ConcCursor *pCsr = (ConcCursor *)pCursor;
  ConcRow *pFree = pCsr->pRow;
  if( pFree ){
    pCsr->pRow = pFree->pRowNext;
    sqlite3_free(pFree->zK1);
    sqlite3_free(pFree->zK2);
    sqlite3_free(pFree);
  }
  return SQLITE_OK;
}

/* !SQLITE_OMIT_CONCURRENT
**
** Return true if the sqlite_concurrent cursor is at EOF.
*/
static int concEof(sqlite3_vtab_cursor *pCursor){
  ConcCursor *pCsr = (ConcCursor *)pCursor;
  return pCsr->pRow==0;
}

/* !SQLITE_OMIT_CONCURRENT
**
** xFilter method for sqlite_concurrent.
**
** In all cases this function populates the cursor with rows for each
** read and write currently accumulated by the datbase connection.
**
** idxNum may be either 0 or 1. If it is 1, then there is a single
** argument passed. If this is a non-zero integer, the reads are
** sorted before any rows are returned.
*/
static int concFilter(
  sqlite3_vtab_cursor *pCursor,
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  ConcCursor *pCsr = (ConcCursor *)pCursor;
  ConcTable *pTab = (ConcTable *)pCursor->pVtab;
  sqlite3 *db = pTab->db;
  BtConcurrent *pConc = &db->aDb[0].pBt->pBt->conc;
  int rc = SQLITE_OK;

  assert( idxNum==0 || idxNum==1 );
  assert( idxNum==argc );

  if( idxNum==1 ){
    int bSort = sqlite3_value_int(argv[0]);
    if( bSort ){
      rc = sqlite3BtreeSortReadArrays(pConc);
    }
  }

  if( rc==SQLITE_OK ){
    int ii;

    for(ii=0; rc==SQLITE_OK && ii<pConc->nWrite; ii++){
      BtWrite *pWrite = &pConc->aWrite[ii];
      ConcRow *pRow = (ConcRow*)sqlite3MallocZero(sizeof(ConcRow));
      if( pRow==0 ){
        rc = SQLITE_NOMEM_BKPT;
      }else{
        pRow->root = pWrite->iRoot;
        pRow->zOp = pWrite->bDel ? "delete" : "insert";
        if( pWrite->pKeyInfo ){
          pRow->zK1 = bcRecordToText(pWrite->aRec, pWrite->nRec, 0);
          if( pRow->zK1==0 ) rc = SQLITE_NOMEM_BKPT;
        }else{
          pRow->zK1 = sqlite3_mprintf("%lld", pWrite->iKey);
          if( pRow->zK1==0 ){
            rc = SQLITE_NOMEM_BKPT;
          }else if( pWrite->bDel==0 ){
            pRow->zK2 = bcRecordToText(pWrite->aRec, pWrite->nRec, 0);
            if( pRow->zK2==0 ) rc = SQLITE_NOMEM_BKPT;
          }
        }
        pRow->pRowNext = pCsr->pRow;
        pCsr->pRow = pRow;
      }
    }

    for(ii=pConc->nReadIndex-1; rc==SQLITE_OK && ii>=0; ii--){
      BtReadIndex *p = &pConc->aReadIndex[ii];
      ConcRow *pRow = (ConcRow*)sqlite3MallocZero(sizeof(ConcRow));
      if( pRow==0 ){
        rc = SQLITE_NOMEM_BKPT;
      }else{
        pRow->root = p->iRoot;
        pRow->zOp = "read";
        pRow->zK1 = bcRecordToText(p->aRecMin, p->nRecMin, p->drc_min);
        pRow->zK2 = bcRecordToText(p->aRecMax, p->nRecMax, p->drc_max);
        pRow->pRowNext = pCsr->pRow;
        pCsr->pRow = pRow;
        if( pRow->zK1==0 || pRow->zK2==0 ){
          rc = SQLITE_NOMEM_BKPT;
        }
      }
    }

    for(ii=pConc->nReadIntkey-1; rc==SQLITE_OK && ii>=0; ii--){
      BtReadIntkey *p = &pConc->aReadIntkey[ii];
      ConcRow *pRow = (ConcRow*)sqlite3MallocZero(sizeof(ConcRow));
      if( pRow==0 ){
        rc = SQLITE_NOMEM_BKPT;
      }else{
        pRow->root = p->iRoot;
        pRow->zOp = "read";
        pRow->zK1 = sqlite3_mprintf("%lld", p->iMin);
        pRow->zK2 = sqlite3_mprintf("%lld", p->iMax);
        pRow->pRowNext = pCsr->pRow;
        pCsr->pRow = pRow;
        if( pRow->zK1==0 || pRow->zK2==0 ){
          rc = SQLITE_NOMEM_BKPT;
        }
      }
    }
  }

  return rc;
}

/* !SQLITE_OMIT_CONCURRENT
**
** xColumn method for sqlite_concurrent.
*/
static int concColumn(
  sqlite3_vtab_cursor *pCursor,
  sqlite3_context *ctx,
  int i
){
  ConcCursor *pCsr = (ConcCursor *)pCursor;
  int rc = SQLITE_OK;
  ConcRow *pRow = pCsr->pRow;
  assert( pRow );
  switch( i ){
    case 0: {           /* root */
      sqlite3_result_int64(ctx, (sqlite3_int64)pRow->root);
      break;
    }
    case 1: {           /* op */
      sqlite3_result_text(ctx, pRow->zOp, -1, SQLITE_TRANSIENT);
      break;
    }
    case 2: {           /* k1 */
      sqlite3_result_text(ctx, pRow->zK1, -1, SQLITE_TRANSIENT);
      break;
    }
    case 3: {           /* k2 */
      sqlite3_result_text(ctx, pRow->zK2, -1, SQLITE_TRANSIENT);
      break;
    }
  }
  return rc;
}

/* !SQLITE_OMIT_CONCURRENT
**
** xRowid method for sqlite_concurrent.
*/
static int concRowid(sqlite3_vtab_cursor *pCursor, sqlite_int64 *pRowid){
  *pRowid = 0;
  return SQLITE_OK;
}

/* !SQLITE_OMIT_CONCURRENT
**
** Register the sqlite_concurrent eponymous virtual table with database
** connection db. Return SQLITE_OK if successful, or an SQLite error code
** if an error occurs.
*/
int sqlite3ConcurrentRegister(sqlite3 *db){
  static sqlite3_module conc_module = {
    2,                            /* iVersion */
    concConnect,                  /* xCreate */
    concConnect,                  /* xConnect */
    concBestIndex,                /* xBestIndex */
    concDisconnect,               /* xDisconnect */
    concDisconnect,               /* xDestroy */
    concOpen,                     /* xOpen - open a cursor */
    concClose,                    /* xClose - close a cursor */
    concFilter,                   /* xFilter - configure scan constraints */
    concNext,                     /* xNext - advance a cursor */
    concEof,                      /* xEof - check for end of scan */
    concColumn,                   /* xColumn - read data */
    concRowid,                    /* xRowid - read data */
    0,                            /* xUpdate */
    0,                            /* xBegin */
    0,                            /* xSync */
    0,                            /* xCommit */
    0,                            /* xRollback */
    0,                            /* xFindMethod */
    0,                            /* xRename */
    0,                            /* xSavepoint */
    0,                            /* xRelease */
    0,                            /* xRollbackTo */
    0,                            /* xShadowName */
    0                             /* xIntegrity */
  };
  return sqlite3_create_module(db, "sqlite_concurrent", &conc_module, 0);
}

#endif /* !SQLITE_OMIT_CONCURRENT */
