###############################################################################
# This is the main makefile for sqlite. It expects to be included from
# a higher-level makefile which configures any dynamic state needed by
# this one.
#
# The following variables must be defined before this script is
# invoked:
#
#XX# FIXME: the list of vars from the historical main.mk is dated and
#XX# needs to be updated for autosetup.
#
# TOP              The toplevel directory of the source tree.  This is the
#                  directory that contains this "Makefile.in" and the
#                  "configure.in" script.
#
# BCC              C Compiler and options for use in building executables that
#                  will run on the platform that is doing the build.
#
# TCC              C Compiler and options for use in building executables that
#                  will run on the target platform.  This is usually the same
#                  as BCC, unless you are cross-compiling.
#
# AR               Tools used to build a static library.
#
# BEXE             File extension for executables on the build platform. ".exe"
#                  for Windows and "" everywhere else.
#
# TEXE             File extension for executables on the target platform. ".exe"
#                  for Windows and "" everywhere else.
#
# ... and many, many more ...
#
# Once the variables above are defined, the rest of this make script
# will build the SQLite library and testing tools.
################################################################################

#
# Ideally these all come from the calling makefile, but we can provide some
# sane defaults for many of them...
#
prefix ?= /usr/local
exec_prefix ?= $(prefix)
libdir ?= $(prefix)/lib
pkgconfigdir ?= $(libdir)/pkgconfig
bindir ?= $(prefix)/bin
includedir ?= $(prefix)/include
USE_AMALGAMATION ?= 2
AMALGAMATION_LINE_MACROS ?= --linemacros=0
INSTALL ?= install

LDFLAGS_ZLIB ?= -lz
LDFLAGS_MATH ?= -lm
LDFLAGS_RPATH ?= -Wl,-rpath -Wl,$(prefix)/lib
LDFLAGS_READLINE ?= -lreadline
LDFLAGS_PTHREAD ?= -lpthread
LDFLAGS_SHOBJ ?= -shared
ENABLE_SHARED ?= 1
HAVE_WASI_SDK ?= 0

INSTALL_noexec = $(INSTALL) -m 0644
# ^^^ do not use GNU-specific flags to $(INSTALL), e.g. --mode=...

# TCOMPILE = generic target platform compiler invocation
TCOMPILE = $(TCC) $(TCOMPILE_EXTRAS)
# TLINK = compiler invocation for when the target will be an executable
TLINK = $(TCC) $(TLINK_EXTRAS)
# TLINK_shared = $(TLINK) invocation specifically for shared libraries
LDFLAGS_SHOBJ ?= -shared
TLINK_shared = $(TLINK) $(LDFLAGS_SHOBJ)

# TCCX is $(TCC) plus any CFLAGS which are common to most compilations
# for the target platform. In auto-configured builds it is typically
# defined by the main makefile to include configure-time-dependent
# options.
TCCX ?= $(TCC)
TCCX += -I. -I$(TOP)/src -I$(TOP)/ext/rtree -I$(TOP)/ext/icu
TCCX += -I$(TOP)/ext/fts3 -I$(TOP)/ext/async -I$(TOP)/ext/session
TCCX += -I$(TOP)/ext/userauth

TEMP_STORE ?= -DSQLITE_TEMP_STORE=1

#
# LDFLAGS_libsqlite3 should be used with any target which either
# results in building libsqlite3.so, compiles sqlite3.c directly, or
# links in either of $(LIBOBJSO) or $(LIBOBJS1).  Note that these
# flags are for the target build platform, not necessarily localhost.
# i.e. it should be used with $(TCCX) or $(TLINK) but not $(BCC).
#
LDFLAGS_libsqlite3 = \
  $(LDFLAGS_RPATH) $(TLIBS) $(LDFLAGS_PTHREAD) \
  $(LDFLAGS_MATH) $(LDFLAGS_ZLIB)

#
# install.XYZ = dirs for installation. They're in quotes to
# accommodate installations where paths have spaces in them.
#
install.bindir = "$(DESTDIR)$(bindir)"
install.libdir = "$(DESTDIR)$(libdir)"
install.includedir = "$(DESTDIR)$(prefix)/include"
install.pkgconfigdir = "$(DESTDIR)$(pkgconfigdir)"
$(install.bindir) $(install.libdir) $(install.includedir) $(install.pkgconfigdir):
	$(INSTALL) -d $@


#
# Object files for the SQLite library (non-amalgamation).
#
LIBOBJS0 = alter.o analyze.o attach.o auth.o \
         backup.o bitvec.o btmutex.o btree.o build.o \
         callback.o complete.o ctime.o \
         date.o dbpage.o dbstat.o delete.o \
         expr.o fault.o fkey.o \
         fts3.o fts3_aux.o fts3_expr.o fts3_hash.o fts3_icu.o \
         fts3_porter.o fts3_snippet.o fts3_tokenizer.o fts3_tokenizer1.o \
         fts3_tokenize_vtab.o \
         fts3_unicode.o fts3_unicode2.o fts3_write.o \
         fts5.o \
         func.o global.o hash.o \
         icu.o insert.o json.o legacy.o loadext.o \
         main.o malloc.o mem0.o mem1.o mem2.o mem3.o mem5.o \
         memdb.o memjournal.o \
         mutex.o mutex_noop.o mutex_unix.o mutex_w32.o \
         notify.o opcodes.o os.o os_kv.o os_unix.o os_win.o \
         pager.o parse.o pcache.o pcache1.o pragma.o prepare.o printf.o \
         random.o resolve.o rowset.o rtree.o \
         sqlite3session.o select.o sqlite3rbu.o status.o stmt.o \
         table.o threads.o tokenize.o treeview.o trigger.o \
         update.o upsert.o userauth.o utf.o util.o vacuum.o \
         vdbe.o vdbeapi.o vdbeaux.o vdbeblob.o vdbemem.o vdbesort.o \
         vdbetrace.o vdbevtab.o vtab.o \
         wal.o walker.o where.o wherecode.o whereexpr.o \
         window.o
LIBOBJS = $(LIBOBJS0)

# Object files for the amalgamation.
#
LIBOBJS1 = sqlite3.o

# Determine the real value of LIBOBJ based on the 'configure' script
#
LIBOBJ = $(LIBOBJS$(USE_AMALGAMATION))

# All of the source code files.
#
SRC = \
  $(TOP)/src/alter.c \
  $(TOP)/src/analyze.c \
  $(TOP)/src/attach.c \
  $(TOP)/src/auth.c \
  $(TOP)/src/backup.c \
  $(TOP)/src/bitvec.c \
  $(TOP)/src/btmutex.c \
  $(TOP)/src/btree.c \
  $(TOP)/src/btree.h \
  $(TOP)/src/btreeInt.h \
  $(TOP)/src/build.c \
  $(TOP)/src/callback.c \
  $(TOP)/src/complete.c \
  $(TOP)/src/ctime.c \
  $(TOP)/src/date.c \
  $(TOP)/src/dbpage.c \
  $(TOP)/src/dbstat.c \
  $(TOP)/src/delete.c \
  $(TOP)/src/expr.c \
  $(TOP)/src/fault.c \
  $(TOP)/src/fkey.c \
  $(TOP)/src/func.c \
  $(TOP)/src/global.c \
  $(TOP)/src/hash.c \
  $(TOP)/src/hash.h \
  $(TOP)/src/hwtime.h \
  $(TOP)/src/insert.c \
  $(TOP)/src/json.c \
  $(TOP)/src/legacy.c \
  $(TOP)/src/loadext.c \
  $(TOP)/src/main.c \
  $(TOP)/src/malloc.c \
  $(TOP)/src/mem0.c \
  $(TOP)/src/mem1.c \
  $(TOP)/src/mem2.c \
  $(TOP)/src/mem3.c \
  $(TOP)/src/mem5.c \
  $(TOP)/src/memdb.c \
  $(TOP)/src/memjournal.c \
  $(TOP)/src/msvc.h \
  $(TOP)/src/mutex.c \
  $(TOP)/src/mutex.h \
  $(TOP)/src/mutex_noop.c \
  $(TOP)/src/mutex_unix.c \
  $(TOP)/src/mutex_w32.c \
  $(TOP)/src/notify.c \
  $(TOP)/src/os.c \
  $(TOP)/src/os.h \
  $(TOP)/src/os_common.h \
  $(TOP)/src/os_setup.h \
  $(TOP)/src/os_kv.c \
  $(TOP)/src/os_unix.c \
  $(TOP)/src/os_win.c \
  $(TOP)/src/os_win.h \
  $(TOP)/src/pager.c \
  $(TOP)/src/pager.h \
  $(TOP)/src/parse.y \
  $(TOP)/src/pcache.c \
  $(TOP)/src/pcache.h \
  $(TOP)/src/pcache1.c \
  $(TOP)/src/pragma.c \
  $(TOP)/src/pragma.h \
  $(TOP)/src/prepare.c \
  $(TOP)/src/printf.c \
  $(TOP)/src/random.c \
  $(TOP)/src/resolve.c \
  $(TOP)/src/rowset.c \
  $(TOP)/src/select.c \
  $(TOP)/src/status.c \
  $(TOP)/src/shell.c.in \
  $(TOP)/src/sqlite.h.in \
  $(TOP)/src/sqlite3ext.h \
  $(TOP)/src/sqliteInt.h \
  $(TOP)/src/sqliteLimit.h \
  $(TOP)/src/table.c \
  $(TOP)/src/tclsqlite.c \
  $(TOP)/src/threads.c \
  $(TOP)/src/tokenize.c \
  $(TOP)/src/treeview.c \
  $(TOP)/src/trigger.c \
  $(TOP)/src/utf.c \
  $(TOP)/src/update.c \
  $(TOP)/src/upsert.c \
  $(TOP)/src/util.c \
  $(TOP)/src/vacuum.c \
  $(TOP)/src/vdbe.c \
  $(TOP)/src/vdbe.h \
  $(TOP)/src/vdbeapi.c \
  $(TOP)/src/vdbeaux.c \
  $(TOP)/src/vdbeblob.c \
  $(TOP)/src/vdbemem.c \
  $(TOP)/src/vdbesort.c \
  $(TOP)/src/vdbetrace.c \
  $(TOP)/src/vdbevtab.c \
  $(TOP)/src/vdbeInt.h \
  $(TOP)/src/vtab.c \
  $(TOP)/src/vxworks.h \
  $(TOP)/src/wal.c \
  $(TOP)/src/wal.h \
  $(TOP)/src/walker.c \
  $(TOP)/src/where.c \
  $(TOP)/src/wherecode.c \
  $(TOP)/src/whereexpr.c \
  $(TOP)/src/whereInt.h \
  $(TOP)/src/window.c

