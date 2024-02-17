/*
** 2024-02-08
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
*/

#ifndef _SQLITE_INTCK_H
#define _SQLITE_INTCK_H

#include "sqlite3.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sqlite3_intck sqlite3_intck;

int sqlite3_intck_open(
  sqlite3 *db, 
  const char *zDb, 
  const char *zFile, 
  sqlite3_intck **ppOut
);

void sqlite3_intck_close(sqlite3_intck*);

int sqlite3_intck_step(sqlite3_intck *pCk);

const char *sqlite3_intck_message(sqlite3_intck *pCk);

int sqlite3_intck_error(sqlite3_intck *pCk, const char **pzErr);

int sqlite3_intck_suspend(sqlite3_intck *pCk);

/*
** This API is used for testing only. It returns the full-text of an SQL
** statement used to test object zObj, which may be a table or index.
** The returned buffer is valid until the next call to either this function
** or sqlite3_intck_close() on the same sqlite3_intck handle.
*/
const char *sqlite3_intck_test_sql(sqlite3_intck *pCk, const char *zObj);


#ifdef __cplusplus
}  /* end of the 'extern "C"' block */
#endif

#endif /* ifndef _SQLITE_INTCK_H */
