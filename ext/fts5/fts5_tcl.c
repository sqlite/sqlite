/*
** 2014 Dec 01
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
*/


#ifdef SQLITE_TEST
#include "tclsqlite.h"

#ifdef SQLITE_ENABLE_FTS5

#include "fts5.h"
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifdef SQLITE_DEBUG
extern int sqlite3_fts5_may_be_corrupt;
#endif
extern int sqlite3Fts5TestRegisterMatchinfo(sqlite3*);
extern int sqlite3Fts5TestRegisterTok(sqlite3*, fts5_api*);

/*************************************************************************
** This is a copy of the first part of the SqliteDb structure in 
** tclsqlite.c.  We need it here so that the get_sqlite_pointer routine
** can extract the sqlite3* pointer from an existing Tcl SQLite
** connection.
*/

extern const char *sqlite3ErrName(int);

struct SqliteDb {
  sqlite3 *db;
};

/*
** Decode a pointer to an sqlite3 object.
*/
static int f5tDbPointer(Tcl_Interp *interp, Tcl_Obj *pObj, sqlite3 **ppDb){
  struct SqliteDb *p;
  Tcl_CmdInfo cmdInfo;
  char *z = Tcl_GetString(pObj);
  if( Tcl_GetCommandInfo(interp, z, &cmdInfo) ){
    p = (struct SqliteDb*)cmdInfo.objClientData;
    *ppDb = p->db;
    return TCL_OK;
  }
  return TCL_ERROR;
}

/* End of code that accesses the SqliteDb struct.
**************************************************************************/

static int f5tResultToErrorCode(const char *zRes){
  struct ErrorCode {
    int rc;
    const char *zError;
  } aErr[] = {
    { SQLITE_DONE,  "SQLITE_DONE" },
    { SQLITE_ERROR, "SQLITE_ERROR" },
    { SQLITE_OK,    "SQLITE_OK" },
    { SQLITE_OK,    "" },
  };
  int i;

  for(i=0; i<sizeof(aErr)/sizeof(aErr[0]); i++){
    if( 0==sqlite3_stricmp(zRes, aErr[i].zError) ){
      return aErr[i].rc;
    }
  }

  return SQLITE_ERROR;
}

static int SQLITE_TCLAPI f5tDbAndApi(
  Tcl_Interp *interp, 
  Tcl_Obj *pObj, 
  sqlite3 **ppDb, 
  fts5_api **ppApi
){
  sqlite3 *db = 0;
  int rc = f5tDbPointer(interp, pObj, &db);
  if( rc!=TCL_OK ){
    return TCL_ERROR;
  }else{
    sqlite3_stmt *pStmt = 0;
    fts5_api *pApi = 0;

    rc = sqlite3_prepare_v2(db, "SELECT fts5(?1)", -1, &pStmt, 0);
    if( rc!=SQLITE_OK ){
      Tcl_AppendResult(interp, "error: ", sqlite3_errmsg(db), (char*)0);
      return TCL_ERROR;
    }
    sqlite3_bind_pointer(pStmt, 1, (void*)&pApi, "fts5_api_ptr", 0);
    sqlite3_step(pStmt);

    if( sqlite3_finalize(pStmt)!=SQLITE_OK ){
      Tcl_AppendResult(interp, "error: ", sqlite3_errmsg(db), (char*)0);
      return TCL_ERROR;
    }

    *ppDb = db;
    *ppApi = pApi;
  }

  return TCL_OK;
}

typedef struct F5tFunction F5tFunction;
struct F5tFunction {
  Tcl_Interp *interp;
  Tcl_Obj *pScript;
};

typedef struct F5tApi F5tApi;
struct F5tApi {
  const Fts5ExtensionApi *pApi;
  Fts5Context *pFts;
};

/*
** An object of this type is used with the xSetAuxdata() and xGetAuxdata()
** API test wrappers. The tcl interface allows a single tcl value to be 
** saved using xSetAuxdata(). Instead of simply storing a pointer to the
** tcl object, the code in this file wraps it in an sqlite3_malloc'd 
** instance of the following struct so that if the destructor is not 
** correctly invoked it will be reported as an SQLite memory leak.
*/
typedef struct F5tAuxData F5tAuxData;
struct F5tAuxData {
  Tcl_Obj *pObj;
};

static int xTokenizeCb(
  void *pCtx, 
  int tflags,
  const char *zToken, int nToken, 
  int iStart, int iEnd
){
  F5tFunction *p = (F5tFunction*)pCtx;
  Tcl_Obj *pEval = Tcl_DuplicateObj(p->pScript);
  int rc;

  Tcl_IncrRefCount(pEval);
  Tcl_ListObjAppendElement(p->interp, pEval, Tcl_NewStringObj(zToken, nToken));
  Tcl_ListObjAppendElement(p->interp, pEval, Tcl_NewIntObj(iStart));
  Tcl_ListObjAppendElement(p->interp, pEval, Tcl_NewIntObj(iEnd));

  rc = Tcl_EvalObjEx(p->interp, pEval, 0);
  Tcl_DecrRefCount(pEval);
  if( rc==TCL_OK ){
    rc = f5tResultToErrorCode(Tcl_GetStringResult(p->interp));
  }

  return rc;
}

static int SQLITE_TCLAPI xF5tApi(void*, Tcl_Interp*, int, Tcl_Obj *CONST []);

static int xQueryPhraseCb(
  const Fts5ExtensionApi *pApi, 
  Fts5Context *pFts, 
  void *pCtx
){
  F5tFunction *p = (F5tFunction*)pCtx;
  static sqlite3_int64 iCmd = 0;
  Tcl_Obj *pEval;
  int rc;

  char zCmd[64];
  F5tApi sApi;

  sApi.pApi = pApi;
  sApi.pFts = pFts;
  sprintf(zCmd, "f5t_2_%lld", iCmd++);
  Tcl_CreateObjCommand(p->interp, zCmd, xF5tApi, &sApi, 0);

  pEval = Tcl_DuplicateObj(p->pScript);
  Tcl_IncrRefCount(pEval);
  Tcl_ListObjAppendElement(p->interp, pEval, Tcl_NewStringObj(zCmd, -1));
  rc = Tcl_EvalObjEx(p->interp, pEval, 0);
  Tcl_DecrRefCount(pEval);
  Tcl_DeleteCommand(p->interp, zCmd);

  if( rc==TCL_OK ){
    rc = f5tResultToErrorCode(Tcl_GetStringResult(p->interp));
  }

  return rc;
}

static void xSetAuxdataDestructor(void *p){
  F5tAuxData *pData = (F5tAuxData*)p;
  Tcl_DecrRefCount(pData->pObj);
  sqlite3_free(pData);
}