# Source code for extensions
#
SRC += \
  $(TOP)/ext/fts3/fts3.c \
  $(TOP)/ext/fts3/fts3.h \
  $(TOP)/ext/fts3/fts3Int.h \
  $(TOP)/ext/fts3/fts3_aux.c \
  $(TOP)/ext/fts3/fts3_expr.c \
  $(TOP)/ext/fts3/fts3_hash.c \
  $(TOP)/ext/fts3/fts3_hash.h \
  $(TOP)/ext/fts3/fts3_icu.c \
  $(TOP)/ext/fts3/fts3_porter.c \
  $(TOP)/ext/fts3/fts3_snippet.c \
  $(TOP)/ext/fts3/fts3_tokenizer.h \
  $(TOP)/ext/fts3/fts3_tokenizer.c \
  $(TOP)/ext/fts3/fts3_tokenizer1.c \
  $(TOP)/ext/fts3/fts3_tokenize_vtab.c \
  $(TOP)/ext/fts3/fts3_unicode.c \
  $(TOP)/ext/fts3/fts3_unicode2.c \
  $(TOP)/ext/fts3/fts3_write.c
SRC += \
  $(TOP)/ext/icu/sqliteicu.h \
  $(TOP)/ext/icu/icu.c
SRC += \
  $(TOP)/ext/rtree/rtree.h \
  $(TOP)/ext/rtree/rtree.c \
  $(TOP)/ext/rtree/geopoly.c
SRC += \
  $(TOP)/ext/session/sqlite3session.c \
  $(TOP)/ext/session/sqlite3session.h
SRC += \
  $(TOP)/ext/userauth/userauth.c \
  $(TOP)/ext/userauth/sqlite3userauth.h
SRC += \
  $(TOP)/ext/rbu/sqlite3rbu.h \
  $(TOP)/ext/rbu/sqlite3rbu.c
SRC += \
  $(TOP)/ext/misc/stmt.c

# Generated source code files
#
SRC += \
  keywordhash.h \
  opcodes.c \
  opcodes.h \
  parse.c \
  parse.h \
  sqlite_cfg.h \
  shell.c \
  sqlite3.h

# Source code to the test files.
#
TESTSRC = \
  $(TOP)/src/test1.c \
  $(TOP)/src/test2.c \
  $(TOP)/src/test3.c \
  $(TOP)/src/test4.c \
  $(TOP)/src/test5.c \
  $(TOP)/src/test6.c \
  $(TOP)/src/test8.c \
  $(TOP)/src/test9.c \
  $(TOP)/src/test_autoext.c \
  $(TOP)/src/test_async.c \
  $(TOP)/src/test_backup.c \
  $(TOP)/src/test_bestindex.c \
  $(TOP)/src/test_blob.c \
  $(TOP)/src/test_btree.c \
  $(TOP)/src/test_config.c \
  $(TOP)/src/test_delete.c \
  $(TOP)/src/test_demovfs.c \
  $(TOP)/src/test_devsym.c \
  $(TOP)/src/test_fs.c \
  $(TOP)/src/test_func.c \
  $(TOP)/src/test_hexio.c \
  $(TOP)/src/test_init.c \
  $(TOP)/src/test_intarray.c \
  $(TOP)/src/test_journal.c \
  $(TOP)/src/test_malloc.c \
  $(TOP)/src/test_md5.c \
  $(TOP)/src/test_multiplex.c \
  $(TOP)/src/test_mutex.c \
  $(TOP)/src/test_onefile.c \
  $(TOP)/src/test_osinst.c \
  $(TOP)/src/test_pcache.c \
  $(TOP)/src/test_quota.c \
  $(TOP)/src/test_rtree.c \
  $(TOP)/src/test_schema.c \
  $(TOP)/src/test_superlock.c \
  $(TOP)/src/test_syscall.c \
  $(TOP)/src/test_tclsh.c \
  $(TOP)/src/test_tclvar.c \
  $(TOP)/src/test_thread.c \
  $(TOP)/src/test_vdbecov.c \
  $(TOP)/src/test_vfs.c \
  $(TOP)/src/test_windirent.c \
  $(TOP)/src/test_window.c \
  $(TOP)/src/test_wsd.c       \
  $(TOP)/ext/fts3/fts3_term.c \
  $(TOP)/ext/fts3/fts3_test.c  \
  $(TOP)/ext/session/test_session.c \
  $(TOP)/ext/recover/sqlite3recover.c \
  $(TOP)/ext/recover/dbdata.c \
  $(TOP)/ext/recover/test_recover.c \
  $(TOP)/ext/intck/test_intck.c  \
  $(TOP)/ext/intck/sqlite3intck.c \
  $(TOP)/ext/rbu/test_rbu.c

# Statically linked extensions
#
TESTSRC += \
  $(TOP)/ext/expert/sqlite3expert.c \
  $(TOP)/ext/expert/test_expert.c \
  $(TOP)/ext/misc/amatch.c \
  $(TOP)/ext/misc/appendvfs.c \
  $(TOP)/ext/misc/basexx.c \
  $(TOP)/ext/misc/carray.c \
  $(TOP)/ext/misc/cksumvfs.c \
  $(TOP)/ext/misc/closure.c \
  $(TOP)/ext/misc/csv.c \
  $(TOP)/ext/misc/decimal.c \
  $(TOP)/ext/misc/eval.c \
  $(TOP)/ext/misc/explain.c \
  $(TOP)/ext/misc/fileio.c \
  $(TOP)/ext/misc/fuzzer.c \
  $(TOP)/ext/fts5/fts5_tcl.c \
  $(TOP)/ext/fts5/fts5_test_mi.c \
  $(TOP)/ext/fts5/fts5_test_tok.c \
  $(TOP)/ext/misc/ieee754.c \
  $(TOP)/ext/misc/mmapwarm.c \
  $(TOP)/ext/misc/nextchar.c \
  $(TOP)/ext/misc/normalize.c \
  $(TOP)/ext/misc/percentile.c \
  $(TOP)/ext/misc/prefixes.c \
  $(TOP)/ext/misc/qpvtab.c \
  $(TOP)/ext/misc/randomjson.c \
  $(TOP)/ext/misc/regexp.c \
  $(TOP)/ext/misc/remember.c \
  $(TOP)/ext/misc/series.c \
  $(TOP)/ext/misc/spellfix.c \
  $(TOP)/ext/misc/stmtrand.c \
  $(TOP)/ext/misc/totype.c \
  $(TOP)/ext/misc/unionvtab.c \
  $(TOP)/ext/misc/wholenumber.c \
  $(TOP)/ext/misc/zipfile.c \
  $(TOP)/ext/userauth/userauth.c \
  $(TOP)/ext/rtree/test_rtreedoc.c

# Source code to the library files needed by the test fixture
#
TESTSRC2 = \
  $(TOP)/src/attach.c \
  $(TOP)/src/backup.c \
  $(TOP)/src/bitvec.c \
  $(TOP)/src/btree.c \
  $(TOP)/src/build.c \
  $(TOP)/src/ctime.c \
  $(TOP)/src/date.c \
  $(TOP)/src/dbpage.c \
  $(TOP)/src/dbstat.c \
  $(TOP)/src/expr.c \
  $(TOP)/src/func.c \
  $(TOP)/src/global.c \
  $(TOP)/src/insert.c \
  $(TOP)/src/wal.c \
  $(TOP)/src/main.c \
  $(TOP)/src/mem5.c \
  $(TOP)/src/os.c \
  $(TOP)/src/os_kv.c \
  $(TOP)/src/os_unix.c \
  $(TOP)/src/os_win.c \
  $(TOP)/src/pager.c \
  $(TOP)/src/pragma.c \
  $(TOP)/src/prepare.c \
  $(TOP)/src/printf.c \
  $(TOP)/src/random.c \
  $(TOP)/src/pcache.c \
  $(TOP)/src/pcache1.c \
  $(TOP)/src/select.c \
  $(TOP)/src/tokenize.c \
  $(TOP)/src/treeview.c \
  $(TOP)/src/utf.c \
  $(TOP)/src/util.c \
  $(TOP)/src/vdbeapi.c \
  $(TOP)/src/vdbeaux.c \
  $(TOP)/src/vdbe.c \
  $(TOP)/src/vdbemem.c \
  $(TOP)/src/vdbetrace.c \
  $(TOP)/src/vdbevtab.c \
  $(TOP)/src/where.c \
  $(TOP)/src/wherecode.c \
  $(TOP)/src/whereexpr.c \
  $(TOP)/src/window.c \
  parse.c \
  $(TOP)/ext/fts3/fts3.c \
  $(TOP)/ext/fts3/fts3_aux.c \
  $(TOP)/ext/fts3/fts3_expr.c \
  $(TOP)/ext/fts3/fts3_term.c \
  $(TOP)/ext/fts3/fts3_tokenizer.c \
  $(TOP)/ext/fts3/fts3_write.c \
  $(TOP)/ext/async/sqlite3async.c \
  $(TOP)/ext/session/sqlite3session.c \
  $(TOP)/ext/misc/stmt.c \
  fts5.c

