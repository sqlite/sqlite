/*
** 2024-09-23
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This app's single purpose is to emit parts of the Makefile code for
** sqlite3's canonical WASM build. The main motivation is to generate
** code which "could" be created via GNU Make's eval command but is
** highly illegible when constructed that way. Attempts to write this
** app in Bash and TCL have suffered from the problem that those
** languages require escaping $ symbols, making the resulting script
** code as illegible as the eval spaghetti we want to get away
** from. Maintaining it in C is, somewhat surprisingly, _slightly_
** less illegible than writing it in bash, tcl, or native Make code.
**
** The emitted makefile code is not standalone - it depends on
** variables and $(call)able functions from the main makefile.
*/

#undef NDEBUG
#define DEBUG 1
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define pf printf
#define ps puts

/*
** Valid build names. Each build is a combination of one of these and
** one of JS_BUILD_MODES, but only certain combinations are legal.
** This macro and JS_BUILD_MODES exist solely for documentation
** purposes: they are not expanded into code anywhere.
*/
#define JS_BUILD_NAMES sqlite3 sqlite3-wasmfs
/*
** Valid build modes. For the "sqlite3-wasmfs" build, only "esm" (ES6
** Module) is legal.
*/
#define JS_BUILD_MODES \
  vanilla vanilla64 esm esm64 bundler-friendly bundler-friendly64 node

/* Separator to help eyeballs find the different output sections */
static const char * zBanner =
  "\n########################################################################\n";

/*
** Flags for use with BuildDef::flags and the 3rd argument to
** mk_pre_post().
**
** Maintenance reminder: do not combine flags within this enum,
** e.g. LIBMODE_BUNDLER_FRIENDLY=0x02|LIBMODE_ESM, as that will lead
** to breakage in some of the flag checks.
*/
enum LibModeFlags {
  /* Indicates an ESM module build. */
  LIBMODE_ESM = 0x01,
  /* Indicates a "bundler-friendly" build mode. */
  LIBMODE_BUNDLER_FRIENDLY = 0x02,
  /* Indicates that this build is unsupported. Such builds are not
  ** added to the 'all' target. The unsupported builds exist primarily
  ** for experimentation's sake. */
  LIBMODE_UNSUPPORTED = 0x04,
  /* Elide this build from the 'all' target. */
  LIBMODE_NOT_IN_ALL = 0x08,
  LIBMODE_64BIT = 0x10,
  /* Indicates a node.js-for-node.js build (untested and
  ** unsupported). */
  LIBMODE_NODEJS = 0x20,
  /* Indicates a wasmfs build (untested and unsupported). */
  LIBMODE_WASMFS = 0x40
};

/*
** Info needed for building one combination of JS_BUILD_NAMES and
** JS_BUILD_MODE, noting that only a subset of those combinations are
** legal/sensical.
*/
struct BuildDef {
  const char *zName;      /* Name from JS_BUILD_NAMES */
  const char *zMode;      /* Name from JS_BUILD_MODES */
  int flags;              /* Flags from LibModeFlags */
  const char *zJsOut;     /* Name of generated sqlite3.js/.mjs */
  /* TODO?: dynamically determine zJsOut based on zName, zMode, and
     flags. */
  const char *zWasmOut;   /* zJsOut w/ .wasm extension if it needs to
                             be renamed.  Do we still need this? */
  const char *zCmppD;     /* Extra -D... flags for c-pp */
  const char *zEmcc;      /* Extra flags for emcc */
};
typedef struct BuildDef BuildDef;

#if !defined(WASM_CUSTOM_INSTANTIATE)
#  define WASM_CUSTOM_INSTANTIATE 0
#elif (WASM_CUSTOM_INSTANTIATE+0)==0
#  undef WASM_CUSTOM_INSTANTIATE
#  define WASM_CUSTOM_INSTANTIATE 0
#endif

