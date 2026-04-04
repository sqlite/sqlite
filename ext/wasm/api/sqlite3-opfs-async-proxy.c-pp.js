/* @preserve
  2022-09-16

  The author disclaims copyright to this source code.  In place of a
  legal notice, here is a blessing:

  *   May you do good and not evil.
  *   May you find forgiveness for yourself and forgive others.
  *   May you share freely, never taking more than you give.

  ***********************************************************************

  A Worker which manages asynchronous OPFS handles on behalf of a
  synchronous API which controls it via a combination of Worker
  messages, SharedArrayBuffer, and Atomics. It is the asynchronous
  counterpart of the API defined in sqlite3-vfs-opfs.js.

  Highly indebted to:

  https://github.com/rhashimoto/wa-sqlite/blob/master/src/examples/OriginPrivateFileSystemVFS.js

  for demonstrating how to use the OPFS APIs.

  This file is to be loaded as a Worker. It does not have any direct
  access to the sqlite3 JS/WASM bits, so any bits which it needs (most
  notably SQLITE_xxx integer codes) have to be imported into it via an
  initialization process.

  This file represents an implementation detail of a larger piece of
  code, and not a public interface. Its details may change at any time
  and are not intended to be used by any client-level code.

  2022-11-27: Chrome v108 changes some async methods to synchronous, as
  documented at:

  https://developer.chrome.com/blog/sync-methods-for-accesshandles/

  Firefox v111 and Safari 16.4, both released in March 2023, also
  include this.

  We cannot change to the sync forms at this point without breaking
  clients who use Chrome v104-ish or higher. truncate(), getSize(),
  flush(), and close() are now (as of v108) synchronous. Calling them
  with an "await", as we have to for the async forms, is still legal
  with the sync forms but is superfluous. Calling the async forms with
  theFunc().then(...) is not compatible with the change to
  synchronous, but we do do not use those APIs that way. i.e. we don't
  _need_ to change anything for this, but at some point (after Chrome
  versions (approximately) 104-107 are extinct) we should change our
  usage of those methods to remove the "await".
*/
//#if 0
/**
   2026-04-04: this file gets included by both the "opfs" and "opfs-wl"
   VFSes. It would, in hindsight, hypothetically be possible to restructure
   it very slightly to support both VFSes via a single Worker instance.

   Some of the changes we would need for that:

   - The xLock/xUnlock "op codes" would need to differ for each impl.
   i.e. we'd need state.opIds.xLock{,WL} and state.opIds.xUnlock{,WL}
   to distinguish between the two, rather than doing so when this Worker
   is loaded.

   - We would need to centralize loading of this Worker, outside of
   the VFS-specific pieces, and change the handshake in order to be
   able to distinguish between clients which support
   Atomics.waitAsync() and those which do not ("opfs-wl" requires
   waitAsync()).

   One down-side would be for clients which, for whatever reason, want
   to use both "opfs" and "opfs-wl" within the same session: because
   both would go through the same Worker, any operations for one VFS
   would, while they're being processed on this side of the proxy,
   effectively block the other VFS from doing anything, potentially
   deadlocking. This use case seems unlikely enough that it can
   possibly be ruled out (or even reasonably flat-out prohibited by
   the library).
*/
//#/if

