//#if not target:node
/*
  2026-03-04

  The author disclaims copyright to this source code.  In place of a
  legal notice, here is a blessing:

  *   May you do good and not evil.
  *   May you find forgiveness for yourself and forgive others.
  *   May you share freely, never taking more than you give.

  ***********************************************************************

  This file holds code shared by sqlite3-vfs-opfs{,-wl}.c-pp.js. It
  creates a private/internal sqlite3.opfs namespace common to the two
  and used (only) by them and the test framework. It is not part of
  the public API.
*/
globalThis.sqlite3ApiBootstrap.initializers.push(function(sqlite3){
  'use strict';
  const toss = sqlite3.util.toss;
  const capi = sqlite3.capi;
  const util = sqlite3.util;
  const wasm = sqlite3.wasm;

  /**
     Generic utilities for working with OPFS. This will get filled out
     by the Promise setup and, on success, installed as sqlite3.opfs.

     This is an internal/private namespace intended for use solely
     by the OPFS VFSes and test code for them. The library bootstrapping
     process removes this object in non-testing contexts.

  */
  const opfsUtil = sqlite3.opfs = Object.create(null);

  /**
     Returns true if _this_ thread has access to the OPFS APIs.
  */
  opfsUtil.thisThreadHasOPFS = ()=>{
    return globalThis.FileSystemHandle &&
      globalThis.FileSystemDirectoryHandle &&
      globalThis.FileSystemFileHandle &&
      globalThis.FileSystemFileHandle.prototype.createSyncAccessHandle &&
      navigator?.storage?.getDirectory;
  };

  /**
     Must be called by the OPFS VFSes immediately after they determine
     whether OPFS is available by calling
     thisThreadHasOPFS(). Resolves to the OPFS storage root directory
     and sets opfsUtil.rootDirectory to that value.
  */
  opfsUtil.getRootDir = async function f(){
    return f.promise ??= navigator.storage.getDirectory().then(d=>{
      opfsUtil.rootDirectory = d;
      return d;
    }).catch(e=>{
      delete f.promise;
      throw e;
    });
  };

  /**
     Expects an OPFS file path. It gets resolved, such that ".."
     components are properly expanded, and returned. If the 2nd arg
     is true, the result is returned as an array of path elements,
     else an absolute path string is returned.
  */
  opfsUtil.getResolvedPath = function(filename,splitIt){
    const p = new URL(filename, "file://irrelevant").pathname;
    return splitIt ? p.split('/').filter((v)=>!!v) : p;
  };

  /**
     Takes the absolute path to a filesystem element. Returns an
     array of [handleOfContainingDir, filename]. If the 2nd argument
     is truthy then each directory element leading to the file is
     created along the way. Throws if any creation or resolution
     fails.
  */
  opfsUtil.getDirForFilename = async function f(absFilename, createDirs = false){
    const path = opfsUtil.getResolvedPath(absFilename, true);
    const filename = path.pop();
    let dh = await opfsUtil.getRootDir();
    for(const dirName of path){
      if(dirName){
        dh = await dh.getDirectoryHandle(dirName, {create: !!createDirs});
      }
    }
    return [dh, filename];
  };

  /**
     Creates the given directory name, recursively, in
     the OPFS filesystem. Returns true if it succeeds or the
     directory already exists, else false.
  */
  opfsUtil.mkdir = async function(absDirName){
    try {
      await opfsUtil.getDirForFilename(absDirName+"/filepart", true);
      return true;
    }catch(e){
      //sqlite3.config.warn("mkdir(",absDirName,") failed:",e);
      return false;
    }
  };
  /**
     Checks whether the given OPFS filesystem entry exists,
     returning true if it does, false if it doesn't or if an
     exception is intercepted while trying to make the
     determination.
  */
  opfsUtil.entryExists = async function(fsEntryName){
    try {
      const [dh, fn] = await opfsUtil.getDirForFilename(fsEntryName);
      await dh.getFileHandle(fn);
      return true;
    }catch(e){
      return false;
    }
  };

  /**
     Generates a random ASCII string len characters long, intended for
     use as a temporary file name.
  */
  opfsUtil.randomFilename = function f(len=16){
    if(!f._chars){
      f._chars = "abcdefghijklmnopqrstuvwxyz"+
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"+
        "012346789";
      f._n = f._chars.length;
    }
    const a = [];
    let i = 0;
    for( ; i < len; ++i){
      const ndx = Math.random() * (f._n * 64) % f._n | 0;
      a[i] = f._chars[ndx];
    }
    return a.join("");
    /*
      An alternative impl. with an unpredictable length
      but much simpler:

      Math.floor(Math.random() * Number.MAX_SAFE_INTEGER).toString(36)
    */
  };

  /**
     Returns a promise which resolves to an object which represents
     all files and directories in the OPFS tree. The top-most object
     has two properties: `dirs` is an array of directory entries
     (described below) and `files` is a list of file names for all
     files in that directory.

     Traversal starts at sqlite3.opfs.rootDirectory.

     Each `dirs` entry is an object in this form:

     ```
     { name: directoryName,
     dirs: [...subdirs],
     files: [...file names]
     }
     ```

     The `files` and `subdirs` entries are always set but may be
     empty arrays.

     The returned object has the same structure but its `name` is
     an empty string. All returned objects are created with
     Object.create(null), so have no prototype.

     Design note: the entries do not contain more information,
     e.g. file sizes, because getting such info is not only
     expensive but is subject to locking-related errors.
  */
  opfsUtil.treeList = async function(){
    const doDir = async function callee(dirHandle,tgt){
      tgt.name = dirHandle.name;
      tgt.dirs = [];
      tgt.files = [];
      for await (const handle of dirHandle.values()){
        if('directory' === handle.kind){
          const subDir = Object.create(null);
          tgt.dirs.push(subDir);
          await callee(handle, subDir);
        }else{
          tgt.files.push(handle.name);
        }
      }
    };
    const root = Object.create(null);
    const dir = await opfsUtil.getRootDir();
    await doDir(dir, root);
    return root;
  };

  /**
     Irrevocably deletes _all_ files in the current origin's OPFS.
     Obviously, this must be used with great caution. It may throw
     an exception if removal of anything fails (e.g. a file is
     locked), but the precise conditions under which the underlying
     APIs will throw are not documented (so we cannot tell you what
     they are).
  */
  opfsUtil.rmfr = async function(){
    const rd = await opfsUtil.getRootDir();
    const dir = rd, opt = {recurse: true};
    for await (const handle of dir.values()){
      dir.removeEntry(handle.name, opt);
    }
  };

  /**
     Deletes the given OPFS filesystem entry.  As this environment
     has no notion of "current directory", the given name must be an
     absolute path. If the 2nd argument is truthy, deletion is
     recursive (use with caution!).

     The returned Promise resolves to true if the deletion was
     successful, else false (but...). The OPFS API reports the
     reason for the failure only in human-readable form, not
     exceptions which can be type-checked to determine the
     failure. Because of that...

     If the final argument is truthy then this function will
     propagate any exception on error, rather than returning false.
  */
  opfsUtil.unlink = async function(fsEntryName, recursive = false,
                                   throwOnError = false){
    try {
      const [hDir, filenamePart] =
            await opfsUtil.getDirForFilename(fsEntryName, false);
      await hDir.removeEntry(filenamePart, {recursive});
      return true;
    }catch(e){
      if(throwOnError){
        throw new Error("unlink(",arguments[0],") failed: "+e.message,{
          cause: e
        });
      }
      return false;
    }
  };

  /**
     Traverses the OPFS filesystem, calling a callback for each
     entry.  The argument may be either a callback function or an
     options object with any of the following properties:

     - `callback`: function which gets called for each filesystem
     entry.  It gets passed 3 arguments: 1) the
     FileSystemFileHandle or FileSystemDirectoryHandle of each
     entry (noting that both are instanceof FileSystemHandle). 2)
     the FileSystemDirectoryHandle of the parent directory. 3) the
     current depth level, with 0 being at the top of the tree
     relative to the starting directory. If the callback returns a
     literal false, as opposed to any other falsy value, traversal
     stops without an error. Any exceptions it throws are
     propagated. Results are undefined if the callback manipulate
     the filesystem (e.g. removing or adding entries) because the
     how OPFS iterators behave in the face of such changes is
     undocumented.

     - `recursive` [bool=true]: specifies whether to recurse into
     subdirectories or not. Whether recursion is depth-first or
     breadth-first is unspecified!

     - `directory` [FileSystemDirectoryEntry=sqlite3.opfs.rootDirectory]
     specifies the starting directory.

     If this function is passed a function, it is assumed to be the
     callback.

     Returns a promise because it has to (by virtue of being async)
     but that promise has no specific meaning: the traversal it
     performs is synchronous. The promise must be used to catch any
     exceptions propagated by the callback, however.
  */
  opfsUtil.traverse = async function(opt){
    const defaultOpt = {
      recursive: true,
      directory: await opfsUtil.getRootDir()
    };
    if('function'===typeof opt){
      opt = {callback:opt};
    }
    opt = Object.assign(defaultOpt, opt||{});
    const doDir = async function callee(dirHandle, depth){
      for await (const handle of dirHandle.values()){
        if(false === opt.callback(handle, dirHandle, depth)) return false;
        else if(opt.recursive && 'directory' === handle.kind){
          if(false === await callee(handle, depth + 1)) break;
        }
      }
    };
    doDir(opt.directory, 0);
  };

  /**
     Impl of opfsUtil.importDb() when it's given a function as its
     second argument.
  */
  const importDbChunked = async function(filename, callback){
    const [hDir, fnamePart] = await opfsUtil.getDirForFilename(filename, true);
    const hFile = await hDir.getFileHandle(fnamePart, {create:true});
    let sah = await hFile.createSyncAccessHandle();
    let nWrote = 0, chunk, checkedHeader = false, err = false;
    try{
      sah.truncate(0);
      while( undefined !== (chunk = await callback()) ){
        if(chunk instanceof ArrayBuffer) chunk = new Uint8Array(chunk);
        if( !checkedHeader && 0===nWrote && chunk.byteLength>=15 ){
          util.affirmDbHeader(chunk);
          checkedHeader = true;
        }
        sah.write(chunk, {at: nWrote});
        nWrote += chunk.byteLength;
      }
      if( nWrote < 512 || 0!==nWrote % 512 ){
        toss("Input size",nWrote,"is not correct for an SQLite database.");
      }
      if( !checkedHeader ){
        const header = new Uint8Array(20);
        sah.read( header, {at: 0} );
        util.affirmDbHeader( header );
      }
      sah.write(new Uint8Array([1,1]), {at: 18}/*force db out of WAL mode*/);
      return nWrote;
    }catch(e){
      await sah.close();
      sah = undefined;
      await hDir.removeEntry( fnamePart ).catch(()=>{});
      throw e;
    }finally {
      if( sah ) await sah.close();
    }
  };

  /**
     Asynchronously imports the given bytes (a byte array or
     ArrayBuffer) into the given database file.

     Results are undefined if the given db name refers to an opened
     db.

     If passed a function for its second argument, its behaviour
     changes: imports its data in chunks fed to it by the given
     callback function. It calls the callback (which may be async)
     repeatedly, expecting either a Uint8Array or ArrayBuffer (to
     denote new input) or undefined (to denote EOF). For so long as
     the callback continues to return non-undefined, it will append
     incoming data to the given VFS-hosted database file. When
     called this way, the resolved value of the returned Promise is
     the number of bytes written to the target file.

     It very specifically requires the input to be an SQLite3
     database and throws if that's not the case.  It does so in
     order to prevent this function from taking on a larger scope
     than it is specifically intended to. i.e. we do not want it to
     become a convenience for importing arbitrary files into OPFS.

     This routine rewrites the database header bytes in the output
     file (not the input array) to force disabling of WAL mode.

     On error this throws and the state of the input file is
     undefined (it depends on where the exception was triggered).

     On success, resolves to the number of bytes written.
  */
  opfsUtil.importDb = async function(filename, bytes){
    if( bytes instanceof Function ){
      return importDbChunked(filename, bytes);
    }
    if(bytes instanceof ArrayBuffer) bytes = new Uint8Array(bytes);
    util.affirmIsDb(bytes);
    const n = bytes.byteLength;
    const [hDir, fnamePart] = await opfsUtil.getDirForFilename(filename, true);
    let sah, err, nWrote = 0;
    try {
      const hFile = await hDir.getFileHandle(fnamePart, {create:true});
      sah = await hFile.createSyncAccessHandle();
      sah.truncate(0);
      nWrote = sah.write(bytes, {at: 0});
      if(nWrote != n){
        toss("Expected to write "+n+" bytes but wrote "+nWrote+".");
      }
      sah.write(new Uint8Array([1,1]), {at: 18}) /* force db out of WAL mode */;
      return nWrote;
    }catch(e){
      if( sah ){ await sah.close(); sah = undefined; }
      await hDir.removeEntry( fnamePart ).catch(()=>{});
      throw e;
    }finally{
      if( sah ) await sah.close();
    }
  };

  /**
     Checks for features required for OPFS VFSes and throws with a
     descriptive error message if they're not found. This is intended
     to be run as part of async VFS installation steps.
  */
  opfsUtil.vfsInstallationFeatureCheck = function(){
    if(!globalThis.SharedArrayBuffer
       || !globalThis.Atomics){
      throw new Error("Cannot install OPFS: Missing SharedArrayBuffer and/or Atomics. "+
                      "The server must emit the COOP/COEP response headers to enable those. "+
                      "See https://sqlite.org/wasm/doc/trunk/persistence.md#coop-coep");
    }else if('undefined'===typeof WorkerGlobalScope){
      throw new Error("The OPFS sqlite3_vfs cannot run in the main thread "+
                      "because it requires Atomics.wait().");
    }else if(!globalThis.FileSystemHandle ||
             !globalThis.FileSystemDirectoryHandle ||
             !globalThis.FileSystemFileHandle ||
             !globalThis.FileSystemFileHandle.prototype.createSyncAccessHandle ||
             !navigator?.storage?.getDirectory){
      throw new newError("Missing required OPFS APIs.");
    }
  };

  /**
     Populates the main state object used by "opfs" and "opfs-wl", and
     transfered from those to their async counterpart.

     State which we send to the async-api Worker or share with it.
     This object must initially contain only cloneable or sharable
     objects. After the worker's "inited" message arrives, other types
     of data may be added to it.

     For purposes of Atomics.wait() and Atomics.notify(), we use a
     SharedArrayBuffer with one slot reserved for each of the API
     proxy's methods. The sync side of the API uses Atomics.wait()
     on the corresponding slot and the async side uses
     Atomics.notify() on that slot.

     The approach of using a single SAB to serialize comms for all
     instances might(?) lead to deadlock situations in multi-db
     cases. We should probably have one SAB here with a single slot
     for locking a per-file initialization step and then allocate a
     separate SAB like the above one for each file. That will
     require a bit of acrobatics but should be feasible. The most
     problematic part is that xOpen() would have to use
     postMessage() to communicate its SharedArrayBuffer, and mixing
     that approach with Atomics.wait/notify() gets a bit messy.
  */
  opfsUtil.createVfsStateObject = function(opfsVfs){
    if( !(opfsVfs instanceof capi.sqlite3_vfs) ){
      toss("Expecting a sqlite3_vfs instance");
    }
    const vfsName = wasm.cstrToJs(opfsVfs.$zName);
    const isWebLocker = 'opfs-wl'===vfsName;
    const state = util.nu();
    opfsVfs.metrics = util.nu({
      counters: util.nu(),
      dump: function(){
        let k, n = 0, t = 0, w = 0;
        for(k in state.opIds){
          const m = metrics[k];
          n += m.count;
          t += m.time;
          w += m.wait;
          m.avgTime = (m.count && m.time) ? (m.time / m.count) : 0;
          m.avgWait = (m.count && m.wait) ? (m.wait / m.count) : 0;
        }
        sqlite3.config.log(globalThis.location.href,
                    "metrics for",globalThis.location.href,":",metrics,
                    "\nTotal of",n,"op(s) for",t,
                    "ms (incl. "+w+" ms of waiting on the async side)");
        sqlite3.config.log("Serialization metrics:",opfsVfs.metrics.counters.s11n);
        //W.postMessage({type:'opfs-async-metrics'});
      },
      reset: function(){
        let k;
        const r = (m)=>(m.count = m.time = m.wait = 0);
        const m = opfsVfs.metrics.counters;
        for(k in state.opIds){
          r(m[k] = Object.create(null));
        }
        let s = m.s11n = Object.create(null);
        s = s.serialize = Object.create(null);
        s.count = s.time = 0;
        s = m.s11n.deserialize = Object.create(null);
        s.count = s.time = 0;
      }
    })/*opfsVfs.metrics*/;

    /**
       asyncIdleWaitTime is how long (ms) to wait, in the async proxy,
       for each Atomics.wait() when waiting on inbound VFS API calls.
       We need to wake up periodically to give the thread a chance to
       do other things. If this is too high (e.g. 500ms) then even two
       workers/tabs can easily run into locking errors. Some multiple
       of this value is also used for determining how long to wait on
       lock contention to free up.
    */
    state.asyncIdleWaitTime = isWebLocker ? 100 : 150;

    /**
       Whether the async counterpart should log exceptions to
       the serialization channel. That produces a great deal of
       noise for seemingly innocuous things like xAccess() checks
       for missing files, so this option may have one of 3 values:

       0 = no exception logging.

       1 = only log exceptions for "significant" ops like xOpen(),
       xRead(), and xWrite().

       2 = log all exceptions.
    */
    state.asyncS11nExceptions = 1;
    /* Size of file I/O buffer block. 64k = max sqlite3 page size, and
       xRead/xWrite() will never deal in blocks larger than that. */
    state.fileBufferSize = 1024 * 64;
    state.sabS11nOffset = state.fileBufferSize;
    /**
       The size of the block in our SAB for serializing arguments and
       result values. Needs to be large enough to hold serialized
       values of any of the proxied APIs. Filenames are the largest
       part but are limited to opfsVfs.$mxPathname bytes. We also
       store exceptions there, so it needs to be long enough to hold
       a reasonably long exception string.
    */
    state.sabS11nSize = opfsVfs.$mxPathname * 2;
    /**
       The SAB used for all data I/O between the synchronous and
       async halves (file i/o and arg/result s11n).
    */
    state.sabIO = new SharedArrayBuffer(
      state.fileBufferSize/* file i/o block */
      + state.sabS11nSize/* argument/result serialization block */
    );
    state.opIds = Object.create(null);
    {
      /*
        Maintenance reminder:

        Some of these fields are only for use by the "opfs-wl" VFS,
        but they must also be set up for the "ofps" VFS so that the
        sizes and offsets calculated here are consistent in the async
        proxy. Hypothetically they could differ and it would cope
        but... why invite disaster over eliding a few superfluous (for
        "opfs') properties?
      */
      /* Indexes for use in our SharedArrayBuffer... */
      let i = 0;
      /* SAB slot used to communicate which operation is desired
         between both workers. This worker writes to it and the other
         listens for changes. */
      state.opIds.whichOp = i++;
      /* Slot for storing return values. This worker listens to that
         slot and the other worker writes to it. */
      state.opIds.rc = i++;
      /* Each function gets an ID which this worker writes to
         the whichOp slot. The async-api worker uses Atomic.wait()
         on the whichOp slot to figure out which operation to run
         next. */
      state.opIds.xAccess = i++;
      state.opIds.xClose = i++;
      state.opIds.xDelete = i++;
      state.opIds.xDeleteNoWait = i++;
      state.opIds.xFileSize = i++;
      state.opIds.xLock = i++;
      state.opIds.xOpen = i++;
      state.opIds.xRead = i++;
      state.opIds.xSleep = i++;
      state.opIds.xSync = i++;
      state.opIds.xTruncate = i++;
      state.opIds.xUnlock = i++;
      state.opIds.xWrite = i++;
      state.opIds.mkdir = i++;
      state.opIds.lockControl = i++ /* opfs-wl signals the intent to lock here */;
      /** Internal signals which are used only during development and
          testing via the dev console. */
      state.opIds['opfs-async-metrics'] = i++;
      state.opIds['opfs-async-shutdown'] = i++;
      /* The retry slot is used by the async part for wait-and-retry
         semantics. Though we could hypothetically use the xSleep slot
         for that, doing so might lead to undesired side effects. */
      state.opIds.retry = i++;

      /* Slots for submitting the lock type and receiving its acknowledgement.
         Only used by "opfs-wl". */
      state.lock = util.nu({
        type: i++ /* SQLITE_LOCK_xyz value */,
        atomicsHandshake: i++ /* 0=pending, 1=release, 2=granted */
      });
      state.sabOP = new SharedArrayBuffer(
        i * 4/* ==sizeof int32, noting that Atomics.wait() and friends
                can only function on Int32Array views of an SAB. */);
    }
    /**
       SQLITE_xxx constants to export to the async worker
       counterpart...
    */
    state.sq3Codes = Object.create(null);
    [
      'SQLITE_ACCESS_EXISTS',
      'SQLITE_ACCESS_READWRITE',
      'SQLITE_BUSY',
      'SQLITE_CANTOPEN',
      'SQLITE_ERROR',
      'SQLITE_IOERR',
      'SQLITE_IOERR_ACCESS',
      'SQLITE_IOERR_CLOSE',
      'SQLITE_IOERR_DELETE',
      'SQLITE_IOERR_FSYNC',
      'SQLITE_IOERR_LOCK',
      'SQLITE_IOERR_READ',
      'SQLITE_IOERR_SHORT_READ',
      'SQLITE_IOERR_TRUNCATE',
      'SQLITE_IOERR_UNLOCK',
      'SQLITE_IOERR_WRITE',
      'SQLITE_LOCK_EXCLUSIVE',
      'SQLITE_LOCK_NONE',
      'SQLITE_LOCK_PENDING',
      'SQLITE_LOCK_RESERVED',
      'SQLITE_LOCK_SHARED',
      'SQLITE_LOCKED',
      'SQLITE_MISUSE',
      'SQLITE_NOTFOUND',
      'SQLITE_OPEN_CREATE',
      'SQLITE_OPEN_DELETEONCLOSE',
      'SQLITE_OPEN_MAIN_DB',
      'SQLITE_OPEN_READONLY',
      'SQLITE_LOCK_NONE',
      'SQLITE_LOCK_SHARED',
      'SQLITE_LOCK_RESERVED',
      'SQLITE_LOCK_PENDING',
      'SQLITE_LOCK_EXCLUSIVE'
    ].forEach((k)=>{
      if(undefined === (state.sq3Codes[k] = capi[k])){
        toss("Maintenance required: not found:",k);
      }
    });

    state.opfsFlags = Object.assign(Object.create(null),{
      /**
         Flag for use with xOpen(). URI flag "opfs-unlock-asap=1"
         enables this. See defaultUnlockAsap, below.
       */
      OPFS_UNLOCK_ASAP: 0x01,
      /**
         Flag for use with xOpen(). URI flag "delete-before-open=1"
         tells the VFS to delete the db file before attempting to open
         it. This can be used, e.g., to replace a db which has been
         corrupted (without forcing us to expose a delete/unlink()
         function in the public API).

         Failure to unlink the file is ignored but may lead to
         downstream errors.  An unlink can fail if, e.g., another tab
         has the handle open.

         It goes without saying that deleting a file out from under another
         instance results in Undefined Behavior.
      */
      OPFS_UNLINK_BEFORE_OPEN: 0x02,
      /**
         If true, any async routine which implicitly acquires a sync
         access handle (i.e. an OPFS lock) will release that lock at
         the end of the call which acquires it. If false, such
         "autolocks" are not released until the VFS is idle for some
         brief amount of time.

         The benefit of enabling this is much higher concurrency. The
         down-side is much-reduced performance (as much as a 4x decrease
         in speedtest1).
      */
      defaultUnlockAsap: false
    });

    opfsVfs.metrics.reset();
//#if not defined nope
//#// does not yet work this way
//#define vfs.metrics.enable
    const metrics = opfsVfs.metrics.counters;
//#include api/opfs-common-inline.c-pp.js
//#// import initS11n()
//#undef vfs.metrics.enable
    opfsVfs.initS11n = initS11n;
//#endif
    return state;
  }/*createVfsStateObject()*/;

}/*sqlite3ApiBootstrap.initializers*/);
//#endif target:node