/*
**      api sub-command...
**
** Description...
*/
static int SQLITE_TCLAPI xF5tApi(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  struct Sub {
    const char *zName;
    int nArg;
    const char *zMsg;
  } aSub[] = {
    { "xColumnCount",      0, "" },                   /*  0 */
    { "xRowCount",         0, "" },                   /*  1 */
    { "xColumnTotalSize",  1, "COL" },                /*  2 */
    { "xTokenize",         2, "TEXT SCRIPT" },        /*  3 */
    { "xPhraseCount",      0, "" },                   /*  4 */
    { "xPhraseSize",       1, "PHRASE" },             /*  5 */
    { "xInstCount",        0, "" },                   /*  6 */
    { "xInst",             1, "IDX" },                /*  7 */
    { "xRowid",            0, "" },                   /*  8 */
    { "xColumnText",       1, "COL" },                /*  9 */
    { "xColumnSize",       1, "COL" },                /* 10 */
    { "xQueryPhrase",      2, "PHRASE SCRIPT" },      /* 11 */
    { "xSetAuxdata",       1, "VALUE" },              /* 12 */
    { "xGetAuxdata",       1, "CLEAR" },              /* 13 */
    { "xSetAuxdataInt",    1, "INTEGER" },            /* 14 */
    { "xGetAuxdataInt",    1, "CLEAR" },              /* 15 */
    { "xPhraseForeach",    4, "IPHRASE COLVAR OFFVAR SCRIPT" }, /* 16 */
    { "xPhraseColumnForeach", 3, "IPHRASE COLVAR SCRIPT" }, /* 17 */

    { "xQueryToken",       2, "IPHRASE ITERM" },      /* 18 */
    { "xInstToken",        2, "IDX ITERM" },          /* 19 */
    { "xColumnLocale",     1, "COL" },                /* 20 */
    { 0, 0, 0}
  };

  int rc;
  int iSub = 0;
  F5tApi *p = (F5tApi*)clientData;

  if( objc<2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "SUB-COMMAND");
    return TCL_ERROR;
  }

  rc = Tcl_GetIndexFromObjStruct(
      interp, objv[1], aSub, sizeof(aSub[0]), "SUB-COMMAND", 0, &iSub
  );
  if( rc!=TCL_OK ) return rc;
  if( aSub[iSub].nArg!=objc-2 ){
    Tcl_WrongNumArgs(interp, 1, objv, aSub[iSub].zMsg);
    return TCL_ERROR;
  }

#define CASE(i,str) case i: assert( strcmp(aSub[i].zName, str)==0 );
  switch( iSub ){
    CASE(0, "xColumnCount") {
      int nCol;
      nCol = p->pApi->xColumnCount(p->pFts);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewIntObj(nCol));
      }
      break;
    }
    CASE(1, "xRowCount") {
      sqlite3_int64 nRow;
      rc = p->pApi->xRowCount(p->pFts, &nRow);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewWideIntObj(nRow));
      }
      break;
    }
    CASE(2, "xColumnTotalSize") {
      int iCol;
      sqlite3_int64 nSize;
      if( Tcl_GetIntFromObj(interp, objv[2], &iCol) ) return TCL_ERROR;
      rc = p->pApi->xColumnTotalSize(p->pFts, iCol, &nSize);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewWideIntObj(nSize));
      }
      break;
    }
    CASE(3, "xTokenize") {
      Tcl_Size nText;
      char *zText = Tcl_GetStringFromObj(objv[2], &nText);
      F5tFunction ctx;
      ctx.interp = interp;
      ctx.pScript = objv[3];
      rc = p->pApi->xTokenize(p->pFts, zText, (int)nText, &ctx, xTokenizeCb);
      if( rc==SQLITE_OK ){
        Tcl_ResetResult(interp);
      }
      return rc;
    }
    CASE(4, "xPhraseCount") {
      int nPhrase;
      nPhrase = p->pApi->xPhraseCount(p->pFts);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewIntObj(nPhrase));
      }
      break;
    }
    CASE(5, "xPhraseSize") {
      int iPhrase;
      int sz;
      if( Tcl_GetIntFromObj(interp, objv[2], &iPhrase) ){
        return TCL_ERROR;
      }
      sz = p->pApi->xPhraseSize(p->pFts, iPhrase);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewIntObj(sz));
      }
      break;
    }
    CASE(6, "xInstCount") {
      int nInst;
      rc = p->pApi->xInstCount(p->pFts, &nInst);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewIntObj(nInst));
      }
      break;
    }
    CASE(7, "xInst") {
      int iIdx, ip, ic, io;
      if( Tcl_GetIntFromObj(interp, objv[2], &iIdx) ){
        return TCL_ERROR;
      }
      rc = p->pApi->xInst(p->pFts, iIdx, &ip, &ic, &io);
      if( rc==SQLITE_OK ){
        Tcl_Obj *pList = Tcl_NewObj();
        Tcl_ListObjAppendElement(interp, pList, Tcl_NewIntObj(ip));
        Tcl_ListObjAppendElement(interp, pList, Tcl_NewIntObj(ic));
        Tcl_ListObjAppendElement(interp, pList, Tcl_NewIntObj(io));
        Tcl_SetObjResult(interp, pList);
      }
      break;
    }
    CASE(8, "xRowid") {
      sqlite3_int64 iRowid = p->pApi->xRowid(p->pFts);
      Tcl_SetObjResult(interp, Tcl_NewWideIntObj(iRowid));
      break;
    }
    CASE(9, "xColumnText") {
      const char *z = 0;
      int n = 0;
      int iCol;
      if( Tcl_GetIntFromObj(interp, objv[2], &iCol) ){
        return TCL_ERROR;
      }
      rc = p->pApi->xColumnText(p->pFts, iCol, &z, &n);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewStringObj(z, n));
      }
      break;
    }
    CASE(10, "xColumnSize") {
      int n = 0;
      int iCol;
      if( Tcl_GetIntFromObj(interp, objv[2], &iCol) ){
        return TCL_ERROR;
      }
      rc = p->pApi->xColumnSize(p->pFts, iCol, &n);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewIntObj(n));
      }
      break;
    }
    CASE(11, "xQueryPhrase") {
      int iPhrase;
      F5tFunction ctx;
      if( Tcl_GetIntFromObj(interp, objv[2], &iPhrase) ){
        return TCL_ERROR;
      }
      ctx.interp = interp;
      ctx.pScript = objv[3];
      rc = p->pApi->xQueryPhrase(p->pFts, iPhrase, &ctx, xQueryPhraseCb);
      if( rc==SQLITE_OK ){
        Tcl_ResetResult(interp);
      }
      break;
    }
    CASE(12, "xSetAuxdata") {
      F5tAuxData *pData = (F5tAuxData*)sqlite3_malloc(sizeof(F5tAuxData));
      if( pData==0 ){
        Tcl_AppendResult(interp, "out of memory", (char*)0);
        return TCL_ERROR;
      }
      pData->pObj = objv[2];
      Tcl_IncrRefCount(pData->pObj);
      rc = p->pApi->xSetAuxdata(p->pFts, pData, xSetAuxdataDestructor);
      break;
    }
    CASE(13, "xGetAuxdata") {
      F5tAuxData *pData;
      int bClear;
      if( Tcl_GetBooleanFromObj(interp, objv[2], &bClear) ){
        return TCL_ERROR;
      }
      pData = (F5tAuxData*)p->pApi->xGetAuxdata(p->pFts, bClear);
      if( pData==0 ){
        Tcl_ResetResult(interp);
      }else{
        Tcl_SetObjResult(interp, pData->pObj);
        if( bClear ){
          xSetAuxdataDestructor((void*)pData);
        }
      }
      break;
    }

    /* These two - xSetAuxdataInt and xGetAuxdataInt - are similar to the
    ** xSetAuxdata and xGetAuxdata methods implemented above. The difference
    ** is that they may only save an integer value as auxiliary data, and
    ** do not specify a destructor function.  */
    CASE(14, "xSetAuxdataInt") {
      int iVal;
      if( Tcl_GetIntFromObj(interp, objv[2], &iVal) ) return TCL_ERROR;
      rc = p->pApi->xSetAuxdata(p->pFts, (void*)((char*)0 + iVal), 0);
      break;
    }
    CASE(15, "xGetAuxdataInt") {
      int iVal;
      int bClear;
      if( Tcl_GetBooleanFromObj(interp, objv[2], &bClear) ) return TCL_ERROR;
      iVal = (int)((char*)p->pApi->xGetAuxdata(p->pFts, bClear) - (char*)0);
      Tcl_SetObjResult(interp, Tcl_NewIntObj(iVal));
      break;
    }

    CASE(16, "xPhraseForeach") {
      int iPhrase;
      int iCol;
      int iOff;
      const char *zColvar;
      const char *zOffvar;
      Tcl_Obj *pScript = objv[5];
      Fts5PhraseIter iter;

      if( Tcl_GetIntFromObj(interp, objv[2], &iPhrase) ) return TCL_ERROR;
      zColvar = Tcl_GetString(objv[3]);
      zOffvar = Tcl_GetString(objv[4]);

      rc = p->pApi->xPhraseFirst(p->pFts, iPhrase, &iter, &iCol, &iOff);
      if( rc!=SQLITE_OK ){
        Tcl_AppendResult(interp, sqlite3ErrName(rc), (char*)0);
        return TCL_ERROR;
      }
      for( ;iCol>=0; p->pApi->xPhraseNext(p->pFts, &iter, &iCol, &iOff) ){
        Tcl_SetVar2Ex(interp, zColvar, 0, Tcl_NewIntObj(iCol), 0);
        Tcl_SetVar2Ex(interp, zOffvar, 0, Tcl_NewIntObj(iOff), 0);
        rc = Tcl_EvalObjEx(interp, pScript, 0);
        if( rc==TCL_CONTINUE ) rc = TCL_OK;
        if( rc!=TCL_OK ){
          if( rc==TCL_BREAK ) rc = TCL_OK;
          break;
        }
      }

      break;
    }

    CASE(17, "xPhraseColumnForeach") {
      int iPhrase;
      int iCol;
      const char *zColvar;
      Tcl_Obj *pScript = objv[4];
      Fts5PhraseIter iter;

      if( Tcl_GetIntFromObj(interp, objv[2], &iPhrase) ) return TCL_ERROR;
      zColvar = Tcl_GetString(objv[3]);

      rc = p->pApi->xPhraseFirstColumn(p->pFts, iPhrase, &iter, &iCol);
      if( rc!=SQLITE_OK ){
        Tcl_SetResult(interp, (char*)sqlite3ErrName(rc), TCL_VOLATILE);
        return TCL_ERROR;
      }
      for( ; iCol>=0; p->pApi->xPhraseNextColumn(p->pFts, &iter, &iCol)){
        Tcl_SetVar2Ex(interp, zColvar, 0, Tcl_NewIntObj(iCol), 0);
        rc = Tcl_EvalObjEx(interp, pScript, 0);
        if( rc==TCL_CONTINUE ) rc = TCL_OK;
        if( rc!=TCL_OK ){
          if( rc==TCL_BREAK ) rc = TCL_OK;
          break;
        }
      }

      break;
    }

    CASE(18, "xQueryToken") {
      const char *pTerm = 0;
      int nTerm = 0;
      int iPhrase = 0;
      int iTerm = 0;

      if( Tcl_GetIntFromObj(interp, objv[2], &iPhrase) ) return TCL_ERROR;
      if( Tcl_GetIntFromObj(interp, objv[3], &iTerm) ) return TCL_ERROR;
      rc = p->pApi->xQueryToken(p->pFts, iPhrase, iTerm, &pTerm, &nTerm);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewStringObj(pTerm, nTerm));
      }

      break;
    }

    CASE(19, "xInstToken") {
      const char *pTerm = 0;
      int nTerm = 0;
      int iIdx = 0;
      int iTerm = 0;

      if( Tcl_GetIntFromObj(interp, objv[2], &iIdx) ) return TCL_ERROR;
      if( Tcl_GetIntFromObj(interp, objv[3], &iTerm) ) return TCL_ERROR;
      rc = p->pApi->xInstToken(p->pFts, iIdx, iTerm, &pTerm, &nTerm);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewStringObj(pTerm, nTerm));
      }

      break;
    }

    CASE(20, "xColumnLocale") {
      const char *z = 0;
      int n = 0;
      int iCol;
      if( Tcl_GetIntFromObj(interp, objv[2], &iCol) ){
        return TCL_ERROR;
      }
      rc = p->pApi->xColumnLocale(p->pFts, iCol, &z, &n);
      if( rc==SQLITE_OK && z ){
        Tcl_SetObjResult(interp, Tcl_NewStringObj(z, n));
      }
      break;
    }

    default: 
      assert( 0 );
      break;
  }
