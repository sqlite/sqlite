//#if not omit-kvvfs
/*
  2025-11-21

  The author disclaims copyright to this source code.  In place of a
  legal notice, here is a blessing:

  *   May you do good and not evil.
  *   May you find forgiveness for yourself and forgive others.
  *   May you share freely, never taking more than you give.

  ***********************************************************************

  This file houses the "kvvfs" pieces of the JS API.

  Main project home page: https://sqlite.org

  Documentation home page: https://sqlite.org/wasm
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

  const util = sqlite3.util,
        wasm = sqlite3.wasm,
        hop = (o,k)=>Object.prototype.hasOwnProperty.call(o,k);

  const cache = Object.assign(Object.create(null),{
    rxJournalSuffix: /^-journal$/, // TOOD: lazily init once we figure out where
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
     of the kvvfs. Storage's constructor cannot be legally called from
     JS, making it impossible to directly subclass Storage.

     This impl simply proxies a plain, prototype-less Object, suitable
     for JSON-ing.
  */
  class TransientStorage {
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
      this.#map[k]=v;
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
  }/*TransientStorage*/;

  /**
     Map of JS-stringified KVVfsFile::zClass names to
     reference-counted Storage objects. These objects are created in
     xOpen(). Their refcount is decremented in xClose(), and the
     record is destroyed if the refcount reaches 0. We refcount so
     that concurrent active xOpen()s on a given name, and within a
     given thread, use the same storage object.
  */
  cache.jzClassToStorage = Object.assign(Object.create(null),{
    /* Start off with mappings for well-known names. */
    localThread: {
      refc: 3/*never reaches 0*/,
      s: new TransientStorage
    }
  });
  if( globalThis.localStorage ){
    cache.jzClassToStorage.local =
      {
        refc: 3/*never reaches 0*/,
        s: globalThis.localStorage,
        /* If useFullZClass is true, kvvfs storage keys are in
           the form kvvfs-{zClass}-*, else they lack the "-{zClass}"
           part. local/session storage must use the long form for
           backwards compatibility. */
        useFullZClass: true
      };
  }
  if( globalThis.sessionStorage ){
    cache.jzClassToStorage.session =
      {
        refc: 3/*never reaches 0*/,
        s: globalThis.sessionStorage,
        useFullZClass: true
      }
  }
  for(const k of Object.keys(cache.jzClassToStorage)){
    /* Journals in kvvfs are are stored as individual records within
       their Storage-ish object, named "kvvfs-${zClass}-jrnl". We
       always create mappings for both the db file's name and the
       journal's name referring to the same Storage object. */
    const orig = cache.jzClassToStorage[k];
    orig.jzClass = k;
    cache.jzClassToStorage[k+'-journal'] = orig;
  }

  /**
     Internal helper for sqlite3_js_kvvfs_clear() and friends.  Its
     argument should be one of ('local','session',"") or the name of
     an opened transient kvvfs db.

     It returns an object in the form:

     .prefix = the key prefix for this storage. Typically
     ("kvvfs-"+which) for persistent storage and "kvvfs-" for
     transient. (The former is historical, retained for backwards
     compatibility.)

     .stores = [ array of Storage-like objects ]. Will only have >1
     element if which is falsy, in which case it contains (if called
     from the main thread) localStorage and sessionStorage. It will
     be empty if no mapping is found.
  */
  const kvvfsWhich = function callee(which){
    const rc = Object.assign(Object.create(null),{
      prefix: 'kvvfs-' + which,
      stores: []
    });
    if( which ){
      const s = cache.jzClassToStorage[which];
      if( s ){
        //debug("kvvfsWhich",s.jzClass,rc.prefix, s.s);
        if( !s.useFullZClass ){
          rc.prefix = 'kvvfs-';
        }
        rc.stores.push(s.s);
      }
    }else{
      if( globalThis.sessionStorage ) rc.stores.push(globalThis.sessionStorage);
      if( globalThis.localStorage ) rc.stores.push(globalThis.localStorage);
    }
    //debug("kvvfsWhich",which,rc);
    return rc;
  };

  /**
     Clears all storage used by the kvvfs DB backend, deleting any
     DB(s) stored there.

     Its argument must be either 'session', 'local', "", or the name
     of a transient kvvfs storage object file. In the first two cases,
     only sessionStorage resp. localStorage is cleared. If which is an
     empty string (the default) then both localStorage and
     sessionStorage are cleared. Only storage keys which match the
     pattern used by kvvfs are cleared: any other client-side data are
     retained.

     This function only manipulates localStorage and sessionStorage in
     the main UI thread (they don't exist in Worker threads).
     It affects transient kvvfs objects in any thread.

     Returns the number of entries cleared.
  */
  capi.sqlite3_js_kvvfs_clear = function(which=""){
    let rc = 0;
    const store = kvvfsWhich(which);
    store.stores.forEach((s)=>{
      const toRm = [] /* keys to remove */;
      let i, n = s.length;
      //debug("kvvfs_clear",store,s);
      for( i = 0; i < n; ++i ){
        const k = s.key(i);
        //debug("kvvfs_clear ?",k);
        if(k.startsWith(store.prefix)) toRm.push(k);
      }
      toRm.forEach((kk)=>s.removeItem(kk));
      rc += toRm.length;
    });
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
        if(k.startsWith(store.prefix)){
          sz += k.length;
          sz += s.getItem(k).length;
        }
      }
    });
    return sz * 2 /* because JS uses 2-byte char encoding */;
  };

  /** pstack-allocates a key. Caller must eventually restore
      the pstack to free the memory. */
  const keyForStorage = (store, zClass, zKey)=>{
    //debug("keyForStorage(",store, wasm.cstrToJs(zClass), wasm.cstrToJs(zKey));
    return wasm.exports.sqlite3__wasm_kvvfsMakeKeyOnPstack(
      store.useFullZClass ? zClass : null, zKey
    );
  };
  const pstack = wasm.pstack;
  const storageForZClass =
        (zClass)=>cache.jzClassToStorage[wasm.cstrToJs(zClass)];

  const pFileHandles = new Map(
    /* sqlite3_file pointers => objects, each of which has:
       .s = Storage object
       .f = KVVfsFile instance
       .n = JS-string form of f.$zClass
    */
  );

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
    const recordHandler = Object.create(null);
    /**
       Implementations for members of the object referred to by
       sqlite3__wasm_kvvfs_methods(). We swap out the native
       implementations with these, which use JS Storage for their
       backing store.

       The interface docs for these methods are in
       src/os_kv.c:kvrecordRead(), kvrecordWrite(), and
       kvrecordDelete().
    */
    sqlite3_kvvfs_methods.override = {

      /**
        sqlite3_kvvfs_methods's member methods.  These perform the
        fetching, setting, and removal of storage keys on behalf of
        kvvfs. In the native impl these write each db page to a
        separate file. This impl stores each db page as a single
        record in a Storage object which is mapped to zClass.
      */
      recordHandler: {
        xRcrdRead: (zClass, zKey, zBuf, nBuf)=>{
          const stack = pstack.pointer,
                astack = wasm.scopedAllocPush();
          try{
            const store = storageForZClass(zClass);
            if( !store ) return -1;
            if( 0 ){
              debug("xRcrdRead", nBuf, zClass, wasm.cstrToJs(zClass),
                    wasm.cstrToJs(zKey), store);
            }
            const zXKey = keyForStorage(store, zClass, zKey);
            if(!zXKey) return -3/*OOM*/;
            const jV = store.s.getItem(wasm.cstrToJs(zXKey));
            if(!jV) return -1;
            const nV = jV.length /* We are relying 100% on v being
                                 ** ASCII so that jV.length is equal
                                 ** to the C-string's byte length. */;
            if(nBuf<=0) return nV;
            else if(1===nBuf){
              wasm.poke(zBuf, 0);
              return nV;
            }
            const zV = wasm.scopedAllocCString(jV)
            /* TODO: allocate a single 128kb buffer (largest page
               size) for reuse here, or maybe even preallocate
               it somewhere in sqlite3__wasm_kvvfs_...(). */;
            if(nBuf > nV + 1) nBuf = nV + 1;
            wasm.heap8u().copyWithin(
              Number(zBuf), Number(zV), wasm.ptr.addn(zV, nBuf,- 1)
            );
            wasm.poke(wasm.ptr.add(zBuf, nBuf), 0);
            return nBuf - 1;
          }catch(e){
            error("kvrecordRead()",e);
            return -2;
          }finally{
            pstack.restore(stack);
            wasm.scopedAllocPop(astack);
          }
        },

        xRcrdWrite: (zClass, zKey, zData)=>{
          const stack = pstack.pointer;
          try {
            const store = storageForZClass(zClass);
            const zXKey = keyForStorage(store, zClass, zKey);
            if(!zXKey) return SQLITE_NOMEM;
            store.s.setItem(
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
          const stack = pstack.pointer;
          try {
            const store = storageForZClass(zClass);
            const zXKey = keyForStorage(store, zClass, zKey);
            if(!zXKey) return capi.SQLITE_NOMEM;
            store.s.removeItem(wasm.cstrToJs(zXKey));
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
         two sqlite3_io_methods instances so that we can tie Storage
         objects to db names.
      */
      vfs:{
        /* sqlite3_kvvfs_methods::pVfs's methods */
        xOpen: function(pProtoVfs,zName,pProtoFile,flags,pOutFlags){
          try{
            //cache.zReadBuf ??= wasm.malloc(kvvfsMethods.$nBufferSize);
            const n = wasm.cstrlen(zName);
            if( n > kvvfsMethods.$nKeySize - 8 /*"-journal"*/ - 1 ){
              warn("file name is too long:", wasm.cstrToJs(zName));
              return capi.SQLITE_RANGE;
            }
            const rc = originalMethods.vfs.xOpen(pProtoVfs, zName, pProtoFile,
                                                 flags, pOutFlags);
            if( 0==rc ){
              const f = new KVVfsFile(pProtoFile);
              util.assert(f.$zClass, "Missing f.$zClass");
                const jzName = wasm.cstrToJs(zName);
              const jzClass = wasm.cstrToJs(f.$zClass);
              let s = cache.jzClassToStorage[jzClass];
              //debug("xOpen", jzName, jzClass, s);
              if( s ){
                ++s.refc;
              }else{
                /* TODO: a url flag which tells it to keep the storage
                   around forever so that future xOpen()s get the same
                   Storage-ish objects. We can accomplish that by
                   simply increasing the refcount once more. */
                util.assert( !f.$isJournal, "Opening a journal before its db? "+jzName );
                const other = f.$isJournal
                      ? jzName.replace(cache.rxJournalSuffix,'')
                      : jzName + '-journal';
                s = cache.jzClassToStorage[jzClass]
                  = cache.jzClassToStorage[other]
                  = Object.assign(Object.create(null),{
                    jzClass: jzClass,
                    refc: 1/* if this is a db-open, the journal open
                              will follow soon enough and bump the
                              refcount. If we start at 2 here, that
                              pending open will increment it again. */,
                    s: new TransientStorage
                  });
                debug("xOpen installed storage handles [",
                      jzName, other,"]", s);
              }
              pFileHandles.set(pProtoFile, {s,f,n:jzName});
            }
            return rc;
          }catch(e){
            warn("xOpen:",e);
            return capi.SQLITE_ERROR;
          }
        }/*xOpen()*/,

        xDelete: function(pVfs, zName, iSyncFlag){
          try{
            if( 0 ){
              util.assert(zName, "null zName?");
              const jzName = wasm.cstrToJs(zName);
              const s = cache.jzClassToStorage[jzName];
              debug("xDelete", jzName, s);
              if( cache.rxJournalSuffix.test(jzName) ){
                recordHandler.xRcrdDelete(zName, cache.zKeyJrnl);
              }
              return 0;
            }
            return originalMethods.vfs.xDelete(pVfs, zName, iSyncFlag);
          }catch(e){
            warn("xDelete",e);
            return capi.SQLITE_ERROR;
          }
        },

        xAccess:function(pProtoVfs, zPath, flags, pResOut){
          try{
            if( 0 ){
              const jzName = wasm.cstrToJs(zPath);
              const s = cache.jzClassToStorage[jzName];
              debug("xAccess", jzName, s);
              let drc = 1;
              wasm.poke32(pResOut, 0);
              if( s ){
                /* This block is _somehow_ triggering an
                   abort() via xClose(). */
                if(0) debug("cache.zKeyJrnl...",wasm.cstrToJs(cache.zKeyJrnl),
                      wasm.cstrToJs(cache.zKeySz));
                drc = recordHandler.xRcrdRead(
                  zPath,
                  jzName.endsWith("-journal")
                    ? cache.zKeyJrnl
                    : cache.zKeySz,
                  wasm.ptr.null, 0
                );
                debug("xAccess", jzName, drc, pResOut);
                if( drc>0 ){
                  wasm.poke32(
                    pResOut, 1
                    /* This poke is triggering an abort via xClose().
                       If we poke 0 then no problem... except that
                       xAccess() doesn't report the truth. Same effect
                       if we move that to the native impl
                       os_kv.c:kvvfsAccess(). */
                  );
                }
              }
              return 0;
            }
            const rc = originalMethods.vfs.xAccess(
              pProtoVfs, zPath, flags, pResOut
              /* This one is only valid for local/session storage */
            );
            if( 0 && 0===rc ){
              debug("xAccess pResOut", wasm.peek32(pResOut));
            }
            return rc;
          }catch(e){
            error('xAccess',e);
            return capi.SQLITE_ERROR;
          }
        },

//#if nope
        xFullPathname: function(pVfs, zPath, nOut, zOut){},
        xDlOpen: function(pVfs, zFilename){},
        xSleep: function(pVfs,usec){},
        xCurrentTime: function(pVfs,pOutDbl){},
        xCurrentTimeInt64: function(pVfs,pOutI64){},
//#endif
        xRandomness: function(pVfs, nOut, pOut){
          const heap = wasm.heap8u();
          let i = 0;
          const npOut = Number(pOut);
          for(; i < nOut; ++i) heap[npOut + i] = (Math.random()*255000) & 0xFF;
          return i;
        }
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
              const s = cache.jzClassToStorage[h.n];
              util.assert(s, "Missing jzClassToStorage["+h.n+"]");
              if( 0===--s.refc ){
                const other = h.f.$isJournal
                      ? h.n.replace(cache.rxJournalSuffix,'')
                      : h.n+'-journal';
                debug("cleaning up storage handles [", h.n, other,"]",s);
                delete cache.jzClassToStorage[h.n];
                delete cache.jzClassToStorage[other];
                delete s.s;
                delete s.refc;
              }
              originalIoMethods(h.f).xClose(pFile);
              h.f.dispose();
            }else{
              /* Can happen if xOpen fails */
            }
            return 0;
          }catch(e){
            warn("xClose",e);
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
    }/*sqlite3_kvvfs_methods.override*/;

    const ov = sqlite3_kvvfs_methods.override;
    debug("pVfs and friends", pVfs, pIoDb, pIoJrnl,
          kvvfsMethods, capi.sqlite3_file.structInfo,
          KVVfsFile.structInfo);
    try {
      for(const e of Object.entries(ov.recordHandler)){
        // Overwrite kvvfsMethods's callbacks
        recordHandler[e[0]] = e[1];
        kvvfsMethods[kvvfsMethods.memberKey(e[0])] =
          wasm.installFunction(kvvfsMethods.memberSignature(e[0]), e[1]);
      }
      for(const e of Object.entries(ov.vfs)){
        // Overwrite some pVfs entries and stash the original impls
        const k = e[0], f = e[1], km = pVfs.memberKey(k),
              member = pVfs.structInfo.members[k]
              || util.toss("Missing pVfs.structInfo[",k,"]");
        originalMethods.vfs[k] = wasm.functionEntry(pVfs[km])
          || util.toss("Missing native pVfs[",km,"]");
        pVfs[km] = wasm.installFunction(member.signature, f);
      }
      for(const e of Object.entries(ov.ioDb)){
        // Similar treatment for pVfs.$pIoDb a.k.a. pIoDb...
        const k = e[0], f = e[1], km = pIoDb.memberKey(k);
        originalMethods.ioDb[k] = wasm.functionEntry(pIoDb[km])
          || util.toss("Missing native pIoDb[",km,"]");
        pIoDb[km] = wasm.installFunction(pIoDb.memberSignature(k), f);
      }
      for(const e of Object.entries(ov.ioJrnl)){
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
  }/*method overrides*/

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
      DB.dbCtorHelper.call(this, opt);
    };
    sqlite3.oo1.JsStorageDb.defaultStorageName = 'session';
    const jdb = sqlite3.oo1.JsStorageDb;
    jdb.prototype = Object.create(DB.prototype);
    /** Equivalent to sqlite3_js_kvvfs_clear(). */
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
      jdb.prototype.testDbToObject = function(){
        const store = cache.jzClassToStorage[this.affirmOpen().filename];
        if( store ){
          const s = store.s;
          const rc = Object.create(null);
          let i = 0, n = s.length;
          for( ; i < n; ++i ){
            const k = s.key(i), v = s.getItem(k);
            rc[k] = v;
          }
          return rc;
        }
      }
      jdb.testKvvfsWhich = kvvfsWhich;
    }/* __isUnderTest */
  }/*sqlite3.oo1.JsStorageDb*/

})/*globalThis.sqlite3ApiBootstrap.initializers*/;
//#endif not omit-kvvfs