#if WASM_CUSTOM_INSTANTIATE
#define C_PP_D_CUSTOM_INSTANTIATE " -Dcustom-Module.instantiateWasm "
#else
#define C_PP_D_CUSTOM_INSTANTIATE
#endif

/* List of distinct library builds. See next comment block. */
#define BuildDefs_map(E) \
  E(canonical)   \
  E(canonical64) \
  E(esm) \
  E(esm64) \
  E(bundler) \
  E(bundler64) \
  E(node) \
  E(wasmfs)

/*
** The set of WASM builds for the library (as opposed to the apps
** (fiddle, speedtest1)). Their order in BuildDefs_map is mostly
** insignificant, but some makefile vars used by some builds are set
** up by prior builds. Because of that, the (sqlite3, vanilla),
** (sqlite3, esm), and (sqlite3, bundler-friendly) builds should be
** defined first (in that order).
*/
struct BuildDefs {
#define E(N) BuildDef N;
  BuildDefs_map(E)
#undef E
};
typedef struct BuildDefs BuildDefs;

const BuildDefs oBuildDefs = {
  .canonical = {
    .zName ="sqlite3", .zMode = "vanilla",
    .flags = 0,
    .zJsOut ="$(sqlite3.js)",
    .zWasmOut = 0,
    .zCmppD = 0,
    .zEmcc =0
  },

  .canonical64 = {
    .zName="sqlite3", .zMode= "vanilla64",
    .flags= LIBMODE_NOT_IN_ALL | LIBMODE_64BIT,
    .zJsOut="$(sqlite3-64bit.js)",
    .zWasmOut=0,
    .zCmppD=0,
    .zEmcc="-sMEMORY64=1"
  },

  .esm = {
    .zName="sqlite3", .zMode= "esm",
    .flags= LIBMODE_ESM,
    .zJsOut="$(sqlite3.mjs)",
    .zWasmOut=0,
    .zCmppD= "-Dtarget=es6-module",
    .zEmcc=0
  },

  .esm64 = {
    .zName="sqlite3", .zMode= "esm64",
    .flags= LIBMODE_NOT_IN_ALL | LIBMODE_64BIT,
    .zJsOut="$(sqlite3-64bit.mjs)",
    .zWasmOut=0,
    .zCmppD=0,
    .zEmcc="-sMEMORY64=1"
  },

  .bundler = {
    /* Core bundler-friendly build. Untested and "not really"
    ** supported, but required by the downstream npm subproject.
    ** Testing these would require special-purpose node-based tools and
    ** custom test apps. Or we can pass them off as-is to the npm
    ** subproject and they spot failures pretty quickly ;). */
    .zName="sqlite3", .zMode= "bundler-friendly",
    .flags=LIBMODE_BUNDLER_FRIENDLY | LIBMODE_ESM,
    .zJsOut="$(dir.dout)/sqlite3-bundler-friendly.mjs",
    .zWasmOut=0,
    .zCmppD="$(c-pp.D.sqlite3-esm) -Dtarget=es6-bundler-friendly",
    .zEmcc=0
  },

  .bundler64 = {
    .zName="sqlite3", .zMode= "bundler-friendly64",
    .flags= LIBMODE_BUNDLER_FRIENDLY | LIBMODE_ESM,
    .zJsOut="$(dir.dout)/sqlite3-bundler-friendly-64bit.mjs",
    .zWasmOut=0,
    .zCmppD="$(c-pp.D.sqlite3-esm) -Dtarget=es6-bundler-friendly",
    .zEmcc="-sMEMORY64=1"
  },

  /* Entirely unsupported. */
  .node = {
    .zName="sqlite3", .zMode= "node",
    .flags= LIBMODE_UNSUPPORTED | LIBMODE_NODEJS,
    .zJsOut="$(dir.dout)/sqlite3-node.mjs",
    .zWasmOut="sqlite3-node.wasm",
    .zCmppD="$(c-pp.D.sqlite3-bundler-friendly) -Dtarget=node",
    .zEmcc=0
  },

  /* Entirely unsupported. */
  .wasmfs = {
    .zName="sqlite3-wasmfs", .zMode= "esm" ,
    .flags= LIBMODE_UNSUPPORTED | LIBMODE_WASMFS | LIBMODE_ESM,
    .zJsOut="$(dir.wasmfs)/sqlite3-wasmfs.mjs",
    .zWasmOut="sqlite3-wasmfs.wasm",
    .zCmppD="$(c-pp.D.sqlite3-bundler-friendly) -Dwasmfs",
    .zEmcc="-sEXPORT_ES6 -sUSE_ES6_IMPORT_META"
  }
};