#undef CASE

  if( rc!=SQLITE_OK ){
    Tcl_SetResult(interp, (char*)sqlite3ErrName(rc), TCL_VOLATILE);
    return TCL_ERROR;
  }

  return TCL_OK;
}

static void xF5tFunction(
  const Fts5ExtensionApi *pApi,   /* API offered by current FTS version */
  Fts5Context *pFts,              /* First arg to pass to pApi functions */
  sqlite3_context *pCtx,          /* Context for returning result/error */
  int nVal,                       /* Number of values in apVal[] array */
  sqlite3_value **apVal           /* Array of trailing arguments */
){
  F5tFunction *p = (F5tFunction*)pApi->xUserData(pFts);
  Tcl_Obj *pEval;                 /* Script to evaluate */
  int i;
  int rc;

  static sqlite3_int64 iCmd = 0;
  char zCmd[64];
  F5tApi sApi;
  sApi.pApi = pApi;
  sApi.pFts = pFts;

  sprintf(zCmd, "f5t_%lld", iCmd++);
  Tcl_CreateObjCommand(p->interp, zCmd, xF5tApi, &sApi, 0);
  pEval = Tcl_DuplicateObj(p->pScript);
  Tcl_IncrRefCount(pEval);
  Tcl_ListObjAppendElement(p->interp, pEval, Tcl_NewStringObj(zCmd, -1));

  for(i=0; i<nVal; i++){
    Tcl_Obj *pObj = 0;
    switch( sqlite3_value_type(apVal[i]) ){
      case SQLITE_TEXT:
        pObj = Tcl_NewStringObj((const char*)sqlite3_value_text(apVal[i]), -1);
        break;
      case SQLITE_BLOB:
        pObj = Tcl_NewByteArrayObj(
            sqlite3_value_blob(apVal[i]), sqlite3_value_bytes(apVal[i])
        );
        break;
      case SQLITE_INTEGER:
        pObj = Tcl_NewWideIntObj(sqlite3_value_int64(apVal[i]));
        break;
      case SQLITE_FLOAT:
        pObj = Tcl_NewDoubleObj(sqlite3_value_double(apVal[i]));
        break;
      default:
        pObj = Tcl_NewObj();
        break;
    }
    Tcl_ListObjAppendElement(p->interp, pEval, pObj);
  }

  rc = Tcl_EvalObjEx(p->interp, pEval, TCL_GLOBAL_ONLY);
  Tcl_DecrRefCount(pEval);
  Tcl_DeleteCommand(p->interp, zCmd);

  if( rc!=TCL_OK ){
    sqlite3_result_error(pCtx, Tcl_GetStringResult(p->interp), -1);
  }else{
    Tcl_Obj *pVar = Tcl_GetObjResult(p->interp);
    const char *zType = (pVar->typePtr ? pVar->typePtr->name : "");
    char c = zType[0];
    if( c=='b' && strcmp(zType,"bytearray")==0 && pVar->bytes==0 ){
      /* Only return a BLOB type if the Tcl variable is a bytearray and
      ** has no string representation. */
      Tcl_Size nn;
      unsigned char *data = Tcl_GetByteArrayFromObj(pVar, &nn);
      sqlite3_result_blob(pCtx, data, (int)nn, SQLITE_TRANSIENT);
    }else if( c=='b' && strcmp(zType,"boolean")==0 ){
      int n;
      Tcl_GetIntFromObj(0, pVar, &n);
      sqlite3_result_int(pCtx, n);
    }else if( c=='d' && strcmp(zType,"double")==0 ){
      double r;
      Tcl_GetDoubleFromObj(0, pVar, &r);
      sqlite3_result_double(pCtx, r);
    }else if( (c=='w' && strcmp(zType,"wideInt")==0) ||
          (c=='i' && strcmp(zType,"int")==0) ){
      Tcl_WideInt v;
      Tcl_GetWideIntFromObj(0, pVar, &v);
      sqlite3_result_int64(pCtx, v);
    }else{
      Tcl_Size nn;
      unsigned char *data = (unsigned char *)Tcl_GetStringFromObj(pVar, &nn);
      sqlite3_result_text(pCtx, (char*)data, (int)nn, SQLITE_TRANSIENT);
    }
  }
}

