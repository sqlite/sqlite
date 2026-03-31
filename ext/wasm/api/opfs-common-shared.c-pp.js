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
  the public API. The library deletes sqlite3.opfs in its final
  bootstrapping steps unless it's specifically told to keep them (for
  testing purposes only) using an undocumented and unsupported
  mechanism.
*/
globalThis.sqlite3ApiBootstrap.initializers.push(function(sqlite3){
  'use strict';
  if( sqlite3.config.disable?.vfs?.opfs &&
      sqlite3.config.disable.vfs['opfs-vfs'] ){
    return;
  }
  const toss = sqlite3.util.toss,
        capi = sqlite3.capi,
        util = sqlite3.util,
        wasm = sqlite3.wasm;

  /**
     Generic utilities for working with OPFS. This will get filled out
     by the Promise setup and, on success, installed as sqlite3.opfs.

     This is an internal/private namespace intended for use solely by
     the OPFS VFSes and test code for them. The library bootstrapping
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
  opfsUtil.vfsInstallationFeatureCheck = function(vfsName){
    if( !globalThis.SharedArrayBuffer || !globalThis.Atomics ){
      toss("Cannot install OPFS: Missing SharedArrayBuffer and/or Atomics.",
           "The server must emit the COOP/COEP response headers to enable those.",
           "See https://sqlite.org/wasm/doc/trunk/persistence.md#coop-coep");
    }else if( 'undefined'===typeof WorkerGlobalScope ){
      toss("The OPFS sqlite3_vfs cannot run in the main thread",
           "because it requires Atomics.wait().");
    }else if( !globalThis.FileSystemHandle ||
              !globalThis.FileSystemDirectoryHandle ||
              !globalThis.FileSystemFileHandle?.prototype?.createSyncAccessHandle ||
              !navigator?.storage?.getDirectory ){
      toss("Missing required OPFS APIs.");
    }else if( 'opfs-wl'===vfsName && !globalThis.Atomics.waitAsync ){
      toss('The',vfsName,'VFS requires Atomics.waitAsync(), which is not available.');
    }
  };

  /**
     Must be called by the VFS's main installation routine and passed
     the options object that function receives and a reference to that
     function itself (we don't need this anymore).

     It throws if OPFS is not available.

     If it returns falsy, it detected that OPFS should be disabled, in
     which case the callee should immediately return/resolve to the
     sqlite3 object.

     Else it returns a new copy of the options object, fleshed out
     with any missing defaults. The caller must:

     - Set up any local state they need.

     - Call opfsUtil.createVfsState(vfsName,opt), where opt is the
     object returned by this function.

     - Set up any references they may need to state returned
     by the previous step.

     - Call opfvs.bindVfs()
  */
  opfsUtil.initOptions = function callee(vfsName, options){
    const urlParams = new URL(globalThis.location.href).searchParams;
    if( urlParams.has(vfsName+'-disable') ){
      //sqlite3.config.warn('Explicitly not installing "opfs" VFS due to opfs-disable flag.');
      return;
    }
    try{
      opfsUtil.vfsInstallationFeatureCheck(vfsName);
    }catch(e){
      return;
    }
    options = util.nu(options);
    options.vfsName = vfsName;
    options.verbose ??= urlParams.has('opfs-verbose')
      ? +urlParams.get('opfs-verbose') : 1;
    options.sanityChecks ??= urlParams.has('opfs-sanity-check');

    if( !opfsUtil.proxyUri ){
      opfsUtil.proxyUri = "sqlite3-opfs-async-proxy.js";
      if( sqlite3.scriptInfo?.sqlite3Dir ){
        /* Doing this from one scope up, outside of this function, does
           not work. */
        opfsUtil.proxyUri = (
          sqlite3.scriptInfo.sqlite3Dir + opfsUtil.proxyUri
        );
      }
    }
    options.proxyUri ??= opfsUtil.proxyUri;
    if('function' === typeof options.proxyUri){
      options.proxyUri = options.proxyUri();
    }
    //sqlite3.config.warn("opfsUtil options =",JSON.stringify(options), 'urlParams =', urlParams);
    return opfsUtil.options = options;
  };

  /**
     Creates, populates, and returns the main state object used by the
     "opfs" and "opfs-wl" VFSes, and transfered from those to their
     async counterparts.

     The returned object's vfs property holds the fully-populated
     capi.sqlite3_vfs instance, tagged with lots of extra state which
     the current VFSes need to have exposed to them.

     After setting up any local state needed, the caller must call
     theVfs.bindVfs(X,Y), where X is an object containing the
     sqlite3_io_methods to override and Y is a callback which gets
     triggered if init succeeds, before the final Promise decides
     whether or not to reject.

     This object must, when it's passed to the async part, contain
     only cloneable or sharable objects. After the worker's "inited"
     message arrives, other types of data may be added to it.
  */
  opfsUtil.createVfsState = function(){
    const state = util.nu();
    const options = opfsUtil.options;
    state.verbose = options.verbose;

    const loggers = [
      sqlite3.config.error,
      sqlite3.config.warn,
      sqlite3.config.log
    ];
    const vfsName = options.vfsName
          || toss("Maintenance required: missing VFS name");
    const logImpl = (level,...args)=>{
      if(state.verbose>level) loggers[level](vfsName+":",...args);
    };
    const log   = (...args)=>logImpl(2, ...args),
          warn  = (...args)=>logImpl(1, ...args),
          error = (...args)=>logImpl(0, ...args),
          capi  = sqlite3.capi,
          wasm  = sqlite3.wasm;

    const opfsVfs = state.vfs = new capi.sqlite3_vfs();
    const opfsIoMethods = opfsVfs.ioMethods = new capi.sqlite3_io_methods();

    opfsIoMethods.$iVersion = 1;
    opfsVfs.$iVersion = 2/*yes, two*/;
    opfsVfs.$szOsFile = capi.sqlite3_file.structInfo.sizeof;
    opfsVfs.$mxPathname = 1024/* sure, why not? The OPFS name length limit
                                 is undocumented/unspecified. */;
    opfsVfs.$zName = wasm.allocCString(vfsName);
    opfsVfs.addOnDispose(
      '$zName', opfsVfs.$zName, opfsIoMethods
      /**
         Pedantic sidebar: the entries in this array are items to
         clean up when opfsVfs.dispose() is called, but in this
         environment it will never be called. The VFS instance simply
         hangs around until the WASM module instance is cleaned up. We
         "could" _hypothetically_ clean it up by "importing" an
         sqlite3_os_end() impl into the wasm build, but the shutdown
         order of the wasm engine and the JS one are undefined so
         there is no guaranty that the opfsVfs instance would be
         available in one environment or the other when
         sqlite3_os_end() is called (_if_ it gets called at all in a
         wasm build, which is undefined). i.e. addOnDispose() here is
         a matter of "correctness", not necessity. It just wouldn't do
         to leave the impression that we're blindly leaking memory.
      */
    );

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
        opfsVfs.worker?.postMessage?.({type:'opfs-async-metrics'});
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
    state.asyncIdleWaitTime = 150;

    /**
       Whether the async counterpart should log exceptions to
       the serialization channel. That produces a great deal of
       noise for seemingly innocuous things like xAccess() checks
       for missing files, so this option may have one of 3 values:

       0 = no exception logging.

       1 = only log exceptions for "significant" ops like xOpen(),
       xRead(), and xWrite(). Exceptions related to, e.g., wait/retry
       loops in acquiring SyncAccessHandles are not logged.

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

    /**
       For purposes of Atomics.wait() and Atomics.notify(), we use a
       SharedArrayBuffer with one slot reserved for each of the API
       proxy's methods. The sync side of the API uses Atomics.wait()
       on the corresponding slot and the async side uses
       Atomics.notify() on that slot. state.opIds holds the SAB slot
       IDs of each of those.
    */
    state.opIds = Object.create(null);
    {
      /* Indexes for use in our SharedArrayBuffer... */
      let i = 0;
      /* SAB slot used to communicate which operation is desired
         between both workers. This worker writes to it and the other
         listens for changes and clears it. The values written to it
         are state.opIds.x[A-Z][a-z]+, defined below.*/
      state.opIds.whichOp = i++;
      /* Slot for storing return values. This side listens to that
         slot and the async proxy writes to it. */
      state.opIds.rc = i++;
      /* Each function gets an ID which this worker writes to the
         state.opIds.whichOp slot. The async-api worker uses
         Atomic.wait() on the whichOp slot to figure out which
         operation to run next. */
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
      state.opIds.mkdir = i++ /*currently unused*/;
      /** Internal signals which are used only during development and
          testing via the dev console. */
      state.opIds['opfs-async-metrics'] = i++;
      state.opIds['opfs-async-shutdown'] = i++;
      /* The retry slot is used by the async part for wait-and-retry
         semantics. It is never written to, only used as a convenient
         place to wait-with-timeout for a value which will never be
         written, i.e. sleep()ing, before retrying a failed attempt to
         acquire a SharedAccessHandle. */
      state.opIds.retry = i++;
      state.sabOP = new SharedArrayBuffer(
        i * 4/* 4==sizeof int32, noting that Atomics.wait() and
                friends can only function on Int32Array views of an
                SAB. */);
    }
    /**
       SQLITE_xxx constants to export to the async worker
       counterpart...
    */
    state.sq3Codes = Object.create(null);
    for(const k of [
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
    ]){
      state.sq3Codes[k] =
        capi[k] ?? toss("Maintenance required: not found:",k);
    }

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

         It goes without saying that deleting a file out from under
         another instance results in Undefined Behavior.
      */
      OPFS_UNLINK_BEFORE_OPEN: 0x02,
      /**
         If true, any async routine which must implicitly acquire a
         sync access handle (i.e. an OPFS lock), without an active
         xLock(), will release that lock at the end of the call which
         acquires it. If false, such implicit locks are not released
         until the VFS is idle for some brief amount of time, as
         defined by state.asyncIdleWaitTime.

         The benefit of enabling this is higher concurrency. The
         down-side is much-reduced performance (as much as a 4x
         decrease in speedtest1).
      */
      defaultUnlockAsap: false
    });

    opfsVfs.metrics.reset()/*must not be called until state.opIds is set up*/;
    const metrics = opfsVfs.metrics.counters;

    /**
       Runs the given operation (by name) in the async worker
       counterpart, waits for its response, and returns the result
       which the async worker writes to SAB[state.opIds.rc]. The 2nd
       and subsequent arguments must be the arguments for the async op
       (see sqlite3-opfs-async-proxy.c-pp.js).
    */
    const opRun = opfsVfs.opRun = (op,...args)=>{
      const opNdx = state.opIds[op] || toss(opfsVfs.vfsName+": Invalid op ID:",op);
      state.s11n.serialize(...args);
      Atomics.store(state.sabOPView, state.opIds.rc, -1);
      Atomics.store(state.sabOPView, state.opIds.whichOp, opNdx);
      Atomics.notify(state.sabOPView, state.opIds.whichOp)
      /* async thread will take over here */;
      const t = performance.now();
      while('not-equal'!==Atomics.wait(state.sabOPView, state.opIds.rc, -1)){
        /*
          The reason for this loop is buried in the details of a long
          discussion at:

          https://github.com/sqlite/sqlite-wasm/issues/12

          Summary: in at least one browser flavor, under high loads,
          the wait()/notify() pairings can get out of sync and/or
          spuriously wake up. Calling wait() here until it returns
          'not-equal' gets them back in sync.
        */
      }
      /* When the above wait() call returns 'not-equal', the async
         half will have completed the operation and reported its
         results in the state.opIds.rc slot of the SAB. It may have
         also serialized an exception for us. */
      const rc = Atomics.load(state.sabOPView, state.opIds.rc);
      metrics[op].wait += performance.now() - t;
      if(rc && state.asyncS11nExceptions){
        const err = state.s11n.deserialize();
        if(err) error(op+"() async error:",...err);
      }
      return rc;
    };

    const opTimer = Object.create(null);
    opTimer.op = undefined;
    opTimer.start = undefined;
    const mTimeStart = opfsVfs.mTimeStart = (op)=>{
      opTimer.start = performance.now();
      opTimer.op = op;
      ++metrics[op].count;
    };
    const mTimeEnd = opfsVfs.mTimeEnd = ()=>(
      metrics[opTimer.op].time += performance.now() - opTimer.start
    );

    /**
       Map of sqlite3_file pointers to objects constructed by xOpen().
    */
    const __openFiles = opfsVfs.__openFiles = Object.create(null);

    /**
       Impls for the sqlite3_io_methods methods. Maintenance reminder:
       members are in alphabetical order to simplify finding them.
    */
    const ioSyncWrappers = opfsVfs.ioSyncWrappers = util.nu({
      xCheckReservedLock: function(pFile,pOut){
        /**
           After consultation with a topic expert: "opfs-wl" will
           continue to use the same no-op impl which "opfs" does
           because:

           - xCheckReservedLock() is just a hint. If SQLite needs to
           lock, it's still going to try to lock.

           - We cannot do this check synchronously in "opfs-wl",
           so would need to pass it to the async proxy. That would
           make it inordinately expensive considering that it's
           just a hint.
        */
        wasm.poke(pOut, 0, 'i32');
        return 0;
      },
      xClose: function(pFile){
        mTimeStart('xClose');
        let rc = 0;
        const f = __openFiles[pFile];
        if(f){
          delete __openFiles[pFile];
          rc = opRun('xClose', pFile);
          if(f.sq3File) f.sq3File.dispose();
        }
        mTimeEnd();
        return rc;
      },
      xDeviceCharacteristics: function(pFile){
        return capi.SQLITE_IOCAP_UNDELETABLE_WHEN_OPEN;
      },
      xFileControl: function(pFile, opId, pArg){
        /*mTimeStart('xFileControl');
         mTimeEnd();*/
        return capi.SQLITE_NOTFOUND;
      },
      xFileSize: function(pFile,pSz64){
        mTimeStart('xFileSize');
        let rc = opRun('xFileSize', pFile);
        if(0==rc){
          try {
            const sz = state.s11n.deserialize()[0];
            wasm.poke(pSz64, sz, 'i64');
          }catch(e){
            error("Unexpected error reading xFileSize() result:",e);
            rc = state.sq3Codes.SQLITE_IOERR;
          }
        }
        mTimeEnd();
        return rc;
      },
      xRead: function(pFile,pDest,n,offset64){
       mTimeStart('xRead');
        const f = __openFiles[pFile];
        let rc;
        try {
          rc = opRun('xRead',pFile, n, Number(offset64));
          if(0===rc || capi.SQLITE_IOERR_SHORT_READ===rc){
            /**
               Results get written to the SharedArrayBuffer f.sabView.
               Because the heap is _not_ a SharedArrayBuffer, we have
               to copy the results. TypedArray.set() seems to be the
               fastest way to copy this. */
            wasm.heap8u().set(f.sabView.subarray(0, n), Number(pDest));
          }
        }catch(e){
          error("xRead(",arguments,") failed:",e,f);
          rc = capi.SQLITE_IOERR_READ;
        }
        mTimeEnd();
        return rc;
      },
      xSync: function(pFile,flags){
        mTimeStart('xSync');
        const rc = opRun('xSync', pFile, flags);
        mTimeEnd();
        return rc;
      },
      xTruncate: function(pFile,sz64){
        mTimeStart('xTruncate');
        const rc = opRun('xTruncate', pFile, Number(sz64));
        mTimeEnd();
        return rc;
      },
      xWrite: function(pFile,pSrc,n,offset64){
        mTimeStart('xWrite');
        const f = __openFiles[pFile];
        let rc;
        try {
          f.sabView.set(wasm.heap8u().subarray(
            Number(pSrc), Number(pSrc) + n
          ));
          rc = opRun('xWrite', pFile, n, Number(offset64));
        }catch(e){
          error("xWrite(",arguments,") failed:",e,f);
          rc = capi.SQLITE_IOERR_WRITE;
        }
        mTimeEnd();
        return rc;
      }
    })/*ioSyncWrappers*/;

    /**
       Impls for the sqlite3_vfs methods. Maintenance reminder: members
       are in alphabetical order to simplify finding them.
    */
    const vfsSyncWrappers = opfsVfs.vfsSyncWrappers = {
      xAccess: function(pVfs,zName,flags,pOut){
       mTimeStart('xAccess');
        const rc = opRun('xAccess', wasm.cstrToJs(zName));
        wasm.poke( pOut, (rc ? 0 : 1), 'i32' );
        mTimeEnd();
        return 0;
      },
      xCurrentTime: function(pVfs,pOut){
        wasm.poke(pOut, 2440587.5 + (new Date().getTime()/86400000),
                  'double');
        return 0;
      },
      xCurrentTimeInt64: function(pVfs,pOut){
        wasm.poke(pOut, (2440587.5 * 86400000) + new Date().getTime(),
                  'i64');
        return 0;
      },
      xDelete: function(pVfs, zName, doSyncDir){
        mTimeStart('xDelete');
        const rc = opRun('xDelete', wasm.cstrToJs(zName), doSyncDir, false);
        mTimeEnd();
        return rc;
      },
      xFullPathname: function(pVfs,zName,nOut,pOut){
        /* Until/unless we have some notion of "current dir"
           in OPFS, simply copy zName to pOut... */
        const i = wasm.cstrncpy(pOut, zName, nOut);
        return i<nOut ? 0 : capi.SQLITE_CANTOPEN
        /*CANTOPEN is required by the docs but SQLITE_RANGE would be a closer match*/;
      },
      xGetLastError: function(pVfs,nOut,pOut){
        /* Mutex use in the overlying APIs cause xGetLastError() to
           not be terribly useful for us. e.g. it can't be used to
           convey error messages from xOpen() because there would be a
           race condition between sqlite3_open()'s call to xOpen() and
           this function. */
        sqlite3.config.warn("OPFS xGetLastError() has nothing sensible to return.");
        return 0;
      },
      //xSleep is optionally defined below
      xOpen: function f(pVfs, zName, pFile, flags, pOutFlags){
        mTimeStart('xOpen');
        let opfsFlags = 0;
        let jzName, zToFree;
        if( !zName ){
          jzName = opfsUtil.randomFilename();
          zName = zToFree = wasm.allocCString(jzName);
        }else if(wasm.isPtr(zName)){
          if(capi.sqlite3_uri_boolean(zName, "opfs-unlock-asap", 0)){
            /* -----------------------^^^^^ MUST pass the untranslated
               C-string here. */
            opfsFlags |= state.opfsFlags.OPFS_UNLOCK_ASAP;
          }
          if(capi.sqlite3_uri_boolean(zName, "delete-before-open", 0)){
            opfsFlags |= state.opfsFlags.OPFS_UNLINK_BEFORE_OPEN;
          }
          jzName = wasm.cstrToJs(zName);
          //sqlite3.config.warn("xOpen zName =",zName, "opfsFlags =",opfsFlags);
        }else{
          sqlite3.config.error("Impossible zName value in xOpen?", zName);
          return capi.SQLITE_CANTOPEN;
        }
        const fh = util.nu({
          fid: pFile,
          filename: jzName,
          sab: new SharedArrayBuffer(state.fileBufferSize),
          flags: flags,
          readOnly: !(capi.SQLITE_OPEN_CREATE & flags)
            && !!(flags & capi.SQLITE_OPEN_READONLY)
        });
        const rc = opRun('xOpen', pFile, jzName, flags, opfsFlags);
        if(rc){
          if( zToFree ) wasm.dealloc(zToFree);
        }else{
          /* Recall that sqlite3_vfs::xClose() will be called, even on
             error, unless pFile->pMethods is NULL. */
          if(fh.readOnly){
            wasm.poke(pOutFlags, capi.SQLITE_OPEN_READONLY, 'i32');
          }
          __openFiles[pFile] = fh;
          fh.sabView = state.sabFileBufView;
          fh.sq3File = new capi.sqlite3_file(pFile);
          if( zToFree ) fh.sq3File.addOnDispose(zToFree);
          fh.sq3File.$pMethods = opfsIoMethods.pointer;
          fh.lockType = capi.SQLITE_LOCK_NONE;
        }
        mTimeEnd();
        return rc;
      }/*xOpen()*/
    }/*vfsSyncWrappers*/;

    const pDVfs = capi.sqlite3_vfs_find(null)/*pointer to default VFS*/;
    if(pDVfs){
      const dVfs = new capi.sqlite3_vfs(pDVfs);
      opfsVfs.$xRandomness = dVfs.$xRandomness;
      opfsVfs.$xSleep = dVfs.$xSleep;
      dVfs.dispose();
    }
    if(!opfsVfs.$xRandomness){
      /* If the default VFS has no xRandomness(), add a basic JS impl... */
      opfsVfs.vfsSyncWrappers.xRandomness = function(pVfs, nOut, pOut){
        const heap = wasm.heap8u();
        let i = 0;
        const npOut = Number(pOut);
        for(; i < nOut; ++i) heap[npOut + i] = (Math.random()*255000) & 0xFF;
        return i;
      };
    }
    if(!opfsVfs.$xSleep){
      /* If we can inherit an xSleep() impl from the default VFS then
         assume it's sane and use it, otherwise install a JS-based
         one. */
      opfsVfs.vfsSyncWrappers.xSleep = function(pVfs,ms){
        mTimeStart('xSleep');
        Atomics.wait(state.sabOPView, state.opIds.xSleep, 0, ms);
        mTimeEnd();
        return 0;
      };
    }

//#define vfs.metrics.enable
//#// import initS11n()
//#include api/opfs-common-inline.c-pp.js
//#undef vfs.metrics.enable
    opfsVfs.initS11n = initS11n;

    /**
       To be called by the VFS's main installation routine after it has
       wired up enough state to provide its overridden io-method impls
       (which must be properties of the ioMethods argument). Returns a
       Promise which the installation routine must return. callback must
       be a function which performs any post-bootstrap touchups, namely
       plugging in a sqlite3.oo1 wrapper. It is passed (sqlite3, opfsVfs),
       where opfsVfs is the sqlite3_vfs object which was set up by
       opfsUtil.createVfsState().
    */
    opfsVfs.bindVfs = function(ioMethods, callback){
      Object.assign(opfsVfs.ioSyncWrappers, ioMethods);
      const thePromise = new Promise(function(promiseResolve_, promiseReject_){
        let promiseWasRejected = undefined;
        const promiseReject = (err)=>{
          promiseWasRejected = true;
          opfsVfs.dispose();
          return promiseReject_(err);
        };
        const promiseResolve = ()=>{
          try{
            callback(sqlite3, opfsVfs);
          }catch(e){
            return promiseReject(e);
          }
          promiseWasRejected = false;
          return promiseResolve_(sqlite3);
        };
        const options = opfsUtil.options;
        let proxyUri = options.proxyUri +(
          (options.proxyUri.indexOf('?')<0) ? '?' : '&'
        )+'vfs='+vfsName;
        //sqlite3.config.error("proxyUri",options.proxyUri, (new Error()));
        const W = opfsVfs.worker =
//#if target:es6-bundler-friendly
              (()=>{
                /* _Sigh_... */
                switch(vfsName){
                  case 'opfs':
                    return new Worker(new URL("sqlite3-opfs-async-proxy.js?vfs=opfs", import.meta.url));
                  case 'opfs-wl':
                    return new Worker(new URL("sqlite3-opfs-async-proxy.js?vfs=opfs-wl", import.meta.url));
                }
              })();
//#elif target:es6-module
        new Worker(new URL(proxyUri, import.meta.url));
//#else
        new Worker(proxyUri);
//#/if
        let zombieTimer = setTimeout(()=>{
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
//#if 0
        /**
           Not part of the public API. Only for test/development use.
        */
        opfsVfs.debug = {
          asyncShutdown: ()=>{
            warn("Shutting down OPFS async listener. The OPFS VFS will no longer work.");
            opRun('opfs-async-shutdown');
          },
          asyncRestart: ()=>{
            warn("Attempting to restart OPFS VFS async listener. Might work, might not.");
            W.postMessage({type: 'opfs-async-restart'});
          }
        };
//#/if

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
          //sqlite3.config.warn(vfsName,"Worker.onmessage:",data);
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
              delete state.vfs;
              W.postMessage({type: 'opfs-async-init', args: util.nu(state)});
              break;
            case 'opfs-async-inited': {
              /* Indicates that the async partner has received the 'init'
                 and has finished initializing, so the real work can
                 begin... */
              if(true===promiseWasRejected){
                break /* promise was already rejected via timer */;
              }
              clearTimeout(zombieTimer);
              zombieTimer = null;
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
                    log("End of OPFS sqlite3_vfs setup.", opfsVfs);
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
            case 'debug':
              warn("debug message from worker:",data);
              break;
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
    }/*bindVfs()*/;

    return state;
  }/*createVfsState()*/;

}/*sqlite3ApiBootstrap.initializers*/);
//#/if target:node
