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

   - The (officially undocumented) 'opfs-wl-disable' URL
   argument will disable OPFS, making this function a no-op.

   Aside from locking differences in the VFSes, this function
   otherwise behaves the same as
   sqlite3-vfs-opfs.c-pp.js:installOpfsVfs().
*/
const installOpfsWlVfs = async function(options){
  options = opfsUtil.initOptions('opfs-wl',options);
  if( !options ) return sqlite3;
  options.verbose = 2;
  const capi = sqlite3.capi,
        state = opfsUtil.createVfsState(),
        opfsVfs = state.vfs,
        metrics = opfsVfs.metrics.counters,
        mTimeStart = opfsVfs.mTimeStart,
        mTimeEnd = opfsVfs.mTimeEnd,
        opRun = opfsVfs.opRun,
        debug = (...args)=>sqlite3.config.debug("opfs-wl:",...args),
        warn = (...args)=>sqlite3.config.warn("opfs-wl:",...args),
        __openFiles = opfsVfs.__openFiles;

  //debug("state",JSON.stringify(options));
  /*
    At this point, createVfsState() has populated:

    - state: the configuration object we share with the async proxy.

    - opfsVfs: an sqlite3_vfs instance with lots of JS state attached
    to it.

    with any code common to both the "opfs" and "opfs-wl" VFSes. Now
    comes the VFS-dependent work...
  */
  return opfsVfs.bindVfs(util.nu({
    xLock: function(pFile,lockType){
      mTimeStart('xLock');
      //debug("xLock()...");
      const f = __openFiles[pFile];
      const rc = opRun('xLock', pFile, lockType);
      if( !rc ) f.lockType = lockType;
      mTimeEnd();
      return rc;
    },
    xUnlock: function(pFile,lockType){
      mTimeStart('xUnlock');
      const f = __openFiles[pFile];
      const rc = opRun('xUnlock', pFile, lockType);
      if( !rc ) f.lockType = lockType;
      mTimeEnd();
      return rc;
    }
  }), function(sqlite3, vfs){
    /* Post-VFS-registration initialization... */
    if(sqlite3.oo1){
      const OpfsWlDb = function(...args){
        const opt = sqlite3.oo1.DB.dbCtorHelper.normalizeArgs(...args);
        opt.vfs = vfs.$zName;
        sqlite3.oo1.DB.dbCtorHelper.call(this, opt);
      };
      OpfsWlDb.prototype = Object.create(sqlite3.oo1.DB.prototype);
      sqlite3.oo1.OpfsWlDb = OpfsWlDb;
      OpfsWlDb.importDb = opfsUtil.importDb;
      /* The "opfs" VFS variant adds a
         oo1.DB.dbCtorHelper.setVfsPostOpenCallback() callback to set
         a high busy_timeout. That was a design mis-decision and is
         inconsistent with sqlite3_open() and friends, but is retained
         against the risk of introducing regressions if it's removed.
         This variant does not repeat that mistake.
      */
    }
  })/*bindVfs()*/;
}/*installOpfsWlVfs()*/;
globalThis.sqlite3ApiBootstrap.initializersAsync.push(async (sqlite3)=>{
  return installOpfsWlVfs().catch((e)=>{
    sqlite3.config.warn("Ignoring inability to install the 'opfs-wl' sqlite3_vfs:",e);
  });
});
}/*sqlite3ApiBootstrap.initializers.push()*/);
//#endif target:node