static void xF5tDestroy(void *pCtx){
  F5tFunction *p = (F5tFunction*)pCtx;
  Tcl_DecrRefCount(p->pScript);
  ckfree((char *)p);
}

/*
**      sqlite3_fts5_create_function DB NAME SCRIPT
**
** Description...
*/
static int SQLITE_TCLAPI f5tCreateFunction(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  char *zName;
  Tcl_Obj *pScript;
  sqlite3 *db = 0;
  fts5_api *pApi = 0;
  F5tFunction *pCtx = 0;
  int rc;

  if( objc!=4 ){
    Tcl_WrongNumArgs(interp, 1, objv, "DB NAME SCRIPT");
    return TCL_ERROR;
  }
  if( f5tDbAndApi(interp, objv[1], &db, &pApi) ) return TCL_ERROR;

  zName = Tcl_GetString(objv[2]);
  pScript = objv[3];
  pCtx = (F5tFunction*)ckalloc(sizeof(F5tFunction));
  pCtx->interp = interp;
  pCtx->pScript = pScript;
  Tcl_IncrRefCount(pScript);

  rc = pApi->xCreateFunction(
      pApi, zName, (void*)pCtx, xF5tFunction, xF5tDestroy
  );
  if( rc!=SQLITE_OK ){
    Tcl_AppendResult(interp, "error: ", sqlite3_errmsg(db), (char*)0);
    return TCL_ERROR;
  }

  return TCL_OK;
}

typedef struct F5tTokenizeCtx F5tTokenizeCtx;
struct F5tTokenizeCtx {
  Tcl_Obj *pRet;
  int bSubst;
  const char *zInput;
};

static int xTokenizeCb2(
  void *pCtx, 
  int tflags,
  const char *zToken, int nToken, 
  int iStart, int iEnd
){
  F5tTokenizeCtx *p = (F5tTokenizeCtx*)pCtx;
  if( p->bSubst ){
    Tcl_ListObjAppendElement(0, p->pRet, Tcl_NewStringObj(zToken, nToken));
    Tcl_ListObjAppendElement(
        0, p->pRet, Tcl_NewStringObj(&p->zInput[iStart], iEnd-iStart)
    );
  }else{
    Tcl_ListObjAppendElement(0, p->pRet, Tcl_NewStringObj(zToken, nToken));
    Tcl_ListObjAppendElement(0, p->pRet, Tcl_NewIntObj(iStart));
    Tcl_ListObjAppendElement(0, p->pRet, Tcl_NewIntObj(iEnd));
  }
  return SQLITE_OK;
}


/*
**      sqlite3_fts5_tokenize DB TOKENIZER TEXT
**
** Description...
*/
static int SQLITE_TCLAPI f5tTokenize(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  char *pCopy = 0;
  char *zText = 0;
  Tcl_Size nText = 0;
  sqlite3 *db = 0;
  fts5_api *pApi = 0;
  Fts5Tokenizer *pTok = 0;
  fts5_tokenizer tokenizer;
  Tcl_Obj *pRet = 0;
  void *pUserdata;
  int rc;

  Tcl_Size nArg;
  const char **azArg;
  F5tTokenizeCtx ctx;

  if( objc!=4 && objc!=5 ){
    Tcl_WrongNumArgs(interp, 1, objv, "?-subst? DB NAME TEXT");
    return TCL_ERROR;
  }
  if( objc==5 ){
    char *zOpt = Tcl_GetString(objv[1]);
    if( strcmp("-subst", zOpt) ){
      Tcl_AppendResult(interp, "unrecognized option: ", zOpt, (char*)0);
      return TCL_ERROR;
    }
  }
  if( f5tDbAndApi(interp, objv[objc-3], &db, &pApi) ) return TCL_ERROR;
  if( Tcl_SplitList(interp, Tcl_GetString(objv[objc-2]), &nArg, &azArg) ){
    return TCL_ERROR;
  }
  if( nArg==0 ){
    Tcl_AppendResult(interp, "no such tokenizer: ", (char*)0);
    Tcl_Free((void*)azArg);
    return TCL_ERROR;
  }
  zText = Tcl_GetStringFromObj(objv[objc-1], &nText);

  rc = pApi->xFindTokenizer(pApi, azArg[0], &pUserdata, &tokenizer);
  if( rc!=SQLITE_OK ){
    Tcl_AppendResult(interp, "no such tokenizer: ", azArg[0], (char*)0);
    return TCL_ERROR;
  }

  rc = tokenizer.xCreate(pUserdata, &azArg[1], (int)(nArg-1), &pTok);
  if( rc!=SQLITE_OK ){
    Tcl_AppendResult(interp, "error in tokenizer.xCreate()", (char*)0);
    return TCL_ERROR;
  }

  if( nText>0 ){
    pCopy = sqlite3_malloc(nText);
    if( pCopy==0 ){
      tokenizer.xDelete(pTok);
      Tcl_AppendResult(interp, "error in sqlite3_malloc()", (char*)0);
      return TCL_ERROR;
    }else{
      memcpy(pCopy, zText, nText);
    }
  }

  pRet = Tcl_NewObj();
  Tcl_IncrRefCount(pRet);
  ctx.bSubst = (objc==5);
  ctx.pRet = pRet;
  ctx.zInput = pCopy;
  rc = tokenizer.xTokenize(
      pTok, (void*)&ctx, FTS5_TOKENIZE_DOCUMENT, pCopy,(int)nText, xTokenizeCb2
  );
  tokenizer.xDelete(pTok);
  sqlite3_free(pCopy);
  if( rc!=SQLITE_OK ){
    Tcl_AppendResult(interp, "error in tokenizer.xTokenize()", (char*)0);
    Tcl_DecrRefCount(pRet);
    return TCL_ERROR;
  }

  Tcl_Free((void*)azArg);
  Tcl_SetObjResult(interp, pRet);
  Tcl_DecrRefCount(pRet);
  return TCL_OK;
}

/*************************************************************************
** Start of tokenizer wrapper.
*/

typedef struct F5tTokenizerContext F5tTokenizerContext;
typedef struct F5tTokenizerCb F5tTokenizerCb;
typedef struct F5tTokenizerModule F5tTokenizerModule;
typedef struct F5tTokenizerInstance F5tTokenizerInstance;

struct F5tTokenizerContext {
  void *pCtx;
  int (*xToken)(void*, int, const char*, int, int, int);
  F5tTokenizerInstance *pInst;
};

struct F5tTokenizerModule {
  Tcl_Interp *interp;
  Tcl_Obj *pScript;
  void *pParentCtx;
  fts5_tokenizer_v2 parent_v2;
  fts5_tokenizer parent;
  F5tTokenizerContext *pContext;
};

