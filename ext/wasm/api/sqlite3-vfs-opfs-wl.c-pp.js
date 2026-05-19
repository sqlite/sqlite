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
  Atomics.wait()/notify() protocol. This file holds the "synchronous
  half" of the VFS, whereas it shares the "asynchronous half" with the
  "opfs" VFS.

  Testing has failed to show any genuine functional difference between
  these VFSes other than "opfs-wl" being able to dole out xLock()
  requests in a strictly FIFO manner by virtue of WebLocks being
  globally managed by the browser. This tends to lead to, but does not
  guaranty, fairer distribution of locks. Differences are unlikely to
  be noticed except, perhaps, under very high contention.

  This file is intended to be appended to the main sqlite3 JS
  deliverable somewhere after opfs-common-shared.c-pp.js.
*/
'use strict';
globalThis.sqlite3ApiBootstrap.initializers.push(function(sqlite3){
  if( !sqlite3.opfs || sqlite3.config.disable?.vfs?.['opfs-wl'] ){
    return;
  }
  const util = sqlite3.util,
        toss  = sqlite3.util.toss;
  const opfsUtil = sqlite3.opfs;
  const vfsName = 'opfs-wl';
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
  options = opfsUtil.initOptions(vfsName,options);
  if( !options ) return sqlite3;
  const capi = sqlite3.capi,
        state = opfsUtil.createVfsState(),
        opfsVfs = state.vfs,
        metrics = opfsVfs.metrics.counters,
        mTimeStart = opfsVfs.mTimeStart,
        mTimeEnd = opfsVfs.mTimeEnd,
        opRun = opfsVfs.opRun,
        debug = (...args)=>sqlite3.config.debug(vfsName+":",...args),
        warn = (...args)=>sqlite3.config.warn(vfsName+":",...args),
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
    sqlite3.config.warn("Ignoring inability to install the",vfsName,"sqlite3_vfs:",e);
  });
});
}/*sqlite3ApiBootstrap.initializers.push()*/);
//#/if target:node
