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

/* Separator to help eyeballs find the different output sections */
#define zBanner \
  "\n########################################################################\n"

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
  /**
     Base name of output JS and WASM files.
  */
  const char *zWasmFile;
  const char *zJsOut;     /* Name of generated sqlite3.js/.mjs */
  const char *zWasmOut;   /* zJsOut w/ .wasm extension if it needs to
                             be renamed.  Do we still need this? */
  const char *zCmppD;     /* Extra -D... flags for c-pp */
  const char *zEmcc;      /* Extra flags for emcc */
  const char *zEnv;       /* emcc -sENVIRONMENT=X flag */
  int flags;              /* Flags from LibModeFlags */
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
  E(canonical) \
  E(esm)
/*
  E(canonical64) \
  E(esm64) \
  E(bundler) \
  E(bundler64) \
  E(node) \
  E(wasmfs)
*/
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
    .zWasmFile ="sqlite3",
    .zJsOut ="$(dir.dout)/sqlite3.js",
    .zWasmOut = 0,
    .zCmppD = "-Djust-testing",
    .zEmcc =0,
    .zEnv = "web,worker"
    /* MUST be non-NULL in the canonical build so it can be used as
       a default for all others. */,
    .flags = 0
  },

  .esm = {
    .zWasmFile="sqlite3",
    .zJsOut="$(sqlite3.mjs)",
    .zWasmOut=0,
    .zCmppD= "-Dtarget=es6-module",
    .zEmcc=0,
    .zEnv = 0,
    .flags= LIBMODE_ESM
  },
#if 0

  .canonical64 = {
    .zWasmFile="sqlite3-64bit",
    .zJsOut="$(sqlite3-64bit.js)",
    .zWasmOut=0,
    .zCmppD=0,
    .zEmcc="-sMEMORY64=1",
    .zEnv = 0,
    .flags= LIBMODE_NOT_IN_ALL | LIBMODE_64BIT
  },

  .esm64 = {
    .zWasmFile="sqlite3",
    .zJsOut="$(sqlite3-64bit.mjs)",
    .zWasmOut=0,
    .zCmppD=0,
    .zEmcc="-sMEMORY64=1",
    .zEnv = 0,
    .flags= LIBMODE_NOT_IN_ALL | LIBMODE_64BIT
  },

  .bundler = {
    /* Core bundler-friendly build. Untested and "not really"
    ** supported, but required by the downstream npm subproject.
    ** Testing these would require special-purpose node-based tools and
    ** custom test apps. Or we can pass them off as-is to the npm
    ** subproject and they spot failures pretty quickly ;). */
    .zWasmFile="sqlite3",
    .zJsOut="$(dir.dout)/sqlite3-bundler-friendly.mjs",
    .zWasmOut=0,
    .zCmppD="$(c-pp.D.sqlite3-esm) -Dtarget=es6-bundler-friendly",
    .zEmcc=0,
    .zEnv = 0,
    .flags=LIBMODE_BUNDLER_FRIENDLY | LIBMODE_ESM
  },

  .bundler64 = {
    .zWasmFile="sqlite3",
    .zJsOut="$(dir.dout)/sqlite3-bundler-friendly-64bit.mjs",
    .zWasmOut=0,
    .zCmppD="$(c-pp.D.sqlite3-esm) -Dtarget=es6-bundler-friendly",
    .zEmcc="-sMEMORY64=1",
    .zEnv = 0,
    .flags= LIBMODE_BUNDLER_FRIENDLY | LIBMODE_ESM
  },

  /* Entirely unsupported. */
  .node = {
    .zWasmFile="sqlite3",
    .zJsOut="$(dir.dout)/sqlite3-node.mjs",
    .zWasmOut="sqlite3-node.wasm",
    .zCmppD="$(c-pp.D.sqlite3-bundler-friendly) -Dtarget=node",
    .zEmcc=0,
    .zEnv = "node"
    /* Adding ",node" to the list for the other -sENVIRONMENT values
       causes Emscripten to generate code which confuses node: it
       cannot reliably determine whether the build is for a browser or
       for node. We neither build nor test node builds on a regular
       basis. They are fully unsupported. */,
    .flags= LIBMODE_UNSUPPORTED | LIBMODE_NODEJS
  },

  /* Entirely unsupported. */
  .wasmfs = {
    .zWasmFile="sqlite3-wasmfs",
    .zJsOut="$(dir.wasmfs)/sqlite3-wasmfs.mjs",
    .zWasmOut="sqlite3-wasmfs.wasm",
    .zCmppD="$(c-pp.D.sqlite3-bundler-friendly) -Dwasmfs",
    .zEmcc="-sEXPORT_ES6 -sUSE_ES6_IMPORT_META",
    .zEnv = 0,
    .flags= LIBMODE_UNSUPPORTED | LIBMODE_WASMFS | LIBMODE_ESM
  }