# Header files used by all library source files.
#
HDR = \
   $(TOP)/src/btree.h \
   $(TOP)/src/btreeInt.h \
   $(TOP)/src/hash.h \
   $(TOP)/src/hwtime.h \
   keywordhash.h \
   $(TOP)/src/msvc.h \
   $(TOP)/src/mutex.h \
   opcodes.h \
   $(TOP)/src/os.h \
   $(TOP)/src/os_common.h \
   $(TOP)/src/os_setup.h \
   $(TOP)/src/os_win.h \
   $(TOP)/src/pager.h \
   $(TOP)/src/pcache.h \
   parse.h  \
   $(TOP)/src/pragma.h \
   sqlite3.h  \
   $(TOP)/src/sqlite3ext.h \
   $(TOP)/src/sqliteInt.h  \
   $(TOP)/src/sqliteLimit.h \
   $(TOP)/src/vdbe.h \
   $(TOP)/src/vdbeInt.h \
   $(TOP)/src/vxworks.h \
   $(TOP)/src/whereInt.h \
   sqlite_cfg.h

# Header files used by extensions
#
EXTHDR += \
  $(TOP)/ext/fts3/fts3.h \
  $(TOP)/ext/fts3/fts3Int.h \
  $(TOP)/ext/fts3/fts3_hash.h \
  $(TOP)/ext/fts3/fts3_tokenizer.h
EXTHDR += \
  $(TOP)/ext/rtree/rtree.h \
  $(TOP)/ext/rtree/geopoly.c
EXTHDR += \
  $(TOP)/ext/icu/sqliteicu.h
EXTHDR += \
  $(TOP)/ext/rtree/sqlite3rtree.h
EXTHDR += \
  $(TOP)/ext/userauth/sqlite3userauth.h

# executables needed for testing
#
TESTPROGS = \
  testfixture$(TEXE) \
  sqlite3$(TEXE) \
  sqlite3_analyzer$(TEXE) \
  sqldiff$(TEXE) \
  dbhash$(TEXE) \
  sqltclsh$(TEXE)

# Databases containing fuzzer test cases
#
FUZZDATA = \
  $(TOP)/test/fuzzdata1.db \
  $(TOP)/test/fuzzdata2.db \
  $(TOP)/test/fuzzdata3.db \
  $(TOP)/test/fuzzdata4.db \
  $(TOP)/test/fuzzdata5.db \
  $(TOP)/test/fuzzdata6.db \
  $(TOP)/test/fuzzdata7.db \
  $(TOP)/test/fuzzdata8.db

# Standard options to testfixture
#
TESTOPTS = --verbose=file --output=test-out.txt

# Extra compiler options for various shell tools
#
SHELL_OPT += -DSQLITE_DQS=0
SHELL_OPT += -DSQLITE_ENABLE_FTS4
#SHELL_OPT += -DSQLITE_ENABLE_FTS5
SHELL_OPT += -DSQLITE_ENABLE_RTREE
SHELL_OPT += -DSQLITE_ENABLE_EXPLAIN_COMMENTS
SHELL_OPT += -DSQLITE_ENABLE_UNKNOWN_SQL_FUNCTION
SHELL_OPT += -DSQLITE_ENABLE_STMTVTAB
SHELL_OPT += -DSQLITE_ENABLE_DBPAGE_VTAB
SHELL_OPT += -DSQLITE_ENABLE_DBSTAT_VTAB
SHELL_OPT += -DSQLITE_ENABLE_BYTECODE_VTAB
SHELL_OPT += -DSQLITE_ENABLE_OFFSET_SQL_FUNC
SHELL_OPT += -DSQLITE_STRICT_SUBTYPE=1
FUZZERSHELL_OPT =
FUZZCHECK_OPT += -I$(TOP)/test
FUZZCHECK_OPT += -I$(TOP)/ext/recover
FUZZCHECK_OPT += \
  -DSQLITE_OSS_FUZZ \
  -DSQLITE_ENABLE_BYTECODE_VTAB \
  -DSQLITE_ENABLE_DBPAGE_VTAB \
  -DSQLITE_ENABLE_DBSTAT_VTAB \
  -DSQLITE_ENABLE_BYTECODE_VTAB \
  -DSQLITE_ENABLE_DESERIALIZE \
  -DSQLITE_ENABLE_EXPLAIN_COMMENTS \
  -DSQLITE_ENABLE_FTS3_PARENTHESIS \
  -DSQLITE_ENABLE_FTS4 \
  -DSQLITE_ENABLE_FTS5 \
  -DSQLITE_ENABLE_GEOPOLY \
  -DSQLITE_ENABLE_MATH_FUNCTIONS \
  -DSQLITE_ENABLE_MEMSYS5 \
  -DSQLITE_ENABLE_NORMALIZE \
  -DSQLITE_ENABLE_OFFSET_SQL_FUNC \
  -DSQLITE_ENABLE_PREUPDATE_HOOK \
  -DSQLITE_ENABLE_RTREE \
  -DSQLITE_ENABLE_SESSION \
  -DSQLITE_ENABLE_STMTVTAB \
  -DSQLITE_ENABLE_UNKNOWN_SQL_FUNCTION \
  -DSQLITE_ENABLE_STAT4 \
  -DSQLITE_ENABLE_STMT_SCANSTATUS \
  -DSQLITE_MAX_MEMORY=50000000 \
  -DSQLITE_MAX_MMAP_SIZE=0 \
  -DSQLITE_OMIT_LOAD_EXTENSION \
  -DSQLITE_PRINTF_PRECISION_LIMIT=1000 \
  -DSQLITE_PRIVATE="" \
  -DSQLITE_STRICT_SUBTYPE=1 \
  -DSQLITE_STATIC_RANDOMJSON

FUZZCHECK_SRC += $(TOP)/test/fuzzcheck.c
FUZZCHECK_SRC += $(TOP)/test/ossfuzz.c
FUZZCHECK_SRC += $(TOP)/test/fuzzinvariants.c
FUZZCHECK_SRC += $(TOP)/ext/recover/dbdata.c
FUZZCHECK_SRC += $(TOP)/ext/recover/sqlite3recover.c
FUZZCHECK_SRC += $(TOP)/test/vt02.c
FUZZCHECK_SRC += $(TOP)/ext/misc/percentile.c
FUZZCHECK_SRC += $(TOP)/ext/misc/randomjson.c
DBFUZZ_OPT =
ST_OPT = -DSQLITE_OS_KV_OPTIONAL


has_tclsh84:
	sh $(TOP)/tool/cktclsh.sh 8.4 $(TCLSH_CMD)
	touch has_tclsh84

has_tclsh85:
	sh $(TOP)/tool/cktclsh.sh 8.5 $(TCLSH_CMD)
	touch has_tclsh85

has_tclconfig:
	@if [ x = "x$(TCL_CONFIG_SH)" ]; then \
		echo 'TCL_CONFIG_SH must be set to point to a "tclConfig.sh"' 1>&2; exit 1; \
	fi
	touch has_tclconfig

#
# This target creates a directory named "tsrc" and fills it with
# copies of all of the C source code and header files needed to
# build on the target system.  Some of the C source code and header
# files are automatically generated.  This target takes care of
# all that automatic generation.
#
.target_source:	$(SRC) $(TOP)/tool/vdbe-compress.tcl fts5.c $(BTCLSH) # has_tclsh84
	rm -rf tsrc
	mkdir tsrc
	cp -f $(SRC) tsrc
	rm tsrc/sqlite.h.in tsrc/parse.y
	$(BTCLSH) $(TOP)/tool/vdbe-compress.tcl $(OPTS) <tsrc/vdbe.c >vdbe.new
	mv vdbe.new tsrc/vdbe.c
	cp fts5.c fts5.h tsrc
	touch .target_source

libsqlite3.LIB = libsqlite3$(TLIB)
libsqlite3.SO = libsqlite3$(TDLL)

# Rules to build the LEMON compiler generator
#
lemon$(BEXE):	$(TOP)/tool/lemon.c $(TOP)/tool/lempar.c
	$(BCC) -o $@ $(TOP)/tool/lemon.c
	cp $(TOP)/tool/lempar.c .

# Rules to build the program that generates the source-id
#
mksourceid$(BEXE):	$(TOP)/tool/mksourceid.c
	$(BCC) -o $@ $(TOP)/tool/mksourceid.c

sqlite3.h:	$(TOP)/src/sqlite.h.in $(TOP)/manifest mksourceid$(BEXE) \
		$(TOP)/VERSION $(BTCLSH) # has_tclsh84
	$(BTCLSH) $(TOP)/tool/mksqlite3h.tcl $(TOP) >sqlite3.h

sqlite3.c:	.target_source sqlite3.h $(TOP)/tool/mksqlite3c.tcl src-verify \
		$(BTCLSH) # has_tclsh84
	$(BTCLSH) $(TOP)/tool/mksqlite3c.tcl $(AMALGAMATION_LINE_MACROS) $(EXTRA_SRC)
	cp tsrc/sqlite3ext.h .
	cp $(TOP)/ext/session/sqlite3session.h .

sqlite3r.h: sqlite3.h $(BTCLSH) # has_tclsh84
	$(BTCLSH) $(TOP)/tool/mksqlite3h.tcl $(TOP) --enable-recover >sqlite3r.h

sqlite3r.c: sqlite3.c sqlite3r.h $(BTCLSH) # has_tclsh84
	cp $(TOP)/ext/recover/sqlite3recover.c tsrc/
	cp $(TOP)/ext/recover/sqlite3recover.h tsrc/
	cp $(TOP)/ext/recover/dbdata.c tsrc/
	$(BTCLSH) $(TOP)/tool/mksqlite3c.tcl --enable-recover $(AMALGAMATION_LINE_MACROS) $(EXTRA_SRC)