/*
** Emits common vars needed by the rest of the emitted code (but not
** needed by makefile code outside of these generated pieces).
*/
static void mk_prologue(void){
  /* A 0-terminated list of makefile vars which we expect to have been
  ** set up by this point in the build process. */
  char const * aRequiredVars[] = {
    "dir.top",
    "dir.api", "dir.dout", "dir.tmp",
    "sqlite3-license-version.js",
    "MAKEFILE", "MAKEFILE_LIST",
    /* Fiddle... */
    "dir.fiddle", "dir.fiddle-debug",
    "MAKEFILE.fiddle",
    "EXPORTED_FUNCTIONS.fiddle",
    /* Some core JS files... */
    "sqlite3.js", "sqlite3.mjs",
    "sqlite3-64bit.js", "sqlite3-64bit.mjs",
    /*"just-testing",*/
    0
  };
  char const * zVar;
  int i;
  pf("%s# Build setup sanity checks...\n", zBanner);
  for( i = 0; (zVar = aRequiredVars[i]); ++i ){
    pf("ifeq (,$(%s))\n", zVar);
    pf("  $(error build process error: expecting make var $$(%s) to "
       "have been set up by now)\n", zVar);
    ps("endif");
  }
  pf("%s", zBanner);
  ps("# extern-post-js* and extern-pre-js* are files for use with");
  ps("# Emscripten's --extern-pre-js and --extern-post-js flags.");
  ps("extern-pre-js.js = $(dir.api)/extern-pre-js.js");
  ps("extern-post-js.js.in = $(dir.api)/extern-post-js.c-pp.js");
  ps("# Emscripten flags for --[extern-][pre|post]-js=... for the");
  ps("# various builds.");
  ps("pre-post-common.flags = --extern-pre-js=$(sqlite3-license-version.js)");
  ps("# pre-post-jses.deps.* = a list of dependencies for the\n"
     "# --[extern-][pre/post]-js files.");
  ps("pre-post-jses.deps.common = $(extern-pre-js.js) $(sqlite3-license-version.js)");

  {
    /* SQLITE.CALL.WASM-OPT = shell code to run $(1) (source wasm file
    ** name) through $(bin.wasm-opt) */
    const char * zOptFlags =
      /*
      ** Flags for wasm-opt. It has many, many, MANY "passes" options
      ** and the ones which appear here were selected solely on the
      ** basis of trial and error.
      **
      ** All wasm file size savings/costs mentioned below are based on
      ** the vanilla build of sqlite3.wasm with -Oz (our shipping
      ** configuration). Comments like "saves nothing" may not be
      ** technically correct: "nothing" means "some neglible amount."
      **
      ** Note that performance gains/losses are _not_ taken into
      ** account here: only wasm file size.
      */
      "--enable-bulk-memory-opt " /* required */
      "--all-features "           /* required */
      "--post-emscripten "        /* Saves roughly 12kb */
      "--strip-debug "            /* We already wasm-strip, but in
                                  ** case this environment has no
                                  ** wasm-strip... */
      /*
      ** The rest are trial-and-error. See wasm-opt --help and search
      ** for "Optimization passes" to find the full list.
      **
      ** With many flags this gets unusuably slow.
      */
      /*"--converge " saves nothing for the options we're using */
      /*"--dce " saves nothing */
      /*"--directize " saves nothing */
      /*"--gsi " no: requires --closed-world flag, which does not
      ** sound like something we want. */
      /*"--gufa --gufa-cast-all --gufa-optimizing " costs roughly 2kb */
      /*"--heap-store-optimization " saves nothing */
      /*"--heap2local " saves nothing */
      //"--inlining --inlining-optimizing " costs roughly 3kb */
      "--local-cse " /* saves roughly 1kb */
      /*"--once-reduction " saves nothing */
      /*"--remove-memory-init " presumably a performance tweak */
      /*"--remove-unused-names " saves nothing */
      /*"--safe-heap "*/
      /*"--vacuum " saves nothing */
      ;
    ps("ifeq (,$(bin.wasm-opt))");
    ps("define SQLITE.CALL.WASM-OPT");
    ps("echo 'wasm-opt not available for $(1)'");
    ps("endef");
    ps("else");
    ps("define SQLITE.CALL.WASM-OPT");
    pf("echo -n 'Before wasm-opt:'; ls -l $(1);\\\n"
       "\trm -f wasm-opt-tmp.wasm;\\\n"
       /* It's very likely that the set of wasm-opt flags varies from
       ** version to version, so we'll ignore any errors here. */
       "\tif $(bin.wasm-opt) $(1) -o wasm-opt-tmp.wasm \\\n"
       "\t\t%s; then \\\n"
       "\t\tmv wasm-opt-tmp.wasm $(1); \\\n"
       "\t\techo -n 'After wasm-opt: '; \\\n"
       "\t\tls -l $(1); \\\n"
       "\telse \\\n"
       "\t\techo 'WARNING: ignoring wasm-opt failure for $(1)'; \\\n"
       "\tfi\n",
       zOptFlags
    );
    ps("endef");
    ps("endif");
  }
}

