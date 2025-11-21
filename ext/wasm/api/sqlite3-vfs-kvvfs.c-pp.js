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
        wasm = sqlite3.wasm;

  if( !util.isUIThread() ){
    /* One test currently relies on this VFS not being visible in
       Workers. Once we add generic object storage, we can retain this
       VFS in Workers, we just can't provide local/sessionStorage
       access there. */
    capi.sqlite3_vfs_unregister(pKvvfs);
    return;
  }

  const __hop = (o,k)=>Object.prototype.hasOwnProperty.call(o,k);
  /**
     Implementation of JS's Storage interface for use
     as backing store of the kvvfs.
  */
  class ObjectStorage extends Storage {
    #map;
    #keys;
    #getKeys(){return this.#keys ??= Object.keys(this.#map);}

    constructor(){
      super();
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
      if( !__hop(this.#map, k) ){
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

  const kvvfsState = Object.assign(Object.create(null),{
    jClassToStorage: new Map(
      /* Map of JS-format KVVfsFile::zClass values to Storage
         object. We can't use WeakMap because that requires an Object
         or Symbol as a key, and we can't use Symbol in this
         context. We don't use KVVfsFile::zClass pointers as keys only
         because of some special-casing for local/session journal
         files which remaps those pointers. */
    )
  });
  kvvfsState.jClassToStorage.set('local', localStorage);
  kvvfsState.jClassToStorage.set('session', sessionStorage);

  /**
     Internal helper for sqlite3_js_kvvfs_clear() and friends.
     Its argument should be one of ('local','session',"").
  */
  const __kvfsWhich = function(which){
    const rc = Object.create(null);
    rc.prefix = 'kvvfs-'+which;
    rc.stores = [];
    let got = 0;
    if( globalThis.sessionStorage
        && ('session'===which || ""===which)){
      rc.stores.push(globalThis.sessionStorage);
      ++got;
    }
    if( globalThis.localStorage
        && ('local'===which || ""===which) ){
      rc.stores.push(globalThis.localStorage);
      ++got;
    }
    if( !got && which ){
      const x = kvvfsState.jClassToStorage.get(which);
      if( x ) rc.stores.push(x);
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
  const kvvfsStorage = function(zClass){
    const s = wasm.cstrToJs(zClass);
    if( !this.rxSession ){
      this.rxSession = /^session(-journal)?$/;
      this.rxLocal = /^local(-journal)?$/;
    }
    if( this.rxSession.test(s) ) return sessionStorage;
    if( this.rxLocal.test(s) ) return localStorage;
    return kvvfsStorage.jClassToStorage.get(s);
  }.bind(Object.create(null));

  { /* Override native sqlite3_kvvfs_methods */
    const kvvfsMethods = new sqlite3_kvvfs_methods(
      /* Wraps the static sqlite3_api_methods singleton */
      wasm.exports.sqlite3__wasm_kvvfs_methods()
    );
    try{
      /**
         Implementations for members of the object referred to by
         sqlite3__wasm_kvvfs_methods(). We swap out the native
         implementations with these, which use JS Storage for their
         backing store.

         The interface docs for these methods are in
         src/os_kv.c:kvrecordRead(), kvrecordWrite(), and
         kvrecordDelete().
      */
      for(const e of Object.entries({
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
      })){
        kvvfsMethods[kvvfsMethods.memberKey(e[0])] =
          wasm.installFunction(kvvfsMethods.memberSignature(e[0]), e[1]);
      }
    }finally{
      kvvfsMethods.dispose();
    }
  }/* Override native sqlite3_api_methods */

  /**
     After initial refactoring to support the use of arbitrary Storage
     objects (the interface from which localStorage and sessionStorage
     dervie), we will apparently need to override some of the
     associated sqlite3_vfs and sqlite3_io_methods members.

     We can call back into the native impls when needed, but we need
     to override certain operations here to bypass its strict
     db-naming rules (which, funnily enough, are in place because
     they're relevant (only) for what should soon be the previous
     version of this browser-side implementation). Apropos: the port
     to generic objects would also make non-persistent kvvfs available
     in Worker threads and non-browser builds. They could optionally
     be exported to/from JSON.
  */
  const eventualTodo = {
    vfsMethods:{
//#if nope
      /**
      */
      xOpen: function(pProtoVfs,zName,pProtoFile,flags,pOutFlags){
      },
      xDelete: function(pVfs, zName, iSyncFlag){},
      xAccess:function(pProtoVfs, zPath, flags, pResOut){},
      xFullPathname: function(pVfs, zPath, nOut, zOut){},
      xDlOpen: function(pVfs, zFilename){},
      xSleep: function(pVfs,usec){},
      xCurrentTime: function(pVfs,pOutDbl){},
      xCurrentTimeInt64: function(pVfs,pOutI64){},
      xRandomness: function(pVfs, nOut, pOut){
        const heap = wasm.heap8u();
        let i = 0;
        const npOut = Number(pOut);
        for(; i < nOut; ++i) heap[npOut + i] = (Math.random()*255000) & 0xFF;
        return i;
      }
//#endif
    },

//#if nope
    /**
       kvvfs has separate impls for some of the I/O methods,
       depending on whether it's a db or journal file.
    */
    ioMethods:{
      db:{
        xClose: function(pFile){},
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
      },
      jrnl:{
        xClose: todoIOMethodsDb.xClose,
        xRead: function(pFile,pTgt,n,iOff64){},
        xWrite: function(pFile,pSrc,n,iOff64){},
        xTruncate: function(pFile,i64){},
        xSync: function(pFile,flags){},
        xFileControl: function(pFile, opId, pArg){},
        xFileSize: function(pFile,pi64Out){},
        xLock: todoIOMethodsDb.xLock,
        xUnlock: todoIOMethodsDb.xUnlock,
        xCheckReservedLock: todoIOMethodsDb.xCheckReservedLock,
        xFileControl: function(pFile,iOp,pArg){},
        xSectorSize: todoIOMethodsDb.xSectorSize,
        xDeviceCharacteristics: todoIOMethodsDb.xDeviceCharacteristics
      }
    }
//#endif
  }/*eventualTodo*/;

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
        toss3("JsStorageDb db name must be one of 'session' or 'local'.");
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
