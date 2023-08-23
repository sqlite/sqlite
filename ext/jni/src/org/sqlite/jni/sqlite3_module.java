/*
** 2023-08-23
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file is part of the JNI bindings for the sqlite3 C API.
*/
package org.sqlite.jni;

/**
   Incomplete and non-function. Just thinking out loud.
*/
public class sqlite3_module extends NativePointerHolder<sqlite3_module> {
  public final int iVersion;

  public sqlite3_module(int ver){ iVersion = ver; }

  public static interface XCreateConnect {
    int call( @NotNull sqlite3 db, @NotNull String[] argv,
              @NotNull OutputPointer.sqlite3_vtab out,
              @Nullable OutputPointer.String errMsg );
  }

  public static interface XDisconnectDestroy {
    int call( @NotNull sqlite3 db, @NotNull String[] argv,
              @NotNull OutputPointer.sqlite3_vtab out,
              @Nullable OutputPointer.String errMsg );
  }

  public static interface XOpen {
    int call( @NotNull sqlite3_vtab vt,
              @NotNull OutputPointer.sqlite3_vtab_cursor out );
  }

  public static interface XVTabCursor {
    int call( @NotNull sqlite3_vtab_cursor cur );
  }

  public static interface XVTabInt {
    int call( @NotNull sqlite3_vtab vt, int i );
  }

  public static interface XVTab {
    int call( @NotNull sqlite3_vtab vt );
  }

  public static interface XFilter {
    int call( @NotNull sqlite3_vtab_cursor cur, int idxNum,
              @Nullable String idxStr, @NotNull sqlite3_value[] argv );
  }

  // int (*xCreate)(sqlite3*, void *pAux,
  //              int argc, const char *const*argv,
  //              sqlite3_vtab **ppVTab, char**);
  public XCreateConnect xCreate = null;
  // int (*xConnect)(sqlite3*, void *pAux,
  //              int argc, const char *const*argv,
  //              sqlite3_vtab **ppVTab, char**);
  public XCreateConnect xConnect = null;
  // int (*xBestIndex)(sqlite3_vtab *pVTab, sqlite3_index_info*);
  // int (*xDisconnect)(sqlite3_vtab *pVTab);
  public XVTab xDisconnect = null;
  // int (*xDestroy)(sqlite3_vtab *pVTab);
  public XVTab xDestroy = null;
  // int (*xOpen)(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCursor);
  public XOpen xOpen = null;
  // int (*xClose)(sqlite3_vtab_cursor*);
  public XVTabCursor xClose = null;
  // int (*xFilter)(sqlite3_vtab_cursor*, int idxNum, const char *idxStr,
  //               int argc, sqlite3_value **argv);
  public XFilter xFilter = null;
  // int (*xNext)(sqlite3_vtab_cursor*);
  public XVTabCursor xNext = null;
  // int (*xEof)(sqlite3_vtab_cursor*);
  public XVTabCursor xEof = null;
  // int (*xColumn)(sqlite3_vtab_cursor*, sqlite3_context*, int);
  // int (*xRowid)(sqlite3_vtab_cursor*, sqlite3_int64 *pRowid);
  // int (*xUpdate)(sqlite3_vtab *, int, sqlite3_value **, sqlite3_int64 *);
  // int (*xBegin)(sqlite3_vtab *pVTab);
  public XVTabCursor xBegin = null;
  // int (*xSync)(sqlite3_vtab *pVTab);
  public XVTabCursor xSync = null;
  // int (*xCommit)(sqlite3_vtab *pVTab);
  public XVTabCursor xCommit = null;
  // int (*xRollback)(sqlite3_vtab *pVTab);
  public XVTabCursor xRollback = null;
  // int (*xFindFunction)(sqlite3_vtab *pVtab, int nArg, const char *zName,
  //                      void (**pxFunc)(sqlite3_context*,int,sqlite3_value**),
  //                      void **ppArg);
  // int (*xRename)(sqlite3_vtab *pVtab, const char *zNew);
  //
  // v2:
  // int (*xSavepoint)(sqlite3_vtab *pVTab, int);
  public XVTabInt xSavepoint = null;
  // int (*xRelease)(sqlite3_vtab *pVTab, int);
  public XVTabInt xRelease = null;
  // int (*xRollbackTo)(sqlite3_vtab *pVTab, int);
  public XVTabInt xRollbackTo = null;
  //
  // v3:
  // int (*xShadowName)(const char*);

}
