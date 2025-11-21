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

  const util = sqlite3.util;

  if( !util.isUIThread() ){
    /* One test currently relies on this VFS not being visible in
       Workers. Once we add generic object storage, we can retain this
       VFS in Workers, we just can't provide local/sessionStorage
       access there. */
    capi.sqlite3_vfs_unregister(pKvvfs);
    return;
  }

  const wasm = sqlite3.wasm,
        hop = (o,k)=>Object.prototype.hasOwnProperty.call(o,k);
  /**
     Implementation of JS's Storage interface for use
     as backing store of the kvvfs.
  */
  class ObjectStorage /* extends Storage (ctor may not be legally called) */ {
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
  }/*ObjectStorage*/;

  /**
     Internal helper for sqlite3_js_kvvfs_clear() and friends.
     Its argument should be one of ('local','session',"").
  */
  const __kvfsWhich = function(which){
    const rc = Object.create(null);
    rc.prefix = 'kvvfs-'+which;
    rc.stores = [];
    if( globalThis.sessionStorage
        && ('session'===which || ""===which)){
      rc.stores.push(globalThis.sessionStorage);
    }
    if( globalThis.localStorage
        && ('local'===which || ""===which) ){
      rc.stores.push(globalThis.localStorage);
    }
    return rc;
  };

  /**
     Clears all storage used by the kvvfs DB backend, deleting any
     DB(s) stored there. Its argument must be either 'session',
     'local', or "". In the first two cases, only sessionStorage
     resp. localStorage is cleared. If it's an empty string (the
     default) then both are cleared. Only storage keys which match
     the pattern used by kvvfs are cleared: any other client-side
     data are retained.

     This function is only available in the main window thread.

     Returns the number of entries cleared.
  */
  capi.sqlite3_js_kvvfs_clear = function(which=""){
    let rc = 0;
    const kvWhich = __kvfsWhich(which);
    kvWhich.stores.forEach((s)=>{
      const toRm = [] /* keys to remove */;
      let i;
      for( i = 0; i < s.length; ++i ){
        const k = s.key(i);
        if(k.startsWith(kvWhich.prefix)) toRm.push(k);
      }
      toRm.forEach((kk)=>s.removeItem(kk));
      rc += toRm.length;
    });
    return rc;
  };

  /**
     This routine guesses the approximate amount of
     window.localStorage and/or window.sessionStorage in use by the
     kvvfs database backend. Its argument must be one of ('session',
     'local', ""). In the first two cases, only sessionStorage
     resp. localStorage is counted. If it's an empty string (the
     default) then both are counted. Only storage keys which match
     the pattern used by kvvfs are counted. The returned value is
     twice the "length" value of every matching key and value,
     noting that JavaScript stores each character in 2 bytes.

     The returned size is not authoritative from the perspective of
     how much data can fit into localStorage and sessionStorage, as
     the precise algorithms for determining those limits are
     unspecified and may include per-entry overhead invisible to
     clients.
  */
  capi.sqlite3_js_kvvfs_size = function(which=""){
    let sz = 0;
    const kvWhich = __kvfsWhich(which);
    kvWhich.stores.forEach((s)=>{
      let i;
      for(i = 0; i < s.length; ++i){
        const k = s.key(i);
        if(k.startsWith(kvWhich.prefix)){
          sz += k.length;
          sz += s.getItem(k).length;
        }
      }
    });
    return sz * 2 /* because JS uses 2-byte char encoding */;
  };

  const kvvfsMakeKey = wasm.exports.sqlite3__wasm_kvvfsMakeKeyOnPstack;
  const pstack = wasm.pstack;
  const cache = Object.create(null);
  cache.jzClassToStorage = Object.assign(Object.create(null),{
    /* Map of JS-stringified KVVfsFile::zClass names to
       reference-counted Storage objects. We refcount so that xClose()
       does not pull one out from another instance. */
    local:             {refc: 2, s: globalThis.localStorage},
    session:           {refc: 2, s: globalThis.sessionStorage}
  });
  cache.jzClassToStorage['local-journal'] =
    cache.jzClassToStorage.local;
  cache.jzClassToStorage['session-journal'] =
    cache.jzClassToStorage.session;


  const kvvfsStorage = function(zClass){
    const s = wasm.cstrToJs(zClass);
    if( cache.jzClassToStorage[s] ){
      return cache.jzClassToStorage[s].s;
    }
    if( !cache.rxSession ){
      cache.rxSession = /^session(-journal)?$/;
      cache.rxLocal = /^local(-journal)?$/;
    }
    if( cache.rxSession.test(s) ) return sessionStorage;
    if( cache.rxLocal.test(s) ) return localStorage;
    return cache.jzClassToStorage(s)?.s;
  }.bind(Object.create(null));

  const pFileHandles = new Map(
    /* sqlite3_file pointers => objects, each of which has:
       .s = Storage object
       .f = KVVfsFile instance
       .n = JS-string form of f.$zName
    */
  );

  const debug = sqlite3.config.debug.bind(sqlite3.config);
  const warn = sqlite3.config.warn.bind(sqlite3.config);

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

      /* sqlite3_kvvfs_methods's own direct methods */
      recordHandler: {
        xRcrdRead: (zClass, zKey, zBuf, nBuf)=>{
          const stack = pstack.pointer,
                astack = wasm.scopedAllocPush();
          try{
            const zXKey = kvvfsMakeKey(zClass,zKey);
            if(!zXKey) return -3/*OOM*/;
            const jKey = wasm.cstrToJs(zXKey);
            const jV = kvvfsStorage(zClass).getItem(jKey);
            if(!jV) return -1;
            const nV = jV.length /* We are relying 100% on v being
                                    ASCII so that jV.length is equal
                                    to the C-string's byte length. */;
            if(nBuf<=0) return nV;
            else if(1===nBuf){
              wasm.poke(zBuf, 0);
              return nV;
            }
            const zV = wasm.scopedAllocCString(jV);
            if(nBuf > nV + 1) nBuf = nV + 1;
            wasm.heap8u().copyWithin(
              Number(zBuf), Number(zV), wasm.ptr.addn(zV, nBuf,- 1)
            );
            wasm.poke(wasm.ptr.add(zBuf, nBuf, -1), 0);
            return nBuf - 1;
          }catch(e){
            sqlite3.config.error("kvrecordRead()",e);
            return -2;
          }finally{
            pstack.restore(stack);
            wasm.scopedAllocPop(astack);
          }
        },

        xRcrdWrite: (zClass, zKey, zData)=>{
          const stack = pstack.pointer;
          try {
            const zXKey = kvvfsMakeKey(zClass,zKey);
            if(!zXKey) return SQLITE_NOMEM;
            const jKey = wasm.cstrToJs(zXKey);
            kvvfsStorage(zClass).setItem(jKey, wasm.cstrToJs(zData));
            return 0;
          }catch(e){
            sqlite3.config.error("kvrecordWrite()",e);
            return capi.SQLITE_IOERR;
          }finally{
            pstack.restore(stack);
          }
        },

        xRcrdDelete: (zClass, zKey)=>{
          const stack = pstack.pointer;
          try {
            const zXKey = kvvfsMakeKey(zClass,zKey);
            if(!zXKey) return capi.SQLITE_NOMEM;
            kvvfsStorage(zClass).removeItem(wasm.cstrToJs(zXKey));
            return 0;
          }catch(e){
            sqlite3.config.error("kvrecordDelete()",e);
            return capi.SQLITE_IOERR;
          }finally{
            pstack.restore(stack);
          }
        }
      }/*recordHandler*/,
      /**
         After initial refactoring to support the use of arbitrary Storage
         objects (the interface from which localStorage and sessionStorage
         dervie), we will apparently need to override some of the
         associated sqlite3_vfs and sqlite3_io_methods members.

         We can call back into the native impls when needed, but we
         need to override certain operations here to bypass its strict
         db-naming rules (which, funnily enough, are in place because
         they're relevant (only) for what should soon be the previous
         version of this browser-side implementation). Apropos: the
         port to generic objects would also make non-persistent kvvfs
         available in Worker threads and non-browser builds. They
         could optionally be exported to/from JSON.
      */
      /* sqlite3_kvvfs_methods::pVfs's methods */
      vfs:{
        /**
         */
        xOpen: function(pProtoVfs,zName,pProtoFile,flags,pOutFlags){
          try{
            const rc = originalMethods.vfs.xOpen(pProtoVfs, zName, pProtoFile,
                                                 flags, pOutFlags);
            if( 0==rc ){
              const jzName = wasm.cstrToJs(zName);
              const f = new KVVfsFile(pProtoFile);
              let s = cache.jzClassToStorage[jzName];
              if( s ){
                ++s.refc;
              }else{
                s = cache.jzClassToStorage[jzName] = {
                  refc: 1,
                  s: new ObjectStorage
                };
              }
              debug("kvvfs xOpen", f, jzName, s);
              pFileHandles.set(pProtoFile, {s,f,n:jzName});
            }
            return rc;
          }catch(e){
            warn("kvvfs xOpen:",e);
            return capi.SQLITE_ERROR;
          }
        },
//#if nope
        xDelete: function(pVfs, zName, iSyncFlag){},
        xAccess:function(pProtoVfs, zPath, flags, pResOut){},
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
         methods, depending on whether it's a db or journal file. Some
         of the methods use shared impls but others are specific to
         either db or journal files.
      */
      ioDb:{
        /* sqlite3_kvvfs_methods::pIoDb's methods */
        xClose: function(pFile){
          try{
            const h = pFileHandles.get(pFile);
            debug("kvvfs xClose", pFile, h);
            pFileHandles.delete(pFile);
            const s = cache.jzClassToStorage[h.n];
            if( 0===--s.refc ){
              delete cache.jzClassToStorage[h.n];
              delete s.s;
              delete s.refc;
            }
            originalIoMethods(h.f).xClose(pFile);
            h.f.dispose();
            return 0;
          }catch(e){
            warn("kvvfs xClose",e);
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
    const kvvfsMethods = new sqlite3_kvvfs_methods(
      /* Wraps the static sqlite3_api_methods singleton */
      wasm.exports.sqlite3__wasm_kvvfs_methods()
    );
    const pVfs = new capi.sqlite3_vfs(kvvfsMethods.$pVfs);
    const pIoDb = new capi.sqlite3_io_methods(kvvfsMethods.$pIoDb);
    const pIoJrnl = new capi.sqlite3_io_methods(kvvfsMethods.$pIoJrnl);
    debug("pVfs and friends",pVfs, pIoDb, pIoJrnl);
    try {
      for(const e of Object.entries(ov.recordHandler)){
        // Overwrite kvvfsMethods's callbacks
        kvvfsMethods[kvvfsMethods.memberKey(e[0])] =
          wasm.installFunction(kvvfsMethods.memberSignature(e[0]), e[1]);
      }
      for(const e of Object.entries(ov.vfs)){
        // Overwrite some pVfs entries and stash the original impls
        const k = e[0],
              f = e[1],
              km = pVfs.memberKey(k),
              mbr = pVfs.structInfo.members[k] || util.toss("Missing pVfs member ",km);
        originalMethods.vfs[k] = wasm.functionEntry(pVfs[km]);
        pVfs[km] = wasm.installFunction(mbr.signature, f);
      }
      for(const e of Object.entries(ov.ioDb)){
        // Similar treatment for pVfs.$pIoDb a.k.a. pIoDb...
        const k = e[0],
              f = e[1],
              km = pIoDb.memberKey(k),
              mbr = pIoDb.structInfo.members[k];
        if( !mbr ){
          warn("Missinog pIoDb member",k,km,pIoDb.structInfo);
          util.toss("Missing pIoDb member",k,km);
        }
        originalMethods.ioDb[k] = wasm.functionEntry(pIoDb[km]);
        pIoDb[km] = wasm.installFunction(mbr.signature, f);
      }
      for(const e of Object.entries(ov.ioJrnl)){
        // Similar treatment for pVfs.$pIoJrnl a.k.a. pIoJrnl...
        const k = e[0],
              f = e[1],
              km = pIoJrnl.memberKey(k);
        originalMethods.ioJrnl[k] = wasm.functionEntry(pIoJrnl[km]);
        if( true===f ){
          /* use pIoDb's copy */
          pIoJrnl[km] = pIoDb[km] || util.toss("Missing copied pIoDb member",km);
        }else{
          const mbr = pIoJrnl.structInfo.members[k] || util.toss("Missing pIoJrnl member",km)
          pIoJrnl[km] = wasm.installFunction(mbr.signature, f);
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
    sqlite3.oo1.JsStorageDb = function(
      storageName = sqlite3.oo1.JsStorageDb.defaultStorageName
    ){
      const opt = sqlite3.oo1.DB.dbCtorHelper.normalizeArgs(...arguments);
      storageName = opt.filename;
      if('session'!==storageName && 'local'!==storageName){
        util.toss3("JsStorageDb db name must be one of 'session' or 'local'.");
      }
      opt.vfs = 'kvvfs';
      sqlite3.oo1.DB.dbCtorHelper.call(this, opt);
    };
    sqlite3.oo1.JsStorageDb.defaultStorageName = 'session';
    const jdb = sqlite3.oo1.JsStorageDb;
    jdb.prototype = Object.create(sqlite3.oo1.DB.prototype);
    /** Equivalent to sqlite3_js_kvvfs_clear(). */
    jdb.clearStorage = capi.sqlite3_js_kvvfs_clear;
    /**
       Clears this database instance's storage or throws if this
       instance has been closed. Returns the number of
       database blocks which were cleaned up.
    */
    jdb.prototype.clearStorage = function(){
      return jdb.clearStorage(this.affirmDbOpen().filename);
    };
    /** Equivalent to sqlite3_js_kvvfs_size(). */
    jdb.storageSize = capi.sqlite3_js_kvvfs_size;
    /**
       Returns the _approximate_ number of bytes this database takes
       up in its storage or throws if this instance has been closed.
    */
    jdb.prototype.storageSize = function(){
      return jdb.storageSize(this.affirmDbOpen().filename);
    };
  }/*sqlite3.oo1.JsStorageDb*/

})/*globalThis.sqlite3ApiBootstrap.initializers*/;
//#endif not omit-kvvfs
