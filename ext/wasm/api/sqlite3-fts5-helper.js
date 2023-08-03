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
  const __dbCleanupMap = sqlite3.__dbCleanupMap
        || toss("Maintenance needed: can't reach sqlite3.__dbCleanupMap.");
  if(!capi.fts5_api_from_db){
    return /*this build does not have FTS5*/;
  }
  const fts = sqlite3.fts5 = Object.create(null);
  /**
     Move FTS-specific APIs from sqlite3.capi to sqlite3.fts.
  */
  for(const c of [
    'Fts5ExtensionApi', 'Fts5PhraseIter', 'fts5_api',
    'fts5_api_from_db'
  ]){
    fts[c] = capi[c] || toss("Cannot find capi."+c);
    delete capi[c];
  }

  const cache = Object.create(null);

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

  const xDestroyArgAdapter = new wasm.xWrap.FuncPtrAdapter({
    name: 'fts5_api::xCreateFunction(xDestroy)',
    signature: 'v(p)',
    contextKey: xFunctionArgAdapter.contextKey,
    callProxy: (callback)=>{
      return (pArg)=>{
        try{ callback(pArg) }
        catch(e){
          sqlite3.config.warn("FTS5 function xDestroy() threw. Ignoring.",e);
        }
      }
    }
  });

  /**
     Arrange for WASM functions dynamically created via this API to be
     uninstalled when the db they were installed for is closed... */
  const __fts5FuncsPerDb = Object.create(null);
  const __cleanupForDb = function(pDb){
    return (__fts5FuncsPerDb[pDb] || (__fts5FuncsPerDb[pDb] = new Set));
  };
  __dbCleanupMap._addFts5Function = function ff(pDb, name){
    __cleanupForDb(pDb).add(name.toLowerCase());
  };
  __dbCleanupMap.extraCallbacks.push(function(pDb){
    const list = __fts5FuncsPerDb[pDb];
    if(list){
      const fapi = fts.fts5_api_from_db(pDb)
      const sfapi = new fts.fts5_api(fapi);
      const scope = wasm.scopedAllocPush();
      //wasm.xWrap.FuncPtrAdapter.debugFuncInstall = true;
      try{
        const xCF = wasm.functionEntry(sfapi.$xCreateFunction);
        for(const name of list){
          //sqlite3.config.warn("Clearing FTS function",pDb,name);
          try{
            const zName = wasm.scopedAllocCString(name);
            /* In order to clean up properly we first have to invoke
               the low-level (unwrapped) sfapi.$xCreateFunction and
               then fake some calls into the Function-to-funcptr
               apapters. If we just call cache.xcf(...) then the
               adapter ends up uninstalling the xDestroy pointer which
               sfapi.$xCreateFunction is about to invoke, leading to
               an invalid memory access. */
            xCF(fapi, zName, 0, 0, 0);
            // Now clean up our FuncPtrAdapters in a round-about way...
            const argv = [fapi, zName, 0, 0, 0];
            xFunctionArgAdapter.convertArg(argv[3], argv, 3);
            // xDestroyArgAdapter leads to an invalid memory access
            // for as-yet-unknown reasons. For the time being we're
            // leaking these.
            //xDestroyArgAdapter.convertArg(argv[4], argv, 4);
          }catch(e){
            sqlite3.config.error("Error removing FTS func",name,e);
          }
        }
      }finally{
        sfapi.dispose();
        delete __fts5FuncsPerDb[pDb];
        wasm.scopedAllocPop(scope);
      }
      //wasm.xWrap.FuncPtrAdapter.debugFuncInstall = false;
    }
  });

  const xArgDb = wasm.xWrap.argAdapter('sqlite3*');

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
       array-of-values).

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
    const sfapi = new fts.fts5_api(fapi);
    try{
      const xcf = cache.xcf || (
        cache.xcf = wasm.xWrap(sfapi.$xCreateFunction,'int',[
          '*', 'string', '*', xFunctionArgAdapter, xDestroyArgAdapter
        ])
      )/* this cache entry is only legal as long as nobody replaces
          the xCreateFunction() method */;
      const rc = xcf(fapi, name, 0, xFunction || 0, xDestroy || 0 );
      if(rc) toss(rc,"FTS5::xCreateFunction() failed.");
      __dbCleanupMap._addFts5Function(db, name);
    }finally{
      sfapi.dispose();
    }
  };

}/*sqlite3ApiBootstrap.initializers.push()*/);
