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
  const __xArgDb = wasm.xWrap.argAdapter('sqlite3*');

  /**
     Move FTS-specific APIs (installed via automation) from
     sqlite3.capi to sqlite3.fts.
  */
  for(const c of [
    'Fts5ExtensionApi', 'Fts5PhraseIter', 'fts5_api',
    'fts5_api_from_db', 'fts5_tokenizer'
  ]){
    fts[c] = capi[c] || toss("Cannot find capi."+c);
    delete capi[c];
  }

  /**
     Requires a JS Function intended to be used as an xFunction()
     implementation. This function returns a proxy xFunction
     wrapper which:

     - Converts all of its sqlite3_value arguments to an array
       of JS values using sqlite3_values_to_js().

       - Calls the given callback, passing it:

       (pFtsXApi, pFtsCx, pCtx, array-of-values)

       where the first 3 arguments are the first 3 pointers
       in the xFunction interface.


       The call is intended to set a result value into the db, and may
       do so be either (A) explicitly returning non-undefined or (B)
       using one of the sqlite3_result_XYZ() functions and returning
       undefined. If the callback throws, its exception will be passed
       to sqlite3_result_error_js().
  */
  fts.xFunctionProxy1 = function(callback){
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
  };

  /**
     Identical to xFunctionProxy1 except that the callback wrapper it
     creates does _not_ perform sqlite3_value-to-JS conversion in
     advance and calls the callback with:

     (pFtsXApi, pFtsCx, pCtx, array-of-ptr-to-sqlite3_value)

     It is up to the callback to use the sqlite3_value_XYZ() family of
     functions to inspect or convert the values.
  */
  fts.xFunctionProxy2 = function(callback){
    return (pFtsXApi, pFtsCx, pCtx, argc, pArgv)=>{
      try{
        const list = [];
        let i;
        for(i = 0; i < argc; ++i){
          list.push( wasm.peekPtr(pArgv + (wasm.ptrSizeof * i)) );
        }
        capi.sqlite3_result_js(pCtx, callback(
          pFtsXApi, pFtsCx, pCtx, list
        ));
      }catch(e){
        capi.sqlite3_result_error_js(pCtx, e);
      }
    }
  };

  /**
     JS-to-WASM arg adapter for xCreateFunction()'s xFunction arg.
     This binds JS impls of xFunction to WASM so that they can be
     called from native code. Its context is limited to the
     combination of ((fts5_api*) + functionNameCaseInsensitive), and
     will replace any existing impl for subsequent invocations for the
     same combination.

     The functions is creates are intended to set a result value into
     the db, and may do so be either (A) explicitly returning
     non-undefined or (B) using one of the sqlite3_result_XYZ()
     functions and returning undefined. If the callback throws, its
     exception will be passed to sqlite3_result_error_js().

     PENDING DESIGN DECISION: this framework currently converts each
     argument in its JS equivalent before passing them on to the
     xFunction impl. We could, and possibly should, instead pass a JS
     array of sqlite3_value pointers. The advantages would be:

     - No in-advance to-JS overhead which xFunction might not use.

     Disadvantages include:

     - xFunction would be required to call sqlite3_value_to_js(),
     or one of the many sqlite3_value_XYZ() functions on their own.
     This would be more cumbersome for most users.

     Regardless of which approach is chosen here, clients could
     provide a function of their own which takes the _other_ approach,
     install it with wasm.installFunction(), and then pass that
     generated pointer to createFunction(), in which case this layer
     does not proxying and passes all native-level arguments as-is to
     the client-defined function.
  */
  const xFunctionArgAdapter = new wasm.xWrap.FuncPtrAdapter({
    name: 'fts5_api::xCreateFunction(xFunction)',
    signature: 'v(pppip)',
    contextKey: (argv,argIndex)=>{
      return (argv[0]/*(fts5_api*)*/
              + wasm.cstrToJs(argv[1]).toLowerCase()/*name*/)
    },
    callProxy: fts.xFunctionProxy1
  });

  /** Map of (sqlite3*) to fts.fts5_api. */
  const __ftsApiToStruct = Object.create(null);
  const __fts5_api_from_db = function(pDb, createIfNeeded){
    let rc = __ftsApiToStruct[pDb];
    if(!rc && createIfNeeded){
      const fapi = fts.fts5_api_from_db(pDb)
            || toss("Internal error - cannot get FTS5 API object for db.");
      rc = new fts.fts5_api(fapi);
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
     after the db has been closed, meaning that the argument to this
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
              sqlite3.config.warn("Could not remove FTS func",name,e);
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

  const __affirmDbArg = (arg)=>{
    arg = __xArgDb(arg);
    if(!arg || !wasm.isPtr(arg)) toss("Invalid db argument.");
    return arg;
  };

  /**
     Convenience wrapper to fts5_api::xCreateFunction.

     Creates a new FTS5 function for the given database. The arguments are:

     - db must be either an sqlite3.oo1.DB instance or a WASM pointer
       to (sqlite3*).

     - name: name (JS string) of the function

     - xFunction either a Function or a pointer to a WASM function. In
       the former case a WASM-bound wrapper, behaving as documented
       for fts5.xFunctionProxy1(), gets installed for the life of the
       given db handle. In the latter case the function is
       passed-through as-is, with no argument conversion or lifetime
       tracking.  In the former case the function is called as
       documented for xFunctionProxy1() and in the latter it must
       return void and is called with args (ptrToFts5ExtensionApi,
       ptrToFts5Context, ptrToSqlite3Context, int argc,
       C-array-of-sqlite3_value-pointers).

     - xDestroy optional Function or pointer to WASM function to call
       when the binding is destroyed (when the db handle is
       closed). The function will, in this context, always be passed 0
       as its only argument. A passed-in function must, however,
       have one parameter so that type signature checks will pass.
       It must return void and must not throw.

     The 2nd and subsequent aruguments may optionally be packed into
     a single Object with like-named properties.

     This function throws on error, of which there are many potential
     candidates. It returns `undefined`.
  */
  fts.createFunction = function(db, name, xFunction, xDestroy = 0){
    db = __affirmDbArg(db);
    if( 2 === arguments.length && 'string' !== typeof name){
      xDestroy = name.xDestroy || null;
      xFunction = name.xFunction || null;
      name = name.name;
    }
    if( !name || 'string' !== typeof name ) toss("Invalid name argument.");
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
      const rc = xcf(sfapi.pointer, name, 0, xFunction || 0, pDestroy || xDestroy || 0 );
      if(rc) toss(rc,"FTS5::xCreateFunction() failed.");
      __addCleanupForFunc(sfapi, name, pDestroy);
    }catch(e){
      if(pDestroy) wasm.uninstallFunction(pDestroy);
      sfapi.dispose();
      throw e;
    }
  };

  /**
     ! UNTESTED

     Convenience wrapper for fts5_api::xCreateTokenizer().

     - db = the db to install the tokenizer into.

     - name = the JS string name of the tokenizer.

     - pTokenizer = the tokenizer instance, which must be a
       fts5.fts5_tokenizer instance or a valid WASM pointer to one.

     - xDestroy = as documented for createFunction().

     The C layer makes a bitwise copy of the tokenizer, so any
     changes made to it after installation will have no effect.

     Throws on error.
  */
  const createTokenizer = function(db, name, pTokenizer, xDestroy = 0){
    db = __affirmDbArg(db);
    if( 2 === arguments.length && 'string' !== typeof name){
      pTokenizer = name.pTokenizer;
      xDestroy = name.xDestroy || null;
      name = name.name;
    }
    if( !name || 'string' !== typeof name ) toss("Invalid name argument.");
    if(pTokenizer instanceof fts.fts5_tokenizer){
      pTokenizer = pTokenizer.pointer;
    }
    if(!pTokenizer || !wasm.isPtr(pTokenizer)){
      toss("Invalid pTokenizer argument - must be a valid fts5.fts5_tokenizer",
           "instance or a WASM pointer to one.");
    }
    const sfapi = __fts5_api_from_db(db, true);
    let pDestroy = 0;
    const stackPos = wasm.pstack.pointer;
    try{
      if(xDestroy instanceof Function){
        pDestroy = wasm.installFunction(xDestroy, 'v(p)');
      }
      const xct = sfapi.$$xCreateTokenizer || (
        sfapi.$$xCreateTokenizer = wasm.xWrap(sfapi.$xCreateTokenizer, 'int', [
          '*', 'string', '*', '*', '*'
          /* fts5_api*, const char *zName, void *pContext,
             fts5_tokenizer *pTokenizer, void(*xDestroy)(void*) */
        ])
      );
      const outPtr = wasm.pstack.allocPtr();
      const rc = xct(fapi.pointer, name, 0, pTokenizer, pDestroy || xDestroy || 0 );
      if(rc) toss(rc,"FTS5::xCreateFunction() failed.");
      if(pDestroy) __addCleanupForFunc(sfapi, name, pDestroy);
    }catch(e){
      if(pDestroy) wasm.uninstallFunction(pDestroy);
      sfapi.dispose();
      throw e;
    }finally{
      wasm.pstack.restore(stackPost);
    }
  };
  //fts.createTokenizer = createTokenizer;

}/*sqlite3ApiBootstrap.initializers.push()*/);