sqlite3ext.h:	.target_source
	cp tsrc/sqlite3ext.h .

# Rules to build individual *.o files from generated *.c files. This
# applies to:
#
#     parse.o
#     opcodes.o
#
parse.o:	parse.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c parse.c

opcodes.o:	opcodes.c
	$(TCOMPILE) $(TEMP_STORE) -c opcodes.c

# Rules to build individual *.o files from files in the src directory.
#
alter.o:	$(TOP)/src/alter.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/alter.c

analyze.o:	$(TOP)/src/analyze.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/analyze.c

attach.o:	$(TOP)/src/attach.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/attach.c

auth.o:	$(TOP)/src/auth.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/auth.c

backup.o:	$(TOP)/src/backup.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/backup.c

bitvec.o:	$(TOP)/src/bitvec.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/bitvec.c

btmutex.o:	$(TOP)/src/btmutex.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/btmutex.c

btree.o:	$(TOP)/src/btree.c $(HDR) $(TOP)/src/pager.h
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/btree.c

build.o:	$(TOP)/src/build.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/build.c

callback.o:	$(TOP)/src/callback.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/callback.c

complete.o:	$(TOP)/src/complete.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/complete.c

ctime.o:	$(TOP)/src/ctime.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/ctime.c

date.o:	$(TOP)/src/date.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/date.c

dbpage.o:	$(TOP)/src/dbpage.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/dbpage.c

dbstat.o:	$(TOP)/src/dbstat.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/dbstat.c

delete.o:	$(TOP)/src/delete.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/delete.c

expr.o:	$(TOP)/src/expr.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/expr.c

fault.o:	$(TOP)/src/fault.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/fault.c

fkey.o:	$(TOP)/src/fkey.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/fkey.c

func.o:	$(TOP)/src/func.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/func.c

global.o:	$(TOP)/src/global.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/global.c

hash.o:	$(TOP)/src/hash.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/hash.c

insert.o:	$(TOP)/src/insert.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/insert.c

json.o:	$(TOP)/src/json.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/json.c

legacy.o:	$(TOP)/src/legacy.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/legacy.c

loadext.o:	$(TOP)/src/loadext.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/loadext.c

main.o:	$(TOP)/src/main.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/main.c

malloc.o:	$(TOP)/src/malloc.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/malloc.c

mem0.o:	$(TOP)/src/mem0.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/mem0.c

mem1.o:	$(TOP)/src/mem1.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/mem1.c

mem2.o:	$(TOP)/src/mem2.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/mem2.c

mem3.o:	$(TOP)/src/mem3.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/mem3.c

mem5.o:	$(TOP)/src/mem5.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/mem5.c

memdb.o:	$(TOP)/src/memdb.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/memdb.c

memjournal.o:	$(TOP)/src/memjournal.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/memjournal.c

mutex.o:	$(TOP)/src/mutex.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/mutex.c

mutex_noop.o:	$(TOP)/src/mutex_noop.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/mutex_noop.c

mutex_unix.o:	$(TOP)/src/mutex_unix.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/mutex_unix.c

mutex_w32.o:	$(TOP)/src/mutex_w32.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/mutex_w32.c

notify.o:	$(TOP)/src/notify.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/notify.c

pager.o:	$(TOP)/src/pager.c $(HDR) $(TOP)/src/pager.h
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/pager.c

pcache.o:	$(TOP)/src/pcache.c $(HDR) $(TOP)/src/pcache.h
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/pcache.c

pcache1.o:	$(TOP)/src/pcache1.c $(HDR) $(TOP)/src/pcache.h
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/pcache1.c

os.o:	$(TOP)/src/os.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/os.c

os_kv.o:	$(TOP)/src/os_kv.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/os_kv.c

os_unix.o:	$(TOP)/src/os_unix.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/os_unix.c

os_win.o:	$(TOP)/src/os_win.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/os_win.c

pragma.o:	$(TOP)/src/pragma.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/pragma.c

prepare.o:	$(TOP)/src/prepare.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/prepare.c

printf.o:	$(TOP)/src/printf.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/printf.c

random.o:	$(TOP)/src/random.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/random.c

resolve.o:	$(TOP)/src/resolve.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/resolve.c

rowset.o:	$(TOP)/src/rowset.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/rowset.c

select.o:	$(TOP)/src/select.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/select.c

status.o:	$(TOP)/src/status.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/status.c

sqlite3.o:	sqlite3.h sqlite3.c
	$(TCOMPILE) $(TEMP_STORE) -c sqlite3.c

table.o:	$(TOP)/src/table.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/table.c

threads.o:	$(TOP)/src/threads.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/threads.c

tokenize.o:	$(TOP)/src/tokenize.c keywordhash.h $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/tokenize.c

treeview.o:	$(TOP)/src/treeview.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/treeview.c

trigger.o:	$(TOP)/src/trigger.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/trigger.c

update.o:	$(TOP)/src/update.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/update.c

upsert.o:	$(TOP)/src/upsert.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/upsert.c

utf.o:	$(TOP)/src/utf.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/utf.c

util.o:	$(TOP)/src/util.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/util.c

vacuum.o:	$(TOP)/src/vacuum.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/vacuum.c

vdbe.o:	$(TOP)/src/vdbe.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/vdbe.c

vdbeapi.o:	$(TOP)/src/vdbeapi.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/vdbeapi.c

vdbeaux.o:	$(TOP)/src/vdbeaux.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/vdbeaux.c

vdbeblob.o:	$(TOP)/src/vdbeblob.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/vdbeblob.c

vdbemem.o:	$(TOP)/src/vdbemem.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/vdbemem.c

vdbesort.o:	$(TOP)/src/vdbesort.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/vdbesort.c

vdbetrace.o:	$(TOP)/src/vdbetrace.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/vdbetrace.c

vdbevtab.o:	$(TOP)/src/vdbevtab.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/vdbevtab.c

vtab.o:	$(TOP)/src/vtab.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/vtab.c

wal.o:	$(TOP)/src/wal.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/wal.c

walker.o:	$(TOP)/src/walker.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/walker.c

where.o:	$(TOP)/src/where.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/where.c

wherecode.o:	$(TOP)/src/wherecode.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/wherecode.c

whereexpr.o:	$(TOP)/src/whereexpr.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/whereexpr.c

window.o:	$(TOP)/src/window.c $(HDR)
	$(TCOMPILE) $(TEMP_STORE) -c $(TOP)/src/window.c

tclsqlite.o:	$(TOP)/src/tclsqlite.c $(HDR)
	$(TCOMPILE) -DUSE_TCL_STUBS=1 $(TCL_INCLUDE_SPEC) \
		-c $(TOP)/src/tclsqlite.c

tclsqlite-shell.o:	$(TOP)/src/tclsqlite.c $(HDR)
	$(TCOMPILE) -DTCLSH -o $@ -c $(TOP)/src/tclsqlite.c $(TCL_INCLUDE_SPEC)

tclsqlite-stubs.o:	$(TOP)/src/tclsqlite.c $(HDR)
	$(TCOMPILE) -DUSE_TCL_STUBS=1 -o $@ -c $(TOP)/src/tclsqlite.c $(TCL_INCLUDE_SPEC)

tclsqlite3$(TEXE):	has_tclconfig tclsqlite-shell.o $(libsqlite3.LIB)
	$(TLINK) -o $@ tclsqlite-shell.o \
		 $(libsqlite3.LIB) $(TCL_INCLUDE_SPEC) $(TCL_LIB_SPEC) $(LDFLAGS_libsqlite3)

# Rules to build opcodes.c and opcodes.h
#
opcodes.c:	opcodes.h $(TOP)/tool/mkopcodec.tcl $(BTCLSH) # has_tclsh84
	$(BTCLSH) $(TOP)/tool/mkopcodec.tcl opcodes.h >opcodes.c

opcodes.h:	parse.h $(TOP)/src/vdbe.c \
		$(TOP)/tool/mkopcodeh.tcl $(BTCLSH) # has_tclsh84
	cat parse.h $(TOP)/src/vdbe.c | $(BTCLSH) $(TOP)/tool/mkopcodeh.tcl >opcodes.h

# Rules to build parse.c and parse.h - the outputs of lemon.
#
parse.h:	parse.c

parse.c:	$(TOP)/src/parse.y lemon$(BEXE)
	cp $(TOP)/src/parse.y .
	./lemon$(BEXE) $(OPT_FEATURE_FLAGS) $(OPTS) -S parse.y

sqlite3rc.h:	$(TOP)/src/sqlite3.rc $(TOP)/VERSION $(BTCLSH) # has_tclsh84
	echo '#ifndef SQLITE_RESOURCE_VERSION' >$@
	echo -n '#define SQLITE_RESOURCE_VERSION ' >>$@
	cat $(TOP)/VERSION | $(BTCLSH) $(TOP)/tool/replace.tcl exact . , >>$@
	echo '#endif' >>sqlite3rc.h

mkkeywordhash$(BEXE): $(TOP)/tool/mkkeywordhash.c
	$(BCC) -o $@ $(OPT_FEATURE_FLAGS) $(OPTS) $(TOP)/tool/mkkeywordhash.c
keywordhash.h:	mkkeywordhash$(BEXE)
	./mkkeywordhash$(BEXE) > $@


$(libsqlite3.LIB): $(LIBOBJ)
	$(AR) crs $@ $(LIBOBJ)
lib: $(libsqlite3.LIB)
all: lib

target_libsqlite3_so_1 = $(libsqlite3.SO)
target_libsqlite3_so = $(target_libsqlite3_so_$(ENABLE_SHARED))
$(libsqlite3.SO):	$(LIBOBJ)
	$(TLINK_shared) -o $@ $(LIBOBJ) $(LDFLAGS_libsqlite3)
