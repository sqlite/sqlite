//#if 0
/**
   This file is for preprocessor #include into the "opfs" and
   "opfs-wl" impls, as well as their async-proxy part. It must be
   inlined in those files, as opposed to being a shared copy in the
   library, because (A) the async proxy does not load the library and
   (B) it references an object which is local to each of those files
   but which has a 99% identical structure for each.
*/
//#/if
//#// vfs.metrics.enable is a refactoring crutch.
//#define vfs.metrics.enable 0
const initS11n = function(){
  /**
     This proxy de/serializes cross-thread function arguments and
     output-pointer values via the state.sabIO SharedArrayBuffer,
     using the region defined by (state.sabS11nOffset,
     state.sabS11nOffset + state.sabS11nSize]. Only one dataset is
     recorded at a time.

     This is not a general-purpose format. It only supports the
     range of operations, and data sizes, needed by the
     sqlite3_vfs and sqlite3_io_methods operations. Serialized
     data are transient and this serialization algorithm may
     change at any time.

     The data format can be succinctly summarized as:

     Nt...Td...D

     Where:

     - N = number of entries (1 byte)

     - t = type ID of first argument (1 byte)

     - ...T = type IDs of the 2nd and subsequent arguments (1 byte
     each).

     - d = raw bytes of first argument (per-type size).

     - ...D = raw bytes of the 2nd and subsequent arguments (per-type
     size).

     All types except strings have fixed sizes. Strings are stored
     using their TextEncoder/TextDecoder representations. It would
     arguably make more sense to store them as Int16Arrays of
     their JS character values, but how best/fastest to get that
     in and out of string form is an open point. Initial
     experimentation with that approach did not gain us any speed.

     Historical note: this impl was initially about 1% this size by
     using using JSON.stringify/parse(), but using fit-to-purpose
     serialization saves considerable runtime.
  */
  if(state.s11n) return state.s11n;
  const textDecoder = new TextDecoder(),
        textEncoder = new TextEncoder('utf-8'),
        viewU8 = new Uint8Array(state.sabIO, state.sabS11nOffset, state.sabS11nSize),
        viewDV = new DataView(state.sabIO, state.sabS11nOffset, state.sabS11nSize);
  state.s11n = Object.create(null);
  /* Only arguments and return values of these types may be
     serialized. This covers the whole range of types needed by the
     sqlite3_vfs API. */
  const TypeIds = Object.create(null);
  TypeIds.number  = { id: 1, size: 8, getter: 'getFloat64', setter: 'setFloat64' };
  TypeIds.bigint  = { id: 2, size: 8, getter: 'getBigInt64', setter: 'setBigInt64' };
  TypeIds.boolean = { id: 3, size: 4, getter: 'getInt32', setter: 'setInt32' };
  TypeIds.string =  { id: 4 };

  const getTypeId = (v)=>(
    TypeIds[typeof v]
      || toss("Maintenance required: this value type cannot be serialized.",v)
  );
  const getTypeIdById = (tid)=>{
    switch(tid){
      case TypeIds.number.id: return TypeIds.number;
      case TypeIds.bigint.id: return TypeIds.bigint;
      case TypeIds.boolean.id: return TypeIds.boolean;
      case TypeIds.string.id: return TypeIds.string;
      default: toss("Invalid type ID:",tid);
    }
  };

  /**
     Returns an array of the deserialized state stored by the most
     recent serialize() operation (from this thread or the
     counterpart thread), or null if the serialization buffer is
     empty.  If passed a truthy argument, the serialization buffer
     is cleared after deserialization.
  */
  state.s11n.deserialize = function(clear=false){
//#if vfs.metrics.enable
    ++metrics.s11n.deserialize.count;
//#/if
    const t = performance.now();
    const argc = viewU8[0];
    const rc = argc ? [] : null;
    if(argc){
      const typeIds = [];
      let offset = 1, i, n, v;
      for(i = 0; i < argc; ++i, ++offset){
        typeIds.push(getTypeIdById(viewU8[offset]));
      }
      for(i = 0; i < argc; ++i){
        const t = typeIds[i];
        if(t.getter){
          v = viewDV[t.getter](offset, state.littleEndian);
          offset += t.size;
        }else{/*String*/
          n = viewDV.getInt32(offset, state.littleEndian);
          offset += 4;
          v = textDecoder.decode(viewU8.slice(offset, offset+n));
          offset += n;
        }
        rc.push(v);
      }
    }
    if(clear) viewU8[0] = 0;
    //log("deserialize:",argc, rc);
//#if vfs.metrics.enable
    metrics.s11n.deserialize.time += performance.now() - t;
//#/if
    return rc;
  };

  /**
     Serializes all arguments to the shared buffer for consumption
     by the counterpart thread.

     This routine is only intended for serializing OPFS VFS
     arguments and (in at least one special case) result values,
     and the buffer is sized to be able to comfortably handle
     those.

     If passed no arguments then it zeroes out the serialization
     state.
  */
  state.s11n.serialize = function(...args){
    const t = performance.now();
//#if vfs.metrics.enable
    ++metrics.s11n.serialize.count;
//#/if
    if(args.length){
      //log("serialize():",args);
      const typeIds = [];
      let i = 0, offset = 1;
      viewU8[0] = args.length & 0xff /* header = # of args */;
      for(; i < args.length; ++i, ++offset){
        /* Write the TypeIds.id value into the next args.length
           bytes. */
        typeIds.push(getTypeId(args[i]));
        viewU8[offset] = typeIds[i].id;
      }
      for(i = 0; i < args.length; ++i) {
        /* Deserialize the following bytes based on their
           corresponding TypeIds.id from the header. */
        const t = typeIds[i];
        if(t.setter){
          viewDV[t.setter](offset, args[i], state.littleEndian);
          offset += t.size;
        }else{/*String*/
          const s = textEncoder.encode(args[i]);
          viewDV.setInt32(offset, s.byteLength, state.littleEndian);
          offset += 4;
          viewU8.set(s, offset);
          offset += s.byteLength;
        }
      }
      //log("serialize() result:",viewU8.slice(0,offset));
    }else{
      viewU8[0] = 0;
    }
//#if vfs.metrics.enable
    metrics.s11n.serialize.time += performance.now() - t;
//#/if
  };

//#if defined opfs-async-proxy
  state.s11n.storeException = state.asyncS11nExceptions
    ? ((priority,e)=>{
      if(priority<=state.asyncS11nExceptions){
        state.s11n.serialize([e.name,': ',e.message].join(""));
      }
    })
    : ()=>{};
//#/if

  return state.s11n;
//#undef vfs.metrics.enable
}/*initS11n()*/;