"use strict";
const urlParams = new URL(globalThis.location.href).searchParams;
const vfsName = urlParams.get('vfs');
if( !vfsName ){
  throw new Error("Expecting vfs=opfs|opfs-wl URL argument for this worker");
}
/**
   We use this to allow us to differentiate debug output from
   multiple instances, e.g. multiple Workers to the "opfs"
   VFS or both the "opfs" and "opfs-wl" VFSes.
*/
const workerId = (Math.random() * 10000000) | 0;
const isWebLocker = 'opfs-wl'===urlParams.get('vfs');
const wPost = (type,...args)=>postMessage({type, payload:args});
const installAsyncProxy = function(){
  const toss = function(...args){throw new Error(args.join(' '))};
  if(globalThis.window === globalThis){
    toss("This code cannot run from the main thread.",
         "Load it as a Worker from a separate Worker.");
  }else if(!navigator?.storage?.getDirectory){
    toss("This API requires navigator.storage.getDirectory.");
  }

  /**
     Will hold state copied to this object from the synchronous side of
     this API.
  */
  const state = Object.create(null);

  /* initS11n() is preprocessor-injected so that we have identical
     copies in the synchronous and async halves. This side does not
     load the SQLite library, so does not have access to that copy. */
//#define opfs-async-proxy
//#include api/opfs-common-inline.c-pp.js
//#undef opfs-async-proxy

  /**
     verbose:

     0 = no logging output
     1 = only errors
     2 = warnings and errors
     3 = debug, warnings, and errors
  */
  state.verbose = 1;

  const loggers = {
    0:console.error.bind(console),
    1:console.warn.bind(console),
    2:console.log.bind(console)
  };
  const logImpl = (level,...args)=>{
    if(state.verbose>level) loggers[level](vfsName+' async-proxy',workerId+":",...args);
  };
  const log =    (...args)=>logImpl(2, ...args);
  const warn =   (...args)=>logImpl(1, ...args);
  const error =  (...args)=>logImpl(0, ...args);

  /**
     __openFiles is a map of sqlite3_file pointers (integers) to
     metadata related to a given OPFS file handles. The pointers are, in
     this side of the interface, opaque file handle IDs provided by the
     synchronous part of this constellation. Each value is an object
     with a structure demonstrated in the xOpen() impl.
  */
  const __openFiles = Object.create(null);
  /**
     __implicitLocks is a Set of sqlite3_file pointers (integers)
     which were "auto-locked".  i.e. those for which we necessarily
     obtain a sync access handle without an explicit xLock() call
     guarding access. Such locks will be released during
     `waitLoop()`'s idle time, whereas a sync access handle obtained
     via xLock(), or subsequently xLock()'d after auto-acquisition,
     will not be released until xUnlock() is called.

     Maintenance reminder: if we relinquish auto-locks at the end of the
     operation which acquires them, we pay a massive performance
     penalty: speedtest1 benchmarks take up to 4x as long. By delaying
     the lock release until idle time, the hit is negligible.
  */
  const __implicitLocks = new Set();

  /**
     Expects an OPFS file path. It gets resolved, such that ".."
     components are properly expanded, and returned. If the 2nd arg is
     true, the result is returned as an array of path elements, else an
     absolute path string is returned.
  */
  const getResolvedPath = function(filename,splitIt){
    const p = new URL(
      filename, 'file://irrelevant'
    ).pathname;
    return splitIt ? p.split('/').filter((v)=>!!v) : p;
  };

  /**
     Takes the absolute path to a filesystem element. Returns an array
     of [handleOfContainingDir, filename]. If the 2nd argument is truthy
     then each directory element leading to the file is created along
     the way. Throws if any creation or resolution fails.
  */
  const getDirForFilename = async function f(absFilename, createDirs = false){
    const path = getResolvedPath(absFilename, true);
    const filename = path.pop();
    let dh = state.rootDir;
    for(const dirName of path){
      if(dirName){
        dh = await dh.getDirectoryHandle(dirName, {create: !!createDirs});
      }
    }
    return [dh, filename];
  };

  /**
     If the given file-holding object has a sync handle attached to it,
     that handle is removed and asynchronously closed. Though it may
     sound sensible to continue work as soon as the close() returns
     (noting that it's asynchronous), doing so can cause operations
     performed soon afterwards, e.g. a call to getSyncHandle(), to fail
     because they may happen out of order from the close(). OPFS does
     not guaranty that the actual order of operations is retained in
     such cases. i.e.  always "await" on the result of this function.
  */
  const closeSyncHandle = async (fh)=>{
    if(fh.syncHandle){
      log("Closing sync handle for",fh.filenameAbs);
      const h = fh.syncHandle;
      delete fh.syncHandle;
      delete fh.xLock;
      __implicitLocks.delete(fh.fid);
      return h.close();
    }
  };

  /**
     A proxy for closeSyncHandle() which is guaranteed to not throw.

     This function is part of a lock/unlock step in functions which
     require a sync access handle but may be called without xLock()
     having been called first. Such calls need to release that
     handle to avoid locking the file for all of time. This is an
     _attempt_ at reducing cross-tab contention but it may prove
     to be more of a problem than a solution and may need to be
     removed.
  */
  const closeSyncHandleNoThrow = async (fh)=>{
    try{await closeSyncHandle(fh)}
    catch(e){
      warn("closeSyncHandleNoThrow() ignoring:",e,fh);
    }
  };

  /* Release all auto-locks. */
  const releaseImplicitLocks = async ()=>{
    if(__implicitLocks.size){
      /* Release all auto-locks. */
      for(const fid of __implicitLocks){
        const fh = __openFiles[fid];
        await closeSyncHandleNoThrow(fh);
        log("Auto-unlocked",fid,fh.filenameAbs);
      }
    }
  };

  /**
     An experiment in improving concurrency by freeing up implicit locks
     sooner. This is known to impact performance dramatically but it has
     also shown to improve concurrency considerably.

     If fh.releaseImplicitLocks is truthy and fh is in __implicitLocks,
     this routine returns closeSyncHandleNoThrow(), else it is a no-op.
  */
  const releaseImplicitLock = async (fh)=>{
    if(fh.releaseImplicitLocks && __implicitLocks.has(fh.fid)){
      return closeSyncHandleNoThrow(fh);
    }
  };

  /**
     An error class specifically for use with getSyncHandle(), the goal
     of which is to eventually be able to distinguish unambiguously
     between locking-related failures and other types, noting that we
     cannot currently do so because createSyncAccessHandle() does not
     define its exceptions in the required level of detail.

     2022-11-29: according to:

     https://github.com/whatwg/fs/pull/21

     NoModificationAllowedError will be the standard exception thrown
     when acquisition of a sync access handle fails due to a locking
     error. As of this writing, that error type is not visible in the
     dev console in Chrome v109, nor is it documented in MDN, but an
     error with that "name" property is being thrown from the OPFS
     layer.
  */
  class GetSyncHandleError extends Error {
    constructor(errorObject, ...msg){
      super([
        ...msg, ': '+errorObject.name+':',
        errorObject.message
      ].join(' '), {
        cause: errorObject
      });
      this.name = 'GetSyncHandleError';
    }
  };

  /**
     Attempts to find a suitable SQLITE_xyz result code for Error
     object e. Returns either such a translation or rc if if it does
     not know how to translate the exception.
  */
  GetSyncHandleError.convertRc = (e,rc)=>{
    if( e instanceof GetSyncHandleError ){
      if( e.cause.name==='NoModificationAllowedError'
        /* Inconsistent exception.name from Chrome/ium with the
           same exception.message text: */
          || (e.cause.name==='DOMException'
              && 0===e.cause.message.indexOf('Access Handles cannot')) ){
        return state.sq3Codes.SQLITE_BUSY;
      }else if( 'NotFoundError'===e.cause.name ){
        /**
           Maintenance reminder: SQLITE_NOTFOUND, though it looks like
           a good match, has different semantics than NotFoundError
           and is not suitable here.
        */
        return state.sq3Codes.SQLITE_CANTOPEN;
      }
    }else if( 'NotFoundError'===e?.name ){
      return state.sq3Codes.SQLITE_CANTOPEN;
    }
    return rc;
  };

  /**
     Returns the sync access handle associated with the given file
     handle object (which must be a valid handle object, as created by
     xOpen()), lazily opening it if needed.

     In order to help alleviate cross-tab contention for a dabase, if
     an exception is thrown while acquiring the handle, this routine
     will wait briefly and try again, up to `maxTries` of times. If
     acquisition still fails at that point it will give up and
     propagate the exception. Client-level code will see that either
     as an I/O error or SQLITE_BUSY, depending on the exception and
     the context.

     2024-06-12: there is a rare race condition here which has been
     reported a single time:

     https://sqlite.org/forum/forumpost/9ee7f5340802d600

     What appears to be happening is that file we're waiting for a
     lock on is deleted while we wait. What currently happens here is
     that a locking exception is thrown but the exception type is
     NotFoundError. In such cases, we very probably should attempt to
     re-open/re-create the file an obtain the lock on it (noting that
     there's another race condition there). That's easy to say but
     creating a viable test for that condition has proven challenging
     so far.

     Interface quirk: if fh.xLock is falsy and the handle is acquired
     then fh.fid is added to __implicitLocks(). If fh.xLock is truthy,
     it is not added as an implicit lock. i.e. xLock() impls must set
     fh.xLock immediately _before_ calling this and must arrange to
     restore it to its previous value if this function throws.

     2026-03-06:

     - baseWaitTime is the number of milliseconds to wait for the
     first retry, increasing by one factor for each retry. It defaults
     to (state.asyncIdleWaitTime*2).

     - maxTries is the number of attempt to make, each one spaced out
     by one additional factor of the baseWaitTime (e.g. 300, then 600,
     then 900, the 1200...). This MUST be an integer >0.

     Only the Web Locks impl should use the 3rd and 4th parameters.
  */
  const getSyncHandle = async (fh, opName, baseWaitTime, maxTries = 6)=>{
    if(!fh.syncHandle){
      const t = performance.now();
      log("Acquiring sync handle for",fh.filenameAbs);
      const msBase = baseWaitTime ?? (state.asyncIdleWaitTime * 2);
      maxTries ??= 6;
      let i = 1, ms = msBase;
      for(; true; ms = msBase * ++i){
        try {
          //if(i<3) toss("Just testing getSyncHandle() wait-and-retry.");
          //TODO? A config option which tells it to throw here
          //randomly every now and then, for testing purposes.
          fh.syncHandle = await fh.fileHandle.createSyncAccessHandle();
          break;
        }catch(e){
          if(i === maxTries){
            throw new GetSyncHandleError(
              e, "Error getting sync handle for",opName+"().",maxTries,
              "attempts failed.",fh.filenameAbs
            );
          }
          warn("Error getting sync handle for",opName+"(). Waiting",ms,
               "ms and trying again.",fh.filenameAbs,e);
          Atomics.wait(state.sabOPView, state.opIds.retry, 0, ms);
        }
      }
      log("Got",opName+"() sync handle for",fh.filenameAbs,
          'in',performance.now() - t,'ms');
      if(!fh.xLock){
        __implicitLocks.add(fh.fid);
        log("Acquired implicit lock for",opName+"()",fh.fid,fh.filenameAbs);
      }
    }
    return fh.syncHandle;
  };

  /**
     Stores the given value at state.sabOPView[state.opIds.rc] and then
     Atomics.notify()'s it.

     The opName is only used for logging and debugging - all result
     codes are expected on the same state.sabOPView slot.
  */
  const storeAndNotify = (opName, value)=>{
    log(opName+"() => notify(",value,")");
    Atomics.store(state.sabOPView, state.opIds.rc, value);
    Atomics.notify(state.sabOPView, state.opIds.rc);
  };

  /**
     Throws if fh is a file-holding object which is flagged as read-only.
  */
  const affirmNotRO = function(opName,fh){
    if(fh.readOnly) toss(opName+"(): File is read-only: "+fh.filenameAbs);
  };

  /**
     Gets set to true by the 'opfs-async-shutdown' command to quit the
     wait loop. This is only intended for debugging purposes: we cannot
     inspect this file's state while the tight waitLoop() is running and
     need a way to stop that loop for introspection purposes.
  */
  let flagAsyncShutdown = false;

  /**
     Asynchronous wrappers for sqlite3_vfs and sqlite3_io_methods
     methods, as well as helpers like mkdir().
  */
  const vfsAsyncImpls = {
    'opfs-async-shutdown': async ()=>{
      flagAsyncShutdown = true;
      storeAndNotify('opfs-async-shutdown', 0);
    },
    mkdir: async (dirname)=>{
      let rc = 0;
      try {
        await getDirForFilename(dirname+"/filepart", true);
      }catch(e){
        state.s11n.storeException(2,e);
        rc = state.sq3Codes.SQLITE_IOERR;
      }
      storeAndNotify('mkdir', rc);
    },
    xAccess: async (filename)=>{
      /* OPFS cannot support the full range of xAccess() queries
         sqlite3 calls for. We can essentially just tell if the file
         is accessible, but if it is then it's automatically writable
         (unless it's locked, which we cannot(?) know without trying
         to open it). OPFS does not have the notion of read-only.

         The return semantics of this function differ from sqlite3's
         xAccess semantics because we are limited in what we can
         communicate back to our synchronous communication partner: 0 =
         accessible, non-0 means not accessible.
      */
      let rc = 0;
      try{
        const [dh, fn] = await getDirForFilename(filename);
        await dh.getFileHandle(fn);
      }catch(e){
        state.s11n.storeException(2,e);
        rc = state.sq3Codes.SQLITE_IOERR;
      }
      storeAndNotify('xAccess', rc);
    },
    xClose: async function(fid/*sqlite3_file pointer*/){
      const opName = 'xClose';
      __implicitLocks.delete(fid);
      const fh = __openFiles[fid];
      let rc = 0;
      if(fh){
        delete __openFiles[fid];
        await closeSyncHandle(fh);
        if(fh.deleteOnClose){
          try{ await fh.dirHandle.removeEntry(fh.filenamePart) }
          catch(e){ warn("Ignoring dirHandle.removeEntry() failure of",fh,e) }
        }
      }else{
        state.s11n.serialize();
        rc = state.sq3Codes.SQLITE_NOTFOUND;
      }
      storeAndNotify(opName, rc);
    },
    xDelete: async function(...args){
      const rc = await vfsAsyncImpls.xDeleteNoWait(...args);
      storeAndNotify('xDelete', rc);
    },
    xDeleteNoWait: async function(filename, syncDir = 0, recursive = false){
      /* The syncDir flag is, for purposes of the VFS API's semantics,
         ignored here. However, if it has the value 0x1234 then: after
         deleting the given file, recursively try to delete any empty
         directories left behind in its wake (ignoring any errors and
         stopping at the first failure).

         That said: we don't know for sure that removeEntry() fails if
         the dir is not empty because the API is not documented. It has,
         however, a "recursive" flag which defaults to false, so
         presumably it will fail if the dir is not empty and that flag
         is false.
      */
      let rc = 0;
      try {
        while(filename){
          const [hDir, filenamePart] = await getDirForFilename(filename, false);
          if(!filenamePart) break;
          await hDir.removeEntry(filenamePart, {recursive});
          if(0x1234 !== syncDir) break;
          recursive = false;
          filename = getResolvedPath(filename, true);
          filename.pop();
          filename = filename.join('/');
        }
      }catch(e){
        state.s11n.storeException(2,e);
        rc = state.sq3Codes.SQLITE_IOERR_DELETE;
      }
      return rc;
    },
    xFileSize: async function(fid/*sqlite3_file pointer*/){
      const fh = __openFiles[fid];
      let rc = 0;
      try{
        const sz = await (await getSyncHandle(fh,'xFileSize')).getSize();
        state.s11n.serialize(Number(sz));
      }catch(e){
        state.s11n.storeException(1,e);
        rc = GetSyncHandleError.convertRc(e,state.sq3Codes.SQLITE_IOERR);
      }
      await releaseImplicitLock(fh);
      storeAndNotify('xFileSize', rc);
    },
    /**
       The first argument is semantically invalid here - it's an
       address in the synchronous side's heap. We can do nothing with
       it here except use it as a unique-per-file identifier.
       i.e. a lookup key.
    */
    xOpen: async function(fid/*sqlite3_file pointer*/, filename,
                          flags/*SQLITE_OPEN_...*/,
                          opfsFlags/*OPFS_...*/){
      const opName = 'xOpen';
      const create = (state.sq3Codes.SQLITE_OPEN_CREATE & flags);
      try{
        let hDir, filenamePart;
        try {
          [hDir, filenamePart] = await getDirForFilename(filename, !!create);
        }catch(e){
          state.s11n.storeException(1,e);
          storeAndNotify(opName, state.sq3Codes.SQLITE_NOTFOUND);
          return;
        }
        if( state.opfsFlags.OPFS_UNLINK_BEFORE_OPEN & opfsFlags ){
          try{
            await hDir.removeEntry(filenamePart);
          }catch(e){
            /* ignoring */
            //warn("Ignoring failed Unlink of",filename,":",e);
          }
        }
        const hFile = await hDir.getFileHandle(filenamePart, {create});
        const fh = Object.assign(Object.create(null),{
          fid: fid,
          filenameAbs: filename,
          filenamePart: filenamePart,
          dirHandle: hDir,
          fileHandle: hFile,
          sabView: state.sabFileBufView,
          readOnly: !create && !!(state.sq3Codes.SQLITE_OPEN_READONLY & flags),
          deleteOnClose: !!(state.sq3Codes.SQLITE_OPEN_DELETEONCLOSE & flags)
        });
        fh.releaseImplicitLocks =
          (opfsFlags & state.opfsFlags.OPFS_UNLOCK_ASAP)
          || state.opfsFlags.defaultUnlockAsap;
        __openFiles[fid] = fh;
        storeAndNotify(opName, 0);
      }catch(e){
        error(opName,e);
        state.s11n.storeException(1,e);
        storeAndNotify(opName, state.sq3Codes.SQLITE_IOERR);
      }
    },
    xRead: async function(fid/*sqlite3_file pointer*/,n,offset64){
      let rc = 0, nRead;
      const fh = __openFiles[fid];
      try{
        nRead = (await getSyncHandle(fh,'xRead')).read(
          fh.sabView.subarray(0, n),
          {at: Number(offset64)}
        );
        if(nRead < n){/* Zero-fill remaining bytes */
          fh.sabView.fill(0, nRead, n);
          rc = state.sq3Codes.SQLITE_IOERR_SHORT_READ;
        }
      }catch(e){
        //error("xRead() failed",e,fh);
        state.s11n.storeException(1,e);
        rc = GetSyncHandleError.convertRc(e,state.sq3Codes.SQLITE_IOERR_READ);
      }
      await releaseImplicitLock(fh);
      storeAndNotify('xRead',rc);
    },
    xSync: async function(fid/*sqlite3_file pointer*/,flags/*ignored*/){
      const fh = __openFiles[fid];
      let rc = 0;
      if(!fh.readOnly && fh.syncHandle){
        try {
          await fh.syncHandle.flush();
        }catch(e){
          state.s11n.storeException(2,e);
          rc = state.sq3Codes.SQLITE_IOERR_FSYNC;
        }
      }
      storeAndNotify('xSync',rc);
    },
    xTruncate: async function(fid/*sqlite3_file pointer*/,size){
      let rc = 0;
      const fh = __openFiles[fid];
      try{
        affirmNotRO('xTruncate', fh);
        await (await getSyncHandle(fh,'xTruncate')).truncate(size);
      }catch(e){
        //error("xTruncate():",e,fh);
        state.s11n.storeException(2,e);
        rc = GetSyncHandleError.convertRc(e,state.sq3Codes.SQLITE_IOERR_TRUNCATE);
      }
      await releaseImplicitLock(fh);
      storeAndNotify('xTruncate',rc);
    },
    xWrite: async function(fid/*sqlite3_file pointer*/,n,offset64){
      let rc;
      const fh = __openFiles[fid];
      try{
        affirmNotRO('xWrite', fh);
        rc = (
          n === (await getSyncHandle(fh,'xWrite'))
            .write(fh.sabView.subarray(0, n),
                   {at: Number(offset64)})
        ) ? 0 : state.sq3Codes.SQLITE_IOERR_WRITE;
      }catch(e){
        //error("xWrite():",e,fh);
        state.s11n.storeException(1,e);
        rc = GetSyncHandleError.convertRc(e,state.sq3Codes.SQLITE_IOERR_WRITE);
      }
      await releaseImplicitLock(fh);
      storeAndNotify('xWrite',rc);
    }
  }/*vfsAsyncImpls*/;

  if( isWebLocker ){
    /* We require separate xLock() and xUnlock() implementations for the
       original and Web Lock implementations. The ones in this block
       are for the WebLock impl.

       The Golden Rule for this impl is: if we have a web lock, we
       must also hold the SAH. When "upgrading" an implicit lock to a
       requested (explicit) lock, we must remove the SAH from the
       __implicitLocks set. When we unlock, we release both the web
       lock and the SAH. That invariant must be kept intact or race
       conditions on SAHs will ensue.
    */
    /** Registry of active Web Locks: fid -> { mode, resolveRelease } */
    const __activeWebLocks = Object.create(null);

    vfsAsyncImpls.xLock = async function(fid/*sqlite3_file pointer*/,
                                         lockType/*SQLITE_LOCK_...*/,
                                         isFromUnlock/*only if called from this.xUnlock()*/){
      const whichOp = isFromUnlock ? 'xUnlock' : 'xLock';
      const fh = __openFiles[fid];
      //error("xLock()",fid, lockType, isFromUnlock, fh);
      const requestedMode = (lockType >= state.sq3Codes.SQLITE_LOCK_RESERVED)
            ? 'exclusive' : 'shared';
      const existing = __activeWebLocks[fid];
      if( existing ){
        if( existing.mode === requestedMode
            || (existing.mode === 'exclusive'
                && requestedMode === 'shared') ) {
          fh.xLock = lockType;
          storeAndNotify(whichOp, 0);
          /* Don't do this: existing.mode = requestedMode;

             Paraphrased from advice given by a consulting developer:

             If you hold an exclusive lock and SQLite requests shared,
             you should keep exiting.mode as exclusive in because the
             underlying Web Lock is still exclusive. Changing it to
             shared would trick xLock into thinking it needs to
             perform a release/re-acquire dance if an exclusive is
             later requested.
          */
          return 0 /* Already held at required or higher level */;
        }
        /*
          Upgrade path: we must release shared and acquire exclusive.
          This transition is NOT atomic in Web Locks API.

          It _effectively_ is atomic if we don't call
          closeSyncHandle(fh), as no other worker can lock that until
          we let it go. But we can't do that without eventually
          leading to deadly embrace situations, so we don't do that.
          (That's not a hypothetical, it has happened.)
        */
        await closeSyncHandle(fh);
        existing.resolveRelease();
        delete __activeWebLocks[fid];
      }

      const lockName = "sqlite3-vfs-opfs:" + fh.filenameAbs;
      const oldLockType = fh.xLock;
      return new Promise((resolveWaitLoop) => {
        //log("xLock() initial promise entered...");
        navigator.locks.request(lockName, { mode: requestedMode }, async (lock) => {
          //log("xLock() Web Lock entered.", fh);
          __implicitLocks.delete(fid);
          let rc = 0;
          try{
            fh.xLock = lockType/*must be set before getSyncHandle() is called!*/;
            await getSyncHandle(fh, 'xLock', state.asyncIdleWaitTime, 5);
          }catch(e){
            fh.xLock = oldLockType;
            state.s11n.storeException(1, e);
            rc = GetSyncHandleError.convertRc(e, state.sq3Codes.SQLITE_BUSY);
          }
          const releasePromise = rc
                ? undefined
                : new Promise((resolveRelease) => {
                  __activeWebLocks[fid] = { mode: requestedMode, resolveRelease };
                });
          storeAndNotify(whichOp, rc) /* unblock the C side */;
          resolveWaitLoop(0) /* unblock waitLoop() */;
          await releasePromise /* hold the lock until xUnlock */;
        });
      });
    };

    /** Internal helper for the opfs-wl xUnlock() */
    const wlCloseHandle = async(fh)=>{
      let rc = 0;
      try{
        /* For the record, we've never once seen closeSyncHandle()
           throw, nor should it because destructors do not throw. */
        await closeSyncHandle(fh);
      }catch(e){
        state.s11n.storeException(1,e);
        rc = state.sq3Codes.SQLITE_IOERR_UNLOCK;
      }
      return rc;
    };

    vfsAsyncImpls.xUnlock = async function(fid/*sqlite3_file pointer*/,
                                           lockType/*SQLITE_LOCK_...*/){
      const fh = __openFiles[fid];
      const existing = __activeWebLocks[fid];
      if( !existing ){
        const rc = await wlCloseHandle(fh);
        storeAndNotify('xUnlock', rc);
        return rc;
      }
      //log("xUnlock()",fid, lockType, fh);
      let rc = 0;
      if( lockType === state.sq3Codes.SQLITE_LOCK_NONE ){
        /* SQLite usually unlocks all the way to NONE */
        rc = await wlCloseHandle(fh);
        existing.resolveRelease();
        delete __activeWebLocks[fid];
        fh.xLock = lockType;
      }else if( lockType === state.sq3Codes.SQLITE_LOCK_SHARED
                && existing.mode === 'exclusive' ){
        /* downgrade EXCLUSIVE -> SHARED */
        rc = await wlCloseHandle(fh);
        if( 0===rc ){
          fh.xLock = lockType;
          existing.resolveRelease();
          delete __activeWebLocks[fid];
          return vfsAsyncImpls.xLock(fid, lockType, true);
        }
      }else{
        /* ??? */
        error("xUnlock() unhandled condition", fh);
      }
      storeAndNotify('xUnlock', rc);
      return 0;
    }

  }else{
    /* Original/"legacy" xLock() and xUnlock() */

    vfsAsyncImpls.xLock = async function(fid/*sqlite3_file pointer*/,
                                         lockType/*SQLITE_LOCK_...*/){
      const fh = __openFiles[fid];
      let rc = 0;
      const oldLockType = fh.xLock;
      fh.xLock = lockType;
      if( !fh.syncHandle ){
        try {
          await getSyncHandle(fh,'xLock');
          __implicitLocks.delete(fid);
        }catch(e){
          state.s11n.storeException(1,e);
          rc = GetSyncHandleError.convertRc(e,state.sq3Codes.SQLITE_IOERR_LOCK);
          fh.xLock = oldLockType;
        }
      }
      storeAndNotify('xLock',rc);
    };

    vfsAsyncImpls.xUnlock = async function(fid/*sqlite3_file pointer*/,
                                           lockType/*SQLITE_LOCK_...*/){
      let rc = 0;
      const fh = __openFiles[fid];
      if( fh.syncHandle
          && state.sq3Codes.SQLITE_LOCK_NONE===lockType
          /* Note that we do not differentiate between lock types in
             this VFS. We're either locked or unlocked. */ ){
        try { await closeSyncHandle(fh) }
        catch(e){
          state.s11n.storeException(1,e);
          rc = state.sq3Codes.SQLITE_IOERR_UNLOCK;
        }
      }
      storeAndNotify('xUnlock',rc);
    }

  }/*xLock() and xUnlock() impls*/

  const waitLoop = async function f(){
    if( !f.inited ){
      f.inited = true;
      f.opHandlers = Object.create(null);
      for(let k of Object.keys(state.opIds)){
        const vi = vfsAsyncImpls[k];
        if(!vi) continue;
        const o = Object.create(null);
        f.opHandlers[state.opIds[k]] = o;
        o.key = k;
        o.f = vi;
      }
    }
    const opIds = state.opIds;
    const opView = state.sabOPView;
    const slotWhichOp = opIds.whichOp;
    const idleWaitTime = state.asyncIdleWaitTime;
    const hasWaitAsync = !!Atomics.waitAsync;
//#if 0
    error("waitLoop init: isWebLocker",isWebLocker,
          "idleWaitTime",idleWaitTime,
          "hasWaitAsync",hasWaitAsync);
//#/if
    while(!flagAsyncShutdown){
      try {
        let opId;
        if( hasWaitAsync ){
          opId = Atomics.load(opView, slotWhichOp);
          if( 0===opId ){
            const rv = Atomics.waitAsync(opView, slotWhichOp, 0,
                                         idleWaitTime);
            if( rv.async ) await rv.value;
            await releaseImplicitLocks();
            continue;
          }
        }else{
          /**
             For browsers without Atomics.waitAsync(), we require
             the legacy implementation. Browser versions where
             waitAsync() arrived:

             Chrome: 90 (2021-04-13)
             Firefox: 145 (2025-11-11)
             Safari: 16.4 (2023-03-27)

             The "opfs" VFS was not born until Chrome was somewhere in
             the v104-108 range (Summer/Autumn 2022) and did not work
             with Safari < v17 (2023-09-18) due to a WebKit bug which
             restricted OPFS access from sub-Workers.

             The waitAsync() counterpart of this block can be used by
             both "opfs" and "opfs-wl", whereas this block can only be
             used by "opfs". Performance comparisons between the two
             in high-contention tests have been indecisive.
          */
          if('not-equal'!==Atomics.wait(
            state.sabOPView, slotWhichOp, 0, state.asyncIdleWaitTime
          )){
            /* Maintenance note: we compare against 'not-equal' because

               https://github.com/tomayac/sqlite-wasm/issues/12

               is reporting that this occasionally, under high loads,
               returns 'ok', which leads to the whichOp being 0 (which
               isn't a valid operation ID and leads to an exception,
               along with a corresponding ugly console log
               message). Unfortunately, the conditions for that cannot
               be reliably reproduced. The only place in our code which
               writes a 0 to the state.opIds.whichOp SharedArrayBuffer
               index is a few lines down from here, and that instance
               is required in order for clear communication between
               the sync half of this proxy and this half.

               Much later (2026-03-07): that phenomenon is apparently
               called a spurious wakeup.
            */
            await releaseImplicitLocks();
            continue;
          }
          opId = Atomics.load(state.sabOPView, slotWhichOp);
        }
        Atomics.store(opView, slotWhichOp, 0);
        const hnd = f.opHandlers[opId]?.f ?? toss("No waitLoop handler for whichOp #",opId);
        const args = state.s11n.deserialize(
          true /* clear s11n to keep the caller from confusing this with
                  an exception string written by the upcoming
                  operation */
        ) || [];
        //error("waitLoop() whichOp =",opId, f.opHandlers[opId].key, args);
        await hnd(...args);
      }catch(e){
        error('in waitLoop():', e);
      }
    }
  };

  navigator.storage.getDirectory().then(function(d){
    state.rootDir = d;
    globalThis.onmessage = function({data}){
      //log(globalThis.location.href,"onmessage()",data);
      switch(data.type){
          case 'opfs-async-init':{
            /* Receive shared state from synchronous partner */
            const opt = data.args;
            for(const k in opt) state[k] = opt[k];
            state.verbose = opt.verbose ?? 1;
            state.sabOPView = new Int32Array(state.sabOP);
            state.sabFileBufView = new Uint8Array(state.sabIO, 0, state.fileBufferSize);
            state.sabS11nView = new Uint8Array(state.sabIO, state.sabS11nOffset, state.sabS11nSize);
            Object.keys(vfsAsyncImpls).forEach((k)=>{
              if(!Number.isFinite(state.opIds[k])){
                toss("Maintenance required: missing state.opIds[",k,"]");
              }
            });
            initS11n();
            //warn("verbosity =",opt.verbose, state.verbose);
            log("init state",state);
            wPost('opfs-async-inited');
            waitLoop();
            break;
          }
          case 'opfs-async-restart':
            if(flagAsyncShutdown){
              warn("Restarting after opfs-async-shutdown. Might or might not work.");
              flagAsyncShutdown = false;
              waitLoop();
            }
          break;
      }
    };
    wPost('opfs-async-loaded');
  }).catch((e)=>error("error initializing OPFS asyncer:",e));
}/*installAsyncProxy()*/;
if(globalThis.window === globalThis){
  wPost('opfs-unavailable',
        "This code cannot run from the main thread.",
        "Load it as a Worker from a separate Worker.");
}else if(!globalThis.SharedArrayBuffer){
  wPost('opfs-unavailable', "Missing SharedArrayBuffer API.",
        "The server must emit the COOP/COEP response headers to enable that.");
}else if(!globalThis.Atomics){
  wPost('opfs-unavailable', "Missing Atomics API.",
        "The server must emit the COOP/COEP response headers to enable that.");
}else if(isWebLocker && !globalThis.Atomics.waitAsync){
  wPost('opfs-unavailable',"Missing required Atomics.waitSync() for "+vfsName);
}else if(!globalThis.FileSystemHandle ||
         !globalThis.FileSystemDirectoryHandle ||
         !globalThis.FileSystemFileHandle?.prototype?.createSyncAccessHandle ||
         !navigator?.storage?.getDirectory){
  wPost('opfs-unavailable',"Missing required OPFS APIs.");
}else{
  installAsyncProxy();
}