/*
** Emits makefile code for setting up values for the --pre-js=FILE,
** --post-js=FILE, and --extern-post-js=FILE emcc flags, as well as
** populating those files.
*/
static void mk_pre_post(const char *zName  /* build name */,
                        const char *zMode  /* build mode */,
                        const char *zCmppD /* optional -D flags for c-pp for the
                                           ** --pre/--post-js files. */,
                        const char *zWasmOut){
/* Very common printf() args combo. */
#define zNM zName, zMode

  pf("%s# Begin --pre/--post flags for %s-%s\n", zBanner, zNM);
  if( zCmppD && *zCmppD ){
    pf("c-pp.D.%s-%s = %s\n", zNM, zCmppD ? zCmppD : "");
  }
  pf("pre-post-%s-%s.flags ?=\n", zNM);

  /* --pre-js=... */
  pf("pre-js.js.%s-%s = $(dir.tmp)/pre-js.%s-%s.js\n",
     zNM, zNM);
  pf("$(pre-js.js.%s-%s): $(MAKEFILE_LIST) "
     "$(sqlite3-license-version.js)\n", zNM);
  if( 0==WASM_CUSTOM_INSTANTIATE || 0==zWasmOut ){
    pf("$(eval $(call SQLITE.CALL.C-PP.FILTER,$(pre-js.js.in),"
       "$(pre-js.js.%s-%s),"
       C_PP_D_CUSTOM_INSTANTIATE "$(c-pp.D.%s-%s)))\n", zNM, zNM);
  }else{
    /* This part is needed for builds which have to rename the wasm file
       in zJsOut so that the loader can find it. */
    pf("pre-js.js.%s-%s.intermediary = "
       "$(dir.tmp)/pre-js.%s-%s.intermediary.js\n",
       zNM, zNM);
    pf("$(eval $(call SQLITE.CALL.C-PP.FILTER,$(pre-js.js.in),"
       "$(pre-js.js.%s-%s.intermediary),"
       C_PP_D_CUSTOM_INSTANTIATE "$(c-pp.D.%s-%s)))\n", zNM, zNM);
    pf("$(pre-js.js.%s-%s): $(pre-js.js.%s-%s.intermediary)\n", zNM, zNM);
    pf("\tcp $(pre-js.js.%s-%s.intermediary) $@\n", zNM);
    pf("\t@echo 'sIMS.wasmFilename = \"%s\";' >> $@\n", zWasmOut)
      /* see api/pre-js.c-pp.js:Module.instantiateModule() */;
  }

  /* --post-js=... */
  pf("post-js.js.%s-%s = $(dir.tmp)/post-js.%s-%s.js\n", zNM, zNM);
  pf("$(eval $(call SQLITE.CALL.C-PP.FILTER,$(post-js.js.in),"
     "$(post-js.js.%s-%s),$(c-pp.D.%s-%s)))\n", zNM, zNM);

  /* --extern-post-js=... */
  pf("extern-post-js.js.%s-%s = $(dir.tmp)/extern-post-js.%s-%s.js\n",
     zNM, zNM);
  pf("$(eval $(call SQLITE.CALL.C-PP.FILTER,$(extern-post-js.js.in),"
     "$(extern-post-js.js.%s-%s),"
     C_PP_D_CUSTOM_INSTANTIATE "$(c-pp.D.%s-%s)))\n", zNM, zNM);

  /* Combined flags for use with emcc... */
  pf("pre-post-common.flags.%s-%s = "
     "$(pre-post-common.flags) "
     "--post-js=$(post-js.js.%s-%s) "
     "--extern-post-js=$(extern-post-js.js.%s-%s)\n", zNM, zNM, zNM);

  pf("pre-post-%s-%s.flags += $(pre-post-common.flags.%s-%s) "
     "--pre-js=$(pre-js.js.%s-%s)\n", zNM, zNM, zNM);

  /* Set up deps... */
  pf("pre-post-jses.%s-%s.deps = $(pre-post-jses.deps.common) "
     "$(post-js.js.%s-%s) $(extern-post-js.js.%s-%s)\n",
     zNM, zNM, zNM);
  pf("pre-post-%s-%s.deps = $(pre-post-jses.%s-%s.deps) "
     "$(dir.tmp)/pre-js.%s-%s.js\n",
     zNM, zNM, zNM);
  pf("# End --pre/--post flags for %s-%s%s", zNM, zBanner);
#undef zNM
}