/*
** zLocale:
**   Within a call to xTokenize_v2(), pLocale/nLocale store the locale
**   passed to the call by fts5. This can be retrieved by a Tcl tokenize 
**   script using [sqlite3_fts5_locale].
*/
struct F5tTokenizerInstance {
  Tcl_Interp *interp;
  Tcl_Obj *pScript;
  F5tTokenizerModule *pModule;
  Fts5Tokenizer *pParent;
  F5tTokenizerContext *pContext;
  const char *pLocale;
  int nLocale;
};

static int f5tTokenizerCreate(
  void *pCtx, 
  const char **azArg, 
  int nArg, 
  Fts5Tokenizer **ppOut
){
  Fts5Tokenizer *pParent = 0;
  F5tTokenizerModule *pMod = (F5tTokenizerModule*)pCtx;
  Tcl_Obj *pEval;
  int rc = TCL_OK;
  int i;

  assert( pMod->parent_v2.xCreate==0 || pMod->parent.xCreate==0 );
  if( pMod->parent_v2.xCreate ){
    rc = pMod->parent_v2.xCreate(pMod->pParentCtx, 0, 0, &pParent);
  }
  if( pMod->parent.xCreate ){
    rc = pMod->parent.xCreate(pMod->pParentCtx, 0, 0, &pParent);
  }

  pEval = Tcl_DuplicateObj(pMod->pScript);
  Tcl_IncrRefCount(pEval);
  for(i=0; rc==TCL_OK && i<nArg; i++){
    Tcl_Obj *pObj = Tcl_NewStringObj(azArg[i], -1);
    rc = Tcl_ListObjAppendElement(pMod->interp, pEval, pObj);
  }

  if( rc==TCL_OK ){
    rc = Tcl_EvalObjEx(pMod->interp, pEval, TCL_GLOBAL_ONLY);
  }
  Tcl_DecrRefCount(pEval);

  if( rc==TCL_OK ){
    F5tTokenizerInstance *pInst;
    pInst = (F5tTokenizerInstance*)ckalloc(sizeof(F5tTokenizerInstance));
    memset(pInst, 0, sizeof(F5tTokenizerInstance));
    pInst->interp = pMod->interp;
    pInst->pScript = Tcl_GetObjResult(pMod->interp);
    pInst->pContext = pMod->pContext;
    pInst->pParent = pParent;
    pInst->pModule = pMod;
    Tcl_IncrRefCount(pInst->pScript);
    *ppOut = (Fts5Tokenizer*)pInst;
  }

  return rc;
}


static void f5tTokenizerDelete(Fts5Tokenizer *p){
  F5tTokenizerInstance *pInst = (F5tTokenizerInstance*)p;
  if( pInst ){
    if( pInst->pParent ){
      if( pInst->pModule->parent_v2.xDelete ){
        pInst->pModule->parent_v2.xDelete(pInst->pParent);
      }else{
        pInst->pModule->parent.xDelete(pInst->pParent);
      }
    }
    Tcl_DecrRefCount(pInst->pScript);
    ckfree((char *)pInst);
  }
}


static int f5tTokenizerReallyTokenize(
  Fts5Tokenizer *p, 
  void *pCtx,
  int flags,
  const char *pText, int nText, 
  int (*xToken)(void*, int, const char*, int, int, int)
){
  F5tTokenizerInstance *pInst = (F5tTokenizerInstance*)p;
  F5tTokenizerInstance *pOldInst = 0;
  void *pOldCtx;
  int (*xOldToken)(void*, int, const char*, int, int, int);
  Tcl_Obj *pEval;
  int rc;
  const char *zFlags;

  pOldCtx = pInst->pContext->pCtx;
  xOldToken = pInst->pContext->xToken;
  pOldInst = pInst->pContext->pInst;

  pInst->pContext->pCtx = pCtx;
  pInst->pContext->xToken = xToken;
  pInst->pContext->pInst = pInst;

  assert( 
      flags==FTS5_TOKENIZE_DOCUMENT
   || flags==FTS5_TOKENIZE_AUX
   || flags==FTS5_TOKENIZE_QUERY
   || flags==(FTS5_TOKENIZE_QUERY | FTS5_TOKENIZE_PREFIX)
  );
  pEval = Tcl_DuplicateObj(pInst->pScript);
  Tcl_IncrRefCount(pEval);
  switch( flags ){
    case FTS5_TOKENIZE_DOCUMENT:
      zFlags = "document";
      break;
    case FTS5_TOKENIZE_AUX:
      zFlags = "aux";
      break;
    case FTS5_TOKENIZE_QUERY:
      zFlags = "query";
      break;
    case (FTS5_TOKENIZE_PREFIX | FTS5_TOKENIZE_QUERY):
      zFlags = "prefixquery";
      break;
    default:
      assert( 0 );
      zFlags = "invalid";
      break;
  }

  Tcl_ListObjAppendElement(pInst->interp, pEval, Tcl_NewStringObj(zFlags, -1));
  Tcl_ListObjAppendElement(pInst->interp, pEval, Tcl_NewStringObj(pText,nText));
  rc = Tcl_EvalObjEx(pInst->interp, pEval, TCL_GLOBAL_ONLY);
  Tcl_DecrRefCount(pEval);

  pInst->pContext->pCtx = pOldCtx;
  pInst->pContext->xToken = xOldToken;
  pInst->pContext->pInst = pOldInst;
  return rc;
}

typedef struct CallbackCtx CallbackCtx;
struct CallbackCtx {
  Fts5Tokenizer *p;
  void *pCtx;
  int flags;
  int (*xToken)(void*, int, const char*, int, int, int);
};

static int f5tTokenizeCallback(
  void *pCtx, 
  int tflags, 
  const char *z, int n, 
  int iStart, int iEnd
){
  CallbackCtx *p = (CallbackCtx*)pCtx;
  return f5tTokenizerReallyTokenize(p->p, p->pCtx, p->flags, z, n, p->xToken);
}

static int f5tTokenizerTokenize_v2(
  Fts5Tokenizer *p, 
  void *pCtx,
  int flags,
  const char *pText, int nText, 
  const char *pLoc, int nLoc, 
  int (*xToken)(void*, int, const char*, int, int, int)
){
  int rc = SQLITE_OK;
  F5tTokenizerInstance *pInst = (F5tTokenizerInstance*)p;

  pInst->pLocale = pLoc;
  pInst->nLocale = nLoc;

  if( pInst->pParent ){
    CallbackCtx ctx;
    ctx.p = p;
    ctx.pCtx = pCtx;
    ctx.flags = flags;
    ctx.xToken = xToken;
    if( pInst->pModule->parent_v2.xTokenize ){
      rc = pInst->pModule->parent_v2.xTokenize(
          pInst->pParent, (void*)&ctx, flags, pText, nText, 
          pLoc, nLoc, f5tTokenizeCallback
      );
    }else{
      rc = pInst->pModule->parent.xTokenize(
          pInst->pParent, (void*)&ctx, flags, pText, nText, f5tTokenizeCallback
      );
    }
  }else{
    rc = f5tTokenizerReallyTokenize(p, pCtx, flags, pText, nText, xToken);
  }

  pInst->pLocale = 0;
  pInst->nLocale = 0;
  return rc;
}
static int f5tTokenizerTokenize(
  Fts5Tokenizer *p, 
  void *pCtx,
  int flags,
  const char *pText, int nText, 
  int (*xToken)(void*, int, const char*, int, int, int)
){
  return f5tTokenizerTokenize_v2(p, pCtx, flags, pText, nText, 0, 0, xToken);
}

