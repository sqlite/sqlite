
//#if 0
/* ^^^^ ACHTUNG: blank line at the start is necessary because
   Emscripten will not add a newline in some cases and we need
   a blank line for a sed-based kludge for the ES6 build.

   extern-post-js.js must be appended to the resulting sqlite3.js
   file. It gets its name from being used as the value for the
   --extern-post-js=... Emscripten flag. This code, unlike most of the
   associated JS code, runs outside of the Emscripten-generated module
   init scope, in the current global scope.

   At the time this is run, the sqlite3InitModule function will have
   just been defined via a `var` decl. This file overwrites it with a
   custom one and then (in ESM builds) exports it. Getting ESM to work
   here requires a build-time kludge of stripping the 'export default'
   which Emscripten forcibly injects into its glue.
*/
//#/if
//#if target:es6-module
export default
//#/if
(function(){
  //console.warn("this is extern-post-js");
  /**
     In order to hide the sqlite3InitModule()'s resulting
     Emscripten module from downstream clients (and simplify our
     documentation by being able to elide those details), we hide that
     function and expose a hand-written sqlite3InitModule() to return
     the sqlite3 object (most of the time).
  */
  const originalInit = sqlite3InitModule;
  if(!originalInit){
    throw new Error("Expecting sqlite3InitModule to be defined by the Emscripten build.");
  }
  /**
     sIMS => sqlite3InitModuleState

     We need to add some state which our custom Module.locateFile()
     can see, but an Emscripten limitation currently prevents us from
     attaching it to the sqlite3InitModule function object:

     https://github.com/emscripten-core/emscripten/issues/18071

     The only(?) current workaround is to temporarily stash this state
     into the global scope and delete it when sqlite3InitModule()
     is called.

     This state is consumed by pre-js.c-pp.js. That file lives above
     this one in the built JS module but it's executed after this one.
  */
  const sIMS = Object.assign(Object.create(null),{
    moduleScript: globalThis?.document?.currentScript,
    isWorker: ('undefined' !== typeof WorkerGlobalScope),
    location: globalThis?.location,
    urlParams:  globalThis?.location?.href
      ? new URL(globalThis.location.href).searchParams
      : new URLSearchParams()
    /*
      It is literally impossible to reliably get the name of _this_ script
      at runtime, so impossible to reliably derive X.wasm from script name
      X.js. (This is apparently why Emscripten hard-codes the name of the
      wasm file into their output.)  Thus we need, at build-time, to set
      the name of the WASM file which our custom instantiateWasm() should to
      load. The build process populates this.

      The primary reason we do this is because disparate builds
      usually result in different JS but identical WASM files. To
      avoid, e.g., that we have to ship two separate WASM files for
      vanilla and ESM builds, we rewire all builds which produce
      identical binaries to use a single copy of those binaries.

      Module.instantiateWasm() is found in pre-js.c-pp.js.
    */
//#if defined sqlite3.wasm
//#@ push policy error delimiter "<<<" ">>>"
//#/if
    ,wasmFilename: '<<<sqlite3.wasm>>>' /* replaced by the build process */
//#if defined sqlite3.wasm
//#@ pop policy delimiter
//#/if
  });
//#if not target:es6-bundler-friendly and not target:node
//#// ^^^ mirrored in pre-js.c-pp.js
  globalThis.sqlite3InitModuleState = sIMS;
//#/if
  sIMS.debugModule =
    sIMS.urlParams.has('sqlite3.debugModule')
    ? (...args)=>console.warn('sqlite3.debugModule:',...args)
    : ()=>{};

  /**
     Fetches sIMS.urlParams.get(key). If that looks like it might
     be a remote path value then null is returned and a console
     warning may be emitted. If it looks like it's a local path,
     it is returned.

     [bugs:74f052d3d2]: do not allow URL arguments which are used for
     determining file locations to be anything other than what appears
     to be a local file reference. That is the only documented use for
     these args and allowing URLs as values leads to chaos. This is
     believed to resolve that report but is also considered a stopgap
     measure. Potential TODO is only allow query parameters from the
     same origin as this script.
  */
  const getPathArgument = (key)=>{
    const s = sIMS.urlParams.get(key);
    //console.warn("sIMS.getPathArgument(",key,")",s);
    if( !s ) return s;
    if( s.indexOf(':')>=0 || s.indexOf('//')>=0 ){
      console.warn(
        "Rejecting path string",key," because it does not look",
        "like a local path:",s
      );
      return null;
    }
    return s;
  };
//#if not target:es6-module /* needed for non-ESM in pre-js.c-pp.js */
  sIMS.getPathArgument = getPathArgument;
//#/if
  if( sIMS.urlParams.has('sqlite3.dir') ){
    const s = getPathArgument('sqlite3.dir');
    if( s ){
      sIMS.sqlite3Dir = s + (s[s.length-1]==='/' ? '' : '/');
    }
  }else if( sIMS.moduleScript?.src ){
    const li = sIMS.moduleScript.src.split('/');
    li.pop();
    sIMS.sqlite3Dir = li.join('/') + '/';
  }

  /** The replacement for Emscripten-generated sqlite3InitModule(). */
  const sIM = sqlite3InitModule = function ff(...args){
    //console.warn("Using replaced sqlite3InitModule()",globalThis.location);
    /**
       Conventionally, the first argument to an Emscripten-generated
       module init function is a client-provided Object which
       overrides the Module object. Because we cannot offer any sort
       of API stability for that, we have historically not processed
       or accepted any arguments. As a "back door" we offer selective
       overrides of some options, as handled below via args[0].

       See pre-js.c-pp.js [tag:locateFile] for the details and a link
       to the relevant discussion.
    */
    sIMS.emscriptenLocateFile = args[0]?.locateFile;
    sIMS.emscriptenInstantiateWasm = args[0]?.instantiateWasm;
    return originalInit(...args).then((EmscriptenModule)=>{
      sIMS.debugModule("sqlite3InitModule() sIMS =",sIMS);
      sIMS.debugModule("sqlite3InitModule() EmscriptenModule =",EmscriptenModule);
      const s = EmscriptenModule.runSQLite3PostLoadInit(
        sIMS,
        EmscriptenModule /* see post-js-header/footer.js */,
        !!ff.__isUnderTest
      );
      sIMS.debugModule("sqlite3InitModule() sqlite3 =",s);
//#if wasmfs
      if('undefined'!==typeof WorkerGlobalScope &&
         EmscriptenModule['ENVIRONMENT_IS_PTHREAD']){
        /** Workaround for wasmfs-generated worker, which calls this
            routine from each individual thread and requires that its
            argument be returned. The if() condition above is fragile,
            based solely on inspection of the offending code, not
            public Emscripten details. */
        //console.warn("sqlite3InitModule() returning E-module.",EmscriptenModule);
        return EmscriptenModule;
      }
//#/if
      return s;
    }).catch((e)=>{
      console.error("Exception loading sqlite3 module:",e);
      throw e;
    });
  };
  if( sIMS.moduleScript?.src ){
    /* Record this script's directory part */
    const src = sIMS.moduleScript.src.split('/');
    src.pop();
    sIMS.scriptDir = src.join('/') + '/';
  }
  sIMS.debugModule('extern-post-js.c-pp.js sqlite3InitModuleState =',sIMS);
//#if not target:es6-module
//#if 0
  /* Emscripten does not inject these module-loader bits in ES6 module
     builds and including them here breaks JS bundlers, so elide them
     from ESM builds. */
//#/if
  /* Replace the various module exports performed by the Emscripten
     glue so that they link to our overwritten copy... */
  if( typeof exports === 'object' && typeof module === 'object' ){
    module.exports = sIM;
    module.exports.default = sIM /* allows TS to import this commonjs style */;
  }else if( 'function'===typeof define && define['amd'] ){
    define([], ()=>sIM);
  }else if( typeof exports === 'object' ){
    /* 2026-10-30: Emscripten no longer emits this one, so we can probably
       remove it. */
    exports["sqlite3InitModule"] = sIM;
  }
//#/if // !target:es6-module
  return sIM;
})();
