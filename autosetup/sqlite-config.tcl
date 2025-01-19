# This file holds functions for autosetup which are specific to the
# sqlite build tree.  They are in this file, instead of auto.def, so
# that they can be reused in the TEA sub-tree. This file requires
# functions from proj.tcl.

use cc cc-db cc-shared cc-lib pkg-config proj

# Are we cross-compiling? This value may be changed by certain build
# options, so it's important that config code which checks for
# cross-compilation uses this var instead of
# [proj-is-cross-compiling].
set ::sqliteIsCrossCompiling [proj-is-cross-compiling]


########################################################################
# Runs some common initialization which must happen immediately after
# autosetup's [options] function is called.
proc sqlite-post-options-init {} {
  #
  # Carry values from hidden --flag aliases over to their canonical
  # flag forms. This list must include only options which are common
  # to both the top-level auto.def and autoconf/auto.def.
  #
  proj-xfer-options-aliases {
    with-readline-inc => with-readline-cflags
    with-readline-lib => with-readline-ldflags
    with-debug => debug
  }
  sqlite-autoreconfig
}

########################################################################
# Sets up the SQLITE_AUTORECONFIG define.
proc sqlite-autoreconfig {} {
  #
  # SQLITE_AUTORECONFIG contains make target rules for re-running the
  # configure script with the same arguments it was initially invoked
  # with. This can be used to automatically reconfigure
  #
  proc squote {arg} {
    # Wrap $arg in single-quotes if it looks like it might need that
    # to avoid mis-handling as a shell argument. We assume that $arg
    # will never contain any single-quote characters.
    if {[string match {*[ &;$*"]*} $arg]} { return '$arg' }
    return $arg
  }
  define-append SQLITE_AUTORECONFIG cd [squote $::autosetup(builddir)] && [squote $::autosetup(srcdir)/configure]
  #{*}$::autosetup(argv) breaks with --flag='val with spaces', so...
  foreach arg $::autosetup(argv) {
    define-append SQLITE_AUTORECONFIG [squote $arg]
  }
  rename squote ""
}

define OPT_FEATURE_FLAGS {} ; # -DSQLITE_OMIT/ENABLE flags.
define OPT_SHELL {}         ; # Feature-related CFLAGS for the sqlite3 CLI app
########################################################################
# Adds $args, if not empty, to OPT_FEATURE_FLAGS.  If the first arg is
# -shell then it strips that arg and passes the remaining args the
# sqlite-add-shell-opt in addition to adding them to
# OPT_FEATURE_FLAGS.
proc sqlite-add-feature-flag {args} {
  set shell ""
  if {"-shell" eq [lindex $args 0]} {
    set args [lassign $args shell]
  }
  if {"" ne $args} {
    if {"" ne $shell} {
      sqlite-add-shell-opt {*}$args
    }
    define-append OPT_FEATURE_FLAGS {*}$args
  }
}
# Appends $args, if not empty, to OPT_SHELL.
proc sqlite-add-shell-opt {args} {
  if {"" ne $args} {
    define-append OPT_SHELL {*}$args
  }
}

#########################################################################
# Show the final feature flag sets.
proc sqlite-show-feature-flags {} {
  set oFF [get-define OPT_FEATURE_FLAGS]
  if {"" ne $oFF} {
    define OPT_FEATURE_FLAGS [lsort -unique $oFF]
    msg-result "Library feature flags: [get-define OPT_FEATURE_FLAGS]"
  }
  set oFF [get-define OPT_SHELL]
  if {"" ne $oFF} {
    define OPT_SHELL [lsort -unique $oFF]
    msg-result "Shell options: [get-define OPT_SHELL]"
  }
}

########################################################################
# Checks for the --debug flag, defining SQLITE_DEBUG to 1 if it is
# true.  TARGET_DEBUG gets defined either way, with content depending
# on whether --debug is true or false.
proc sqlite-check-debug {} {
  msg-checking "SQLITE_DEBUG build? "
  proj-if-opt-truthy debug {
    define SQLITE_DEBUG 1
    define TARGET_DEBUG {-g -DSQLITE_DEBUG=1 -DSQLITE_ENABLE_SELECTTRACE -DSQLITE_ENABLE_WHERETRACE -O0 -Wall}
    proj-opt-set memsys5
    msg-result yes
  } {
    define TARGET_DEBUG {-DNDEBUG}
    msg-result no
  }
}

########################################################################
# "soname" for libsqlite3.so. See discussion at:
# https://sqlite.org/src/forumpost/5a3b44f510df8ded
proc sqlite-check-soname {} {
  define LDFLAGS_LIBSQLITE3_SONAME ""
  if {[proj-opt-was-provided soname]} {
    set soname [join [opt-val soname] ""]
  } else {
    # Enabling soname breaks linking for the --dynlink-tools feature,
    # and this project has no direct use for soname, so default to
    # none. Package maintainers, on the other hand, like to have an
    # soname.
    set soname none
  }
  switch -exact -- $soname {
    none - "" { return 0 }
    legacy    { set soname libsqlite3.so.0 }
    default {
      if {[string match libsqlite3.* $soname]} {
        # use it as-is
      } else {
        # Assume it's a suffix
        set soname "libsqlite3.so.${soname}"
      }
    }
  }
  # msg-debug "soname=$soname"
  if {[proj-check-soname $soname]} {
    define LDFLAGS_LIBSQLITE3_SONAME [get-define LDFLAGS_SONAME_PREFIX]$soname
    msg-result "Setting SONAME using: [get-define LDFLAGS_LIBSQLITE3_SONAME]"
  } elseif {[proj-opt-was-provided soname]} {
    # --soname was explicitly requested but not available, so fail fatally
    proj-fatal "This environment does not support SONAME."
  } else {
    # --soname was not explicitly requested but not available, so just warn
    msg-result "This environment does not support SONAME."
  }
}

########################################################################
# If --enable-thresafe is set, this adds -DSQLITE_THREADSAFE=1 to
# OPT_FEATURE_FLAGS and sets LDFLAGS_PTHREAD to the linker flags
# needed for linking pthread. If --enable-threadsafe is not set, adds
# -DSQLITE_THREADSAFE=0 to OPT_FEATURE_FLAGS and sets LDFLAGS_PTHREAD
# to an empty string.
proc sqlite-check-threadsafe {} {
  msg-checking "Support threadsafe operation? "
  proj-if-opt-truthy threadsafe {
    msg-result yes
    sqlite-add-feature-flag -DSQLITE_THREADSAFE=1
    if {![proj-check-function-in-lib pthread_create pthread]
        || ![proj-check-function-in-lib pthread_mutexattr_init pthread]} {
      user-error "Missing required pthread bits"
    }
    define LDFLAGS_PTHREAD [get-define lib_pthread_create]
    undefine lib_pthread_create
    # Recall that LDFLAGS_PTHREAD might be empty even if pthreads if
    # found because it's in -lc on some platforms.
  } {
    msg-result no
    sqlite-add-feature-flag -DSQLITE_THREADSAFE=0
    define LDFLAGS_PTHREAD ""
  }
}

########################################################################
# sqlite-check-line-editing jumps through proverbial hoops to try to
# find a working line-editing library, setting:
#
#   - HAVE_READLINE to 0 or 1
#   - HAVE_LINENOISE to 0, 1, or 2
#   - HAVE_EDITLINE to 0 or 1
#
# Only one of ^^^ those will be set to non-0.
#
#   - LDFLAGS_READLINE = linker flags or empty string
#
#   - CFLAGS_READLINE = compilation flags for clients or empty string.
#
# Note that LDFLAGS_READLINE and CFLAGS_READLINE may refer to
# linenoise or editline, not necessarily libreadline.  In some cases
# it will set HAVE_READLINE=1 when it's really using editline, for
# reasons described in this function's comments.
#
# Returns a string describing which line-editing approach to use, or
# "none" if no option is available.
#
# Order of checks:
#
#  1) --with-linenoise trumps all others and skips all of the
#     complexities involved with the remaining options.
#
#  2) --editline trumps --readline
#
#  3) --disable-readline trumps --readline
#
#  4) Default to automatic search for optional readline
#
#  5) Try to find readline or editline. If it's not found AND the
#     corresponding --FEATURE flag was explicitly given, fail fatally,
#     else fail silently.
proc sqlite-check-line-editing {} {
  rename sqlite-check-line-editing ""
  msg-result "Checking for line-editing capability..."
  define HAVE_READLINE 0
  define HAVE_LINENOISE 0
  define HAVE_EDITLINE 0
  define LDFLAGS_READLINE ""
  define CFLAGS_READLINE ""
  set failIfNotFound 0 ; # Gets set to 1 for explicit --FEATURE requests
                         # so that we know whether to fail fatally or not
                         # if the library is not found.
  set libsForReadline {readline edit} ; # -l<LIB> names to check for readline().
                                        # The libedit check changes this.
  set editLibName "readline" ; # "readline" or "editline"
  set editLibDef "HAVE_READLINE" ; # "HAVE_READLINE" or "HAVE_EDITLINE"
  set dirLn [opt-val with-linenoise]
  if {"" ne $dirLn} {
    # Use linenoise from a copy of its sources (not a library)...
    if {![file isdir $dirLn]} {
      proj-fatal "--with-linenoise value is not a directory"
    }
    set lnH $dirLn/linenoise.h
    if {![file exists $lnH] } {
      proj-fatal "Cannot find linenoise.h in $dirLn"
    }
    set lnC ""
    set lnCOpts {linenoise-ship.c linenoise.c}
    foreach f $lnCOpts {
      if {[file exists $dirLn/$f]} {
        set lnC $dirLn/$f
        break;
      }
    }
    if {"" eq $lnC} {
      proj-fatal "Cannot find any of $lnCOpts in $dirLn"
    }
    set flavor ""
    set lnVal [proj-which-linenoise $lnH]
    switch -- $lnVal {
      1 { set flavor "antirez" }
      2 { set flavor "msteveb" }
      default {
        proj-fatal "Cannot determine the flavor of linenoise from $lnH"
      }
    }
    define CFLAGS_READLINE "-I$dirLn $lnC"
    define HAVE_LINENOISE $lnVal
    sqlite-add-shell-opt -DHAVE_LINENOISE=$lnVal
    if {[info exists sqliteUseJimForCodeGen]
        && $::sqliteUseJimForCodeGen && 2 == $lnVal} {
      define-append CFLAGS_JIMSH -DUSE_LINENOISE [get-define CFLAGS_READLINE]
      user-notice "Adding linenoise support to jimsh."
    }
    return "linenoise ($flavor)"
  } elseif {[opt-bool editline]} {
    # libedit mimics libreadline and on some systems does not have its
    # own header installed (instead, that of libreadline is used).
    #
    # shell.c historically expects HAVE_EDITLINE to be set for
    # libedit, but it then expects to see <editline/readline.h>, which
    # some system's don't actually have despite having libedit.  If we
    # end up finding <editline/readline.h> below, we will use
    # -DHAVE_EDITLINE=1, else we will use -DHAVE_READLINE=1. In either
    # case, we will link against libedit.
    set failIfNotFound 1
    set libsForReadline {edit}
    set editLibName editline
  } elseif {![opt-bool readline]} {
    msg-result "Readline support explicitly disabled with --disable-readline"
    return "none"
  } elseif {[proj-opt-was-provided readline]} {
    # If an explicit --[enable-]readline was used, fail if it's not
    # found, else treat the feature as optional.
    set failIfNotFound 1
  }

  # Transform with-readline-header=X to with-readline-cflags=-I...
  set v [opt-val with-readline-header]
  proj-opt-set with-readline-header ""
  if {"" ne $v} {
    if {"auto" eq $v} {
      proj-opt-set with-readline-cflags auto
    } else {
      set v [file dirname $v]
      if {[string match */readline $v]} {
        # Special case: if the path includes .../readline/readline.h,
        # set the -I to one dir up from that because our sources
        # #include <readline/readline.h> or <editline/readline.h>.
        set v [file dirname $v]
      }
      proj-opt-set with-readline-cflags "-I$v"
    }
  }

  # Look for readline.h
  set rlInc [opt-val with-readline-cflags auto]
  if {"auto" eq $rlInc} {
    set rlInc ""
    if {$::sqliteIsCrossCompiling} {
      # ^^^ this check is derived from the legacy configure script.
      proj-warn "Skipping check for readline.h because we're cross-compiling."
    } else {
      set dirs "[get-define prefix] /usr /usr/local /usr/local/readline /usr/contrib /mingw"
      set subdirs "include/$editLibName"
      if {"editline" eq $editLibName} {
        lappend subdirs include/readline
        # ^^^ editline, on some systems, does not have its own header,
        # and uses libreadline's header.
      }
      lappend subdirs include
      # ^^^ The dirs and subdirs lists are, except for the inclusion
      # of $prefix and editline, from the legacy configure script
      set rlInc [proj-search-for-header-dir readline.h \
                 -dirs $dirs -subdirs $subdirs]
      if {"" ne $rlInc} {
        if {[string match */readline $rlInc]} {
          set rlInc [file dirname $rlInc]; # shell #include's <readline/readline.h>
        } elseif {[string match */editline $rlInc]} {
          set editLibDef HAVE_EDITLINE
          set rlInc [file dirname $rlInc]; # shell #include's <editline/readline.h>
        }
        set rlInc "-I${rlInc}"
      }
    }
  } elseif {"" ne $rlInc && ![string match *-I* $rlInc]} {
    proj-fatal "Argument to --with-readline-cflags is intended to be CFLAGS and contain -I..."
  }

  # If readline.h was found/specified, look for lib(readline|edit)...
  #
  # This is not quite straightforward because both libreadline and
  # libedit typically require some other library which (according to
  # legacy autotools-generated tests) provides tgetent(3). On some
  # systems that's built into libreadline/edit, on some (most?) its in
  # lib[n]curses, and on some it's in libtermcap.
  set rlLib ""
  if {"" ne $rlInc} {
    set rlLib [opt-val with-readline-ldflags]
    if {"" eq $rlLib || "auto" eq $rlLib} {
      set rlLib ""
      set libTerm ""
      if {[proj-check-function-in-lib tgetent "$editLibName ncurses curses termcap"]} {
        # ^^^ that libs list comes from the legacy configure script ^^^
        set libTerm [get-define lib_tgetent]
        undefine lib_tgetent
      }
      if {$editLibName eq $libTerm} {
        set rlLib $libTerm
      } elseif {[proj-check-function-in-lib readline $libsForReadline $libTerm]} {
        set rlLib [get-define lib_readline]
        lappend rlLib $libTerm
        undefine lib_readline
      }
    }
  }

  # If we found a library, configure the build to use it...
  if {"" ne $rlLib} {
    if {"editline" eq $editLibName && "HAVE_READLINE" eq $editLibDef} {
      # Alert the user that, despite outward appearances, we won't be
      # linking to the GPL'd libreadline. Presumably that distinction is
      # significant for those using --editline.
      proj-indented-notice {
        NOTE: the local libedit but uses <readline/readline.h> so we
        will compile with -DHAVE_READLINE=1 but will link with
        libedit.
      }
    }
    set rlLib [join $rlLib]
    set rlInc [join $rlInc]
    define LDFLAGS_READLINE $rlLib
    define CFLAGS_READLINE $rlInc
    proj-assert {$editLibDef in {HAVE_READLINE HAVE_EDITLINE}}
    proj-assert {$editLibName in {readline editline}}
    sqlite-add-shell-opt -D${editLibDef}=1
    msg-result "Using $editLibName flags: $rlInc $rlLib"
    # Check whether rl_completion_matches() has a signature we can use
    # and disable that sub-feature if it doesn't.
    if {![cctest \
            -cflags "$rlInc -D${editLibDef}" -libs $rlLib -nooutput 1 -source {
             #include <stdio.h>
             #ifdef HAVE_EDITLINE
             #include <editline/readline.h>
             #else
             #include <readline/readline.h>
             #endif
             static char * rcg(const char *z, int i){(void)z; (void)i; return 0;}
             int main(void) {
               char ** x = rl_completion_matches("one", rcg);
               (void)x;
               return 0;
             }
           }]} {
      proj-warn "readline-style completion disabled due to rl_completion_matches() signature mismatch"
      sqlite-add-shell-opt -DSQLITE_OMIT_READLINE_COMPLETION
    }
    return $editLibName
  }

  if {$failIfNotFound} {
    proj-fatal "Explicit --$editLibName failed to find a matching library."
  }
  return "none"
}; # sqlite-check-line-editing

########################################################################
# ICU - International Components for Unicode
#
# Handles these flags:
#
#  --with-icu-ldflags=LDFLAGS
#  --with-icu-cflags=CFLAGS
#  --with-icu-config[=auto | pkg-config | /path/to/icu-config]
#  --enable-icu-collations
#
# --with-icu-config values:
#
#   - auto: use the first one of (pkg-config, icu-config) found on the
#     system.
#   - pkg-config: use only pkg-config to determine flags
#   - /path/to/icu-config: use that to determine flags
#
# If --with-icu-config is used as neither pkg-config nor icu-config
# are found, fail fatally.
#
# If both --with-icu-ldflags and --with-icu-config are provided, they
# are cumulative.  If neither are provided, icu-collations is not
# honored and a warning is emitted if it is provided.
#
# Design note: though we could automatically enable ICU if the
# icu-config binary or (pkg-config icu-io) are found, we specifically
# do not. ICU is always an opt-in feature.
proc sqlite-check-icu {} {
  rename sqlite-check-icu ""
  define LDFLAGS_ICU [join [opt-val with-icu-ldflags ""]]
  define CFLAGS_ICU [join [opt-val with-icu-cflags ""]]
  if {[proj-opt-was-provided with-icu-config]} {
    set icuConfigBin [opt-val with-icu-config]
    set tryIcuConfigBin 1; # set to 0 if we end up using pkg-config
    if {"auto" eq $icuConfigBin || "pkg-config" eq $icuConfigBin} {
      if {[pkg-config-init 0] && [pkg-config icu-io]} {
        # Maintenance reminder: historical docs say to use both of
        # (icu-io, icu-uc). icu-uc lacks a required lib and icu-io has
        # all of them on tested OSes.
        set tryIcuConfigBin 0
        define LDFLAGS_ICU [get-define PKG_ICU_IO_LDFLAGS]
        define-append LDFLAGS_ICU [get-define PKG_ICU_IO_LIBS]
        define CFLAGS_ICU [get-define PKG_ICU_IO_CFLAGS]
      } elseif {"pkg-config" eq $icuConfigBin} {
        proj-fatal "pkg-config cannot find package icu-io"
      } else {
        proj-assert {"auto" eq $icuConfigBin}
      }
    }
    if {$tryIcuConfigBin} {
      if {"auto" eq $icuConfigBin} {
        set icuConfigBin [proj-first-bin-of \
                            /usr/local/bin/icu-config \
                            /usr/bin/icu-config]
        if {"" eq $icuConfigBin} {
          proj-fatal "--with-icu-config=auto cannot find (pkg-config icu-io) or icu-config binary"
        }
      }
      if {[file-isexec $icuConfigBin]} {
        set x [exec $icuConfigBin --ldflags]
        if {"" eq $x} {
          proj-fatal "$icuConfigBin --ldflags returned no data"
        }
        define-append LDFLAGS_ICU $x
        set x [exec $icuConfigBin --cppflags]
        define-append CFLAGS_ICU $x
      } else {
        proj-fatal "--with-icu-config=$bin does not refer to an executable"
      }
    }
  }
  set ldflags [define LDFLAGS_ICU [string trim [get-define LDFLAGS_ICU]]]
  set cflags [define CFLAGS_ICU [string trim [get-define CFLAGS_ICU]]]
  if {"" ne $ldflags} {
    sqlite-add-feature-flag -shell -DSQLITE_ENABLE_ICU
    msg-result "Enabling ICU support with flags: $ldflags $cflags"
    if {[opt-bool icu-collations]} {
      msg-result "Enabling ICU collations."
      sqlite-add-feature-flag -shell -DSQLITE_ENABLE_ICU_COLLATIONS
      # Recall that shell.c builds with sqlite3.c
    }
  } elseif {[opt-bool icu-collations]} {
    proj-warn "ignoring --enable-icu-collations because neither --with-icu-ldflags nor --with-icu-config provided any linker flags"
  } else {
    msg-result "ICU support is disabled."
  }
}; # sqlite-check-icu