so: $(target_libsqlite3_so)
all: so

sqlite3-all.c:	sqlite3.c $(TOP)/tool/split-sqlite3c.tcl $(BTCLSH) # has_tclsh84
	$(BTCLSH) $(TOP)/tool/split-sqlite3c.tcl

# Install the $(libsqlite3.SO) as $(libsqlite3.SO).$(RELEASE) and
# create symlinks which point to it. Do we really need all of this
# hoop-jumping? Can we not simply install the .so as-is to
# libsqlite3.so (without the versioned bits)?
#
# The historical SQLite build always used a version number of 0.8.6
# for reasons lost to history but having something to do with libtool
# (which is not longer used in this tree).
#
install-so-1: $(install.libdir) $(libsqlite3.SO)
	$(INSTALL) $(libsqlite3.SO) $(install.libdir); \
	cd $(install.libdir); \
		rm -f $(libsqlite3.SO).3 $(libsqlite3.SO).$(RELEASE); \
		mv $(libsqlite3.SO) $(libsqlite3.SO).$(RELEASE); \
		ln -s $(libsqlite3.SO).$(RELEASE) $(libsqlite3.SO).3; \
		ln -s $(libsqlite3.SO).3 $(libsqlite3.SO)
install-so-0:
install: install-so-$(ENABLE_SHARED)

# Install $(libsqlite3.LIB)
#
install-lib: $(install.libdir) $(libsqlite3.LIB)
	$(INSTALL_noexec) $(libsqlite3.LIB) $(install.libdir)
install: install-lib

# Install C header files
#
install-includes: sqlite3.h $(install.includedir)
	$(INSTALL_noexec) sqlite3.h "$(TOP)/src/sqlite3ext.h" $(install.includedir)
install: install-includes

# libtclsqlite3...
#
pkgIndex.tcl:
	echo 'package ifneeded sqlite3 $(RELEASE) [list load [file join $$dir libtclsqlite3[info sharedlibextension]] sqlite3]' > $@
libtclsqlite3.SO = libtclsqlite3$(TDLL)
target_libtclsqlite3_1 = $(libtclsqlite3.SO)
target_libtclsqlite3 = $(target_libtclsqlite3_$(HAVE_TCL))
$(libtclsqlite3.SO): tclsqlite.o $(libsqlite3.LIB)
	$(TLINK_shared) -o $@ tclsqlite.o \
		$(TCL_INCLUDE_SPEC) $(TCL_STUB_LIB_SPEC) $(LDFLAGS_libsqlite3) \
		$(libsqlite3.LIB) $(TCLLIB_RPATH)
libtcl:	$(target_libtclsqlite3)
all:	libtcl

install.tcldir = "$(DESTDIR)$(TCLLIBDIR)"
install-tcl-1: install-lib $(target_libtclsqlite3) pkgIndex.tcl
	@if [ "x$(DESTDIR)" = "x$(install.tcldir)" ]; then echo "TCLLIBDIR is not set." 1>&2; exit 1; fi
	$(INSTALL) -d $(install.tcldir)
	$(INSTALL) $(libtclsqlite3.SO) $(install.tcldir)
	$(INSTALL_noexec) pkgIndex.tcl $(install.tcldir)
install-tcl-0 install-tcl-:
install: install-tcl-$(HAVE_TCL)

tclsqlite3.c:	sqlite3.c
	echo '#ifndef USE_SYSTEM_SQLITE' >tclsqlite3.c
	cat sqlite3.c >>tclsqlite3.c
	echo '#endif /* USE_SYSTEM_SQLITE */' >>tclsqlite3.c
	cat $(TOP)/src/tclsqlite.c >>tclsqlite3.c

# Build the SQLite TCL extension in a way that make it compatible
# with whatever version of TCL is running as $TCLSH_CMD, possibly defined
# by --with-tclsh=
#
tclextension: tclsqlite3.c
	$(TCLSH_CMD) $(TOP)/tool/buildtclext.tcl --build-only --cc "$(CC)" $(CFLAGS) $(OPT_FEATURE_FLAGS) $(OPTS)

# Install the SQLite TCL extension in a way that is appropriate for $TCLSH_CMD
# to find it.
#
tclextension-install: tclsqlite3.c
	$(TCLSH_CMD) $(TOP)/tool/buildtclext.tcl --cc "$(CC)" $(CFLAGS) $(OPT_FEATURE_FLAGS) $(OPTS)

# Install the SQLite TCL extension that is used by $TCLSH_CMD
#
tclextension-uninstall:
	$(TCLSH_CMD) $(TOP)/tool/buildtclext.tcl --uninstall

# List all installed the SQLite TCL extension that is are accessible
# by $TCLSH_CMD, included prior versions.
#
tclextension-list:
	$(TCLSH_CMD) $(TOP)/tool/buildtclext.tcl --info

# FTS5 things
#
FTS5_SRC = \
   $(TOP)/ext/fts5/fts5.h \
   $(TOP)/ext/fts5/fts5Int.h \
   $(TOP)/ext/fts5/fts5_aux.c \
   $(TOP)/ext/fts5/fts5_buffer.c \
   $(TOP)/ext/fts5/fts5_main.c \
   $(TOP)/ext/fts5/fts5_config.c \
   $(TOP)/ext/fts5/fts5_expr.c \
   $(TOP)/ext/fts5/fts5_hash.c \
   $(TOP)/ext/fts5/fts5_index.c \
   fts5parse.c fts5parse.h \
   $(TOP)/ext/fts5/fts5_storage.c \
   $(TOP)/ext/fts5/fts5_tokenize.c \
   $(TOP)/ext/fts5/fts5_unicode2.c \
   $(TOP)/ext/fts5/fts5_varint.c \
   $(TOP)/ext/fts5/fts5_vocab.c  \

fts5parse.c:	$(TOP)/ext/fts5/fts5parse.y lemon$(BEXE)
	cp $(TOP)/ext/fts5/fts5parse.y .
	rm -f fts5parse.h
	./lemon$(BEXE) $(OPTS) -S fts5parse.y

fts5parse.h: fts5parse.c

fts5.c: $(FTS5_SRC) $(BTCLSH) # has_tclsh84
	$(BTCLSH) $(TOP)/ext/fts5/tool/mkfts5c.tcl
	cp $(TOP)/ext/fts5/fts5.h .

fts5.o:	fts5.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c fts5.c

sqlite3rbu.o:	$(TOP)/ext/rbu/sqlite3rbu.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/rbu/sqlite3rbu.c


# Rules to build the 'testfixture' application.
#
# If using the amalgamation, use sqlite3.c directly to build the test
# fixture.  Otherwise link against libsqlite3.a.  (This distinction is
# necessary because the test fixture requires non-API symbols which are
# hidden when the library is built via the amalgamation).
#
TESTFIXTURE_FLAGS  = -DSQLITE_TEST=1 -DSQLITE_CRASH_TEST=1
TESTFIXTURE_FLAGS += -DTCLSH_INIT_PROC=sqlite3TestInit
TESTFIXTURE_FLAGS += -DSQLITE_SERVER=1 -DSQLITE_PRIVATE="" -DSQLITE_CORE
TESTFIXTURE_FLAGS += -DBUILD_sqlite
TESTFIXTURE_FLAGS += -DSQLITE_SERIES_CONSTRAINT_VERIFY=1
TESTFIXTURE_FLAGS += -DSQLITE_DEFAULT_PAGE_SIZE=1024
TESTFIXTURE_FLAGS += -DSQLITE_ENABLE_STMTVTAB
TESTFIXTURE_FLAGS += -DSQLITE_ENABLE_DBPAGE_VTAB
TESTFIXTURE_FLAGS += -DSQLITE_ENABLE_BYTECODE_VTAB
TESTFIXTURE_FLAGS += -DSQLITE_CKSUMVFS_STATIC
TESTFIXTURE_FLAGS += -DSQLITE_STATIC_RANDOMJSON
TESTFIXTURE_FLAGS += -DSQLITE_STRICT_SUBTYPE=1

TESTFIXTURE_SRC0 = $(TESTSRC2) $(libsqlite3.LIB)
TESTFIXTURE_SRC1 = sqlite3.c
TESTFIXTURE_SRC = $(TESTSRC) $(TOP)/src/tclsqlite.c
TESTFIXTURE_SRC += $(TESTFIXTURE_SRC$(USE_AMALGAMATION))

testfixture$(TEXE):	has_tclconfig has_tclsh85 $(TESTFIXTURE_SRC)
	$(TLINK) -DSQLITE_NO_SYNC=1 $(TEMP_STORE) $(TESTFIXTURE_FLAGS) \
		-o $@ $(TESTFIXTURE_SRC) $(LIBTCL) $(LDFLAGS_libsqlite3) $(TCL_INCLUDE_SPEC)

coretestprogs:	testfixture$(BEXE) sqlite3$(BEXE)

testprogs:	$(TESTPROGS) srcck1$(BEXE) fuzzcheck$(TEXE) sessionfuzz$(TEXE)

# A very detailed test running most or all test cases
fulltest:	alltest fuzztest

# Run most or all tcl test cases
alltest:	$(TESTPROGS)
	./testfixture$(TEXE) $(TOP)/test/all.test $(TESTOPTS)

# Really really long testing
soaktest:	$(TESTPROGS)
	./testfixture$(TEXE) $(TOP)/test/all.test -soak=1 $(TESTOPTS)

# Do extra testing but not everything.
fulltestonly:	$(TESTPROGS) fuzztest
	./testfixture$(TEXE) $(TOP)/test/full.test