/*
** sqlite3_fts5_locale 
*/
static int SQLITE_TCLAPI f5tTokenizerLocale(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  F5tTokenizerContext *p = (F5tTokenizerContext*)clientData;

  if( objc!=1 ){
    Tcl_WrongNumArgs(interp, 1, objv, "");
    return TCL_ERROR;
  }

  if( p->xToken==0 ){
    Tcl_AppendResult(interp, 
        "sqlite3_fts5_locale may only be used by tokenizer callback", (char*)0
    );
    return TCL_ERROR;
  }

  Tcl_SetObjResult(interp, 
      Tcl_NewStringObj(p->pInst->pLocale, p->pInst->nLocale)
  );
  return TCL_OK;
}

/*
** sqlite3_fts5_token ?-colocated? TEXT START END
*/
static int SQLITE_TCLAPI f5tTokenizerReturn(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  F5tTokenizerContext *p = (F5tTokenizerContext*)clientData;
  int iStart;
  int iEnd;
  Tcl_Size nToken;
  int tflags = 0;
  char *zToken;
  int rc;

  if( objc==5 ){
    Tcl_Size nArg;
    char *zArg = Tcl_GetStringFromObj(objv[1], &nArg);
    if( nArg<=10 && nArg>=2 && memcmp("-colocated", zArg, nArg)==0 ){
      tflags |= FTS5_TOKEN_COLOCATED;
    }else{
      goto usage;
    }
  }else if( objc!=4 ){
    goto usage;
  }

  zToken = Tcl_GetStringFromObj(objv[objc-3], &nToken);
  if( Tcl_GetIntFromObj(interp, objv[objc-2], &iStart) 
   || Tcl_GetIntFromObj(interp, objv[objc-1], &iEnd) 
  ){
    return TCL_ERROR;
  }

  if( p->xToken==0 ){
    Tcl_AppendResult(interp, 
        "sqlite3_fts5_token may only be used by tokenizer callback", (char*)0
    );
    return TCL_ERROR;
  }

  rc = p->xToken(p->pCtx, tflags, zToken, (int)nToken, iStart, iEnd);
  Tcl_SetResult(interp, (char*)sqlite3ErrName(rc), TCL_VOLATILE);
  return rc==SQLITE_OK ? TCL_OK : TCL_ERROR;

 usage:
  Tcl_WrongNumArgs(interp, 1, objv, "?-colocated? TEXT START END");
  return TCL_ERROR;
}

static void f5tDelTokenizer(void *pCtx){
  F5tTokenizerModule *pMod = (F5tTokenizerModule*)pCtx;
  Tcl_DecrRefCount(pMod->pScript);
  ckfree((char *)pMod);
}

/*
**      sqlite3_fts5_create_tokenizer DB NAME SCRIPT
**
** Register a tokenizer named NAME implemented by script SCRIPT. When
** a tokenizer instance is created (fts5_tokenizer.xCreate), any tokenizer
** arguments are appended to SCRIPT and the result executed.
**
** The value returned by (SCRIPT + args) is itself a tcl script. This 
** script - call it SCRIPT2 - is executed to tokenize text using the
** tokenizer instance "returned" by SCRIPT. Specifically, to tokenize
** text SCRIPT2 is invoked with a single argument appended to it - the
** text to tokenize.
**
** SCRIPT2 should invoke the [sqlite3_fts5_token] command once for each
** token within the tokenized text.
*/
static int SQLITE_TCLAPI f5tCreateTokenizer(
  ClientData clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  F5tTokenizerContext *pContext = (F5tTokenizerContext*)clientData;
  sqlite3 *db;
  fts5_api *pApi;
  char *zName;
  Tcl_Obj *pScript;
  F5tTokenizerModule *pMod;
  int rc = SQLITE_OK;
  int bV2 = 0;                    /* True to use _v2 API */
  int iVersion = 2;               /* Value for _v2.iVersion */
  const char *zParent = 0;        /* Name of parent tokenizer, if any */
  int ii = 0;

  if( objc<4 ){
    Tcl_WrongNumArgs(interp, 1, objv, "?OPTIONS? DB NAME SCRIPT");
    return TCL_ERROR;
  }

  /* Parse any options. Set stack variables bV2 and zParent. */
  for(ii=1; ii<objc-3; ii++){
    int iOpt = 0;
    const char *azOpt[] = { "-v2", "-parent", "-version", 0 };
    if( Tcl_GetIndexFromObj(interp, objv[ii], azOpt, "OPTION", 0, &iOpt) ){
      return TCL_ERROR;
    }
    switch( iOpt ){
      case 0: /* -v2 */ {
        bV2 = 1;
        break;
      }
      case 1: /* -parent */ {
        ii++;
        if( ii==objc-3 ){
          Tcl_AppendResult(
              interp, "option requires an argument: -parent", (char*)0
          );
          return TCL_ERROR;
        }
        zParent = Tcl_GetString(objv[ii]);
        break;
      }
      case 2: /* -version */ {
        ii++;
        if( ii==objc-3 ){
          Tcl_AppendResult(
              interp, "option requires an argument: -version", (char*)0
          );
          return TCL_ERROR;
        }
        if( Tcl_GetIntFromObj(interp, objv[ii], &iVersion) ){
          return TCL_ERROR;
        }
        break;
      }
      default:
        assert( 0 );
        break;
    }
  }

  if( f5tDbAndApi(interp, objv[objc-3], &db, &pApi) ){
    return TCL_ERROR;
  }
  zName = Tcl_GetString(objv[objc-2]);
  pScript = objv[objc-1];

  pMod = (F5tTokenizerModule*)ckalloc(sizeof(F5tTokenizerModule));
  memset(pMod, 0, sizeof(F5tTokenizerModule));
  pMod->interp = interp;
  pMod->pScript = pScript;
  Tcl_IncrRefCount(pScript);
  pMod->pContext = pContext;
  if( zParent ){
    if( bV2 ){
      fts5_tokenizer_v2 *pParent = 0;
      rc = pApi->xFindTokenizer_v2(pApi, zParent, &pMod->pParentCtx, &pParent);
      if( rc==SQLITE_OK ){
        memcpy(&pMod->parent_v2, pParent, sizeof(fts5_tokenizer_v2));
        pMod->parent_v2.xDelete(0);
      }
    }else{
      rc = pApi->xFindTokenizer(pApi, zParent, &pMod->pParentCtx,&pMod->parent);
      if( rc==SQLITE_OK ){
        pMod->parent.xDelete(0);
      }
    }
  }

  if( rc==SQLITE_OK ){
    void *pModCtx = (void*)pMod;
    if( bV2==0 ){
      fts5_tokenizer t;
      t.xCreate = f5tTokenizerCreate;
      t.xTokenize = f5tTokenizerTokenize;
      t.xDelete = f5tTokenizerDelete;
      rc = pApi->xCreateTokenizer(pApi, zName, pModCtx, &t, f5tDelTokenizer);
    }else{
      fts5_tokenizer_v2 t2;
      memset(&t2, 0, sizeof(t2));
      t2.iVersion = iVersion;
      t2.xCreate = f5tTokenizerCreate;
      t2.xTokenize = f5tTokenizerTokenize_v2;
      t2.xDelete = f5tTokenizerDelete;
      rc = pApi->xCreateTokenizer_v2(pApi, zName, pModCtx, &t2,f5tDelTokenizer);
    }
  }

  if( rc!=SQLITE_OK ){
    Tcl_AppendResult(interp, (
          bV2 ? "error in fts5_api.xCreateTokenizer_v2()"
          : "error in fts5_api.xCreateTokenizer()"
    ), (char*)0);
    return TCL_ERROR;
  }

  return TCL_OK;
}

static void SQLITE_TCLAPI xF5tFree(ClientData clientData){
  ckfree(clientData);
}

