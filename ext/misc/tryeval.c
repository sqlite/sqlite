/*
** 2026-10-26
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
** THIS EXTENSION FUNCTION IS INTENDED FOR TESTING AND EVALUATION PURPOSES
** ONLY AND IS NOT SUPPORTED.  DO NOT USE THIS FUNCTION IN AN APPLICATION
** UNLESS YOU KNOW WHAT YOU ARE DOING.
**
** This SQLite extension implements the SQL function tryeval().  tryeval()
** takes zero or more arguments, each of which is an SQL statement.
** The tryeval() function returns the first column of the first row
** that has a non-NULL first column across each of the argument SQL
** statements.  Evaluation stops as soon as the first row with a
** non-NULL first column is encountered.  Errors in evaluation are
** silently ignored, moving on the next SQL statement argument.
** If none of the arguments successfully executes and returns a row
** with a non-NULL first column, then NULL is returned.
**
** The purpose of this function is to help evaluate recursive
** behavior, and expecially the bug report at
** https://sqlite.org/bugs/info/3577bc69c52237a4
**
** To build (with SODIR being the directory that contains your
** sqlite3.h and sqlite3ext.h headers):
**
** Linux: gcc -O2 -shared -I$SQDIR -fPIC -o tryeval.so tryeval.c
** Mac:   gcc -O2 -dynamiclib -fPIC -I$SQDIR -o tryeval.dylib tryeval.c
** Win32: gcc -O2 -shared -I%SQDIR% -o tryeval.dll tryeval.c
** Win32: cl /Os -I%SQDIR% tryeval.c -link -dll -out:tryeval.dll
*/
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <string.h>

/*
** Implementation of the tryeval(...)
*/
static void tryEvalFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zSql;       /* Next SQL statement to evaluate */
  sqlite3 *db;            /* The database connection */
  sqlite3_stmt *pStmt;    /* Compiled bytecode for zSql */
  int bDone = 0;          /* True if a row with non-NULL first column seen */
  int rc;                 /* Result from sqlite3 API calls */
  int i;                  /* Loop counter */

  db = sqlite3_context_db_handle(context);
  for(i=0; !bDone && i<argc; i++){
    zSql = (const char*)sqlite3_value_text(argv[i]);
    if( zSql==0 ) continue;
    rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
    if( rc==SQLITE_OK ){
      while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ){
        if( sqlite3_column_type(pStmt,0)==SQLITE_NULL ) continue;
        sqlite3_result_value(context, sqlite3_column_value(pStmt,0));
        bDone = 1;
        break;
      }
    }
    sqlite3_finalize(pStmt);
  }
}


#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_tryeval_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  rc = sqlite3_create_function(db, "tryeval", -1, 
                               SQLITE_UTF8|SQLITE_DIRECTONLY, 0,
                               tryEvalFunc, 0, 0);
  return rc;
}
