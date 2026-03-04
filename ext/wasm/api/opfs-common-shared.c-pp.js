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
  const toss3 = sqlite3.util.toss3;
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

}/*sqlite3ApiBootstrap.initializers*/);
//#else
/*
  The OPFS SAH Pool parts are elided from builds targeting node.js.
*/
//#endif target:node