#endif
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
  ps(zBanner "# Build setup sanity checks...");
  for( i = 0; (zVar = aRequiredVars[i]); ++i ){
    pf("ifeq (,$(%s))\n", zVar);
    pf("  $(error build process error: expecting make var $$(%s) to "
       "have been set up by now)\n", zVar);
    ps("endif");
  }

  ps(zBanner
     "# Inputs for the sqlite3-api.js family.\n"
     "#\n"
     "# sqlite3-license-version.js = generated JS file with the license\n"
     "# header and version info.\n"
     "sqlite3-license-version.js = $(dir.tmp)/sqlite3-license-version.js\n"
     "# $(sqlite3-api-build-version.js) = generated JS file which populates the\n"
     "# sqlite3.version object using $(bin.version-info).\n"
     "sqlite3-api-build-version.js = $(dir.tmp)/sqlite3-api-build-version.js\n"
     "# sqlite3-api.jses = the list of JS files which make up\n"
     "# $(sqlite3-api.js.in), in the order they need to be assembled.\n"
     "sqlite3-api.jses = $(sqlite3-license-version.js)\n"
     "# sqlite3-api-prologue.js: initial bootstrapping bits:\n"
     "sqlite3-api.jses += $(dir.api)/sqlite3-api-prologue.js\n"
     "# whwhasm.js and jaccwabyt.js: Low-level utils, mostly replacing\n"
     "# Emscripten glue:\n"
     "sqlite3-api.jses += $(dir.common)/whwasmutil.js\n"
     "sqlite3-api.jses += $(dir.jacc)/jaccwabyt.js\n"
     "# sqlite3-api-glue Glues the previous part together with sqlite:\n"
     "sqlite3-api.jses += $(dir.api)/sqlite3-api-glue.c-pp.js\n"
     "sqlite3-api.jses += $(sqlite3-api-build-version.js)\n"
     "# sqlite3-api-oo1 = the oo1 API:\n"
     "sqlite3-api.jses += $(dir.api)/sqlite3-api-oo1.c-pp.js\n"
     "# sqlite3-api-worker = the Worker1 API:\n"
     "sqlite3-api.jses += $(dir.api)/sqlite3-api-worker1.c-pp.js\n"
     "# sqlite3-vfs-helper = helper APIs for VFSes:\n"
     "sqlite3-api.jses += $(dir.api)/sqlite3-vfs-helper.c-pp.js\n"
     "ifeq (0,$(wasm-bare-bones))\n"
     "  # sqlite3-vtab-helper = helper APIs for VTABLEs:\n"
     "  sqlite3-api.jses += $(dir.api)/sqlite3-vtab-helper.c-pp.js\n"
     "endif\n"
     "# sqlite3-vfs-opfs = the first OPFS VFS:\n"
     "sqlite3-api.jses += $(dir.api)/sqlite3-vfs-opfs.c-pp.js\n"
     "# sqlite3-vfs-opfs-sahpool = the second OPFS VFS:\n"
     "sqlite3-api.jses += $(dir.api)/sqlite3-vfs-opfs-sahpool.c-pp.js\n"
     "# sqlite3-api-cleanup.js = \"finalizes\" the build and cleans up\n"
     "# any extraneous global symbols which are needed temporarily\n"
     "# by the previous files.\n"
     "sqlite3-api.jses += $(dir.api)/sqlite3-api-cleanup.js"
  );

  ps(zBanner
     "# $(sqlite3-license-version.js) contains the license header and\n"
     "# in-comment build version info.\n"
     "#\n"
     "# Maintenance reminder: there are awk binaries out there which do not\n"
     "# support -e SCRIPT.\n"
     "$(sqlite3-license-version.js): $(MKDIR.bld) $(sqlite3.h) "
     "$(dir.api)/sqlite3-license-version-header.js $(MAKEFILE)\n"
     "\t@echo 'Making $@...'; { \\\n"
     "\t\tcat $(dir.api)/sqlite3-license-version-header.js;  \\\n"
     "\t\techo '/*'; \\\n"
     "\t\techo '** This code was built from sqlite3 version...'; \\\n"
     "\t\techo '**'; \\\n"
     "\t\tawk '/define SQLITE_VERSION/{$$1=\"\"; print \"**\" $$0}' $(sqlite3.h); \\\n"
     "\t\tawk '/define SQLITE_SOURCE_ID/{$$1=\"\"; print \"**\" $$0}' $(sqlite3.h); \\\n"
     "\t\techo '**'; \\\n"
     "\t\techo '** with the help of Emscripten SDK version $(emcc.version).'; \\\n"
     "\t\techo '*/'; \\\n"
     "\t} > $@"
  );

  ps(zBanner
     "# $(sqlite3-api-build-version.js) injects the build version info into\n"
     "# the bundle in JSON form.\n"
     "$(sqlite3-api-build-version.js): $(MKDIR.bld) $(bin.version-info) $(MAKEFILE)\n"
     "\t@echo 'Making $@...'; { \\\n"
     "\t\techo 'globalThis.sqlite3ApiBootstrap.initializers.push(function(sqlite3){'; \\\n"
     "\t\techo -n '  sqlite3.version = '; \\\n"
     "\t\t$(bin.version-info) --json; \\\n"
     "\t\techo ';'; \\\n"
     "\t\techo '});'; \\\n"
     "\t} > $@"
  );

  ps(zBanner
     "# extern-post-js* and extern-pre-js* are files for use with\n"
     "# Emscripten's --extern-pre-js and --extern-post-js flags.\n"
     "extern-pre-js.js = $(dir.api)/extern-pre-js.js\n"
     "extern-post-js.js.in = $(dir.api)/extern-post-js.c-pp.js\n"
     "# Emscripten flags for --[extern-][pre|post]-js=... for the\n"
     "# various builds.\n"
     "# pre-post-jses.*.deps = lists of dependencies for the\n"
     "# --[extern-][pre/post]-js files.\n"
     "pre-post-jses.common.deps = "
     "$(extern-pre-js.js) $(sqlite3-license-version.js)"
  );

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
    ps(zBanner
       "# post-compilation WASM file optimization");
    ps("ifeq (,$(bin.wasm-opt))");
    {
      ps("define SQLITE.CALL.WASM-OPT");
      ps("echo 'wasm-opt not available for $(1)'");
      ps("endef");
    }
    ps("else");
    {
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
    }
    ps("endif");
  }
}