/*
** Emits rules for the fiddle builds.
*/
static void mk_fiddle(void){
  int i = 0;

  mk_pre_post("fiddle-module","vanilla", 0, "fiddle-module.wasm");
  for( ; i < 2; ++i ){
    /* 0==normal, 1==debug */
    const char *zTail = i ? ".debug" : "";
    const char *zDir = i ? "$(dir.fiddle-debug)" : "$(dir.fiddle)";

    pf("%s# Begin fiddle%s\n", zBanner, zTail);
    pf("fiddle-module.js%s = %s/fiddle-module.js\n", zTail, zDir);
    pf("$(fiddle-module.js%s):%s $(MAKEFILE_LIST) $(MAKEFILE.fiddle) "
       "$(EXPORTED_FUNCTIONS.fiddle) "
       "$(fiddle.cses) $(pre-post-fiddle-module-vanilla.deps) "
       "$(SOAP.js)\n",
       zTail, (i ? " $(fiddle-module.js)" : ""));
    if( 1==i ){/*fiddle.debug*/
      pf("\t@test -d \"$(dir $@)\" || mkdir -p \"$(dir $@)\"\n");
    }
    pf("\t$(bin.emcc) -o $@ $(fiddle.emcc-flags%s) "
       "$(pre-post-fiddle-module-vanilla.flags) $(fiddle.cses)\n",
       zTail);
    ps("\t@chmod -x $(basename $@).wasm");
    ps("\t@$(maybe-wasm-strip) $(basename $@).wasm");
    ps("\t@$(SQLITE.strip-createExportWrapper)");
    pf("\t@cp -p $(SOAP.js) $(dir $@)\n");
    if( 1==i ){/*fiddle.debug*/
      pf("\tcp -p $(dir.fiddle)/index.html "
         "$(dir.fiddle)/fiddle.js "
         "$(dir.fiddle)/fiddle-worker.js "
         "$(dir $@)\n");
    }
    /* Compress fiddle files. We handle each file separately, rather
       than compressing them in a loop in the previous target, to help
       avoid that hand-edited files, like fiddle-worker.js, do not end
       up with stale .gz files (which althttpd will then serve instead
       of the up-to-date uncompressed one). */
    pf("%s/fiddle-module.js.gz: %s/fiddle-module.js\n", zDir, zDir);
    ps("\tgzip < $< > $@");
    pf("%s/fiddle-module.wasm.gz: %s/fiddle-module.wasm\n", zDir, zDir);
    ps("\tgzip < $< > $@");
    pf("fiddle%s: %s/fiddle-module.js.gz %s/fiddle-module.wasm.gz\n",
       i ? "-debug" : "", zDir, zDir);
    if( 0==i ){
      ps("fiddle: $(fiddle-module.js)");
    }else{
      ps("fiddle-debug: $(fiddle-module.js.debug)");
    }
    pf("# End fiddle%s%s", zTail, zBanner);
  }
}

