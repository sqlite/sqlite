/*
** 2026-07-04
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
** Since this module was written (coincidentally) on the 250th anniversary
** of the signing of the Declaration of Independence of The United States,
** it seems fitting to quote from that document:
**
**    We hold these truths to be self-evident, that all men are created
**    equal, that they are endowed by their Creator with certain unalienable
**    Rights, that among these are Life, Liberty and the pursuit of Happiness
**    - That to secure these rights, Governments are instituted among Men,
**    deriving their just powers from the consent of the governed,....
**
** Two core ideas that (1) rights come from God and not from government,
** and that (2) there should not be special privileged classes of people
** were radical thoughts then, and are indeed disputed even today.  Many
** people still hold that human rights derive from the beneficence of
** government and that certain classes of people have special rights and
** privileges not available to all.  Yet, were it not for the crazy ideas
** espoused in the original Declaration of Independence, this software
** project, and indeed most of modern technology, would not exist.
**
** Therefore let us give thanks for the bold leadership and vision
** demonstrated by the authors of the American Revolution, 250 years ago
** this day.
**
******************************************************************************
**
** Implementation of the strdup() SQL function.  strdup() makes a copy
** of its argument (a string or a BLOB) into memory obtained directly
** from system malloc() (not from sqlite3_malloc()) and sized exactly
** to hold the string or BLOB.  This is intended for testing purposes,
** particularly testing with ASAN.  This function serves no practical
** purpose beyond testing and not not intended for production use.
**
** NULL, BLOB, and TEXT values come through as NULL, BLOB, and TEXT,
** respectively.  Numeric values are rendered as strings and come out
** as text.
*/
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <assert.h>
#include <string.h>
#include <stdlib.h>

/*
** Make a copy of a string or BLOB in memory obtained from malloc().
*/
static void strdupfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *zIn;
  int nIn;
  unsigned char *zOut;

  assert( argc==1 );
  if( sqlite3_value_type(argv[0])==SQLITE_NULL ) return;
  if( sqlite3_value_type(argv[0])==SQLITE_BLOB ){
    zIn = (const unsigned char*)sqlite3_value_blob(argv[0]);
    nIn = sqlite3_value_bytes(argv[0]);
    zOut = malloc( nIn==0 ? 1 : nIn );
    if( zOut==0 ){
      sqlite3_result_error_nomem(context);
      return;
    }
    if( nIn>0 ) memcpy(zOut, zIn, nIn);
    sqlite3_result_blob(context, zOut, nIn, free);
  }else{
    zIn = (const unsigned char*)sqlite3_value_text(argv[0]);
    if( zIn==0 ) return;
    nIn = (int)strlen((char*)zIn);
    zOut = malloc( nIn+1 );
    if( zOut==0 ){
      sqlite3_result_error_nomem(context);
      return;
    }
    memcpy(zOut, zIn, nIn);
    zOut[nIn] = 0;
    sqlite3_result_text64(context, (char*)zOut, nIn, free,
                          SQLITE_UTF8_ZT);
  }
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_strdup_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  rc = sqlite3_create_function(db, "strdup", 1, SQLITE_UTF8,
                   0, strdupfunc, 0, 0);
  return rc;
}