# Fuzz testing
#
# WARNING: When the "fuzztest" target is run by the testrunner.tcl script,
# it does not actually run this code. Instead, it schedules equivalent
# commands. Therefore, if this target is updated, then code in
# testrunner_data.tcl (search for "trd_fuzztest_data") must also be updated.
#
fuzztest:	fuzzcheck$(TEXE) $(FUZZDATA) sessionfuzz$(TEXE)
	./fuzzcheck$(TEXE) $(FUZZDATA)
	./sessionfuzz$(TEXE) run $(TOP)/test/sessionfuzz-data1.db

valgrindfuzz:	fuzzcheck$(TEXT) $(FUZZDATA) sessionfuzz$(TEXE)
	valgrind ./fuzzcheck$(TEXE) --cell-size-check --limit-mem 10M $(FUZZDATA)
	valgrind ./sessionfuzz$(TEXE) run $(TOP)/test/sessionfuzz-data1.db

# The veryquick.test TCL tests.
#
tcltest:	./testfixture$(TEXE)
	./testfixture$(TEXE) $(TOP)/test/veryquick.test $(TESTOPTS)

# Runs all the same tests cases as the "tcltest" target but uses
# the testrunner.tcl script to run them in multiple cores
# concurrently.
testrunner:	testfixture$(TEXE)
	./testfixture$(TEXE) $(TOP)/test/testrunner.tcl

# This is the testing target preferred by the core SQLite developers.
# It runs tests under a standard configuration, regardless of how
# ./configure was run.  The devs run "make devtest" prior to each
# check-in, at a minimum.  Probably other tests too, but at least this
# one.
#
devtest:	srctree-check sourcetest
	$(TCLSH_CMD) $(TOP)/test/testrunner.tcl mdevtest $(TSTRNNR_OPTS)

mdevtest: srctree-check has_tclsh85
	$(TCLSH_CMD) $(TOP)/test/testrunner.tcl mdevtest $(TSTRNNR_OPTS)

sdevtest: has_tclsh85
	$(TCLSH_CMD) $(TOP)/test/testrunner.tcl sdevtest $(TSTRNNR_OPTS)

# Validate that various generated files in the source tree
# are up-to-date.
#
srctree-check:	$(TOP)/tool/srctree-check.tcl
	$(TCLSH_CMD) $(TOP)/tool/srctree-check.tcl

# Testing for a release
#
releasetest: srctree-check has_tclsh85 verify-source
	$(TCLSH_CMD) $(TOP)/test/testrunner.tcl release $(TSTRNNR_OPTS)

# Minimal testing that runs in less than 3 minutes
#
quicktest:	./testfixture$(TEXE)
	./testfixture$(TEXE) $(TOP)/test/extraquick.test $(TESTOPTS)

# Try to run tests on whatever options are specified by the
# ./configure.  The developers seldom use this target.  Instead
# they use "make devtest" which runs tests on a standard set of
# options regardless of how SQLite is configured.  This "test"
# target is provided for legacy only.
#
test:	srctree-check fuzztest sourcetest $(TESTPROGS) tcltest

# Run a test using valgrind.  This can take a really long time
# because valgrind is so much slower than a native machine.
#
valgrindtest:	$(TESTPROGS) valgrindfuzz
	OMIT_MISUSE=1 valgrind -v ./testfixture$(TEXE) $(TOP)/test/permutations.test valgrind $(TESTOPTS)

# A very fast test that checks basic sanity.  The name comes from
# the 60s-era electronics testing:  "Turn it on and see if smoke
# comes out."
#
smoketest:	$(TESTPROGS) fuzzcheck$(TEXE)
	./testfixture$(TEXE) $(TOP)/test/main.test $(TESTOPTS)

shelltest:
	$(TCLSH_CMD) $(TOP)/test/testrunner.tcl release shell

sqlite3_analyzer.c: sqlite3.c $(TOP)/src/tclsqlite.c $(TOP)/tool/spaceanal.tcl \
                    $(TOP)/tool/mkccode.tcl $(TOP)/tool/sqlite3_analyzer.c.in has_tclsh85
	$(BTCLSH) $(TOP)/tool/mkccode.tcl $(TOP)/tool/sqlite3_analyzer.c.in >sqlite3_analyzer.c

sqlite3_analyzer$(TEXE): has_tclconfig sqlite3_analyzer.c
	$(TLINK) sqlite3_analyzer.c -o $@ $(LIBTCL) $(TCL_INCLUDE_SPEC) $(LDFLAGS_libsqlite3)

sqltclsh.c: sqlite3.c $(TOP)/src/tclsqlite.c $(TOP)/tool/sqltclsh.tcl \
            $(TOP)/ext/misc/appendvfs.c $(TOP)/tool/mkccode.tcl \
            $(TOP)/tool/sqltclsh.c.in has_tclsh85
	$(BTCLSH) $(TOP)/tool/mkccode.tcl $(TOP)/tool/sqltclsh.c.in >sqltclsh.c

sqltclsh$(TEXE): has_tclconfig sqltclsh.c
	$(TLINK) sqltclsh.c -o $@ $(LIBTCL) $(LDFLAGS_libsqlite3)

sqlite3_expert$(TEXE): $(TOP)/ext/expert/sqlite3expert.h $(TOP)/ext/expert/sqlite3expert.c \
                       $(TOP)/ext/expert/expert.c sqlite3.c
	$(TLINK)	$(TOP)/ext/expert/sqlite3expert.h $(TOP)/ext/expert/sqlite3expert.c \
		$(TOP)/ext/expert/expert.c sqlite3.c -o sqlite3_expert $(LDFLAGS_libsqlite3)

CHECKER_DEPS =\
  $(TOP)/tool/mkccode.tcl \
  sqlite3.c \
  $(TOP)/src/tclsqlite.c \
  $(TOP)/ext/repair/sqlite3_checker.tcl \
  $(TOP)/ext/repair/checkindex.c \
  $(TOP)/ext/repair/checkfreelist.c \
  $(TOP)/ext/misc/btreeinfo.c \
  $(TOP)/ext/repair/sqlite3_checker.c.in

sqlite3_checker.c:	$(CHECKER_DEPS) has_tclsh85
	$(BTCLSH) $(TOP)/tool/mkccode.tcl $(TOP)/ext/repair/sqlite3_checker.c.in >$@

sqlite3_checker$(TEXE):	has_tclconfig sqlite3_checker.c
	$(TLINK) sqlite3_checker.c -o $@ $(LIBTCL) $(LDFLAGS_libsqlite3)

dbdump$(TEXE): $(TOP)/ext/misc/dbdump.c sqlite3.lo
	$(TLINK) -DDBDUMP_STANDALONE -o $@ \
           $(TOP)/ext/misc/dbdump.c sqlite3.lo $(LDFLAGS_libsqlite3)

dbtotxt$(TEXE): $(TOP)/tool/dbtotxt.c
	$(TLINK)-o $@ $(TOP)/tool/dbtotxt.c

showdb$(TEXE):	$(TOP)/tool/showdb.c sqlite3.lo
	$(TLINK) -o $@ $(TOP)/tool/showdb.c sqlite3.lo $(LDFLAGS_libsqlite3)

showstat4$(TEXE):	$(TOP)/tool/showstat4.c sqlite3.lo
	$(TLINK) -o $@ $(TOP)/tool/showstat4.c sqlite3.lo $(LDFLAGS_libsqlite3)

showjournal$(TEXE):	$(TOP)/tool/showjournal.c sqlite3.lo
	$(TLINK) -o $@ $(TOP)/tool/showjournal.c sqlite3.lo $(LDFLAGS_libsqlite3)

showwal$(TEXE):	$(TOP)/tool/showwal.c sqlite3.lo
	$(TLINK) -o $@ $(TOP)/tool/showwal.c sqlite3.lo $(LDFLAGS_libsqlite3)

showshm$(TEXE):	$(TOP)/tool/showshm.c
	$(TLINK) -o $@ $(TOP)/tool/showshm.c

index_usage$(TEXE): $(TOP)/tool/index_usage.c sqlite3.lo
	$(TLINK) $(SHELL_OPT) -o $@ $(TOP)/tool/index_usage.c sqlite3.lo $(LDFLAGS_libsqlite3)

changeset$(TEXE):	$(TOP)/ext/session/changeset.c sqlite3.lo
	$(TLINK) -o $@ $(TOP)/ext/session/changeset.c sqlite3.lo $(LDFLAGS_libsqlite3)

changesetfuzz$(TEXE):	$(TOP)/ext/session/changesetfuzz.c sqlite3.lo
	$(TLINK) -o $@ $(TOP)/ext/session/changesetfuzz.c sqlite3.lo $(LDFLAGS_libsqlite3)

rollback-test$(TEXE):	$(TOP)/tool/rollback-test.c sqlite3.lo
	$(TLINK) -o $@ $(TOP)/tool/rollback-test.c sqlite3.lo $(LDFLAGS_libsqlite3)

atrc$(TEXX): $(TOP)/test/atrc.c sqlite3.lo
	$(TLINK) -o $@ $(TOP)/test/atrc.c sqlite3.lo $(LDFLAGS_libsqlite3)

LogEst$(TEXE):	$(TOP)/tool/logest.c sqlite3.h
	$(TLINK) -I. -o $@ $(TOP)/tool/logest.c

wordcount$(TEXE):	$(TOP)/test/wordcount.c sqlite3.lo
	$(TLINK) -o $@ $(TOP)/test/wordcount.c sqlite3.lo $(LDFLAGS_libsqlite3)

speedtest1$(TEXE):	$(TOP)/test/speedtest1.c sqlite3.c Makefile
	$(TLINK) $(ST_OPT) -o $@ $(TOP)/test/speedtest1.c sqlite3.c $(LDFLAGS_libsqlite3)

#XX#startup$(TEXE):	$(TOP)/test/startup.c sqlite3.c
#XX#	$(CC) -Os -g -DSQLITE_THREADSAFE=0 -o $@ $(TOP)/test/startup.c sqlite3.c $(TLIBS)
# ^^^ note that it wants $(TLIBS) (a.k.a. $(LDFLAGS_libsqlite3) but is using $(CC)
# instead of $(BCC).

