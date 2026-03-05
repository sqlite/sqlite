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
  const opfsUtil = sqlite3.opfs || toss("Missing sqlite3.opfs");

/**
   installOpfsWlVfs() returns a Promise which, on success, installs an
   sqlite3_vfs named "opfs-wl", suitable for use with all sqlite3 APIs
   which accept a VFS. It is intended to be called via
   sqlite3ApiBootstrap.initializers or an equivalent mechanism.

   This VFS is essentially identical to the "opfs" VFS but uses
   WebLocks for its xLock() and xUnlock() implementations.

   Quirks specific to this VFS:

   - Because WebLocks effectively block until they return, they will
   effectively hang on locks rather than returning SQLITE_BUSY.

   Aside from locking differences in the VFSes, this function
   otherwise behaves the same as
   sqlite3-vfs-opfs.c-pp.js:installOpfsVfs().
*/
const installOpfsWlVfs = async function callee(options){
  options = opfsUtil.initOptions(options, callee);
  if( !options ) return sqlite3;
  options.verbose = 2;
  const capi = sqlite3.capi,
        debug = (...args)=>sqlite3.config.warn("opfs-wl:",...args),
        state = opfsUtil.createVfsState('opfs-wl', options),
        opfsVfs = state.vfs,
        metrics = opfsVfs.metrics.counters,
        mTimeStart = opfsVfs.mTimeStart,
        mTimeEnd = opfsVfs.mTimeEnd,
        __openFiles = opfsVfs.__openFiles;
  //debug("state",JSON.stringify(state,false,'  '));
  /* At this point, createVfsState() has populated state and opfsVfs
     with any code common to both the "opfs" and "opfs-wl" VFSes. Now
     comes the VFS-dependent work... */
  return opfsVfs.bindVfs(util.nu({
     xLock: function(pFile,lockType){
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
        rc = opRun('xLock', pFile, lockType);
        if( 0===rc ) f.lockType = lockType;
      }
      mTimeEnd();
      return rc;
    },
    xUnlock: function(pFile,lockType){
      mTimeStart('xUnlock');
      ++metrics.xUnlock.count;
      const f = __openFiles[pFile];
      let rc = 0;
      if( capi.SQLITE_LOCK_NONE === lockType
          && f.lockType ){
        rc = opRun('xUnlock', pFile, lockType);
      }
      if( 0===rc ) f.lockType = lockType;
      mTimeEnd();
      return rc;
    }
  }), function(sqlite3, vfs){
    //debug("registered VFS");
    if(sqlite3.oo1){
      const OpfsWlDb = function(...args){
        const opt = sqlite3.oo1.DB.dbCtorHelper.normalizeArgs(...args);
        opt.vfs = vfs.$zName;
        sqlite3.oo1.DB.dbCtorHelper.call(this, opt);
      };
      OpfsWlDb.prototype = Object.create(sqlite3.oo1.DB.prototype);
      sqlite3.oo1.OpfsWlDb = OpfsWlDb;
      OpfsWlDb.importDb = opfsUtil.importDb;
    }/*extend sqlite3.oo1*/
  })/*bindVfs()*/;
}/*installOpfsWlVfs()*/;
installOpfsWlVfs.defaultProxyUri = "sqlite3-opfs-async-proxy.js";
globalThis.sqlite3ApiBootstrap.initializersAsync.push(async (sqlite3)=>{
  try{
    let proxyJs = installOpfsWlVfs.defaultProxyUri;
    if( sqlite3.scriptInfo?.sqlite3Dir ){
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
//#endif target:node