/*
** Emits makefile code for setting up values for the --pre-js=FILE,
** --post-js=FILE, and --extern-post-js=FILE emcc flags, as well as
** populating those files.
*/
static void mk_pre_post(char const *zBuildName,
                        const char *zCmppD /* optional -D flags for c-pp for the
                                           ** --pre/--post-js files. */,
                        const char *zWasmOut ){
/* Very common printf() args combo. */

  pf("%s# Begin --pre/--post flags for %s\n", zBanner, zBuildName);
  if( zCmppD && *zCmppD ){
    pf("c-pp.D.%s = %s\n", zBuildName, zCmppD);
  }
  pf("pre-post.%s.flags ?=\n", zBuildName);

  /* --pre-js=... */
  pf("pre-js.%s.js = $(dir.tmp)/pre-js.%s.js\n",
     zBuildName, zBuildName);
  pf("$(pre-js.%s.js): $(MAKEFILE_LIST) "
     "$(sqlite3-license-version.js)\n", zBuildName);
  if( 0==WASM_CUSTOM_INSTANTIATE || 0==zWasmOut ){
    pf("$(eval $(call SQLITE.CALL.C-PP.FILTER,$(pre-js.js.in),"
       "$(pre-js.%s.js),"
       C_PP_D_CUSTOM_INSTANTIATE "$(c-pp.D.%s)))\n",
       zBuildName, zBuildName);
  }else{
    /* This part is needed for builds which have to rename the wasm file
       in zJsOut so that the loader can find it. */
    pf("pre-js.%s.js.intermediary = "
       "$(dir.tmp)/pre-js.%s.intermediary.js\n",
       zBuildName, zBuildName);
    pf("$(eval $(call SQLITE.CALL.C-PP.FILTER,$(pre-js.js.in),"
       "$(pre-js.%s.js.intermediary),"
       C_PP_D_CUSTOM_INSTANTIATE "$(c-pp.D.%s)))\n",
       zBuildName, zBuildName);
    pf("$(pre-js.%s.js): $(pre-js.%s.js.intermediary)\n",
       zBuildName, zBuildName);
    pf("\tcp $(pre-js.%s.js.intermediary) $@\n", zBuildName);
    pf("\t@echo 'sIMS.wasmFilename = \"%s\";' >> $@\n", zWasmOut)
      /* see api/pre-js.c-pp.js:Module.instantiateModule() */;
  }

  /* --post-js=... */
  pf("post-js.%s.js = $(dir.tmp)/post-js.%s.js\n",
     zBuildName, zBuildName);
  pf("post-jses.%s = "
     "$(dir.api)/post-js-header.js "
     "$(sqlite3-api.%s.js) "
     "$(dir.api)/post-js-footer.js\n",
     zBuildName, zBuildName);
  pf("$(eval $(call SQLITE.CALL.C-PP.FILTER,$(post-jses.%s),"
     "$(post-js.%s.js),$(c-pp.D.%s)))\n",
     zBuildName, zBuildName, zBuildName);

  /* --extern-post-js=... */
  pf("extern-post-js.%s.js = $(dir.tmp)/extern-post-js.%s.js\n",
     zBuildName, zBuildName);
  pf("$(eval $(call SQLITE.CALL.C-PP.FILTER,$(extern-post-js.js.in),"
     "$(extern-post-js.%s.js),"
     C_PP_D_CUSTOM_INSTANTIATE "$(c-pp.D.%s)))\n",
     zBuildName, zBuildName);

  /* Combined flags for use with emcc... */
  pf("pre-post.%s.flags += "
     "--extern-pre-js=$(sqlite3-license-version.js) "
     "--pre-js=$(pre-js.%s.js) "
     "--post-js=$(post-js.%s.js) "
     "--extern-post-js=$(extern-post-js.%s.js)\n",
     zBuildName, zBuildName, zBuildName, zBuildName);


  /* Set up deps... */
  pf("pre-post.%s.deps = "
     "$(pre-post-jses.common.deps) "
     "$(post-js.%s.js) $(extern-post-js.%s.js) "
     "$(dir.tmp)/pre-js.%s.js\n",
     zBuildName, zBuildName, zBuildName, zBuildName);
  pf("# End --pre/--post flags for %s%s", zBuildName, zBanner);
}