KV_OPT += -DSQLITE_DIRECT_OVERFLOW_READ

kvtest$(TEXE):	$(TOP)/test/kvtest.c sqlite3.c
	$(TLINK) $(KV_OPT) -o $@ $(TOP)/test/kvtest.c sqlite3.c $(LDFLAGS_libsqlite3)

rbu$(EXE): $(TOP)/ext/rbu/rbu.c $(TOP)/ext/rbu/sqlite3rbu.c sqlite3.o
	$(TLINK) -I. -o $@ $(TOP)/ext/rbu/rbu.c sqlite3.o $(LDFLAGS_libsqlite3)

loadfts$(EXE): $(TOP)/tool/loadfts.c $(libsqlite3.LIB)
	$(TLINK) $(TOP)/tool/loadfts.c $(libsqlite3.LIB) -o $@ $(LDFLAGS_libsqlite3)

# This target will fail if the SQLite amalgamation contains any exported
# symbols that do not begin with "sqlite3_". It is run as part of the
# releasetest.tcl script.
#
VALIDIDS=' sqlite3(changeset|changegroup|session)?_'
checksymbols: sqlite3.o
	nm -g --defined-only sqlite3.o
	nm -g --defined-only sqlite3.o | egrep -v $(VALIDIDS); test $$? -ne 0
	echo '0 errors out of 1 tests'

# Build a ZIP archive containing various command-line tools.
#
tool-zip:	testfixture$(TEXE) sqlite3$(TEXE) sqldiff$(TEXE) \
            sqlite3_analyzer$(TEXE) sqlite3_rsync$(TEXE) $(TOP)/tool/mktoolzip.tcl
	strip sqlite3$(TEXE) sqldiff$(TEXE) sqlite3_analyzer$(TEXE) sqlite3_rsync$(TEXE)
	./testfixture$(TEXE) $(TOP)/tool/mktoolzip.tcl

#XX# TODO: adapt the autoconf amalgamation for autosetup
#XX#
#XX## Build the amalgamation-autoconf package.  The amalamgation-tarball target builds
#XX## a tarball named for the version number.  Ex:  sqlite-autoconf-3110000.tar.gz.
#XX## The snapshot-tarball target builds a tarball named by the SHA1 hash
#XX##
#XX#amalgamation-tarball: sqlite3.c sqlite3rc.h
#XX#	TOP=$(TOP) sh $(TOP)/tool/mkautoconfamal.sh --normal
#XX#
#XX#snapshot-tarball: sqlite3.c sqlite3rc.h
#XX#	TOP=$(TOP) sh $(TOP)/tool/mkautoconfamal.sh --snapshot
#XX#

# The next two rules are used to support the "threadtest" target. Building
# threadtest runs a few thread-safety tests that are implemented in C. This
# target is invoked by the releasetest.tcl script.
#
THREADTEST3_SRC = $(TOP)/test/threadtest3.c    \
                  $(TOP)/test/tt3_checkpoint.c \
                  $(TOP)/test/tt3_index.c      \
                  $(TOP)/test/tt3_vacuum.c      \
                  $(TOP)/test/tt3_stress.c      \
                  $(TOP)/test/tt3_lookaside1.c

threadtest3$(TEXE): sqlite3.lo $(THREADTEST3_SRC)
	$(TLINK) $(TOP)/test/threadtest3.c $(TOP)/src/test_multiplex.c sqlite3.lo -o $@ $(LDFLAGS_libsqlite3)

threadtest: threadtest3$(TEXE)
	./threadtest3$(TEXE)

threadtest5: sqlite3.c $(TOP)/test/threadtest5.c
	$(TLINK) $(TOP)/test/threadtest5.c sqlite3.c -o $@ $(LDFLAGS_libsqlite3)

sqlite3$(TEXE):	shell.c sqlite3.c
	$(TCCX) $(CFLAGS_readline) $(SHELL_OPT) -o $@ \
		shell.c sqlite3.c \
		$(LDFLAGS_libsqlite3) $(LDFLAGS_READLINE)

install-cli-0: sqlite3$(TEXT) $(install.bindir)
	$(INSTALL) -s sqlite3$(TEXT) $(install.bindir)
install-cli-1:
install: install-cli-$(HAVE_WASI_SDK)

sqldiff$(TEXE):	$(TOP)/tool/sqldiff.c $(TOP)/ext/misc/sqlite3_stdio.h sqlite3.o sqlite3.h
	$(TLINK) $(CFLAGS_stdio3) -o $@ $(TOP)/tool/sqldiff.c sqlite3.o $(LDFLAGS_libsqlite3)

install-diff: sqldiff$(TEXE) $(install.bindir)
	$(INSTALL) -s sqldiff$(TEXT) $(install.bindir)
#install: install-diff

dbhash$(TEXE):	$(TOP)/tool/dbhash.c sqlite3.o sqlite3.h
	$(TLINK) -o $@ $(TOP)/tool/dbhash.c sqlite3.o $(LDFLAGS_libsqlite3)

RSYNC_SRC = \
  $(TOP)/tool/sqlite3_rsync.c \
  sqlite3.c

RSYNC_OPT = \
  -DSQLITE_ENABLE_DBPAGE_VTAB \
  -USQLITE_THREADSAFE \
  -DSQLITE_THREADSAFE=0 \
  -DSQLITE_OMIT_LOAD_EXTENSION \
  -DSQLITE_OMIT_DEPRECATED

sqlite3_rsync$(TEXE):	$(RSYNC_SRC)
	$(TCCX) -o $@ $(RSYNC_OPT) $(RSYNC_SRC) $(LDFLAGS_libsqlite3)

install-rsync: sqlite3_rsync$(TEXE) $(install.bindir)
	$(INSTALL) sqlite3_rsync$(TEXT) $(install.bindir)
#install: install-rsync

scrub$(TEXE):	$(TOP)/ext/misc/scrub.c sqlite3.o
	$(TLINK) -o $@ -I. -DSCRUB_STANDALONE \
		$(TOP)/ext/misc/scrub.c sqlite3.o $(LDFLAGS_libsqlite3)

srcck1$(BEXE):	$(TOP)/tool/srcck1.c
	$(BCC) -o srcck1$(BEXE) $(TOP)/tool/srcck1.c

sourcetest:	srcck1$(BEXE) sqlite3.c
	./srcck1$(BEXE) sqlite3.c

src-verify$(BEXE):	$(TOP)/tool/src-verify.c
	$(BCC) -o src-verify$(BEXE) $(TOP)/tool/src-verify.c

verify-source:	./src-verify$(BEXE)
	./src-verify$(BEXE) $(TOP)

fuzzershell$(TEXE):	$(TOP)/tool/fuzzershell.c sqlite3.c sqlite3.h
	$(TLINK) -o $@ $(FUZZERSHELL_OPT) \
	  $(TOP)/tool/fuzzershell.c sqlite3.c $(LDFLAGS_libsqlite3)
fuzzy: fuzzershell$(TEXE)

fuzzcheck$(TEXE):	$(FUZZCHECK_SRC) sqlite3.c sqlite3.h $(FUZZCHECK_DEP)
	$(TLINK) -o $@ $(FUZZCHECK_OPT) $(FUZZCHECK_SRC) sqlite3.c $(LDFLAGS_libsqlite3)
fuzzy: fuzzcheck$(TEXE)

fuzzcheck-asan$(TEXE):	$(FUZZCHECK_SRC) sqlite3.c sqlite3.h $(FUZZCHECK_DEP)
	$(TLINK) -o $@ -fsanitize=address $(FUZZCHECK_OPT) $(FUZZCHECK_SRC) \
		sqlite3.c $(LDFLAGS_libsqlite3)
fuzzy: fuzzcheck-asan$(TEXE)


fuzzcheck-ubsan$(TEXE):	$(FUZZCHECK_SRC) sqlite3.c sqlite3.h $(FUZZCHECK_DEP)
	$(TLINK) -o $@ -fsanitize=undefined $(FUZZCHECK_OPT) $(FUZZCHECK_SRC) \
		sqlite3.c $(LDFLAGS_libsqlite3)
fuzzy: fuzzcheck-ubsan$(TEXE)

# Usage:    FUZZDB=filename make run-fuzzcheck
#
# Where filename is a fuzzcheck database, this target builds and runs
# fuzzcheck, fuzzcheck-asan, and fuzzcheck-ubsan on that database.
#
# FUZZDB can be a glob pattern of two or more databases. Example:
#
#     FUZZDB=test/fuzzdata*.db make run-fuzzcheck
#
run-fuzzcheck:	fuzzcheck$(TEXE) fuzzcheck-asan$(TEXE) fuzzcheck-ubsan$(TEXE)
	@if test "$(FUZZDB)" = ""; then echo 'ERROR: No FUZZDB specified. Rerun with FUZZDB=filename'; exit 1; fi
	./fuzzcheck$(TEXE) --spinner $(FUZZDB)
	./fuzzcheck-asan$(TEXE) --spinner $(FUZZDB)
	./fuzzcheck-ubsan$(TEXE) --spinner $(FUZZDB)

ossshell$(TEXE):	$(TOP)/test/ossfuzz.c $(TOP)/test/ossshell.c sqlite3.c sqlite3.h
	$(TLINK) -o $@ $(FUZZCHECK_OPT) $(TOP)/test/ossshell.c \
             $(TOP)/test/ossfuzz.c sqlite3.c $(LDFLAGS_libsqlite3)
fuzzy: ossshell$(TEXE)

sessionfuzz$(TEXE):	$(TOP)/test/sessionfuzz.c sqlite3.c sqlite3.h
	$(TLINK) -o $@ $(TOP)/test/sessionfuzz.c $(LDFLAGS_libsqlite3)