/*
**      sqlite3_fts5_may_be_corrupt BOOLEAN
**
** Set or clear the global "may-be-corrupt" flag. Return the old value.
*/
static int SQLITE_TCLAPI f5tMayBeCorrupt(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
#ifdef SQLITE_DEBUG
  int bOld = sqlite3_fts5_may_be_corrupt;

  if( objc!=2 && objc!=1 ){
    Tcl_WrongNumArgs(interp, 1, objv, "?BOOLEAN?");
    return TCL_ERROR;
  }
  if( objc==2 ){
    int bNew;
    if( Tcl_GetBooleanFromObj(interp, objv[1], &bNew) ) return TCL_ERROR;
    sqlite3_fts5_may_be_corrupt = bNew;
  }

  Tcl_SetObjResult(interp, Tcl_NewIntObj(bOld));
#endif
  return TCL_OK;
}


static unsigned int f5t_fts5HashKey(int nSlot, const char *p, int n){
  int i;
  unsigned int h = 13;
  for(i=n-1; i>=0; i--){
    h = (h << 3) ^ h ^ p[i];
  }
  return (h % nSlot);
}

static int SQLITE_TCLAPI f5tTokenHash(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  char *z;
  Tcl_Size n;
  unsigned int iVal;
  int nSlot;

  if( objc!=3 ){
    Tcl_WrongNumArgs(interp, 1, objv, "NSLOT TOKEN");
    return TCL_ERROR;
  }
  if( Tcl_GetIntFromObj(interp, objv[1], &nSlot) ){
    return TCL_ERROR;
  }
  z = Tcl_GetStringFromObj(objv[2], &n);

  iVal = f5t_fts5HashKey(nSlot, z, (int)n);
  Tcl_SetObjResult(interp, Tcl_NewIntObj(iVal));
  return TCL_OK;
}

static int SQLITE_TCLAPI f5tRegisterMatchinfo(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  int rc;
  sqlite3 *db = 0;

  if( objc!=2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "DB");
    return TCL_ERROR;
  }
  if( f5tDbPointer(interp, objv[1], &db) ){
    return TCL_ERROR;
  }

  rc = sqlite3Fts5TestRegisterMatchinfo(db);
  if( rc!=SQLITE_OK ){
    Tcl_SetResult(interp, (char*)sqlite3ErrName(rc), TCL_VOLATILE);
    return TCL_ERROR;
  }
  return TCL_OK;
}

static int SQLITE_TCLAPI f5tRegisterTok(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  int rc;
  sqlite3 *db = 0;
  fts5_api *pApi = 0;

  if( objc!=2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "DB");
    return TCL_ERROR;
  }
  if( f5tDbAndApi(interp, objv[1], &db, &pApi) ){
    return TCL_ERROR;
  }

  rc = sqlite3Fts5TestRegisterTok(db, pApi);
  if( rc!=SQLITE_OK ){
    Tcl_SetResult(interp, (char*)sqlite3ErrName(rc), TCL_VOLATILE);
    return TCL_ERROR;
  }
  return TCL_OK;
}

typedef struct OriginTextCtx OriginTextCtx;
struct OriginTextCtx {
  sqlite3 *db;
  fts5_api *pApi;
};

typedef struct OriginTextTokenizer OriginTextTokenizer;
struct OriginTextTokenizer {
  Fts5Tokenizer *pTok;            /* Underlying tokenizer object */
  fts5_tokenizer tokapi;          /* API implementation for pTok */
};

/*
** Delete the OriginTextCtx object indicated by the only argument.
*/
static void f5tOrigintextTokenizerDelete(void *pCtx){
  OriginTextCtx *p = (OriginTextCtx*)pCtx;
  ckfree((char*)p);
}

static int f5tOrigintextCreate(
  void *pCtx, 
  const char **azArg, 
  int nArg, 
  Fts5Tokenizer **ppOut
){
  OriginTextCtx *p = (OriginTextCtx*)pCtx;
  OriginTextTokenizer *pTok = 0;
  void *pTokCtx = 0;
  int rc = SQLITE_OK;

  pTok = (OriginTextTokenizer*)sqlite3_malloc(sizeof(OriginTextTokenizer));
  if( pTok==0 ){
    rc = SQLITE_NOMEM;
  }else if( nArg<1 ){
    rc = SQLITE_ERROR;
  }else{
    /* Locate the underlying tokenizer */
    rc = p->pApi->xFindTokenizer(p->pApi, azArg[0], &pTokCtx, &pTok->tokapi);
  }

  /* Create the new tokenizer instance */
  if( rc==SQLITE_OK ){
    rc = pTok->tokapi.xCreate(pTokCtx, &azArg[1], nArg-1, &pTok->pTok);
  }

  if( rc!=SQLITE_OK ){
    sqlite3_free(pTok);
    pTok = 0;
  }
  *ppOut = (Fts5Tokenizer*)pTok;
  return rc;
}

static void f5tOrigintextDelete(Fts5Tokenizer *pTokenizer){
  OriginTextTokenizer *p = (OriginTextTokenizer*)pTokenizer;
  if( p->pTok ){
    p->tokapi.xDelete(p->pTok);
  }
  sqlite3_free(p);
}

typedef struct OriginTextCb OriginTextCb;
struct OriginTextCb {
  void *pCtx;
  const char *pText; 
  int nText;
  int (*xToken)(void *, int, const char *, int, int, int);

  char *aBuf;                     /* Buffer to use */
  int nBuf;                       /* Allocated size of aBuf[] */
};

static int xOriginToken(
  void *pCtx,                     /* Copy of 2nd argument to xTokenize() */
  int tflags,                     /* Mask of FTS5_TOKEN_* flags */
  const char *pToken,             /* Pointer to buffer containing token */
  int nToken,                     /* Size of token in bytes */
  int iStart,                     /* Byte offset of token within input text */
  int iEnd                        /* Byte offset of end of token within input */
){
  OriginTextCb *p = (OriginTextCb*)pCtx;
  int ret = 0;

  if( nToken==(iEnd-iStart) && 0==memcmp(pToken, &p->pText[iStart], nToken) ){
    /* Token exactly matches document text. Pass it through as is. */
    ret = p->xToken(p->pCtx, tflags, pToken, nToken, iStart, iEnd);
  }else{
    int nReq = nToken + 1 + (iEnd-iStart);
    if( nReq>p->nBuf ){
      sqlite3_free(p->aBuf);
      p->aBuf = sqlite3_malloc(nReq*2);
      if( p->aBuf==0 ) return SQLITE_NOMEM;
      p->nBuf = nReq*2;
    }

    memcpy(p->aBuf, pToken, nToken);
    p->aBuf[nToken] = '\0';
    memcpy(&p->aBuf[nToken+1], &p->pText[iStart], iEnd-iStart);
    ret = p->xToken(p->pCtx, tflags, p->aBuf, nReq, iStart, iEnd);
  }

  return ret;
}


static int f5tOrigintextTokenize(
  Fts5Tokenizer *pTokenizer, 
  void *pCtx,
  int flags,                      /* Mask of FTS5_TOKENIZE_* flags */
  const char *pText, int nText, 
  int (*xToken)(void *, int, const char *, int, int, int)
){
  OriginTextTokenizer *p = (OriginTextTokenizer*)pTokenizer;
  OriginTextCb cb;
  int ret;

  memset(&cb, 0, sizeof(cb));
  cb.pCtx = pCtx;
  cb.pText = pText;
  cb.nText = nText;
  cb.xToken = xToken;

  ret = p->tokapi.xTokenize(p->pTok,(void*)&cb,flags,pText,nText,xOriginToken);
  sqlite3_free(cb.aBuf);
  return ret;
}

