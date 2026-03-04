//#if not target:node
/*
  2026-02-20

  The author disclaims copyright to this source code.  In place of a
  legal notice, here is a blessing:

  *   May you do good and not evil.
  *   May you find forgiveness for yourself and forgive others.
  *   May you share freely, never taking more than you give.

  ***********************************************************************

  This file is a reimplementation of the "opfs" VFS (as distinct from
  "opfs-sahpool") which uses WebLocks for locking instead of a bespoke
  custom Atomics.wait()/notify() protocol. This file holds the
  "synchronous half" of the VFS, whereas it shares the "asynchronous
  half" of the "opfs" VFS.

  This file is intended to be appended to the main sqlite3 JS
  deliverable somewhere after sqlite3-api-oo1.js.

  TODOs (2026-03-03):

  - For purposes of tester1.js we need to figure out which of these
  VFSes will install the (internal-use-only) sqlite3.opfs utility code
  namespace. We need that in order to clean up OPFS files during test
  runs. Alternately, move those into their own
  sqlite3ApiBootstrap.initializers entry which precedes both of the
  VFSes, so they'll have access to it during bootstrapping. The
  sqlite3.opfs namespace is removed at the end of bootstrapping unless
  the library is told to run in testing mode (which is not a
  documented feature).
*/
'use strict';
globalThis.sqlite3ApiBootstrap.initializers.push(function(sqlite3){
  const util = sqlite3.util,
        toss  = sqlite3.util.toss;
  const opfsUtil = sqlite3.opfs || sqlite3.util.toss("Missing sqlite3.opfs")
  /* These get removed from sqlite3 during bootstrap, so we need an
     early reference to it. */;

/**
   installOpfsWlVfs() returns a Promise which, on success, installs an
   sqlite3_vfs named "opfs-wl", suitable for use with all sqlite3 APIs
   which accept a VFS. It is intended to be called via
   sqlite3ApiBootstrap.initializers or an equivalent mechanism.

   This VFS is essentially a copy of the "opfs" VFS but uses
   WebLocks for its xLock() and xUnlock() implementations.

   Quirks specific to this VFS:

   - Because WebLocks effectively block until they return, they will
   effectively hang on locks rather than returning SQLITE_BUSY.


   The argument may optionally be a plain object with the following
   configuration options:

  - proxyUri: name of the async proxy JS file.

  - verbose (=2): an integer 0-3. 0 disables all logging, 1 enables
    logging of errors. 2 enables logging of warnings and errors. 3
    additionally enables debugging info. Logging is performed
    via the sqlite3.config.{log|warn|error}() functions.


  On success, the Promise resolves to the top-most sqlite3 namespace
  object. Success does not necessarily mean that it installs the VFS,
  as there are legitimate non-error reasons for OPFS not to be
  available.
*/
const installOpfsWlVfs = function callee(options){
  try{
    opfsUtil.vfsInstallationFeatureCheck();
  }catch(e){
    return Promise.reject(e);
  }
  options = opfsUtil.initOptions(options, callee);
  if( options.disableOpfs ){
    return Promise.resolve(sqlite3);
  }

  const thePromise = new Promise(function(promiseResolve_, promiseReject_){
    const loggers = [
      sqlite3.config.error,
      sqlite3.config.warn,
      sqlite3.config.log
    ];
    const logImpl = (level,...args)=>{
      if(options.verbose>level) loggers[level]("OPFS syncer:",...args);
    };
    const log   = (...args)=>logImpl(2, ...args),
          warn  = (...args)=>logImpl(1, ...args),
          error = (...args)=>logImpl(0, ...args),
          capi  = sqlite3.capi,
          wasm  = sqlite3.wasm;
    const state = opfsUtil.createVfsState('opfs-wl', options),
          opfsVfs = state.vfs,
          metrics = opfsVfs.metrics.counters,
          mTimeStart = opfsVfs.mTimeStart,
          mTimeEnd = opfsVfs.mTimeEnd,
          __openFiles = opfsVfs.__openFiles;
    delete state.vfs;

    /* At this point, createVfsState() has populated state and
       opfsVfs with any code common to both the "opfs" and "opfs-wl"
       VFSes. Now comes the VFS-dependent work... */

    opfsVfs.ioSyncWrappers.xLock = function(pFile, lockType){
      mTimeStart('xLock');
      ++metrics.xLock.count;
      const f = __openFiles[pFile];
      let rc = 0;
      /* All OPFS locks are exclusive locks. If xLock() has
         previously succeeded, do nothing except record the lock
         type. If no lock is active, have the async counterpart
         lock the file. */
      if( f.lockType ) {
        f.lockType = lockType;
      }else{
        try{
          const view = state.sabOPView;
          /* We need to pass pFile's name to the async proxy so that
             it can create the WebLock name. */
          state.s11n.serialize(f.filename)
          Atomics.store(view, state.lock.atomicsHandshake, 0);
          Atomics.store(view, state.lock.type, lockType);
          Atomics.store(view, state.opIds.whichOp, state.opIds.lockControl);
          Atomics.notify(state.sabOPView, state.opIds.whichOp)
          while('not-equal'!==Atomics.wait(view, state.lock.atomicsHandshake, 0)){
            /* Loop is a workaround for environment-specific quirks. See
               notes in similar loops. */
          }
          f.lockType = lockType;
        }catch(e){
          error("xLock(",arguments,") failed", e, f);
          rc = capi.SQLITE_IOERR_LOCK;
        }
      }
      mTimeEnd();
      return rc;
    };

    opfsVfs.ioSyncWrappers.xUnlock =function(pFile,lockType){
      mTimeStart('xUnlock');
      ++metrics.xUnlock.count;
      const f = __openFiles[pFile];
      let rc = 0;
      if( lockType < f.lockType ){
        try{
          const view = state.sabOPView;
          Atomics.store(view, state.lock.atomicsHandshake, 1);
          Atomics.notify(view, state.lock.atomicsHandshake);
          Atomics.wait(view, state.lock.atomicsHandshake, 1);
        }catch(e){
          error("xUnlock(",pFile,lockType,") failed",e, f);
          rc = capi.SQLITE_IOERR_LOCK;
        }
      }
      if( 0===rc ) f.lockType = lockType;
      mTimeEnd();
      return rc;
    };



    let promiseWasRejected = undefined;
    const promiseReject = (err)=>{
      promiseWasRejected = true;
      opfsVfs.dispose();
      return promiseReject_(err);
    };
    const promiseResolve = ()=>{
      promiseWasRejected = false;
      return promiseResolve_(sqlite3);
    };
    options.proxyUri += '?vfs=opfs-wl';
    const W = opfsVfs.worker =
//#if target:es6-bundler-friendly
    new Worker(new URL("sqlite3-opfs-async-proxy.js?vfs=opfs-wl", import.meta.url));
//#elif target:es6-module
    new Worker(new URL(options.proxyUri, import.meta.url));
//#else
    new Worker(options.proxyUri);
//#endif
    setTimeout(()=>{
      /* At attempt to work around a browser-specific quirk in which
         the Worker load is failing in such a way that we neither
         resolve nor reject it. This workaround gives that resolve/reject
         a time limit and rejects if that timer expires. Discussion:
         https://sqlite.org/forum/forumpost/a708c98dcb3ef */
      if(undefined===promiseWasRejected){
        promiseReject(
          new Error("Timeout while waiting for OPFS async proxy worker.")
        );
      }
    }, 4000);
    W._originalOnError = W.onerror /* will be restored later */;
    W.onerror = function(err){
      // The error object doesn't contain any useful info when the
      // failure is, e.g., that the remote script is 404.
      error("Error initializing OPFS asyncer:",err);
      promiseReject(new Error("Loading OPFS async Worker failed for unknown reasons."));
    };

    const opRun = opfsVfs.opRun;
    /**
       Not part of the public API. Only for test/development use.
    */
    opfsUtil.debug = {
      asyncShutdown: ()=>{
        warn("Shutting down OPFS async listener. The OPFS VFS will no longer work.");
        opRun('opfs-async-shutdown');
      },
      asyncRestart: ()=>{
        warn("Attempting to restart OPFS VFS async listener. Might work, might not.");
        W.postMessage({type: 'opfs-async-restart'});
      }
    };

    if(sqlite3.oo1){
      const OpfsWlDb = function(...args){
        const opt = sqlite3.oo1.DB.dbCtorHelper.normalizeArgs(...args);
        opt.vfs = opfsVfs.$zName;
        sqlite3.oo1.DB.dbCtorHelper.call(this, opt);
      };
      OpfsWlDb.prototype = Object.create(sqlite3.oo1.DB.prototype);
      sqlite3.oo1.OpfsWlDb = OpfsWlDb;
      OpfsWlDb.importDb = opfsUtil.importDb;
//#if nope
      sqlite3.oo1.DB.dbCtorHelper.setVfsPostOpenCallback(
        opfsVfs.pointer,
        function(oo1Db, sqlite3){
          /* Set a relatively high default busy-timeout handler to
             help OPFS dbs deal with multi-tab/multi-worker
             contention. */
          sqlite3.capi.sqlite3_busy_timeout(oo1Db, 10000);
        }
      );
//#endif
    }/*extend sqlite3.oo1*/

    const sanityCheck = function(){
      const scope = wasm.scopedAllocPush();
      const sq3File = new capi.sqlite3_file();
      try{
        const fid = sq3File.pointer;
        const openFlags = capi.SQLITE_OPEN_CREATE
              | capi.SQLITE_OPEN_READWRITE
        //| capi.SQLITE_OPEN_DELETEONCLOSE
              | capi.SQLITE_OPEN_MAIN_DB;
        const pOut = wasm.scopedAlloc(8);
        const dbFile = "/sanity/check/file"+randomFilename(8);
        const zDbFile = wasm.scopedAllocCString(dbFile);
        let rc;
        state.s11n.serialize("This is ä string.");
        rc = state.s11n.deserialize();
        log("deserialize() says:",rc);
        if("This is ä string."!==rc[0]) toss("String d13n error.");
        opfsVfs.vfsSyncWrappers.xAccess(opfsVfs.pointer, zDbFile, 0, pOut);
        rc = wasm.peek(pOut,'i32');
        log("xAccess(",dbFile,") exists ?=",rc);
        rc = opfsVfs.vfsSyncWrappers.xOpen(opfsVfs.pointer, zDbFile,
                                   fid, openFlags, pOut);
        log("open rc =",rc,"state.sabOPView[xOpen] =",
            state.sabOPView[state.opIds.xOpen]);
        if(0!==rc){
          error("open failed with code",rc);
          return;
        }
        opfsVfs.vfsSyncWrappers.xAccess(opfsVfs.pointer, zDbFile, 0, pOut);
        rc = wasm.peek(pOut,'i32');
        if(!rc) toss("xAccess() failed to detect file.");
        rc = opfsVfs.ioSyncWrappers.xSync(sq3File.pointer, 0);
        if(rc) toss('sync failed w/ rc',rc);
        rc = opfsVfs.ioSyncWrappers.xTruncate(sq3File.pointer, 1024);
        if(rc) toss('truncate failed w/ rc',rc);
        wasm.poke(pOut,0,'i64');
        rc = opfsVfs.ioSyncWrappers.xFileSize(sq3File.pointer, pOut);
        if(rc) toss('xFileSize failed w/ rc',rc);
        log("xFileSize says:",wasm.peek(pOut, 'i64'));
        rc = opfsVfs.ioSyncWrappers.xWrite(sq3File.pointer, zDbFile, 10, 1);
        if(rc) toss("xWrite() failed!");
        const readBuf = wasm.scopedAlloc(16);
        rc = opfsVfs.ioSyncWrappers.xRead(sq3File.pointer, readBuf, 6, 2);
        wasm.poke(readBuf+6,0);
        let jRead = wasm.cstrToJs(readBuf);
        log("xRead() got:",jRead);
        if("sanity"!==jRead) toss("Unexpected xRead() value.");
        if(opfsVfs.vfsSyncWrappers.xSleep){
          log("xSleep()ing before close()ing...");
          opfsVfs.vfsSyncWrappers.xSleep(opfsVfs.pointer,2000);
          log("waking up from xSleep()");
        }
        rc = opfsVfs.ioSyncWrappers.xClose(fid);
        log("xClose rc =",rc,"sabOPView =",state.sabOPView);
        log("Deleting file:",dbFile);
        opfsVfs.vfsSyncWrappers.xDelete(opfsVfs.pointer, zDbFile, 0x1234);
        opfsVfs.vfsSyncWrappers.xAccess(opfsVfs.pointer, zDbFile, 0, pOut);
        rc = wasm.peek(pOut,'i32');
        if(rc) toss("Expecting 0 from xAccess(",dbFile,") after xDelete().");
        warn("End of OPFS sanity checks.");
      }finally{
        sq3File.dispose();
        wasm.scopedAllocPop(scope);
      }
    }/*sanityCheck()*/;

    W.onmessage = function({data}){
      //log("Worker.onmessage:",data);
      switch(data.type){
          case 'opfs-unavailable':
            /* Async proxy has determined that OPFS is unavailable. There's
               nothing more for us to do here. */
            promiseReject(new Error(data.payload.join(' ')));
            break;
          case 'opfs-async-loaded':
            /* Arrives as soon as the asyc proxy finishes loading.
               Pass our config and shared state on to the async
               worker. */
            W.postMessage({type: 'opfs-async-init', args: util.nu(state)});
            break;
          case 'opfs-async-inited': {
            /* Indicates that the async partner has received the 'init'
               and has finished initializing, so the real work can
               begin... */
            if(true===promiseWasRejected){
              break /* promise was already rejected via timer */;
            }
            try {
              sqlite3.vfs.installVfs({
                io: {struct: opfsVfs.ioMethods, methods: opfsVfs.ioSyncWrappers},
                vfs: {struct: opfsVfs, methods: opfsVfs.vfsSyncWrappers}
              });
              state.sabOPView = new Int32Array(state.sabOP);
              state.sabFileBufView = new Uint8Array(state.sabIO, 0, state.fileBufferSize);
              state.sabS11nView = new Uint8Array(state.sabIO, state.sabS11nOffset, state.sabS11nSize);
              opfsVfs.initS11n();
              delete opfsVfs.initS11n;
              if(options.sanityChecks){
                warn("Running sanity checks because of opfs-sanity-check URL arg...");
                sanityCheck();
              }
              if(opfsUtil.thisThreadHasOPFS()){
                opfsUtil.getRootDir().then((d)=>{
                  W.onerror = W._originalOnError;
                  delete W._originalOnError;
                  log("End of OPFS-WL sqlite3_vfs setup.", opfsVfs);
                  promiseResolve();
                }).catch(promiseReject);
              }else{
                promiseResolve();
              }
            }catch(e){
              error(e);
              promiseReject(e);
            }
            break;
          }
          default: {
            const errMsg = (
              "Unexpected message from the OPFS async worker: " +
              JSON.stringify(data)
            );
            error(errMsg);
            promiseReject(new Error(errMsg));
            break;
          }
      }/*switch(data.type)*/
    }/*W.onmessage()*/;
  })/*thePromise*/;
  return thePromise;
}/*installOpfsWlVfs()*/;
installOpfsWlVfs.defaultProxyUri =
  "sqlite3-opfs-async-proxy.js";
globalThis.sqlite3ApiBootstrap.initializersAsync.push(async (sqlite3)=>{
  try{
    let proxyJs = installOpfsWlVfs.defaultProxyUri;
    if( sqlite3?.scriptInfo?.sqlite3Dir ){
      installOpfsWlVfs.defaultProxyUri =
        sqlite3.scriptInfo.sqlite3Dir + proxyJs;
      //sqlite3.config.warn("installOpfsWlVfs.defaultProxyUri =",installOpfsWlVfs.defaultProxyUri);
    }
    return installOpfsWlVfs().catch((e)=>{
      sqlite3.config.warn("Ignoring inability to install OPFS-WL sqlite3_vfs:",e);
    });
  }catch(e){
    sqlite3.config.error("installOpfsWlVfs() exception:",e);
    return Promise.reject(e);
  }
});
}/*sqlite3ApiBootstrap.initializers.push()*/);
//#else
/* The OPFS VFS parts are elided from builds targeting node.js. */
//#endif target:node