fuzzy: sessionfuzz$(TEXE)

dbfuzz$(TEXE):	$(TOP)/test/dbfuzz.c sqlite3.c sqlite3.h
	$(TLINK) -o $@ $(DBFUZZ_OPT) $(TOP)/test/dbfuzz.c sqlite3.c $(LDFLAGS_libsqlite3)
fuzzy: dbfuzz$(TEXE)

DBFUZZ2_OPTS = \
  -USQLITE_THREADSAFE \
  -DSQLITE_THREADSAFE=0 \
  -DSQLITE_OMIT_LOAD_EXTENSION \
  -DSQLITE_DEBUG \
  -DSQLITE_ENABLE_DBSTAT_VTAB \
  -DSQLITE_ENABLE_BYTECODE_VTAB \
  -DSQLITE_ENABLE_RTREE \
  -DSQLITE_ENABLE_FTS4 \
  -DSQLITE_ENABLE_FTS5

dbfuzz2$(TEXE):	$(TOP)/test/dbfuzz2.c sqlite3.c sqlite3.h
	$(CC) $(OPT_FEATURE_FLAGS) $(OPTS) -I. -g -O0 \
		-DSTANDALONE -o dbfuzz2 \
		$(DBFUZZ2_OPTS) $(TOP)/test/dbfuzz2.c sqlite3.c $(LDFLAGS_libsqlite3)
	mkdir -p dbfuzz2-dir
	cp $(TOP)/test/dbfuzz2-seed* dbfuzz2-dir
fuzzy: dbfuzz2$(TEXE)

mptester$(TEXE):	$(libsqlite3.LIB) $(TOP)/mptest/mptest.c
	$(TLINK) -o $@ -I. $(TOP)/mptest/mptest.c $(libsqlite3.LIB) \
		$(LDFLAGS_libsqlite3)

MPTEST1=./mptester$(TEXE) mptest.db $(TOP)/mptest/crash01.test --repeat 20
MPTEST2=./mptester$(TEXE) mptest.db $(TOP)/mptest/multiwrite01.test --repeat 20
mptest:	mptester$(TEXE)
	rm -f mptest.db
	$(MPTEST1) --journalmode DELETE
	$(MPTEST2) --journalmode WAL
	$(MPTEST1) --journalmode WAL
	$(MPTEST2) --journalmode PERSIST
	$(MPTEST1) --journalmode PERSIST
	$(MPTEST2) --journalmode TRUNCATE
	$(MPTEST1) --journalmode TRUNCATE
	$(MPTEST2) --journalmode DELETE

# Source and header files that shell.c depends on
SHELL_DEP = \
    $(TOP)/src/shell.c.in \
    $(TOP)/ext/consio/console_io.c \
    $(TOP)/ext/consio/console_io.h \
    $(TOP)/ext/expert/sqlite3expert.c \
    $(TOP)/ext/expert/sqlite3expert.h \
    $(TOP)/ext/intck/sqlite3intck.c \
    $(TOP)/ext/intck/sqlite3intck.h \
    $(TOP)/ext/misc/appendvfs.c \
    $(TOP)/ext/misc/base64.c \
    $(TOP)/ext/misc/base85.c \
    $(TOP)/ext/misc/completion.c \
    $(TOP)/ext/misc/decimal.c \
    $(TOP)/ext/misc/fileio.c \
    $(TOP)/ext/misc/ieee754.c \
    $(TOP)/ext/misc/memtrace.c \
    $(TOP)/ext/misc/pcachetrace.c \
    $(TOP)/ext/misc/percentile.c \
    $(TOP)/ext/misc/regexp.c \
    $(TOP)/ext/misc/series.c \
    $(TOP)/ext/misc/sha1.c \
    $(TOP)/ext/misc/shathree.c \
    $(TOP)/ext/misc/sqlar.c \
    $(TOP)/ext/misc/uint.c \
    $(TOP)/ext/misc/vfstrace.c \
    $(TOP)/ext/misc/zipfile.c \
    $(TOP)/ext/recover/dbdata.c \
    $(TOP)/ext/recover/sqlite3recover.c \
    $(TOP)/ext/recover/sqlite3recover.h \
    $(TOP)/src/test_windirent.c \
    $(TOP)/src/test_windirent.h

shell.c:	$(SHELL_DEP) $(TOP)/tool/mkshellc.tcl $(BTCLSH) # has_tclsh84
	$(BTCLSH) $(TOP)/tool/mkshellc.tcl >shell.c


# Rules to build the extension objects.
#
icu.o:	$(TOP)/ext/icu/icu.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/icu/icu.c

fts3.o:	$(TOP)/ext/fts3/fts3.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3.c

fts3_aux.o:	$(TOP)/ext/fts3/fts3_aux.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_aux.c

fts3_expr.o:	$(TOP)/ext/fts3/fts3_expr.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_expr.c

fts3_hash.o:	$(TOP)/ext/fts3/fts3_hash.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_hash.c

fts3_icu.o:	$(TOP)/ext/fts3/fts3_icu.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_icu.c

fts3_porter.o:	$(TOP)/ext/fts3/fts3_porter.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_porter.c

fts3_snippet.o:	$(TOP)/ext/fts3/fts3_snippet.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_snippet.c

fts3_tokenizer.o:	$(TOP)/ext/fts3/fts3_tokenizer.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_tokenizer.c

fts3_tokenizer1.o:	$(TOP)/ext/fts3/fts3_tokenizer1.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_tokenizer1.c

fts3_tokenize_vtab.o:	$(TOP)/ext/fts3/fts3_tokenize_vtab.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_tokenize_vtab.c

fts3_unicode.o:	$(TOP)/ext/fts3/fts3_unicode.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_unicode.c

fts3_unicode2.o:	$(TOP)/ext/fts3/fts3_unicode2.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_unicode2.c

fts3_write.o:	$(TOP)/ext/fts3/fts3_write.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/fts3/fts3_write.c

rtree.o:	$(TOP)/ext/rtree/rtree.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/rtree/rtree.c

userauth.o:	$(TOP)/ext/userauth/userauth.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/userauth/userauth.c

sqlite3session.o:	$(TOP)/ext/session/sqlite3session.c $(HDR) $(EXTHDR)
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/session/sqlite3session.c

stmt.o:	$(TOP)/ext/misc/stmt.c
	$(TCOMPILE) -DSQLITE_CORE -c $(TOP)/ext/misc/stmt.c

#
# tool/version-info: a utility for emitting sqlite3 version info
# in various forms.
#
version-info$(TEXE):	$(TOP)/tool/version-info.c Makefile sqlite3.h
	$(TLINK) $(ST_OPT) -o $@ $(TOP)/tool/version-info.c

#
# Windows section
#
dll: sqlite3.dll
sqlite3.def: $(LIBOBJ)
	echo 'EXPORTS' >sqlite3.def
	nm $(LIBOBJ) | grep ' T ' | grep ' _sqlite3_' \
		| sed 's/^.* _//' >>sqlite3.def

sqlite3.dll: $(LIBOBJ) sqlite3.def
	$(TCCX) $(LDFLAGS_SHOBJ) -o $@ sqlite3.def \
		-Wl,"--strip-all" $(LIBOBJ)


# Remove build products sufficient so that subsequent makes will recompile
# everything from scratch.  Do not remove:
#
#   *   test results and test logs
#   *   output from ./configure
#
tidy:
	rm -f *.o *.c *.da *.bb *.bbg gmon.* *.rws sqlite3$(TEXE)
	rm -f fts5.h keywordhash.h opcodes.h sqlite3.h sqlite3ext.h sqlite3session.h
	rm -rf .libs .deps tsrc .target_source
	rm -f lemon$(BEXE) sqlite*.tar.gz
	rm -f mkkeywordhash$(BEXE) mksourceid$(BEXE)
	rm -f parse.* fts5parse.*
	rm -f $(libsqlite3.SO) $(libsqlite3.LIB)
	rm -f tclsqlite3$(TEXE) $(TESTPROGS)
	rm -f LogEst$(TEXE) fts3view$(TEXE) rollback-test$(TEXE) showdb$(TEXE)
	rm -f showjournal$(TEXE) showstat4$(TEXE) showwal$(TEXE) speedtest1$(TEXE)
	rm -f wordcount$(TEXE) changeset$(TEXE) version-info$(TEXE)
	rm -f *.dll *.lib *.exp *.pc *.vsix *.so *.dylib pkgIndex.tcl
	rm -f sqlite3_analyzer$(TEXE) sqlite3_rsync$(TEXE) sqlite3_expert$(TEXE)
	rm -f mptester$(TEXE) rbu$(TEXE)	srcck1$(TEXE)
	rm -f fuzzershell$(TEXE) fuzzcheck$(TEXE) sqldiff$(TEXE) dbhash$(TEXE)
	rm -f dbfuzz$(TEXE) dbfuzz2$(TEXE) dbfuzz2-asan$(TEXE) dbfuzz2-msan$(TEXE)
	rm -f fuzzcheck-asan$(TEXE) fuzzcheck-ubsan$(TEXE) ossshell$(TEXE)
	rm -f sessionfuzz$(TEXE)
	rm -f threadtest5$(TEXE)
	rm -f src-verify$(BEXE) has_tclsh* has_tclconfig
	rm -f tclsqlite3.c
	rm -f sqlite3rc.h sqlite3.def

#
# Removes build products and test logs.  Retains ./configure outputs.
#
clean:	tidy
	rm -rf omittest* testrunner* testdir*
	-gmake -C ext/wasm distclean 2>/dev/null; true

# Clean up everything.  No exceptions.
distclean:	clean
	rm -f sqlite_cfg.h config.log config.status $(JIMSH) Makefile
	rm -f $(TOP)/tool/emcc.sh
	-gmake -C ext/wasm distclean 2>/dev/null; true