/*
** Emits rules for the fiddle builds.
*/
static void mk_fiddle(void){
  int i = 0;

  mk_pre_post("fiddle-module", 0, "fiddle-module.wasm");
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
void mk_lib_mode(const char *zBuildName, const BuildDef * pB){
  const char * zWasmOut = "$(basename $@).wasm"
    /* The various targets named X.js or X.mjs (pB->zJsOut) also generate
    ** X.wasm, and we need that part of the name to perform some
    ** post-processing after Emscripten generates X.wasm. */;
  const char * zJsExt = (LIBMODE_ESM & pB->flags)
    ? ".mjs" : ".js";
  assert( pB->zWasmFile );
  assert( pB->zJsOut );
/* Very common printf() args combo. */

  pf("%s# Begin build [%s]. flags=0x%02x\n", zBanner, zBuildName, pB->flags);
  pf("# zJsOut=%s\n# zCmppD=%s\n# zWasmOut=%s\n", pB->zJsOut,
     pB->zCmppD ? pB->zCmppD : "<none>",
     pB->zWasmOut ? pB->zWasmOut : "");
  pf("$(info Setting up build [%s]: %s)\n", zBuildName, pB->zJsOut);

  assert( oBuildDefs.canonical.zEnv );
  pf("emcc.environment.%s = %s\n", zBuildName,
     pB->zEnv ? pB->zEnv : oBuildDefs.canonical.zEnv);
  pf("emcc.flags.%s =\n", zBuildName);
  if( pB->zEmcc ){
    pf("emcc.flags.%s += %s\n", zBuildName, pB->zEmcc);
  }

  pf("sqlite3-api.%s.c-pp.js = $(dir.tmp)/sqlite3-api.%s.c-pp%s\n",
     zBuildName, zBuildName, zJsExt);

  pf("sqlite3-api.%s.js = $(dir.tmp)/sqlite3-api.%s%s\n",
     zBuildName, zBuildName, zJsExt);
  if( pB->zCmppD ){
    pf("c-pp.D.%s = %s\n", zBuildName, pB->zCmppD );
  }
  pf("$(sqlite3-api.%s.c-pp.js): $(sqlite3-api.jses)\n"
     "\t@echo 'Making $@ ...'; \\\n"
     "\tfor i in $(sqlite3-api.jses); do \\\n"
     "\t\techo \"/* BEGIN FILE: $$i */\"; \\\n"
     "\t\tcat $$i; \\\n"
     "\t\techo \"/* END FILE: $$i */\"; \\\n"
     "\tdone > $@\n",
     zBuildName);

  pf("$(sqlite3-api.%s.js): $(sqlite3-api.%s.c-pp.js)\n"
     "$(eval $(call SQLITE.CALL.C-PP.FILTER,"
     "$(sqlite3-api.%s.c-pp.js), " /* $1 = src */
     "$(sqlite3-api.%s.js), "     /* $2 = tgt */
     "$(c-pp.D.%s)"           /* $3 = c-pp -Dx=Y flags */
     "))\n",
     zBuildName, zBuildName, zBuildName,
     zBuildName, zBuildName);

  mk_pre_post(zBuildName, pB->zCmppD, pB->zWasmOut);

  /* target pB->zJsOut */
  pf("%s: $(MAKEFILE_LIST) $(sqlite3-wasm.cfiles) $(EXPORTED_FUNCTIONS.api) "
     "$(bin.mkwb) "
     "$(pre-post.%s.deps) "
     "$(sqlite3-api.ext.jses)"
     /* ^^^ maintenance reminder: we set these as deps so that they
     ** get copied into place early. That allows the developer to
     ** reload the base-most test pages while the later-stage builds
     ** are still compiling, which is especially helpful when running
     ** builds with long build times (like -Oz). */
     "\n",
     pB->zJsOut, zBuildName);
  pf("\t@echo \"Building $@ ...\"\n");
  if( LIBMODE_UNSUPPORTED & pB->flags ){
    ps("\t@echo 'ACHTUNG: $@ is an unsupported build. "
       "Use at your own risk.'");
  }
  pf("\t$(bin.emcc) -o $@ $(emcc_opt_full) $(emcc.flags) \\\n");
  pf("\t\t$(emcc.jsflags) -sENVIRONMENT=$(emcc.environment.%s) \\\n",
     zBuildName);
  pf("\t\t$(pre-post.%s.flags) \\\n", zBuildName);
  if( pB->zEmcc ){
    pf("\t\t$(emcc.%s.flags) \\\n", zBuildName);
  }
  pf("\t\t$(cflags.common) $(cflags.%s) \\\n"
     "\t\t$(SQLITE_OPT) \\\n"
     "\t\t$(cflags.wasm_extra_init) $(sqlite3-wasm.cfiles)\n",
     zBuildName);
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
#if 1
    pf("\t@echo 'FIXME: missing build pieces for build %s'; exit 1\n",
       zBuildName);
#else
    fixme;
    pf("\t@echo 'Patching $@ for %s.wasm...'; \\\n", pB->zWasmFile);
    pf("\t\trm -f %s; \\\n", zWasmOut);
    pf("\t\tsed -i -e 's/\"%s.wasm\"/\"%s.wasm\"/g' $@ || exit;\n",
       /* ^^^^^^ reminder: Mac/BSD sed has no -i flag but this
       ** build process explicitly requires a Linux system. */
       zNM, pB->zWasmFile);
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
#endif
  }else{
    pf("\t@ls -la %s $@\n", zWasmOut);
  }

  if( LIBMODE_64BIT & pB->flags ){
    pf("64bit: %s\n", pB->zJsOut);
  }else if( 0==(LIBMODE_NOT_IN_ALL & pB->flags)
            && 0==(LIBMODE_UNSUPPORTED & pB->flags) ){
    pf("all: %s\n", pB->zJsOut);
  }
  pf("# End build [%s]%s", zBuildName, zBanner);
}

int main(void){
  int rc = 0;
  const BuildDef *pB;
  pf("# What follows was GENERATED by %s. Edit at your own risk.\n", __FILE__);
  mk_prologue();
#define E(N) mk_lib_mode(# N, &oBuildDefs.N);
  BuildDefs_map(E)
#undef E
#if 0
  mk_fiddle();
  mk_pre_post(0, "speedtest1","vanilla", 0, "speedtest1.wasm");
  mk_pre_post(0, "speedtest1-wasmfs", "esm",
              "$(c-pp.D.sqlite3-bundler-friendly) -Dwasmfs",
              "speetest1-wasmfs.wasm");
#endif
  return rc;
}
