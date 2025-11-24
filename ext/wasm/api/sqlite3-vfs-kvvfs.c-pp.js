//#if not omit-kvvfs
/*
  2025-11-21

  The author disclaims copyright to this source code.  In place of a
  legal notice, here is a blessing:

  *   May you do good and not evil.
  *   May you find forgiveness for yourself and forgive others.
  *   May you share freely, never taking more than you give.

  ***********************************************************************

  This file houses the "kvvfs" pieces of the JS API. Most of kvvfs is
  implemented in src/os_kv.c and exposed for use here via
  sqlite3-wasm.c.

  Main project home page: https://sqlite.org

  Documentation home page: https://sqlite.org/wasm
*/

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
  /* We unregister the kvvfs VFS from Worker threads later on. */
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
        hop = (o,k)=>Object.prototype.hasOwnProperty.call(o,k);

  const cache = Object.assign(Object.create(null),{
    rxJournalSuffix: /-journal$/, // TOOD: lazily init once we figure out where
    zKeyJrnl: wasm.allocCString("jrnl"),
    zKeySz: wasm.allocCString("sz")
  });

  const debug = function(){
    sqlite3.config.debug("kvvfs:", ...arguments);
  };
  const warn = function(){
    sqlite3.config.warn("kvvfs:", ...arguments);
  };
  const error = function(){
    sqlite3.config.error("kvvfs:", ...arguments);
  };

  /**
     Implementation of JS's Storage interface for use as backing store
     of the kvvfs. Storage is a native class and its constructor
     cannot be legally called from JS, making it impossible to
     directly subclass Storage. This class implements the Storage
     interface, however, to make it a drop-in replacement for
     localStorage/sessionStorage.

     This impl simply proxies a plain, prototype-less Object, suitable
     for JSON-ing.
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

  /** True if v is one of the special persistant Storage objects. */
  const kvvfsIsPersistentName = (v)=>'local'===v || 'session'===v;

  /**
     Keys in kvvfs have a prefix of "kvvfs-NAME-", where NAME is the
     db name. This key is redundant in JS but it's how kvvfs works (it
     saves each key to a separate file, so needs a distinct namespace
     per data source name). We retain this prefix in 'local' and
     'session' storage for backwards compatibility but elide them from
     "v2" storage, where they're superfluous.
  */
  const kvvfsKeyPrefix = (v)=>kvvfsIsPersistentName(v) ? 'kvvfs-'+v+'-' : '';

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
       real back-end storage. Managed by xOpen().
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
    files: []
  });


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
    if( globalThis.localStorage instanceof globalThis.Storage ){
      cache.storagePool.local = newStorageObj('local', globalThis.localStorage);
    }
    if( globalThis.sessionStorage instanceof globalThis.Storage ){
      cache.storagePool.session = newStorageObj('session', globalThis.sessionStorage);
    }
  }

  for(const k of Object.keys(cache.storagePool)){
    /* Journals in kvvfs are are stored as individual records within
       their Storage-ish object, named "{storage.keyPrefix}jrnl".  We
       always map the db and its journal to the same Storage
       object. */
    const orig = cache.storagePool[k];
    cache.storagePool[k+'-journal'] = orig;
  }

  /**
     Returns the storage object mapped to the given string zClass
     (C-string pointer or JS string).
  */
  const storageForZClass = (zClass)=>
        'string'===typeof zClass
        ? cache.storagePool[zClass]
        : cache.storagePool[wasm.cstrToJs(zClass)];

  /**
     Internal helper for sqlite3_js_kvvfs_clear() and friends.  Its
     argument should be one of ('local','session',"") or the name of
     an opened transient kvvfs db.

     Its handling of which=='' is historical: it means to consider
     both local and session storage. which=='' is also a valid kvvfs
     storage unit name, though. Prior to that possibility, this API
     was only availbe in the main UI thread, so the empty string
     continues to unconditionally work that way in the main UI
     thread. When, however, it's running where local/session storage
     are not availble, it treats which=='' as a storage unit name.

     It returns an object in the form:

     .prefix = the key prefix for this storage. Typically
     ("kvvfs-"+which) for local/sessionStorage and "" for transient
     storage. (The former is historical, retained for backwards
     compatibility.) If `which` is falsy then the prefix is "kvvfs-"
     for backwards compatibility (it will match keys for both local-
     and sessionStorage, but not transient storage or non-kvvfs keys
     in the persistent storages).

     .stores = [ array of Storage-like objects ]. Will only have >1
     element if which is falsy, in which case it contains (if called
     from the main thread) localStorage and sessionStorage. It will be
     empty if no mapping is found or those objects are not available
     in the current environment (e.g. a worker thread).

     .cts = the underlying storagePool entry. This will only be set
     if which is not empty.
  */
  const kvvfsWhich = function callee(which){
    const rc = Object.assign(Object.create(null),{
      stores: []
    });
    if( which || !globalThis.Storage ){
      const s = storageForZClass(which);
      if( s ){
        //debug("kvvfsWhich",s.jzClass,rc.prefix, s.s);
        rc.prefix = s.keyPrefix ?? '';
        rc.stores.push(s.storage);
        rc.cts = s;
      }else{
        rc.prefix = undefined;
      }
    }else{
      rc.prefix = 'kvvfs-';
      // Legacy behavior: return both local and session storage.
      if( cache.storagePool.local ) {
        rc.stores.push(cache.storagePool.local.storage);
      }
      if( cache.storagePool.session ) {
        rc.stores.push(cache.storagePool.session.storage);
      }
    }
    //debug("kvvfsWhich",which,rc);
    return rc;
  };

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

  /** pstack-allocates a key. Caller must eventually restore
      the pstack to free the memory. */
  const keyForStorage = (store, zClass, zKey)=>{
    //debug("keyForStorage(",store, wasm.cstrToJs(zClass), wasm.cstrToJs(zKey));
    return wasm.exports.sqlite3__wasm_kvvfsMakeKeyOnPstack(
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

  if( sqlite3.__isUnderTest ){
    sqlite3.kvvfsStuff = {
      pFileHandles,
      cache
    };
  }

  {
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

    const kvvfsMethods = new sqlite3_kvvfs_methods(
      /* Wraps the static sqlite3_api_methods singleton */
      wasm.exports.sqlite3__wasm_kvvfs_methods()
    );
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
          const stack = pstack.pointer /* keyForStorage() allocs from here */;
          try{
            const jzClass = wasm.cstrToJs(zClass);
            const store = storageForZClass(jzClass);
            if( 0 ){
              debug("xRcrdRead", jzClass, wasm.cstrToJs(zKey),
                    zBuf, nBuf, store );
            }
            if( !store ) return -1;
            const zXKey = keyForStorage(store, zClass, zKey);
            if(!zXKey) return -3/*OOM*/;
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
              util.toss3(capi.SQLITE_RANGE,
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
            return -2;
          }finally{
            pstack.restore(stack);
          }
        },

        xRcrdWrite: (zClass, zKey, zData)=>{
          const stack = pstack.pointer /* keyForStorage() allocs from here */;
          try {
            const jzClass = wasm.cstrToJs(zClass);
            const store = storageForZClass(jzClass);
            const zXKey = keyForStorage(store, zClass, zKey);
            if(!zXKey) return SQLITE_NOMEM;
            store.storage.setItem(
              wasm.cstrToJs(zXKey),
              wasm.cstrToJs(zData)
            );
            return 0;
          }catch(e){
            error("kvrecordWrite()",e);
            return capi.SQLITE_IOERR;
          }finally{
            pstack.restore(stack);
          }
        },

        xRcrdDelete: (zClass, zKey)=>{
          const stack = pstack.pointer /* keyForStorage() allocs from here */;
          try {
            const store = storageForZClass(zClass);
            const zXKey = keyForStorage(store, zClass, zKey);
            if(!zXKey) return capi.SQLITE_NOMEM;
            store.storage.removeItem(wasm.cstrToJs(zXKey));
            return 0;
          }catch(e){
            error("kvrecordDelete()",e);
            return capi.SQLITE_IOERR;
          }finally{
            pstack.restore(stack);
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
          try{
            //cache.zReadBuf ??= wasm.malloc(kvvfsMethods.$nBufferSize);
            if( !zName ){
              zName = (cache.zEmpty ??= wasm.allocCString(""));
            }
            const n = wasm.cstrlen(zName);
            if( !n ){
              warn("file name may not be empty (backwards compatibilty constraint)");
              return capi.SQLITE_RANGE;
            }else if( n > kvvfsMethods.$nKeySize - 8 /*"-journal"*/ - 1 ){
              warn("file name is too long:", wasm.cstrToJs(zName));
              return capi.SQLITE_RANGE;
            }
            const jzClass = wasm.cstrToJs(zName);
            if( jzClass?.length != n ){
              warn("kvvfs file name must be ASCII-only:",jzClass);
              /* This limitation is to avoide any issues with
                 truncating multi-byte characters in kvvfs's key-size
                 limit. */
              return capi.SQLITE_RANGE;
            }
            const rc = originalMethods.vfs.xOpen(pProtoVfs, zName, pProtoFile,
                                                 flags, pOutFlags);
            if( rc ) return rc;
            let deleteAt0 = false;
            if(n && wasm.isPtr(zName)){
              if(capi.sqlite3_uri_boolean(zName, "delete-on-close", 0)){
                deleteAt0 = true;
                //warn("transient=",deleteAt0);
              }
              if(capi.sqlite3_uri_boolean(zName, "wipe-before-open", 0)){
                // TODO
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
              const other = f.$isJournal
                    ? jzClass.replace(cache.rxJournalSuffix,'')
                    : jzClass + '-journal';
              s = cache.storagePool[jzClass]
                = cache.storagePool[other]
                = newStorageObj(jzClass);
              s.files.push(f);
              s.deleteAtRefc0 = deleteAt0;
              debug("xOpen installed storage handles [",
                    jzClass, other,"]", s);
            }
            pFileHandles.set(pProtoFile, {storage: s, file: f, jzClass});
            return 0;
          }catch(e){
            warn("xOpen:",e);
            return capi.SQLITE_ERROR;
          }
        }/*xOpen()*/,

        xDelete: function(pVfs, zName, iSyncFlag){
          try{
            const jzName = wasm.cstrToJs(zName);
            //debug("xDelete", jzName, storageForZClass(jzName));
            if( cache.rxJournalSuffix.test(jzName) ){
              recordHandler.xRcrdDelete(zName, cache.zKeyJrnl);
            }
            return 0;
          }catch(e){
            warn("xDelete",e);
            return capi.SQLITE_ERROR;
          }
        },

        xAccess:function(pProtoVfs, zPath, flags, pResOut){
          try{
            /**
               In every test run to date, if this function sets
               *pResOut to anything other than 0, the VFS fails to
               function.  Why that that is is a mystery. How it seems
               to work despite never reporting "file found" is a
               mystery.
            */
            wasm.poke32(pResOut, 0);
            if( 1 ) return 0;
            if( 1 ){
              /* This is fundamentally exactly what the native impl does. */
              const jzName = wasm.cstrToJs(zPath);
              const drc = recordHandler.xRcrdRead(
                zPath, cache.rxJournalSuffix.test(jzName)
                  ? cache.zKeyJrnl
                  : cache.zKeySz,
                wasm.ptr.null, 0
              );
              if( drc>0 ){
                wasm.poke32(
                  pResOut, 1
                  /* This poke is triggering an abort via xClose().
                     If we poke 0 then no problem... except that
                     xAccess() doesn't report the truth. Same effect
                     if we move that to the native impl
                     os_kv.c's kvvfsAccess(). */
                );
              }
              debug("xAccess", jzName, drc, pResOut, wasm.peek32(pResOut));
              return 0;
            }else{
              const rc = originalMethods.vfs.xAccess(
                pProtoVfs, zPath, flags, pResOut
                /* This one is only valid for local/session storage. */
              );
              if( 0 && !!wasm.peek32(pResOut) ){
                /* The native impl, despite apparently hitting the
                   right data on this side of the call, never sets
                   pResOut to anything other than 0. */
                debug("xAccess *pResOut", wasm.cstrToJs(zPath), wasm.peek32(pResOut));
              }
              return rc;
            }
          }catch(e){
            error('xAccess',e);
            return capi.SQLITE_ERROR;
          }
        },

        xRandomness: function(pVfs, nOut, pOut){
          const heap = wasm.heap8u();
          let i = 0;
          const npOut = Number(pOut);
          for(; i < nOut; ++i) heap[npOut + i] = (Math.random()*255000) & 0xFF;
          return i;
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
      },

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
                const other = h.file.$isJournal
                      ? h.jzClass.replace(cache.rxJournalSuffix,'')
                      : h.jzClass+'-journal';
                debug("cleaning up storage handles [", h.jzClass, other,"]",s);
                delete cache.storagePool[h.jzClass];
                delete cache.storagePool[other];
                if( !sqlite3.__isUnderTest ){
                  delete s.storage;
                  delete s.refc;
                }
              }
              originalIoMethods(h.file).xClose(pFile);
              h.file.dispose();
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
      },

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
      }
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
        recordHandler[e[0]] = e[1];
        kvvfsMethods[kvvfsMethods.memberKey(e[0])] =
          wasm.installFunction(kvvfsMethods.memberSignature(e[0]), e[1]);
      }
      for(const e of Object.entries(methodOverrides.vfs)){
        // Overwrite some pVfs entries and stash the original impls
        const k = e[0], f = e[1], km = pVfs.memberKey(k),
              member = pVfs.structInfo.members[k]
              || util.toss("Missing pVfs.structInfo[",k,"]");
        originalMethods.vfs[k] = wasm.functionEntry(pVfs[km])
          || util.toss("Missing native pVfs[",km,"]");
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
  }/*native method overrides*/

  /**
     Clears all storage used by the kvvfs DB backend, deleting any
     DB(s) stored there.

     Its argument must be the name of a kvvfs storage object:

     - 'session'
     - 'local'
     - An empty string means both of 'local' and 'session' storage.
     - A transient kvvfs storage object name.

     In the first two cases, only sessionStorage resp. localStorage is
     cleared. If passed an an empty string (the default) then its
     behavior depends on:

     - If called in the main thread an empty stream means to act on
     both localStorage and sessionStorage. Only storage keys which
     match the pattern used by kvvfs are cleared: any other
     client-side data are retained. Version 1 of this API always
     behaves this way. (This is backwards-compatibility behavior to
     account for an admitted misuse of "".)

     - If called from a thread where globalThis.Storage is not available
     then an empty string is considered to be a legal storage unit name.

     Returns the number of entries cleared.

     As of kvvfs version 2:

     This API is available in Worker threads but does not have access
     to localStorage or sessionStorage in them. Prior versions did not
     include this API in Worker threads.

     Differences in this function in version 2:

     - It accepts an arbitrary storage name. In v1 this was a silent
     no-op for any names other than ('local','session','').

     - In Worker threads, an empty string argument is treated as a
     storage unit name.
  */
  capi.sqlite3_js_kvvfs_clear = function(which=""){
    let rc = 0;
    const store = kvvfsWhich(which);
    const keyPrefix = store.prefix;
    /**
       Historically this code had no way to check whether the storage
       was in use before wiping it, so could not error in that
       case. Whether or not it should, now that it can (as of late
       2025-11), is TBD.
    */
    store.stores.forEach((s)=>{
      const toRm = [] /* keys to remove */;
      let i, n = s.length;
      //debug("kvvfs_clear",store,s);
      for( i = 0; i < n; ++i ){
        const k = s.key(i);
        //debug("kvvfs_clear ?",k);
        if(!keyPrefix || k.startsWith(keyPrefix)) toRm.push(k);
      }
      toRm.forEach((kk)=>s.removeItem(kk));
      rc += toRm.length;
    });
    //if( store.cts ) alertFilesToReload(store.cts);
    return rc;
  };

  /**
     This routine guesses the approximate amount of
     storage used by the given kvvfs back-end.

     The 'which' argument is as documented for
     sqlite3_js_kvvfs_clear(), only the operation this performs is
     different:

     The returned value is twice the "length" value of every matching
     key and value, noting that JavaScript stores each character in 2
     bytes.

     If passed 'local' or 'session' or '' from a thread other than the
     main UI thread, this is effectively a no-op and returns 0.

     The returned size is not authoritative from the perspective of
     how much data can fit into localStorage and sessionStorage, as
     the precise algorithms for determining those limits are
     unspecified and may include per-entry overhead invisible to
     clients.
  */
  capi.sqlite3_js_kvvfs_size = function(which=""){
    let sz = 0;
    const store = kvvfsWhich(which);
    //warn("kvvfs_size storage",store);
    store?.stores?.forEach?.((s)=>{
      //warn("kvvfs_size backend",s);
      let i;
      for(i = 0; i < s.length; ++i){
        const k = s.key(i);
        if(!store.prefix || k.startsWith(store.prefix)){
          sz += k.length;
          sz += s.getItem(k).length;
        }
      }
    });
    return sz * 2 /* because JS uses 2-byte char encoding */;
  };

  /**
     Copies the entire contents of the given transient storage object
     into a JSON-friendly form.  The returned object is structured as
     follows...

     - "name": the name of the storage. This is 'local' or 'session'
     for localStorage resp. sessionStorage, and an arbitrary name for
     transient storage. This propery may be changed before passing
     this object to sqlite3_js_kvvfs_import_storage() in order to
     import into a different storage object.

     - "timestamp": the time this function was called, in Unix
     epoch milliseconds.

     - "size": the unencoded db size.

     - "journal": if includeJournal is true and this db has a
     journal, it is stored as a string here, otherwise this property
     is not set.

     - "pages": An array holddig the object holding the raw encoded
     db pages in their proper order.

     Throws if this db is not opened.

     The encoding of the underlying database is not part of this
     interface - it is simply passed on as-is. Interested parties
     are directed to src/os_kv.c in the SQLite source tree.

     Added in version 3.?? (tenatively 3.52).
  */
  capi.sqlite3_js_kvvfs_export_storage = function(storageName,includeJournal=true){
    const store = storageForZClass(storageName);
    if( !store ){
      util.toss3(capi.SQLITE_NOTFOUND,
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
              util.toss3(capi.SQLITE_RANGE, "Malformed kvvfs key: "+k);
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
  }/* capi.sqlite3_js_kvvfs_export_storage */;

  /**
     INCOMPLETE. This interface is subject to change.

     The counterpart of sqlite3_js_kvvfs_export_storage(). Its
     argument must be the result of that function().

     This either replaces the contents of an existing transient
     storage object or installs one named exp.name, setting
     the storage's db contents to that of the exp object.

     Throws on error. Error conditions include:

     - The give storage object is currently opened by any db.
     Performing this page-by-page import would invoke undefined
     behavior on them.

     - Malformed input object.

     If it throws after starting the import then it clears the
     storage before returning, to avoid leaving the db in an
     undefined state. It may throw for any of the above-listed
     conditions before reaching that step, in which case the db is
     not modified.
  */
  capi.sqlite3_js_kvvfs_import_storage = function(exp, overwrite=false){
    if( !exp?.timestamp
        || !exp.name
        || undefined===exp.size
        || exp.size<0 || exp.size>=0x7fffffff
        || !Array.isArray(exp.pages) ){
      util.toss3(capi.SQLITE_MISUSE, "Malformed export object.");
    }
    //warn("importFromObject() is incomplete");
    let store = storageForZClass(exp.name);
    if( store ){
      if( !overwrite ){
        //warn("Storage exists:",arguments,store);
        util.toss3(capi.SQLITE_ACCESS,
                   "Storage '"+exp.name+"' already exists and",
                   "overwrite was not specified.");
      }else if( !store.files || !store.jzClass ){
        util.toss3(capi.SQLITE_ERROR,
                   "Internal storage object", exp.name,"seems to be malformed.");
      }else if( store.files.length ){
        util.toss3(capi.SQLITE_IOERR_ACCESS,
                   "Cannot import db storage while it is in use.");
      }
      capi.sqlite3_js_kvvfs_clear(exp.name);
    }else{
      if( cache.rxJournalSuffix.test(exp.name) ){
        /* This isn't actually a problem, but the public API does not
           specifically expose the '-journal' name of the storage so
           exporting it "shouldn't happen." */
        util.toss3(capi.SQLITE_MISUSE,
                   "Cowardly refusing to create storage with a",
                   "'-journal' suffix.");
      }
      store = newStorageObj(exp.name);
      cache.storagePool[exp.name] =
        cache.storagePool[exp.name+'-journal'] = store;
      //warn("Installing new storage:",store);
    }
    //debug("Importing store",store.cts.files.length, store);
    //debug("object to import:",exp);
    const keyPrefix = kvvfsKeyPrefix(exp.name);
    try{
      /* Force the native KVVfsFile instances to re-read the db
         and page size. */;
      const s = store.storage;
      s.setItem(keyPrefix+'sz', exp.size);
      if( exp.journal ) s.setItem(keyPrefix+'jrnl', exp.journal);
      exp.pages.forEach((v,ndx)=>s.setItem(keyPrefix+(ndx+1), v));
      //s.getItem("")/*kludge: for KVVfsStorage to reset its keys*/;
    }catch(e){
      capi.sqlite3_js_kvvfs_clear(exp.name);
      throw e;
    }
    return this;
  };

  if(sqlite3?.oo1?.DB){
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
      DB.dbCtorHelper.call(this, opt);
    };
    sqlite3.oo1.JsStorageDb.defaultStorageName = 'session';
    const jdb = sqlite3.oo1.JsStorageDb;
    jdb.prototype = Object.create(DB.prototype);
    jdb.clearStorage = capi.sqlite3_js_kvvfs_clear;
    /**
       Clears this database instance's storage or throws if this
       instance has been closed. Returns the number of
       database blocks which were cleaned up.
    */
    jdb.prototype.clearStorage = function(){
      return jdb.clearStorage(this.affirmOpen().filename);
    };
    /** Equivalent to sqlite3_js_kvvfs_size(). */
    jdb.storageSize = capi.sqlite3_js_kvvfs_size;
    /**
       Returns the _approximate_ number of bytes this database takes
       up in its storage or throws if this instance has been closed.
    */
    jdb.prototype.storageSize = function(){
      return jdb.storageSize(this.affirmOpen().filename);
    };

    if( sqlite3.__isUnderTest ){
      jdb.test = {
        kvvfsWhich,
        cache
      }
    }/* __isUnderTest */
  }/*sqlite3.oo1.JsStorageDb*/

})/*globalThis.sqlite3ApiBootstrap.initializers*/;
//#endif not omit-kvvfs