/*
**      sqlite3_fts5_register_origintext DB
**
** Description...
*/
static int SQLITE_TCLAPI f5tRegisterOriginText(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  sqlite3 *db = 0;
  fts5_api *pApi = 0;
  int rc;
  fts5_tokenizer tok = {0, 0, 0};
  OriginTextCtx *pCtx = 0;

  if( objc!=2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "DB");
    return TCL_ERROR;
  }
  if( f5tDbAndApi(interp, objv[1], &db, &pApi) ) return TCL_ERROR;

  pCtx = (OriginTextCtx*)ckalloc(sizeof(OriginTextCtx));
  pCtx->db = db;
  pCtx->pApi = pApi;

  tok.xCreate = f5tOrigintextCreate;
  tok.xDelete = f5tOrigintextDelete;
  tok.xTokenize = f5tOrigintextTokenize;
  rc = pApi->xCreateTokenizer(
      pApi, "origintext", (void*)pCtx, &tok, f5tOrigintextTokenizerDelete
  );

  Tcl_ResetResult(interp);
  if( rc!=SQLITE_OK ){
    Tcl_AppendResult(interp, "error: ", sqlite3_errmsg(db), (void*)0);
    return TCL_ERROR;
  }
  return TCL_OK;
}

/*
** This function is used to DROP an fts5 table. It works even if the data
** structures fts5 stores within the database are corrupt, which sometimes
** prevents a straight "DROP TABLE" command from succeeding.
**
** The first parameter is the database handle to use for the DROP TABLE
** operation. The second is the name of the database to drop the fts5 table
** from (i.e. "main", "temp" or the name of an attached database). The
** third parameter is the name of the fts5 table to drop.
**
** SQLITE_OK is returned if the table is successfully dropped. Or, if an
** error occurs, an SQLite error code.
*/
static int sqlite3_fts5_drop_corrupt_table(
  sqlite3 *db,                    /* Database handle */
  const char *zDb,                /* Database name ("main", "temp" etc.) */
  const char *zTab                /* Name of fts5 table to drop */
){
  int rc = SQLITE_OK;
  int bDef = 0;

  rc = sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, -1, &bDef);
  if( rc==SQLITE_OK ){
    char *zScript = sqlite3_mprintf(
        "DELETE FROM %Q.'%q_data';"
        "DELETE FROM %Q.'%q_config';"
        "INSERT INTO %Q.'%q_data' VALUES(10, X'0000000000');"
        "INSERT INTO %Q.'%q_config' VALUES('version', 4);"
        "DROP TABLE %Q.'%q';",
        zDb, zTab, zDb, zTab, zDb, zTab, zDb, zTab, zDb, zTab
    );

    if( zScript==0 ){
      rc = SQLITE_NOMEM;
    }else{
      if( bDef ) sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 0, 0);
      rc = sqlite3_exec(db, zScript, 0, 0, 0);
      if( bDef ) sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, 0);
      sqlite3_free(zScript);
    }
  }

  return rc;
}

/*
**      sqlite3_fts5_drop_corrupt_table DB DATABASE TABLE
**
** Description...
*/
static int SQLITE_TCLAPI f5tDropCorruptTable(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  sqlite3 *db = 0;
  const char *zDb = 0;
  const char *zTab = 0;
  int rc = SQLITE_OK;

  if( objc!=4 ){
    Tcl_WrongNumArgs(interp, 1, objv, "DB DATABASE TABLE");
    return TCL_ERROR;
  }
  if( f5tDbPointer(interp, objv[1], &db) ){
    return TCL_ERROR;
  }
  zDb = Tcl_GetString(objv[2]);
  zTab = Tcl_GetString(objv[3]);

  rc = sqlite3_fts5_drop_corrupt_table(db, zDb, zTab);
  if( rc!=SQLITE_OK ){
    Tcl_AppendResult(interp, "error: ", sqlite3_errmsg(db), (void*)0);
    return TCL_ERROR;
  }

  return TCL_OK;
}

/*
** Free a buffer returned to SQLite by the str() function.
*/
void f5tFree(void *p){
  char *x = (char *)p;
  ckfree(&x[-8]);
}

/*
** Implementation of str().
*/
void f5tStrFunc(sqlite3_context *pCtx, int nArg, sqlite3_value **apArg){
  const char *zText = 0;
  assert( nArg==1 );

  zText = (const char*)sqlite3_value_text(apArg[0]);
  if( zText ){
    sqlite3_int64 nText = strlen(zText);
    char *zCopy = (char*)ckalloc(nText+8);
    if( zCopy==0 ){
      sqlite3_result_error_nomem(pCtx);
    }else{
      zCopy += 8;
      memcpy(zCopy, zText, nText);
      sqlite3_result_text64(pCtx, zCopy, nText, f5tFree, SQLITE_UTF8);
    }
  }
}

/*
**      sqlite3_fts5_register_str DB
**
** Register the str() function with database handle DB. str() interprets
** its only argument as text and returns a copy of the value in a
** non-nul-terminated buffer.
*/
static int SQLITE_TCLAPI f5tRegisterStr(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  sqlite3 *db = 0;

  if( objc!=2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "DB");
    return TCL_ERROR;
  }
  if( f5tDbPointer(interp, objv[1], &db) ){
    return TCL_ERROR;
  }

  sqlite3_create_function(db, "str", 1, SQLITE_UTF8, 0, f5tStrFunc, 0, 0);

  return TCL_OK;
}

/*
** Entry point.
*/
int Fts5tcl_Init(Tcl_Interp *interp){
  static struct Cmd {
    char *zName;
    Tcl_ObjCmdProc *xProc;
    int bTokenizeCtx;
  } aCmd[] = {
    { "sqlite3_fts5_create_tokenizer",   f5tCreateTokenizer, 1 },
    { "sqlite3_fts5_token",              f5tTokenizerReturn, 1 },
    { "sqlite3_fts5_locale",             f5tTokenizerLocale, 1 },
    { "sqlite3_fts5_tokenize",           f5tTokenize, 0 },
    { "sqlite3_fts5_create_function",    f5tCreateFunction, 0 },
    { "sqlite3_fts5_may_be_corrupt",     f5tMayBeCorrupt, 0 },
    { "sqlite3_fts5_token_hash",         f5tTokenHash, 0 },
    { "sqlite3_fts5_register_matchinfo", f5tRegisterMatchinfo, 0 },
    { "sqlite3_fts5_register_fts5tokenize", f5tRegisterTok, 0 },
    { "sqlite3_fts5_register_origintext",f5tRegisterOriginText, 0 },
    { "sqlite3_fts5_drop_corrupt_table", f5tDropCorruptTable, 0 },
    { "sqlite3_fts5_register_str",       f5tRegisterStr, 0 }
  };
  int i;
  F5tTokenizerContext *pContext;

  pContext = (F5tTokenizerContext*)ckalloc(sizeof(F5tTokenizerContext));
  memset(pContext, 0, sizeof(*pContext));

  for(i=0; i<sizeof(aCmd)/sizeof(aCmd[0]); i++){
    struct Cmd *p = &aCmd[i];
    void *pCtx = 0;
    if( p->bTokenizeCtx ) pCtx = (void*)pContext;
    Tcl_CreateObjCommand(interp, p->zName, p->xProc, pCtx, (i ? 0 : xF5tFree));
  }

  return TCL_OK;
}
#else  /* SQLITE_ENABLE_FTS5 */
int Fts5tcl_Init(Tcl_Interp *interp){
  return TCL_OK;
}
#endif /* SQLITE_ENABLE_FTS5 */
#endif /* SQLITE_TEST */
