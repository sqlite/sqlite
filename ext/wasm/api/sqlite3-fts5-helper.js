/*
** 2023-08-03
**
** The author disclaims copyright to this source code.  In place of a
** legal notice, here is a blessing:
**
** *   May you do good and not evil.
** *   May you find forgiveness for yourself and forgive others.
** *   May you share freely, never taking more than you give.
*/

/**
   This file installs sqlite3.fts5, a namespace which exists to assist
   in JavaScript-side extension of FTS5.
*/
'use strict';
globalThis.sqlite3ApiBootstrap.initializers.push(function(sqlite3){
  const wasm = sqlite3.wasm, capi = sqlite3.capi, toss = sqlite3.util.toss3;
  if(!capi.fts5_api_from_db){
    return /*this build does not have FTS5*/;
  }
  const fts = sqlite3.fts5 = Object.create(null);
  const xArgDb = wasm.xWrap.argAdapter('sqlite3*');

  /**
     Move FTS-specific APIs (installed via automation) from
     sqlite3.capi to sqlite3.fts.
  */
  for(const c of [
    'Fts5ExtensionApi', 'Fts5PhraseIter', 'fts5_api',
    'fts5_api_from_db'
  ]){
    fts[c] = capi[c] || toss("Cannot find capi."+c);
    delete capi[c];
  }

  /* JS-to-WASM arg adapter for xCreateFunction()'s xFunction arg. */
  const xFunctionArgAdapter = new wasm.xWrap.FuncPtrAdapter({
    name: 'fts5_api::xCreateFunction(xFunction)',
    signature: 'v(pppip)',
    contextKey: (argv,argIndex)=>{
      return (argv[0]/*(fts5_api*)*/
              + wasm.cstrToJs(argv[1]).toLowerCase()/*name*/)
    },
    callProxy: (callback)=>{
      return (pFtsXApi, pFtsCx, pCtx, argc, pArgv)=>{
        try{
          capi.sqlite3_result_js(pCtx, callback(
            pFtsXApi, pFtsCx, pCtx,
            capi.sqlite3_values_to_js(argc, pArgv)
          ));
        }catch(e){
          capi.sqlite3_result_error_js(pCtx, e);
        }
      }
    }
  });

  /** Map of (sqlite3*) to fts.fts5_api. */
  const __ftsApiToStruct = Object.create(null);
  const __fts5_api_from_db = function(pDb, createIfNeeded){
    let rc = __ftsApiToStruct[pDb];
    if(!rc && createIfNeeded){
      rc = new fts.fts5_api(fts.fts5_api_from_db(pDb));
      __ftsApiToStruct[pDb] = rc;
    }
    return rc;
  };

  /**
     Arrange for WASM functions dynamically created via this API to be
     uninstalled when the db they were installed for is closed... */
  const __addCleanupForFunc = function(sfapi, name, pDestroy){
    if(!sfapi.$$cleanup){
      sfapi.$$cleanup = [];
    }
    sfapi.$$cleanup.push([name.toLowerCase(), pDestroy]);
  };

  /**
     Callback to be invoked via the JS binding of sqlite3_close_v2(),
     after the db has been closed, meaing that the argument to this
     function is not a valid object. We use its address only as a
     lookup key.
  */
  sqlite3.__dbCleanupMap.postCloseCallbacks.push(function(pDb){
    const sfapi = __fts5_api_from_db(pDb, false);
    if(sfapi){
      delete __ftsApiToStruct[pDb];
      if(sfapi.$$cleanup){
        const fapi = sfapi.pointer;
        const scope = wasm.scopedAllocPush();
        //wasm.xWrap.FuncPtrAdapter.debugFuncInstall = true;
        try{
          for(const [name, pDestroy] of sfapi.$$cleanup){
            try{
              /* Uninstall xFunctionArgAdapter's bindings via a
                 roundabout approach: its scoping rules uninstall each
                 new installation at the earliest opportunity, so we
                 simply need to fake a call with a 0-pointer for the
                 xFunction callback to uninstall the most recent
                 one. */
              const zName = wasm.scopedAllocCString(name);
              const argv = [fapi, zName, 0, 0, 0];
              xFunctionArgAdapter.convertArg(argv[3], argv, 3);
              /* xDestroy, on the other hand, requires some
                 hand-holding to ensure we don't prematurely
                 uninstall these when a function is replaced
                 (shadowed). */
              if(pDestroy) wasm.uninstallFunction(pDestroy);
            }catch(e){
              sqlite3.config.error("Error removing FTS func",name,e);
            }
          }
        }finally{
          wasm.scopedAllocPop(scope);
        }
        //wasm.xWrap.FuncPtrAdapter.debugFuncInstall = false;
      }
      sfapi.dispose();
    }
  });

  /**
     Convenience wrapper to fts5_api::xCreateFunction.

     Creates a new FTS5 function for the given database. The arguments are:

     - db must be either an sqlite3.oo1.DB instance or a WASM pointer
       to (sqlite3*).

     - name: name (JS string) of the function

     - xFunction either a Function or a pointer to a WASM function. In
       the former case a WASM-bound wrapper for the function gets
       installed for the life of the given db handle. The function
       must return void and accept args in the form
       (ptrToFts5ExtensionApi, ptrToFts5Context, ptrToSqlite3Context,
       array-of-JS-values).

     - xDestroy optional Function or pointer to WASM function to call
       when the binding is destroyed (when the db handle is
       closed). The function will, in this context, always be passed 0
       as its only argument. A passed-in function must, however,
       have one argument so that type signature checks will pass.

     The 2nd and subsequent aruguments may optionally be packed into
     a single Object with like-named properties.

     The callback functions may optionally be provided in the form of
     a single object with xFunction and xDestroy properties.

     This function throws on error, of which there are many potential
     candidates. It returns `undefined`.
  */
  fts.createFunction = function(db, name, xFunction, xDestroy = 0){
    db = xArgDb(db);
    if(!wasm.isPtr(db)) toss("Invalid db argument.");
    if( 2 === arguments.length && 'string' !== typeof name){
      xDestroy = name.xDestroy || null;
      xFunction = name.xFunction || null;
      name = name.name;
    }
    if( !name || 'string' !== typeof name ) toss("Invalid name argument.");
    const fapi = fts.fts5_api_from_db(db)
          || toss("Internal error - cannot get FTS5 API object for db.");
    const sfapi = __fts5_api_from_db(db, true);
    let pDestroy = 0;
    try{
      /** Because of how fts5_api::xCreateFunction() replaces
          functions (by prepending new ones to a linked list but
          retaining old ones), we cannot use a FuncPtrAdapter to
          automatically convert xDestroy, lest we end up uninstalling
          a bound-to-wasm JS function's wasm pointer before fts5
          cleans it up when the db is closed. */
      if(xDestroy instanceof Function){
        pDestroy = wasm.installFunction(xDestroy, 'v(p)');
      }
      const xcf = sfapi.$$xCreateFunction || (
        sfapi.$$xCreateFunction = wasm.xWrap(sfapi.$xCreateFunction, 'int', [
          '*', 'string', '*', xFunctionArgAdapter, '*'
        ])
      );
      const rc = xcf(fapi, name, 0, xFunction || 0, pDestroy || xDestroy || 0 );
      if(rc) toss(rc,"FTS5::xCreateFunction() failed.");
      __addCleanupForFunc(sfapi, name, pDestroy);
    }catch(e){
      if(pDestroy) wasm.uninstallFunction(pDestroy);
      sfapi.dispose();
      throw e;
    }
  };

}/*sqlite3ApiBootstrap.initializers.push()*/);
