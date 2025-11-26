/*
  2025-11-21

  The author disclaims copyright to this source code.  In place of a
  legal notice, here is a blessing:

  *   May you do good and not evil.
  *   May you find forgiveness for yourself and forgive others.
  *   May you share freely, never taking more than you give.

  ***********************************************************************

  This file houses the "kvvfs" pieces of the SQLite3 JS API. Most of
  kvvfs is implemented in src/os_kv.c and exposed/extended for use
  here via sqlite3-wasm.c.

  Main project home page: https://sqlite.org

  Documentation home page: https://sqlite.org/wasm
*/
//#if omit-kvvfs
globalThis.sqlite3ApiBootstrap.initializers.push(function(sqlite3){
  /* These are JS plumbing, not part of the public API */
  delete sqlite3.capi.sqlite3_kvvfs_methods;
  delete sqlite3.capi.KVVfsFile;
}
//#else
//#@policy error
//#savepoint begin
//#define kvvfs-v2-added-in=TBD
/**
   kvvfs - the Key/Value VFS - is an SQLite3 VFS which delegates
   storage of its pages and metadata to a key-value store.

   It was conceived in order to support JS's localStorage and
   sessionStorage objects. Its native implementation uses files as
   key/value storage (one file per record) but the JS implementation
   replaces a few methods so that it can use the aforementioned
   objects as storage.

   It uses a bespoke ASCII encoding to store each db page as a
   separate record and stores some metadata, like the db's unencoded
   size and its journal, as individual records.

   kvvfs is significantly less efficient than a plain in-memory db but
   it also, as a side effect of its design, offers a JSON-friendly
   interchange format for exporting and importing databases.

   kvvfs is _not_ designed for heavy db loads. It is relatively
   malloc()-heavy, having to de/allocate frequently, and it
   spends much of its time converting the raw db pages into and out of
   an ASCII encoding.

   But it _does_ work and is "performant enough" for db work of the
   scale of a db which will fit within sessionStorage or localStorage
   (just 2-3mb).

   "Version 2" extends it to support using Storage-like objects as
   backing storage, Storage being the JS class which localStorage and
   sessionStorage both derive from. This essentially moves the backing
   store from whatever localStorage and sessionStorage use to an
   in-memory object.

   This effort is primarily a stepping stone towards eliminating, if
   it proves possible, the POSIX I/O API dependencies in SQLite's WASM
   builds. That is: if this VFS works properly, it can be set as the
   default VFS and we can eliminate the "unix" VFS from the JS/WASM
   builds (as opposed to server-wise/WASI builds). That still, as of
   2025-11-23, a ways away, but it's the main driver for version 2 of
   kvvfs.

   Version 2 remains compatible with version 1 databases and always
   writes localStorage/sessionStorage metadata in the v1 format, so
   such dbs can be manipulated freely by either version. For transient
   storage objects (new in version 2), the format of its record keys
   is simpified, requiring less space than v1 keys by eliding
   redundant (in this context) info from the keys.

   Another benefit of v2 is its ability to export dbs into a
   JSON-friendly (but not human-friendly) format.

   A potential, as-yet-unproven, benefit, would be the ability to plug
   arbitrary Storage-compatible objects in so that clients could,
   e.g. asynchronously post updates to db pages to some back-end for
   backups.
*/
globalThis.sqlite3ApiBootstrap.initializers.push(function(sqlite3){
  'use strict';
  const capi = sqlite3.capi,
        sqlite3_kvvfs_methods = capi.sqlite3_kvvfs_methods,
        KVVfsFile = capi.KVVfsFile,
        pKvvfs = sqlite3.capi.sqlite3_vfs_find("kvvfs")

  /* These are JS plumbing, not part of the public API */
  delete capi.sqlite3_kvvfs_methods;
  delete capi.KVVfsFile;

  if( !pKvvfs ) return /* nothing to do */;
  if( 0 ){
    /* This working would be our proverbial holy grail. */
    capi.sqlite3_vfs_register(pKvvfs, 1);
  }

  const util = sqlite3.util,
        wasm = sqlite3.wasm,
        toss3 = util.toss3,
        hop = (o,k)=>Object.prototype.hasOwnProperty.call(o,k);

  const kvvfsMethods = new sqlite3_kvvfs_methods(
    /* Wraps the static sqlite3_api_methods singleton */
    wasm.exports.sqlite3__wasm_kvvfs_methods()
  );
  util.assert( 32<=kvvfsMethods.$nKeySize, "unexpected kvvfsMethods.$nKeySize: "+kvvfsMethods.$nKeySize);

  const cache = Object.assign(Object.create(null),{
    rxJournalSuffix: /-journal$/,
    zKeyJrnl: wasm.allocCString("jrnl"),
    zKeySz: wasm.allocCString("sz"),
    keySize: kvvfsMethods.$nKeySize
  });

  const debug = sqlite3.__isUnderTest
        ? function(){sqlite3.config.debug("kvvfs:", ...arguments)}
        : function(){};
  const warn = function(){sqlite3.config.warn("kvvfs:", ...arguments)};
  const error = function(){sqlite3.config.error("kvvfs:", ...arguments)};

  /**
     Implementation of JS's Storage interface for use as backing store
     of the kvvfs. Storage is a native class and its constructor
     cannot be legally called from JS, making it impossible to
     directly subclass Storage. This class implements (only) the
     Storage interface, however, to make it a drop-in replacement for
     localStorage/sessionStorage.

     This impl simply proxies a plain, prototype-less Object, suitable
     for JSON-ing.

     Design note: Storage has a bit of an odd iteration-related
     interface as does not (AFAIK) specify specific behavior regarding
     modification during traversal. Because of that, this class does
     some seemingly unnecessary things with its #keys member, deleting
     and recreating it whenever a property index might be invalidated.
  */
  class KVVfsStorage {
    #map;
    #keys;
    #getKeys(){return this.#keys ??= Object.keys(this.#map);}

    constructor(){
      this.clear();
    }

    key(n){
      const k = this.#getKeys();
      return n<k.length ? k[n] : null;
    }

    getItem(k){
      return this.#map[k] ?? null;
    }

    setItem(k,v){
      if( !hop(this.#map, k) ){
        this.#keys = null;
      }
      this.#map[k] = ''+v;
    }

    removeItem(k){
      if( delete this.#map[k] ){
        this.#keys = null;
      }
    }

    clear(){
      this.#map = Object.create(null);
      this.#keys = null;
    }

    get length() {
      return this.#getKeys().length;
    }
  }/*KVVfsStorage*/;

  /** True if v is the name of one of the special persistant Storage
      objects. */
  const kvvfsIsPersistentName = (v)=>'local'===v || 'session'===v;

  /**
     Keys in kvvfs have a prefix of "kvvfs-NAME-", where NAME is the
     db name. This key is redundant in JS but it's how kvvfs works (it
     saves each key to a separate file, so needs a distinct namespace
     per data source name). We retain this prefix in 'local' and
     'session' storage for backwards compatibility and so that they
     can co-exist with client data in their storage, but we elide them
     from "v2" storage, where they're superfluous.
  */
  const kvvfsKeyPrefix = (v)=>kvvfsIsPersistentName(v) ? 'kvvfs-'+v+'-' : '';

  /**
     Throws if storage name n is not valid for use as a storage name.
     Much of this goes back to kvvfs having a fixed buffer size for
     its keys, and the storage name needing to be encoded in the keys
     for local/session storage. We disallow non-ASCII to avoid
     problems with truncated multibyte characters at the end of the
     key buffer.

     The second argument must only be true when called from xOpen() -
     it makes names with a "-journal" suffix legal.
  */
  const validateStorageName = function(n,mayBeJournal=false){
    if( kvvfsIsPersistentName(n) ) return;
    const len = n.length;
    if( !len ) toss3(capi.SQLITE_RANGE, "Empty name is not permitted.");
    let maxLen = cache.keySize - 1;
    if( cache.rxJournalSuffix.test(n) ){
      if( !mayBeJournal ){
        toss3(capi.SQLITE_MISUSE,
              "Storage names may not have a '-journal' suffix.");
      }
    }else{
      maxLen -= 8 /* "-journal" */;
    }
    if( len > maxLen ){
      toss3(capi.SQLITE_RANGE, "Storage name is too long. Limit =", maxLen);
    }
    let i;
    for( i = 0; i < len; ++i ){
      const ch = n.codePointAt(i);
      if( ch<45 || ch >126 ){
        toss3(capi.SQLITE_RANGE,
              "Illegal character ("+ch+"d) in storage name:",n);
      }
    }
  };

  /**
     Create a new instance of the objects which go into
     cache.storagePool.
  */
  const newStorageObj = (name,storage)=>Object.assign(Object.create(null),{
    /**
       JS string value of this KVVfsFile::$zClass. i.e. the storage's
       name.
    */
    jzClass: name,
    /**
       Refcount to keep dbs and journals pointing to the same storage
       for the life of both. Managed by xOpen() and xClose().
    */
    refc: 1,
    /**
       deleteAtRefc0 objects will be removed by xClose() when refc
       reaches 0. The others will persist, to give the illusion of
       real back-end storage. Managed by xOpen(). By default this is
       false but the delete-on-close=1 flag can be used to set this to
       true.
     */
    deleteAtRefc0: false,
    /**
       The backing store. Must implement the Storage interface.
    */
    storage: storage || new KVVfsStorage,
    /**
       The storage prefix used for kvvfs keys.  It is
       "kvvfs-STORAGENAME-" for local/session storage and an empty
       string for transient storage. local/session storage must use
       the long form (A) for backwards compatibility and (B) so that
       kvvfs can coexist with non-db client data in those backends.
       Neither (A) nor (B) are concerns for KVVfsStorage objects.

       This prefix mirrors the one generated by os_kv.c's
       kvrecordMakeKey() and must stay in sync with that one.
    */
    keyPrefix: kvvfsKeyPrefix(name),
    /**
       KVVfsFile instances currently using this storage. Managed by
       xOpen() and xClose().
    */
    files: [],
    /**
       A list of objects with various event callbacks. See
       sqlite3_js_kvvfs_listen().
     */
    listeners: []
  });

  /**
     Deletes the cache.storagePool entries for store and its
     db/journal counterpart.
  */
  const deleteStorage = function(store){
    const other = cache.rxJournalSuffix.test(store.jzClass)
          ? store.jzClass.replace(cache.rxJournalSuffix,'')
          : store.jzClass+'-journal';
    debug("cleaning up storage handles [", store.jzClass, other,"]",store);
    delete cache.storagePool[store.jzClass];
    delete cache.storagePool[other];
    if( !sqlite3.__isUnderTest ){
      /* In test runs, leave these for inspection. If we delete them here,
         any prior dumps of them emitted via the console get cleared out
         because the console shows live objects instead of call-time
         static dumps. */
      delete store.storage;
      delete store.refc;
    }
  };

  /**
     Add both store.jzClass and store.jzClass+"-journal"
     to cache,storagePool.
  */
  const installStorageAndJournal = (store)=>
        cache.storagePool[store.jzClass] =
        cache.storagePool[store.jzClass+'-journal'] = store;

  /**
     Map of JS-stringified KVVfsFile::zClass names to
     reference-counted Storage objects. These objects are created in
     xOpen(). Their refcount is decremented in xClose(), and the
     record is destroyed if the refcount reaches 0. We refcount so
     that concurrent active xOpen()s on a given name, and within a
     given thread, use the same storage object.
  */
  cache.storagePool = Object.assign(Object.create(null),{
    /* Start off with mappings for well-known names. */
    localThread: newStorageObj('localThread')
  });

  if( globalThis.Storage ){
    /* If available, install local/session storage. */
    if( globalThis.localStorage instanceof globalThis.Storage ){
      cache.storagePool.local = newStorageObj('local', globalThis.localStorage);
    }
    if( globalThis.sessionStorage instanceof globalThis.Storage ){
      cache.storagePool.session = newStorageObj('session', globalThis.sessionStorage);
    }
  }

  cache.builtinStorageNames = Object.keys(cache.storagePool);

  const isBuiltinName = (n)=>cache.builtinStorageNames.indexOf(n)>-1;

  /* Add "-journal" twins for each cache.storagePool entry... */
  for(const k of Object.keys(cache.storagePool)){
    /* Journals in kvvfs are are stored as individual records within
       their Storage-ish object, named "{storage.keyPrefix}jrnl".  We
       always map the db and its journal to the same Storage
       object. */
    const orig = cache.storagePool[k];
    cache.storagePool[k+'-journal'] = orig;
  }

  cache.setError = (e=undefined, dfltErrCode=capi.SQLITE_ERROR)=>{
    if( e ){
      cache.lastError = e;
      return (e.resultCode | 0) || dfltErrCode;
    }
    delete cache.lastError;
    return 0;
  };

  cache.popError = ()=>{
    const e = cache.lastError;
    delete cache.lastError;
    return e;
  };

  const noop = ()=>{};

  /**
     Listener events and their argument(s):

     'open': number of opened handles on this storage.

     'close': number of opened handles on this storage.

     'write': key, value

     'delete': key
  */
  const notifyListeners = async function(eventName,store,...args){
    for(const v of store.listeners){
      const f = v?.[eventName];
      if( !f ) return;
      const ev = Object.create(null);
      ev.storageName = store.jzClass;
      ev.type = eventName;
      ev.data = ((args.length===1) ? args[0] : args);
      try{f(ev)?.catch?.(noop)}
      catch(e){
        warn("notifyListener",store.jzClass,eventName,e);
      }
    }
  };

  /**
     Returns the storage object mapped to the given string zClass
     (C-string pointer or JS string).
  */
  const storageForZClass = (zClass)=>
        'string'===typeof zClass
        ? cache.storagePool[zClass]
        : cache.storagePool[wasm.cstrToJs(zClass)];

//#if nope
  // fileForDb() works but we don't have a current need for it.
  /**
     Expects an (sqlite3*). Uses sqlite3_file_control() to extract its
     (sqlite3_file*). On success it returns a new KVVfsFile instance
     wrapping that pointer, which the caller must eventual call
     dispose() on (which won't free the underlying pointer, just the
     wrapper). Returns null if no handle is found (which would
     indicate either that pDb is not using kvvfs or a severe bug in
     its management).
  */
  const fileForDb = function(pDb){
    const stack = pstack.pointer;
    try{
      const pOut = pstack.allocPtr();
      return wasm.exports.sqlite3_file_control(
        pDb, wasm.ptr.null, capi.SQLITE_FCNTL_FILE_POINTER, pOut
      )
        ? null
        : new KVVfsFile(wasm.peekPtr(pOut));
    }finally{
      pstack.restore(stack);
    }
  };

  /**
     Expects an object from the storagePool map. The $szPage and
     $szDb members of each store.files entry is set to -1 in an attempt
     to trigger those values to reload.
  */
  const alertFilesToReload = (store)=>{
    try{
      for( const f of store.files ){
        // FIXME: we need to use one of the C APIs for this, maybe an
        // fcntl.
        f.$szPage = -1;
        f.$szDb = -1n
      }
    }catch(e){
      error("alertFilesToReload()",store,e);
      throw e;
    }
  };
//#endif nope

  /** Returns a C string from sqlite3__wasm_kvvfsMakeKey(). The memory
      is static, so must be copied before a second call. */
  const zKeyForStorage = (store, zClass, zKey)=>{
    //debug("zKeyForStorage(",store, wasm.cstrToJs(zClass), wasm.cstrToJs(zKey));
    return wasm.exports.sqlite3__wasm_kvvfsMakeKey(
      store.keyPrefix ? zClass : wasm.ptr.null, zKey
    );
  };
  /* We use this for the many small key allocations we need.
     TODO: prealloc a buffer on demand for this. We know its
     max size from kvvfsMethods.$nKeySize. */
  const pstack = wasm.pstack;

  /**
     sqlite3_file pointers => objects, each of which has:

     .file = KVVfsFile instance

     .jzClass = JS-string form of f.$zClass

     .storage = Storage object. It is shared between a db and its
     journal.
  */
  const pFileHandles = new Map();

  /**
     Original WASM functions for methods we partially override.
  */
  const originalMethods = {
    vfs: Object.create(null),
    ioDb: Object.create(null),
    ioJrnl: Object.create(null)
  };

  /** Returns the appropriate originalMethods[X] instance for the
      given a KVVfsFile instance. */
  const originalIoMethods = (kvvfsFile)=>
        originalMethods[kvvfsFile.$isJournal ? 'ioJrnl' : 'ioDb'];

  const pVfs = new capi.sqlite3_vfs(kvvfsMethods.$pVfs);
  const pIoDb = new capi.sqlite3_io_methods(kvvfsMethods.$pIoDb);
  const pIoJrnl = new capi.sqlite3_io_methods(kvvfsMethods.$pIoJrnl);
  const recordHandler =
        Object.create(null)/** helper for some vfs
                               routines. Populated later. */;

  /**
     Implementations for members of the object referred to by
     sqlite3__wasm_kvvfs_methods(). We swap out some native
     implementations with these so that we can use JS Storage for
     their backing store.
  */
  const methodOverrides = {

    /**
       sqlite3_kvvfs_methods's member methods.  These perform the
       fetching, setting, and removal of storage keys on behalf of
       kvvfs. In the native impl these write each db page to a
       separate file. This impl stores each db page as a single
       record in a Storage object which is mapped to zClass.

       A db's size is stored in a record named kvvfs[-storagename]-sz
       and the journal is stored in kvvfs[-storagename]-jrnl. The
       [-storagename] part is a remnant of the native impl (so that
       it has unique filenames per db) and is only used for
       localStorage and sessionStorage. We elide that part (to save
       space) from other storage objects but retain it on those two
       to avoid invalidating pre-version-2 session/localStorage dbs.

       The interface docs for these methods are in src/os_kv.c's
       kvrecordRead(), kvrecordWrite(), and kvrecordDelete().
    */
    recordHandler: {
      xRcrdRead: (zClass, zKey, zBuf, nBuf)=>{
        try{
          const jzClass = wasm.cstrToJs(zClass);
          const store = storageForZClass(jzClass);
          if( 0 ){
            debug("xRcrdRead", jzClass, wasm.cstrToJs(zKey),
                  zBuf, nBuf, store );
          }
          if( !store ) return -1;
          const zXKey = zKeyForStorage(store, zClass, zKey);
          //if(!zXKey) return -3/*OOM*/;
          const jXKey = wasm.cstrToJs(zXKey);
          //debug("xRcrdRead zXKey", jzClass, wasm.cstrToJs(zXKey), store );
          const jV = store.storage.getItem(jXKey);
          if(null===jV) return -1;
          const nV = jV.length /* We are relying 100% on v being
                               ** ASCII so that jV.length is equal
                               ** to the C-string's byte length. */;
          if( 0 ){
            debug("xRcrdRead", wasm.cstrToJs(zXKey), store, jV);
          }
          if(nBuf<=0) return nV;
          else if(1===nBuf){
            wasm.poke(zBuf, 0);
            return nV;
          }
          if( nBuf+1<nV ){
            toss3(capi.SQLITE_RANGE,
                  "xRcrdRead()",jzClass,jXKey,
                  "input buffer is too small: need",
                  nV,"but have",nBuf);
          }
          if( 0 ){
            debug("xRcrdRead", nBuf, zClass, wasm.cstrToJs(zClass),
                  wasm.cstrToJs(zKey), nV, jV, store);
          }
          const zV = cache.keybuf.mem ??= wasm.alloc.impl(cache.keybuf.n)
          /* We leak this one-time alloc because we've no better
             option.  sqlite3_vfs does not have a finalizer, so
             we've no place to hook in the cleanup. We "could"
             extend sqlite3_shutdown() to have a cleanup list for
             stuff like this but (A) that function is never used in
             JS and (B) its cleanup would leave cache.keybuf
             pointing to stale memory, so if the library were used
             after sqlite3_shutdown() then we'd corrupt memory. */;
          if( !zV ) return -3 /*OOM*/;
          const heap = wasm.heap8();
          let i;
          for(i = 0; i < nV; ++i){
            heap[wasm.ptr.add(zV,i)] = jV.codePointAt(i) & 0xFF;
          }
          heap.copyWithin(
            Number(zBuf), Number(zV), wasm.ptr.addn(zV, i)
          );
          heap[wasm.ptr.add(zBuf, nV)] = 0;
          return nBuf;
        }catch(e){
          error("kvrecordRead()",e);
          cache.setError(e);
          return -2;
        }
      },

      xRcrdWrite: (zClass, zKey, zData)=>{
        try {
          const jzClass = wasm.cstrToJs(zClass);
          const store = storageForZClass(jzClass);
          const zXKey = zKeyForStorage(store, zClass, zKey);
          //if(!zXKey) return SQLITE_NOMEM;
          const jxKey = wasm.cstrToJs(zXKey);
          const jData = wasm.cstrToJs(zData);
          store.storage.setItem(jxKey, jData);
          notifyListeners('write', store, jxKey, jData);
          return 0;
        }catch(e){
          error("kvrecordWrite()",e);
          return cache.setError(e, capi.SQLITE_IOERR);
        }
      },

      xRcrdDelete: (zClass, zKey)=>{
        try {
          const store = storageForZClass(zClass);
          const zXKey = zKeyForStorage(store, zClass, zKey);
          //if(!zXKey) return capi.SQLITE_NOMEM;
          const jxKey = wasm.cstrToJs(zXKey);
          store.storage.removeItem(jxKey);
          notifyListeners('delete', store, jxKey);
          return 0;
        }catch(e){
          error("kvrecordDelete()",e);
          return cache.setError(e, capi.SQLITE_IOERR);
        }
      }
    }/*recordHandler*/,

    /**
       Override certain operations of the underlying sqlite3_vfs and
       the two sqlite3_io_methods instances so that we can tie
       Storage objects to db names.
    */
    vfs:{
      /* sqlite3_kvvfs_methods::pVfs's methods */
      xOpen: function(pProtoVfs,zName,pProtoFile,flags,pOutFlags){
        cache.popError();
        try{
          //cache.zReadBuf ??= wasm.malloc(kvvfsMethods.$nBufferSize);
          if( !zName ){
            zName = (cache.zEmpty ??= wasm.allocCString(""));
          }
          const jzClass = wasm.cstrToJs(zName);
          //debug("xOpen",jzClass);
          validateStorageName(jzClass, true);
          util.assert( jzClass.length===wasm.cstrlen(zName),
                       "ASCII-only validation failed" );
          if( (flags & (capi.SQLITE_OPEN_MAIN_DB
                        | capi.SQLITE_OPEN_TEMP_DB
                        | capi.SQLITE_OPEN_TRANSIENT_DB))
              && cache.rxJournalSuffix.test(jzClass) ){
            toss3(capi.SQLITE_ERROR,
                  "DB files may not have a '-journal' suffix.");
          }
          const rc = originalMethods.vfs.xOpen(pProtoVfs, zName, pProtoFile,
                                               flags, pOutFlags);
          if( rc ) return rc;
          let deleteAt0 = false;
          if(wasm.isPtr(arguments[1]/*original zName*/)){
            if(capi.sqlite3_uri_boolean(zName, "delete-on-close", 0)){
              deleteAt0 = true;
            }
          }
          const f = new KVVfsFile(pProtoFile);
          util.assert(f.$zClass, "Missing f.$zClass");
          let s = storageForZClass(jzClass);
          //debug("xOpen", jzClass, s);
          if( s ){
            ++s.refc;
            //no if( true===deleteAt0 ) s.deleteAtRefc0 = true;
            s.files.push(f);
          }else{
            /* TODO: a url flag which tells it to keep the storage
               around forever so that future xOpen()s get the same
               Storage-ish objects. We can accomplish that by
               simply increasing the refcount once more. */
            wasm.poke32(pOutFlags, flags | sqlite3.SQLITE_OPEN_CREATE);
            util.assert( !f.$isJournal, "Opening a journal before its db? "+jzClass );
            /* Map both zName and zName-journal to the same storage. */
            const nm = jzClass.replace(cache.rxJournalSuffix,'');
            s = newStorageObj(nm);
            installStorageAndJournal(s);
            s.files.push(f);
            s.deleteAtRefc0 = deleteAt0;
            debug("xOpen installed storage handle [",nm, nm+"-journal","]", s);
          }
          pFileHandles.set(pProtoFile, {storage: s, file: f, jzClass});
          notifyListeners('open', s, s.files.length);
          return 0;
        }catch(e){
          warn("xOpen:",e);
          return cache.setError(e);
        }
      }/*xOpen()*/,

      xDelete: function(pVfs, zName, iSyncFlag){
        cache.popError();
        try{
          const jzName = wasm.cstrToJs(zName);
          //debug("xDelete", jzName, storageForZClass(jzName));
          if( cache.rxJournalSuffix.test(jzName) ){
            recordHandler.xRcrdDelete(zName, cache.zKeyJrnl);
          }
          return 0;
        }catch(e){
          warn("xDelete",e);
          return cache.setError(e);
        }
      },

      xAccess: function(pProtoVfs, zPath, flags, pResOut){
        cache.popError();
        try{
          /**
             In every test run to date, if this function sets
             *pResOut to anything other than 0, the VFS fails to
             function.  Why that that is is a mystery. How it seems
             to work despite never reporting "file found" is a
             mystery.
          */
          wasm.poke32(pResOut, 0);
          return 0;
        }catch(e){
          error('xAccess',e);
          return cache.setError(e);
        }
      },

      xRandomness: function(pVfs, nOut, pOut){
        const heap = wasm.heap8u();
        let i = 0;
        const npOut = Number(pOut);
        for(; i < nOut; ++i) heap[npOut + i] = (Math.random()*255000) & 0xFF;
        return i;
      },

      xGetLastError: function(pVfs,nOut,pOut){
        const e = cache.popError();
        debug('xGetLastError',e);
        if(e){
          const scope = wasm.scopedAllocPush();
          try{
            const [cMsg, n] = wasm.scopedAllocCString(e.message, true);
            wasm.cstrncpy(pOut, cMsg, nOut);
            if(n > nOut) wasm.poke8(wasm.ptr.add(pOut,nOut,-1), 0);
            debug("set xGetLastError",e.message);
          }catch(e){
            return capi.SQLITE_NOMEM;
          }finally{
            wasm.scopedAllocPop(scope);
          }
        }
        return e ? ((e.resultCode | 0) || capi.SQLITE_IOERR) : 0;
      }

//#if nope
      // these impls work but there's currently no pressing need _not_ use
      // the native impls.
      xCurrentTime: function(pVfs,pOut){
        wasm.poke64f(pOut, 2440587.5 + (Date.now()/86400000));
        return 0;
      },

      xCurrentTimeInt64: function(pVfs,pOut){
        wasm.poke64(pOut, (2440587.5 * 86400000) + Date.now());
        return 0;
      }
//#endif
    }/*.vfs*/,

    /**
       kvvfs has separate sqlite3_api_methods impls for some of the
       methods depending on whether it's a db or journal file. Some
       of the methods use shared impls but others are specific to
       either db or journal files.
    */
    ioDb:{
      /* sqlite3_kvvfs_methods::pIoDb's methods */
      xClose: function(pFile){
        try{
          const h = pFileHandles.get(pFile);
          //debug("xClose", pFile, h);
          if( h ){
            pFileHandles.delete(pFile);
            const s = storageForZClass(h.jzClass);
            s.files = s.files.filter((v)=>v!==h.file);
            if( --s.refc<=0 && s.deleteAtRefc0 ){
              deleteStorage(s);
            }
            originalIoMethods(h.file).xClose(pFile);
            h.file.dispose();
            notifyListeners('close', s, s.files.length);
          }else{
            /* Can happen if xOpen fails */
          }
          return 0;
        }catch(e){
          error("xClose",e);
          return capi.SQLITE_ERROR;
        }
      },
//#if nope
      xRead: function(pFile,pTgt,n,iOff64){},
      xWrite: function(pFile,pSrc,n,iOff64){},
      xTruncate: function(pFile,i64){},
      xSync: function(pFile,flags){},
      xFileControl: function(pFile, opId, pArg){},
      xFileSize: function(pFile,pi64Out){},
      xLock: function(pFile,iLock){},
      xUnlock: function(pFile,iLock){},
      xCheckReservedLock: function(pFile,piOut){},
      xFileControl: function(pFile,iOp,pArg){},
      xSectorSize: function(pFile){},
      xDeviceCharacteristics: function(pFile){}
//#endif
    }/*.ioDb*/,

    ioJrnl:{
      /* sqlite3_kvvfs_methods::pIoJrnl's methods. Those set to true
         are copied as-is from the ioDb objects. Others are specific
         to journal files. */
      xClose: true,
//#if nope
      xRead: function(pFile,pTgt,n,iOff64){},
      xWrite: function(pFile,pSrc,n,iOff64){},
      xTruncate: function(pFile,i64){},
      xSync: function(pFile,flags){},
      xFileControl: function(pFile, opId, pArg){},
      xFileSize: function(pFile,pi64Out){},
      xLock: true,
      xUnlock: true,
      xCheckReservedLock: true,
      xFileControl: function(pFile,iOp,pArg){},
      xSectorSize: true,
      xDeviceCharacteristics: true
//#endif
    }/*.ioJrnl*/
  }/*methodOverrides*/;

  debug("pVfs and friends", pVfs, pIoDb, pIoJrnl,
        kvvfsMethods, capi.sqlite3_file.structInfo,
        KVVfsFile.structInfo);
  try {
    cache.keybuf = Object.create(null);
    cache.keybuf.n = kvvfsMethods.$nBufferSize;
    util.assert( cache.keybuf.n>1024*129, "Key buffer is not large enough"
                 /* Native is SQLITE_KVOS_SZ is 133073 as of this writing */ );
    for(const e of Object.entries(methodOverrides.recordHandler)){
      // Overwrite kvvfsMethods's callbacks
      const k = e[0], f = e[1];
      recordHandler[k] = f;
      if( 0 ){
        // bug: this should work
        kvvfsMethods.installMethod(k, f);
      }else{
        kvvfsMethods[kvvfsMethods.memberKey(k)] =
          wasm.installFunction(kvvfsMethods.memberSignature(k), f);
      }
    }
    for(const e of Object.entries(methodOverrides.vfs)){
      // Overwrite some pVfs entries and stash the original impls
      const k = e[0], f = e[1], km = pVfs.memberKey(k),
            member = pVfs.structInfo.members[k]
            || util.toss("Missing pVfs.structInfo[",k,"]");
      originalMethods.vfs[k] = wasm.functionEntry(pVfs[km]);
      pVfs[km] = wasm.installFunction(member.signature, f);
    }
    for(const e of Object.entries(methodOverrides.ioDb)){
      // Similar treatment for pVfs.$pIoDb a.k.a. pIoDb...
      const k = e[0], f = e[1], km = pIoDb.memberKey(k);
      originalMethods.ioDb[k] = wasm.functionEntry(pIoDb[km])
        || util.toss("Missing native pIoDb[",km,"]");
      pIoDb[km] = wasm.installFunction(pIoDb.memberSignature(k), f);
    }
    for(const e of Object.entries(methodOverrides.ioJrnl)){
      // Similar treatment for pVfs.$pIoJrnl a.k.a. pIoJrnl...
      const k = e[0], f = e[1], km = pIoJrnl.memberKey(k);
      originalMethods.ioJrnl[k] = wasm.functionEntry(pIoJrnl[km])
        || util.toss("Missing native pIoJrnl[",km,"]");
      if( true===f ){
        /* use pIoDb's copy */
        pIoJrnl[km] = pIoDb[km] || util.toss("Missing copied pIoDb[",km,"]");
      }else{
        pIoJrnl[km] = wasm.installFunction(pIoJrnl.memberSignature(k), f);
      }
    }
  }finally{
    kvvfsMethods.dispose();
    pVfs.dispose();
    pIoDb.dispose();
    pIoJrnl.dispose();
  }

  /*
    That gets all of the low-level bits out of the way. What follows
    are the public API additions.
  */

  /**
     Clears all storage used by the kvvfs DB backend, deleting any
     DB(s) stored there.

     Its argument must be the name of a kvvfs storage object:

     - 'session'
     - 'local'
     - '' - see below.
     - A transient kvvfs storage object name.

     In the first two cases, only sessionStorage resp. localStorage is
     cleared. An empty string resolves to both 'local' and 'session'
     storage.

     Returns the number of entries cleared.

     As of kvvfs version 2:

     This API is available in Worker threads but does not have access
     to localStorage or sessionStorage in them. Prior versions did not
     include this API in Worker threads.

     Differences in this function in version 2:

     - It accepts an arbitrary storage name. In v1 this was a silent
     no-op for any names other than ('local','session','').

     - It throws if a db currently has the storage opened. That
     version 1 did not throw for this case was due to an architectural
     limitation which has since been overcome.
  */
  const sqlite3_js_kvvfs_clear = function callee(which){
    if( !which ){
      return callee('local') + callee('session');
    }
    const store = storageForZClass(which);
    if( !store ) return 0;
    const keyPrefix = store.keyPrefix;
    if( store.files.length ){
      toss3(capi.SQLITE_ACCESS,
            "Cannot clear in-use database storage.");
    }
    const s = store.storage;
    const toRm = [] /* keys to remove */;
    let i, n = s.length;
    //debug("kvvfs_clear",store,s);
    for( i = 0; i < n; ++i ){
      const k = s.key(i);
      //debug("kvvfs_clear ?",k);
      if(!keyPrefix || k.startsWith(keyPrefix)) toRm.push(k);
    }
    toRm.forEach((kk)=>s.removeItem(kk));
    //alertFilesToReload(store);
    return toRm.length;
  };

  /**
     This routine estimates the approximate amount of
     storage used by the given kvvfs back-end.

     Its arguments are as documented for sqlite3_js_kvvfs_clear(),
     only the operation this performs is different.

     The returned value is twice the "length" value of every matching
     key and value, noting that JavaScript stores each character in 2
     bytes.

     The returned size is not authoritative from the perspective of
     how much data can fit into localStorage and sessionStorage, as
     the precise algorithms for determining those limits are
     unspecified and may include per-entry overhead invisible to
     clients.
  */
  const sqlite3_js_kvvfs_size = function callee(which){
    if( !which ){
      return callee('local') + callee('session');
    }
    const store = storageForZClass(which);
    if( !store ) return 0;
    const s = store.storage;
    let i, sz = 0;
    for(i = 0; i < s.length; ++i){
      const k = s.key(i);
      if(!store.keyPrefix || k.startsWith(store.keyPrefix)){
        sz += k.length;
        sz += s.getItem(k).length;
      }
    }
    return sz * 2 /* because JS uses 2-byte char encoding */;
  };

  /**
     Copies the entire contents of the given transient storage object
     into a JSON-friendly form.  The returned object is structured as
     follows...

     - "name": the name of the storage. This is 'local' or 'session'
     for localStorage resp. sessionStorage, and an arbitrary name for
     transient storage. This propery may be changed before passing
     this object to sqlite3_js_kvvfs_import() in order to
     import into a different storage object.

     - "timestamp": the time this function was called, in Unix
     epoch milliseconds.

     - "size": the unencoded db size.

     - "journal": if includeJournal is true and this db has a
     journal, it is stored as a string here, otherwise this property
     is not set.

     - "pages": An array holding the raw encoded db pages in their
     proper order.

     Throws if this db is not opened.

     The encoding of the underlying database is not part of this
     interface - it is simply passed on as-is. Interested parties are
     directed to src/os_kv.c in the SQLite source tree, with the
     caveat that that code also does not offer a public interface.
     i.e. the encoding is a private implementation detail of kvvfs.
     The format may be changed in the future but kvvfs will continue
     to support the current form.

     Added in version @kvvfs-v2-added-in@.
  */
  const sqlite3_js_kvvfs_export = function(storageName,includeJournal=true){
    const store = storageForZClass(storageName);
    if( !store ){
      toss3(capi.SQLITE_NOTFOUND,
            "There is no kvvfs storage named",storageName);
    }
    //debug("store to export=",store);
    const s = store.storage;
    const rc = Object.assign(Object.create(null),{
      name: store.jzClass,
      timestamp: Date.now(),
      pages: []
    });
    const pages = Object.create(null);
    const keyPrefix = kvvfsKeyPrefix(rc.name);
    const rxTail = keyPrefix
          ? /^kvvfs-[^-]+-(\w+)/ /* X... part of kvvfs-NAME-X... */
          : undefined;
    let i = 0, n = s.length;
    for( ; i < n; ++i ){
      const k = s.key(i);
      if( !keyPrefix || k.startsWith(keyPrefix) ){
        let kk = (keyPrefix ? rxTail.exec(k) : undefined)?.[1] ?? k;
        switch( kk ){
          case 'jrnl':
            if( includeJournal ) rc.journal = s.getItem(k);
            break;
          case 'sz':
            rc.size = +s.getItem(k);
            break;
          default:
            kk = +kk /* coerce to number */;
            if( !util.isInt32(kk) || kk<=0 ){
              toss3(capi.SQLITE_RANGE, "Malformed kvvfs key: "+k);
            }
            pages[kk] = s.getItem(k);
            break;
        }
      }
    }
    /* Now sort the page numbers and move them into an array. In JS
       property keys are always strings, so we have to coerce them to
       numbers so we can get them sorted properly for the array. */
    Object.keys(pages).map((v)=>+v).sort().forEach(
      (v)=>rc.pages.push(pages[v])
    );
    return rc;
  }/* sqlite3_js_kvvfs_export */;

  /**
     The counterpart of sqlite3_js_kvvfs_export(). Its
     argument must be the result of that function() or
     a compatible one.

     This either replaces the contents of an existing transient
     storage object or installs one named exp.name, setting
     the storage's db contents to that of the exp object.

     Throws on error. Error conditions include:

     - The given storage object is currently opened by any db.
     Performing this page-by-page import would invoke undefined
     behavior on them.

     - Malformed input object.

     If it throws after starting the import then it clears the storage
     before returning, to avoid leaving the db in an undefined
     state. It may throw for any of the above-listed conditions before
     reaching that step, in which case the db is not modified. If
     exp.name refers to a new storage name then if it throws, the name
     does not get installed.

     Added in version @kvvfs-v2-added-in@.
  */
  const sqlite3_js_kvvfs_import = function(exp, overwrite=false){
    if( !exp?.timestamp
        || !exp.name
        || undefined===exp.size
        || exp.size<0 || exp.size>=0x7fffffff
        || !Array.isArray(exp.pages) ){
      toss3(capi.SQLITE_MISUSE, "Malformed export object.");
    }
    //warn("importFromObject() is incomplete");
    validateStorageName(exp.name);
    let store = storageForZClass(exp.name);
    const isNew = !store;
    if( store ){
      if( !overwrite ){
        //warn("Storage exists:",arguments,store);
        toss3(capi.SQLITE_ACCESS,
              "Storage '"+exp.name+"' already exists and",
              "overwrite was not specified.");
      }else if( !store.files || !store.jzClass ){
        toss3(capi.SQLITE_ERROR,
              "Internal storage object", exp.name,"seems to be malformed.");
      }else if( store.files.length ){
        toss3(capi.SQLITE_IOERR_ACCESS,
              "Cannot import db storage while it is in use.");
      }
      sqlite3_js_kvvfs_clear(exp.name, true);
    }else{
      store = newStorageObj(exp.name);
      //warn("Installing new storage:",store);
    }
    //debug("Importing store",store.poolEntry.files.length, store);
    //debug("object to import:",exp);
    const keyPrefix = kvvfsKeyPrefix(exp.name);
    try{
      /* Force the native KVVfsFile instances to re-read the db
         and page size. */;
      const s = store.storage;
      s.setItem(keyPrefix+'sz', exp.size);
      if( exp.journal ) s.setItem(keyPrefix+'jrnl', exp.journal);
      exp.pages.forEach((v,ndx)=>s.setItem(keyPrefix+(ndx+1), v));
      if( isNew ) installStorageAndJournal(store);
    }catch(e){
      if( !isNew ){
        try{sqlite3_js_kvvfs_clear(exp.name, true);}
        catch(ee){/*ignored*/}
      }
      throw e;
    }
    return this;
  };

  /**
     If no kvvfs storage exists with the given name, one is
     installed. If one exists, its reference count is increased so
     that it won't be freed by the closing of a database or journal
     file.

     Throws if the name is not valid for a new storage object.

     Added in version @kvvfs-v2-added-in@.
  */
  const sqlite3_js_kvvfs_reserve = function(name){
    let store = storageForZClass(name);
    if( store ){
      ++store.refc;
      return;
    }
    validateStorageName(name);
    installStorageAndJournal(newStorageObj(name));
  };

  /**
     Conditionally "unlinks" a kvvfs storage object, reducing its
     reference count by 1.

     This is a no-op if name ends in "-journal" or refers to a
     built-in storage object.

     It will not lower the refcount below the number of
     currently-opened db/journal files for the storage (so that it
     cannot delete it out from under them).

     If the refcount reaches 0 then the storage object is
     removed.

     Returns true if it reduces the refcount, else false.  A result of
     true does not necessarily mean that the storage unit was removed,
     just that its refcount was lowered.

     Added in version @kvvfs-v2-added-in@.
  */
  const sqlite3_js_kvvfs_unlink = function(name){
    const store = storageForZClass(name);
    if( !store
        || kvvfsIsPersistentName(store.jzClass)
        || isBuiltinName(store.jzClass)
        || cache.rxJournalSuffix.test(name) ) return false;
    if( store.refc > store.files.length || 0===store.files.length ){
      if( --store.refc<=0 ){
        /* Ignoring deleteAtRefc0 for an explicit unlink */
        deleteStorage(store);
      }
      return true;
    }
    return false;
  };

  /**
     Adds an event listener to a kvvfs storage object. The idea is
     that this can be used to asynchronously back up one kvvfs storage
     object to another or another channel entirely. (The caveat in the
     latter case is that kvvfs's format is not readily consumable by
     downstream code.)

     Its argument must be an object with the following properties:

     - storage: the name of the kvvfs storage object.

     - reserve [=false]: if true, sqlite3_js_kvvfs_reserve() is used
     to ensure that the storage exists if it does not already.
     If this is false and the storage does not exist then an
     exception is thrown.

     - events: an object which may have any of the following
     callback function properties: open, close, write, delete.

     Each one of the events callbacks will be called asynchronously
     when the given storage performs those operations. They may be
     asynchronous. All exceptions, including those via Promises, are
     ignored but may trigger warning output on the console.

     Each callback gets passed a single object with the following
     properties:

     .type = the same as the name of the callback

     .storageName = the name of the storage object

     .data = callback-dependent:

     - 'open' and 'close' get an integer, the number of
     currently-opened handles on the storage.

     - 'write' gets a length-two array holding the key and value which
     were written (both strings).

     - 'delete' gets the string-type key of the deleted record.

     Passing the same object ot sqlite3_js_kvvfs_unlisten() will
     remove the listener.

     The arguments to 'write' and 'delete' are in one of the following
     forms:

     - 'sz' = the unencoded db size as a string

     - 'jrnl' = the current db journal as a string

     - '[1-9][0-9]*' (a db page number) = an encoded db page

     For 'local' and 'session' storage, all of those keys have a
     prefix of 'kvvfs-local-' resp. 'kvvfs-session-'. This is required
     both for backwards compatibility and to enable dbs in those
     storage objects to coexit with client data. Other storage objects
     do not have a prefix.

     Design note: JS has StorageEvents but only in the main thread,
     which is why the listeners are not based on that.

     Added in version @kvvfs-v2-added-in@.
  */
  const sqlite3_js_kvvfs_listen = function(opt){
    if( !opt || 'object'!==typeof opt ){
      toss3(capi.SQLITE_MISUSE, "Expecting a listener object.");
    }
    let store = storageForZClass(opt.storage);
    if( !store ){
      if( opt.storage && opt.reserve ){
        sqlite3_js_kvvfs_reserve(opt.storage);
        store = storageForZClass(opt.storage);
        util.assert(store,
                    "Unexpectedly cannot fetch reserved storage "
                    +opt.storage);
      }else{
        toss3(capi.SQLITE_NOTFOUND,"No such storage:",opt.storage);
      }
    }
    if( opt.events ){
      store.listeners.push(opt.events);
    }
  };

  /**
     Removes the kvvfs event listeners for the given options
     object. It must be passed the same object instance which was
     passed to sqlite3_js_kvvfs_listen().

     This has no side effects if opt is invalid or is not a match for
     any listeners.

     Added in version @kvvfs-v2-added-in@.
  */
  const sqlite3_js_kvvfs_unlisten = function(opt){
    const store = storageForZClass(opt?.storage);
    if( store && opt.events ){
      store.listeners = store.listeners.filter((v)=>v!==opt.events);
    }
  };

  /**
     Public interface for kvvfs v2. The capi.sqlite3_js_kvvfs_...()
     routines remain in place for v1. Some members of this class proxy
     to those functions but use different default argument values in
     some cases.
  */
  sqlite3.kvvfs = Object.assign(Object.create(null),{
    reserve:  sqlite3_js_kvvfs_reserve,
    import:   sqlite3_js_kvvfs_import,
    export:   sqlite3_js_kvvfs_export,
    unlink:   sqlite3_js_kvvfs_unlink,
    listen:   sqlite3_js_kvvfs_listen,
    unlisten: sqlite3_js_kvvfs_unlisten,
    exists:   (name)=>!!storageForZClass(name),
    size:     sqlite3_js_kvvfs_size,
    clear:    sqlite3_js_kvvfs_clear
  });

  if( sqlite3.__isUnderTest ){
    /* For inspection via the dev tools console. */
    sqlite3.kvvfs.test = {
      pFileHandles,
      cache,
      storageForZClass,
      KVVfsStorage
    };
  }

  if( globalThis.Storage ){
    /**
       Prior to version 2, kvvfs was only available in the main
       thread.  We retain that for the v1 APIs, exposing them only in
       the main UI thread. As of version 2, kvvfs is available in all
       threads but only via its v2 interface (sqlite3.kvvfs).

       These versions have a default argument value of "" which the v2
       versions lack.
    */
    capi.sqlite3_js_kvvfs_size = (which="")=>sqlite3_js_kvvfs_size(which);
    capi.sqlite3_js_kvvfs_clear = (which="")=>sqlite3_js_kvvfs_clear(which);
  }

  if(sqlite3.oo1?.DB){
    /**
       Functionally equivalent to DB(storageName,'c','kvvfs') except
       that it throws if the given storage name is not one of 'local'
       or 'session'.

       As of version 3.46, the argument may optionally be an options
       object in the form:

       {
         filename: 'session'|'local',
         ... etc. (all options supported by the DB ctor)
       }

       noting that the 'vfs' option supported by main DB
       constructor is ignored here: the vfs is always 'kvvfs'.
    */
    const DB = sqlite3.oo1.DB;
    sqlite3.oo1.JsStorageDb = function(
      storageName = sqlite3.oo1.JsStorageDb.defaultStorageName
    ){
      const opt = DB.dbCtorHelper.normalizeArgs(...arguments);
      opt.vfs = 'kvvfs';
      switch( opt.filename ){
          /* sqlite3_open(), in these builds, recognizes the names
             below and performs some magic which we want to bypass
             here for sanity's sake. */
        case ":sessionStorage:": opt.filename = 'session'; break;
        case ":localStorage:": opt.filename = 'local'; break;
      }
      const m = /(file:(\/\/)?)([^?]+)/.exec(opt.filename);
      validateStorageName( m ? m[3] : opt.filename);
      DB.dbCtorHelper.call(this, opt);
    };
    sqlite3.oo1.JsStorageDb.defaultStorageName
      = cache.storagePool.session ? 'session' : 'localThread';
    const jdb = sqlite3.oo1.JsStorageDb;
    jdb.prototype = Object.create(DB.prototype);
    jdb.clearStorage = sqlite3_js_kvvfs_clear;
    /**
       Clears this database instance's storage or throws if this
       instance has been closed. Returns the number of
       database blocks which were cleaned up.
    */
    jdb.prototype.clearStorage = function(){
      return jdb.clearStorage(this.affirmOpen().filename, true);
    };
    /** Equivalent to sqlite3_js_kvvfs_size(). */
    jdb.storageSize = sqlite3_js_kvvfs_size;
    /**
       Returns the _approximate_ number of bytes this database takes
       up in its storage or throws if this instance has been closed.
    */
    jdb.prototype.storageSize = function(){
      return jdb.storageSize(this.affirmOpen().filename, true);
    };
  }/*sqlite3.oo1.JsStorageDb*/

})/*globalThis.sqlite3ApiBootstrap.initializers*/;
//#savepoint rollback
//#endif not omit-kvvfs