/*
** Emits makefile code for one build of the library.
*/
static void mk_lib_mode(const BuildDef * pB){
  const char * zWasmOut = "$(basename $@).wasm"
    /* The various targets named X.js or X.mjs (pB->zJsOut) also generate
    ** X.wasm, and we need that part of the name to perform some
    ** post-processing after Emscripten generates X.wasm. */;
  assert( pB->zName );
  assert( pB->zMode );
  assert( pB->zJsOut );
/* Very common printf() args combo. */
#define zNM pB->zName, pB->zMode

  pf("%s# Begin build [%s-%s]. flags=0x%02x\n", zBanner, zNM, pB->flags);
  pf("# zJsOut=%s\n# zCmppD=%s\n# zWasmOut=%s\n", pB->zJsOut,
     pB->zCmppD ? pB->zCmppD : "<none>",
     pB->zWasmOut ? pB->zWasmOut : "");
  pf("$(info Setting up build [%s-%s]: %s)\n", zNM, pB->zJsOut);
  mk_pre_post(zNM, pB->zCmppD, pB->zWasmOut);
  pf("\nemcc.flags.%s.%s ?=\n", zNM);
  if( pB->zEmcc && pB->zEmcc[0] ){
    pf("emcc.flags.%s.%s += %s\n", zNM, pB->zEmcc);
  }

  /* target pB->zJsOut */
  pf("%s: $(MAKEFILE_LIST) $(sqlite3-wasm.cfiles) $(EXPORTED_FUNCTIONS.api) "
     "$(bin.mkwb) "
     "$(pre-post-%s-%s.deps) "
     "$(sqlite3-api.ext.jses)"
     /* ^^^ maintenance reminder: we set these as deps so that they
     ** get copied into place early. That allows the developer to
     ** reload the base-most test pages while the later-stage builds
     ** are still compiling, which is especially helpful when running
     ** builds with long build times (like -Oz). */
     "\n",
     pB->zJsOut, zNM);
  pf("\t@echo \"Building $@ ...\"\n");
  if( LIBMODE_UNSUPPORTED & pB->flags ){
    ps("\t@echo 'ACHTUNG: $@ is an unsupported build. "
       "Use at your own risk.'");
  }
  pf("\t$(bin.emcc) -o $@ $(emcc_opt_full) $(emcc.flags) \\\n");
  pf("\t\t$(emcc.jsflags) -sENVIRONMENT=$(emcc.environment.%s) \\\n",
     pB->zMode);
  pf("\t\t$(pre-post-%s-%s.flags) \\\n", zNM);
  pf("\t\t$(emcc.flags.%s) $(emcc.flags.%s.%s) \\\n", pB->zName, zNM);
  pf("\t\t$(cflags.common) $(SQLITE_OPT) \\\n"
     "\t\t$(cflags.%s) $(cflags.%s.%s) \\\n"
     "\t\t$(cflags.wasm_extra_init) $(sqlite3-wasm.cfiles)\n", pB->zName, zNM);
  if( (LIBMODE_ESM & pB->flags) || (LIBMODE_NODEJS & pB->flags) ){
    /* TODO? Replace this $(call) with the corresponding makefile
    ** code.  OTOH, we also use this $(call) in the speedtest1-wasmfs
    ** build, which is not part of the rules emitted by this
    ** program. */
    pf("\t@$(call SQLITE.CALL.xJS.ESM-EXPORT-DEFAULT,1,%d)\n",
       (LIBMODE_WASMFS & pB->flags) ? 1 : 0);
  }
  pf("\t@chmod -x %s\n", zWasmOut);
  pf("\t@$(maybe-wasm-strip) %s\n", zWasmOut);
  pf("\t@$(call SQLITE.CALL.WASM-OPT,%s)\n", zWasmOut);
  ps("\t@$(SQLITE.strip-createExportWrapper)");
  /*
  ** The above $(bin.emcc) call will write pB->zJsOut, a.k.a. $@, and
  ** will create a like-named .wasm file (pB->zWasmOut). That .wasm
  ** file name gets hard-coded into $@ so we need to, for some cases,
  ** patch zJsOut to use the name sqlite3.wasm instead.  The resulting
  ** .wasm file is identical for all builds for which pB->zEmcc is
  ** empty.
  */
  if( (LIBMODE_BUNDLER_FRIENDLY & pB->flags) ){
    pf("\t@echo 'Patching $@ for %s.wasm...'; \\\n", pB->zName);
    pf("\t\trm -f %s; \\\n", zWasmOut);
    pf("\t\tsed -i -e 's/%s-%s.wasm/%s.wasm/g' $@ || exit;\n",
       /* ^^^^^^ reminder: Mac/BSD sed has no -i flag but this
       ** build process explicitly requires a Linux system. */
       zNM, pB->zName);
    pf("\t@ls -la $@\n");
    if( LIBMODE_BUNDLER_FRIENDLY & pB->flags ){
      /* Avoid a 3rd occurrence of the bug fixed by 65798c09a00662a3,
      ** which was (in two cases) caused by makefile refactoring and
      ** not recognized until after a release was made with the broken
      ** sqlite3-bundler-friendly.mjs (which is used by the npm
      ** subproject but is otherwise untested/unsupported): */
      pf("\t@if grep -e '^ *importScripts(' $@; "
         "then echo 'ERROR: bug fixed in 65798c09a00662a3 has re-appeared'; "
         "exit 1; fi;\n");
    }
  }else{
    pf("\t@ls -la %s $@\n", zWasmOut);
  }

  if( LIBMODE_64BIT & pB->flags ){
    pf("64bit: %s\n", pB->zJsOut);
  }else if( 0==(LIBMODE_NOT_IN_ALL & pB->flags)
            && 0==(LIBMODE_UNSUPPORTED & pB->flags) ){
    pf("all: %s\n", pB->zJsOut);
  }
  pf("# End build [%s-%s]%s", zNM, zBanner);
#undef zNM
}

int main(void){
  int rc = 0;
  const BuildDef *pB;
  pf("# What follows was GENERATED by %s. Edit at your own risk.\n", __FILE__);
  mk_prologue();
#define E(N) mk_lib_mode(&oBuildDefs.N);
  BuildDefs_map(E)
#undef E
  mk_fiddle();
  mk_pre_post("speedtest1","vanilla", 0, "speedtest1.wasm");
  mk_pre_post("speedtest1-wasmfs", "esm",
              "$(c-pp.D.sqlite3-bundler-friendly) -Dwasmfs",
              "speetest1-wasmfs.wasm");
  return rc;
}
