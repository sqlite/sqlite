########################################################################
# 2024 September 25
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#  * May you do good and not evil.
#  * May you find forgiveness for yourself and forgive others.
#  * May you share freely, never taking more than you give.
#
########################################################################
# Routines for Steve Bennett's autosetup which are common to trees
# managed in and around the umbrella of the SQLite project.
#
# Routines with a suffix of _ are intended for internal use,
# within this file, and are not part of the API which auto.def files
# should rely on.
#
# This file was initially derived from one used in the libfossil
# project, authored by the same person who ported it here, noted here
# only as an indication that there are no licensing issue despite this
# code having at least two near-twins running around in other trees.
#
########################################################################
#
# Design notes: by and large, autosetup prefers to update global state
# with the results of feature checks, e.g. whether the compiler
# supports flag --X.  In this developer's opinion that (A) causes more
# confusion than it solves[^1] and (B) adds an unnecessary layer of
# "voodoo" between the autosetup user and its internals. This module,
# in contrast, instead injects the results of its own tests into
# well-defined variables and leaves the integration of those values to
# the caller's discretion.
#
# [1]: As an example: testing for the -rpath flag, using
# cc-check-flags, can break later checks which use
# [cc-check-function-in-lib ...] because the resulting -rpath flag
# implicitly becomes part of those tests. In the case of an rpath
# test, downstream tests may not like the $prefix/lib path added by
# the rpath test. To avoid such problems, we avoid (intentionally)
# updating global state via feature tests.
########################################################################

########################################################################
# $hwaci_ is an internal-use-only array for storing whatever generic
# internal stuff we need stored.
array set hwaci_ {}

proc hwaci-warn {msg} {
  puts stderr "WARNING: $msg"
}
proc hwaci-notice {msg} {
  puts stderr "NOTICE: $msg"
}
proc hwaci-fatal {msg} {
  user-error "ERROR: $msg"
}

########################################################################
# hwaci-lshift_ shifts $count elements from the list named $listVar
# and returns them as a new list. On empty input, returns "".
#
# Modified slightly from: https://wiki.tcl-lang.org/page/lshift
proc hwaci-lshift_ {listVar {count 1}} {
  upvar 1 $listVar l
  if {![info exists l]} {
    # make the error message show the real variable name
    error "can't read \"$listVar\": no such variable"
  }
  if {![llength $l]} {
    # error Empty
    return ""
  }
  set r [lrange $l 0 [incr count -1]]
  set l [lreplace $l [set l 0] $count]
  return $r
}

########################################################################
# A proxy for cc-check-function-in-lib which "undoes" any changes that
# routine makes to the LIBS define. Returns the result of
# cc-check-function-in-lib.
proc hwaci-check-function-in-lib {function libs {otherlibs {}}} {
  set found 0
  define-push {LIBS} {
    set found [cc-check-function-in-lib $function $libs $otherlibs]
  }
  return $found
}

########################################################################
# If $v is true, [puts $msg] is called, else puts is not called.
#proc hwaci-maybe-verbose {v msg} {
#  if {$v} {
#    puts $msg
#  }
#}

########################################################################
# Usage: hwaci-find-executable-path ?-v? binaryName
#
# Works similarly to autosetup's [find-executable-path $binName] but:
#
# - If the first arg is -v, it's verbose about searching, else it's quiet.
#
# Returns the full path to the result or an empty string.
proc hwaci-find-executable-path {args} {
  set binName $args
  set verbose 0
  if {[lindex $args 0] eq "-v"} {
    set verbose 1
    set binName [lrange $args 1 end]
    msg-checking "Looking for $binName ... "
  }
  set check [find-executable-path $binName]
  if {$verbose} {
    if {"" eq $check} {
      msg-result "not found"
    } else {
      msg-result $check
    }
  }
  return $check
}

########################################################################
# Uses [hwaci-find-executable-path $binName] to (verbosely) search for
# a binary, sets a define (see below) to the result, and returns the
# result (an empty string if not found).
#
# The define'd name is: if defName is empty then "BIN_X" is used,
# where X is the upper-case form of $binName with any '-' characters
# replaced with '_'.
proc hwaci-bin-define {binName {defName {}}} {
  set check [hwaci-find-executable-path -v $binName]
  if {"" eq $defName} {
    set defName "BIN_[string toupper [string map {- _} $binName]]"
  }
  define $defName $check
  return $check
}

########################################################################
# Usage: hwaci-first-bin-of bin...
#
# Looks for the first binary found of the names passed to this
# function.  If a match is found, the full path to that binary is
# returned, else "" is returned.
#
# Despite using cc-path-progs to do the search, this function clears
# any define'd name that function stores for the result (because the
# caller has no sensible way of knowing which result it was unless
# they pass only a single argument).
proc hwaci-first-bin-of {args} {
  foreach b $args {
    if {[cc-path-progs $b]} {
      set u [string toupper $b]
      set x [get-define $u]
      undefine $u
      return $x
    }
  }
  return ""
}

########################################################################
# Looks for `bash` binary and dies if not found. On success, defines
# BIN_BASH to the full path to bash and returns that value.
#
# TODO: move this out of this file and back into the 1 or 2 downstream
# trees which use it.
proc hwaci-require-bash {} {
  set bash [hwaci-bin-define bash]
  if {"" eq $bash} {
    user-error "Cannot find required bash shell"
  }
  return $bash
}

########################################################################
# Force-set autosetup option $flag to $val. The value can be fetched
# later with [opt-val], [opt-bool], and friends.
proc hwaci-opt-set {flag {val 1}} {
  global autosetup
  if {$flag ni $::autosetup(options)} {
    # We have to add this to autosetup(options) or else future calls
    # to [opt-bool $flag] will fail validation of $flag.
    lappend ::autosetup(options) $flag
  }
  dict set ::autosetup(optset) $flag $val
}

########################################################################
# Returns 1 if $val appears to be a truthy value, else returns
# 0. Truthy values are any of {1 on enabled yes}
proc hwaci-val-truthy {val} {
  expr {$val in {1 on enabled yes}}
}

########################################################################
# Returns 1 if [opt-val $flag] appears to be a truthy value or
# [opt-bool $flag] is true. See hwaci-val-truthy.
proc hwaci-opt-truthy {flag} {
  if {[hwaci-val-truthy [opt-val $flag]]} { return 1 }
  set rc 0
  catch {
    # opt-bool will throw if $flag is not a known boolean flag
    set rc [opt-bool $flag]
  }
  return $rc
}

########################################################################
# If [hwaci-opt-truthy $flag] is true, eval $then, else eval $else.
proc hwaci-if-opt-truthy {boolFlag thenScript {elseScript {}}} {
  if {[hwaci-opt-truthy $boolFlag]} {
    uplevel 1 $thenScript
  } else {
    uplevel 1 $elseScript
  }
}

########################################################################
# If [hwaci-opt-truthy $flag] then [define $def $iftrue] else [define
# $def $iffalse]. If $msg is not empty, output [msg-checking $msg] and
# a [msg-results ...] which corresponds to the result. Returns 1 if
# the opt-truthy check passes, else 0.
proc hwaci-define-if-opt-truthy {flag def {msg ""} {iftrue 1} {iffalse 0}} {
  if {"" ne $msg} {
    msg-checking "$msg "
  }
  set rcMsg ""
  set rc 0
  if {[hwaci-opt-truthy $flag]} {
    define $def $iftrue
    set rc 1
  } else {
    define $def $iffalse
  }
  switch -- [hwaci-val-truthy [get-define $def]] {
    0 { set rcMsg no }
    1 { set rcMsg yes }
  }
  if {"" ne $msg} {
    msg-result $rcMsg
  }
  return $rc
}

########################################################################
# Args: [-v] optName defName {descr {}}
#
# Checks [hwaci-opt-truthy $optName] and calls [define $defName X]
# where X is 0 for false and 1 for true. descr is an optional
# [msg-checking] argument which defaults to $defName. Returns X.
#
# If args[0] is -v then the boolean semantics are inverted: if
# the option is set, it gets define'd to 0, else 1. Returns the
# define'd value.
proc hwaci-opt-define-bool {args} {
  set invert 0
  if {[lindex $args 0] eq "-v"} {
    set invert 1
    set args [lrange $args 1 end]
  }
  set optName [hwaci-lshift_ args]
  set defName [hwaci-lshift_ args]
  set descr [hwaci-lshift_ args]
  if {"" eq $descr} {
    set descr $defName
  }
  set rc 0
  msg-checking "$descr ... "
  if {[hwaci-opt-truthy $optName]} {
    if {0 eq $invert} {
      set rc 1
    } else {
      set rc 0
    }
  } elseif {0 ne $invert} {
    set rc 1
  }
  msg-result $rc
  define $defName $rc
  return $rc
}

########################################################################
# Check for module-loading APIs (libdl/libltdl)...
#
# Looks for libltdl or dlopen(), the latter either in -ldl or built in
# to libc (as it is on some platforms). Returns 1 if found, else
# 0. Either way, it `define`'s:
#
#  - HAVE_LIBLTDL to 1 or 0 if libltdl is found/not found
#  - HAVE_LIBDL to 1 or 0 if dlopen() is found/not found
#  - LDFLAGS_MODULE_LOADER one of ("-lltdl", "-ldl", or ""), noting
#    that -ldl may legally be empty on some platforms even if
#    HAVE_LIBDL is true (indicating that dlopen() is available without
#    extra link flags). LDFLAGS_MODULE_LOADER also gets "-rdynamic" appended
#    to it because otherwise trying to open DLLs will result in undefined
#    symbol errors.
#
# Note that if it finds LIBLTDL it does not look for LIBDL, so will
# report only that is has LIBLTDL.
proc hwaci-check-module-loader {} {
  msg-checking "Looking for module-loader APIs... "
  if {99 ne [get-define LDFLAGS_MODULE_LOADER 99]} {
    if {1 eq [get-define HAVE_LIBLTDL 0]} {
      msg-result "(cached) libltdl"
      return 1
    } elseif {1 eq [get-define HAVE_LIBDL 0]} {
      msg-result "(cached) libdl"
      return 1
    }
    # else: wha???
  }
  set HAVE_LIBLTDL 0
  set HAVE_LIBDL 0
  set LDFLAGS_MODULE_LOADER ""
  set rc 0
  puts "" ;# cosmetic kludge for cc-check-XXX
  if {[cc-check-includes ltdl.h] && [cc-check-function-in-lib lt_dlopen ltdl]} {
    set HAVE_LIBLTDL 1
    set LDFLAGS_MODULE_LOADER "-lltdl -rdynamic"
    msg-result " - Got libltdl."
    set rc 1
  } elseif {[cc-with {-includes dlfcn.h} {
    cctest -link 1 -declare "extern char* dlerror(void);" -code "dlerror();"}]} {
    msg-result " - This system can use dlopen() without -ldl."
    set HAVE_LIBDL 1
    set LDFLAGS_MODULE_LOADER ""
    set rc 1
  } elseif {[cc-check-includes dlfcn.h]} {
    set HAVE_LIBDL 1
    set rc 1
    if {[cc-check-function-in-lib dlopen dl]} {
      msg-result " - dlopen() needs libdl."
      set LDFLAGS_MODULE_LOADER "-ldl -rdynamic"
    } else {
      msg-result " - dlopen() not found in libdl. Assuming dlopen() is built-in."
      set LDFLAGS_MODULE_LOADER "-rdynamic"
    }
  }
  define HAVE_LIBLTDL $HAVE_LIBLTDL
  define HAVE_LIBDL $HAVE_LIBDL
  define LDFLAGS_MODULE_LOADER $LDFLAGS_MODULE_LOADER
  return $rc
}

########################################################################
# Sets all flags which would be set by hwaci-check-module-loader to
# empty/falsy values, as if those checks had failed to find a module
# loader. Intended to be called in place of that function when
# a module loader is explicitly not desired.
proc hwaci-no-check-module-loader {} {
  define HAVE_LIBDL 0
  define HAVE_LIBLTDL 0
  define LDFLAGS_MODULE_LOADER ""
}

########################################################################
# Opens the given file, reads all of its content, and returns it.
proc hwaci-file-content {fname} {
  set fp [open $fname r]
  set rc [read $fp]
  close $fp
  return $rc
}

########################################################################
# Returns the contents of the given file as an array of lines, with
# the EOL stripped from each input line.
proc hwaci-file-content-list {fname} {
  set fp [open $fname r]
  set rc {}
  while { [gets $fp line] >= 0 } {
    lappend rc $line
  }
  return $rc
}

########################################################################
# Checks the compiler for compile_commands.json support. If passed an
# argument it is assumed to be the name of an autosetup boolean config
# which controls whether to run/skip this check.
#
# Returns 1 if supported, else 0. Defines MAKE_COMPILATION_DB to "yes"
# if supported, "no" if not.
#
# This test has a long history of false positive results because of
# compilers reacting differently to the -MJ flag.
proc hwaci-check-compile-commands {{configOpt {}}} {
  msg-checking "compile_commands.json support... "
  if {"" ne $configOpt && ![hwaci-opt-truthy $configOpt]} {
    msg-result "explicitly disabled"
    define MAKE_COMPILATION_DB no
    return 0
  } else {
    if {[cctest -lang c -cflags {/dev/null -MJ} -source {}]} {
      # This test reportedly incorrectly succeeds on one of
      # Martin G.'s older systems. drh also reports a false
      # positive on an unspecified older Mac system.
      msg-result "compiler supports compile_commands.json"
      define MAKE_COMPILATION_DB yes
      return 1
    } else {
      msg-result "compiler does not support compile_commands.json"
      define MAKE_COMPILATION_DB no
      return 0
    }
  }
}

########################################################################
# Runs the 'touch' command on one or more files, ignoring any errors.
proc hwaci-touch {filename} {
  catch { exec touch {*}$filename }
}

########################################################################
# Usage:
#
#   hwaci-make-from-dot-in ?-touch? filename(s)...
#
# Uses [make-template] to create makefile(-like) file(s) $filename
# from $filename.in but explicitly makes the output read-only, to
# avoid inadvertent editing (who, me?).
#
# If the first argument is -touch then the generated file is touched
# to update its timestamp. This can be used as a workaround for
# cases where (A) autosetup does not update the file because it was
# not really modified and (B) the file *really* needs to be updated to
# please the build process.
#
# Failures when running chmod or touch are silently ignored.
proc hwaci-make-from-dot-in {args} {
  set filename $args
  set touch 0
  if {[lindex $args 0] eq "-touch"} {
    set touch 1
    set filename [lrange $args 1 end]
  }
  foreach f $filename {
    set f [string trim $f]
    catch { exec chmod u+w $f }
    make-template $f.in $f
    if {$touch} {
      hwaci-touch $f
    }
    catch { exec chmod -w $f }
  }
}

########################################################################
# Checks for the boolean configure option named by $flagname. If set,
# it checks if $CC seems to refer to gcc. If it does (or appears to)
# then it defines CC_PROFILE_FLAG to "-pg" and returns 1, else it
# defines CC_PROFILE_FLAG to "" and returns 0.
#
# Note that the resulting flag must be added to both CFLAGS and
# LDFLAGS in order for binaries to be able to generate "gmon.out".  In
# order to avoid potential problems with escaping, space-containing
# tokens, and interfering with autosetup's use of these vars, this
# routine does not directly modify CFLAGS or LDFLAGS.
proc hwaci-check-profile-flag {{flagname profile}} {
  #puts "flagname=$flagname ?[hwaci-opt-truthy $flagname]?"
  if {[hwaci-opt-truthy $flagname]} {
    set CC [get-define CC]
    regsub {.*ccache *} $CC "" CC
    # ^^^ if CC="ccache gcc" then [exec] treats "ccache gcc" as a
    # single binary name and fails. So strip any leading ccache part
    # for this purpose.
    if { ![catch { exec $CC --version } msg]} {
      if {[string first gcc $CC] != -1} {
        define CC_PROFILE_FLAG "-pg"
        return 1
      }
    }
  }
  define CC_PROFILE_FLAG ""
  return 0
}

########################################################################
# Returns 1 if this appears to be a Windows environment (MinGw,
# Cygwin, MSys), else returns 0. The optional argument is the name of
# an autosetup define which contains platform name info, defaulting to
# "host" (meaning, somewhat counterintuitively, the target system, not
# the current host). The other legal value is "build" (the build
# machine, i.e. the local host). If $key == "build" then some
# additional checks may be performed which are not applicable when
# $key == "host".
proc hwaci-looks-like-windows {{key host}} {
  global autosetup
  switch -glob -- [get-define $key] {
    *-*-ming* - *-*-cygwin - *-*-msys {
      return 1
    }
  }
  if {$key eq "build"} {
    # These apply only to the local OS, not a cross-compilation target,
    # as the above check can potentially.
    if {$::autosetup(iswin)} { return 1 }
    if {[find-an-executable cygpath] ne "" || $::tcl_platform(os)=="Windows NT"} {
      return 1
    }
  }
  return 0
}

########################################################################
# Looks at either the 'host' (==compilation target platform) or
# 'build' (==the being-built-on platform) define value and returns if
# if that value seems to indicate that it represents a Mac platform,
# else returns 0.
#
# TODO: have someone verify whether this is correct for the
# non-Linux/BSD platforms.
proc hwaci-looks-like-mac {{key host}} {
  switch -glob -- [get-define $key] {
    *apple* {
      return 1
    }
    default {
      return 0
    }
  }
}

########################################################################
# Checks autosetup's "host" and "build" defines to see if the build
# host and target are Windows-esque (Cygwin, MinGW, MSys). If the
# build environment is then BUILD_EXEEXT is [define]'d to ".exe", else
# "". If the target, a.k.a. "host", is then TARGET_EXEEXT is
# [define]'d to ".exe", else "".
proc hwaci-exe-extension {} {
  set rH ""
  set rB ""
  if {[hwaci-looks-like-windows host]} {
    set rH ".exe"
  }
  if {[hwaci-looks-like-windows build]} {
    set rB ".exe"
  }
  define BUILD_EXEEXT $rB
  define TARGET_EXEEXT $rH
}

########################################################################
# Works like hwaci-exe-extension except that it defines BUILD_DLLEXT
# and TARGET_DLLEXT to one of (.so, ,dll, .dylib).
#
# Trivia: for .dylib files, the linker needs the -dynamiclib flag
# instead of -shared.
#
# TODO: have someone verify whether this is correct for the
# non-Linux/BSD platforms.
proc hwaci-dll-extension {} {
  proc inner {key} {
    switch -glob -- [get-define $key] {
      *apple* {
        return ".dylib"
      }
      *-*-ming* - *-*-cygwin - *-*-msys {
        return ".dll"
      }
      default {
        return ".so"
      }
    }
  }
  define BUILD_DLLEXT [inner build]
  define TARGET_DLLEXT [inner host]
}

########################################################################
# Static-library counterpart of hwaci-dll-extension. Defines
# BUILD_LIBEXT and TARGET_LIBEXT to the conventional static library
# extension for the being-built-on resp. the target platform.
proc hwaci-lib-extension {} {
  proc inner {key} {
    switch -glob -- [get-define $key] {
      *-*-ming* - *-*-cygwin - *-*-msys {
        return ".lib"
      }
      default {
        return ".a"
      }
    }
  }
  define BUILD_LIBEXT [inner build]
  define TARGET_LIBEXT [inner host]
}

########################################################################
# Calls all of the hwaci-*-extension functions.
proc hwaci-file-extensions {} {
  hwaci-exe-extension
  hwaci-dll-extension
  hwaci-lib-extension
}

########################################################################
# Expects a list of file names. If any one of them does not exist in
# the filesystem, it fails fatally with an informative message.
# Returns the last file name it checks. If the first argument is -v
# then it emits msg-checking/msg-result messages for each file.
proc hwaci-affirm-files-exist {args} {
  set rc ""
  set verbose 0
  if {[lindex $args 0] eq "-v"} {
    set verbose 1
    set args [lrange $args 1 end]
  }
  foreach f $args {
    if {$verbose} { msg-checking "Looking for $f ... " }
    if {![file exists $f]} {
      user-error "not found: $f"
    }
    if {$verbose} { msg-result "" }
    set rc $f
  }
  return rc
}

########################################################################
# Emscripten is used for doing in-tree builds of web-based WASM stuff,
# as opposed to WASI-based WASM or WASM binaries we import from other
# places. This is only set up for Unix-style OSes and is untested
# anywhere but Linux.
#
# Defines the following:
#
# - EMSDK_HOME = top dir of the emsdk or "". It looks for
#   --with-emsdk=DIR or the $EMSDK environment variable.
# - EMSDK_ENV = path to EMSDK_HOME/emsdk_env.sh or ""
# - BIN_EMCC = $EMSDK_HOME/upstream/emscripten/emcc or ""
# - HAVE_EMSDK = 0 or 1 (this function's return value)
#
# Returns 1 if EMSDK_ENV is found, else 0.  If EMSDK_HOME is not empty
# but BIN_EMCC is then emcc was not found in the EMSDK_HOME, in which
# case we have to rely on the fact that sourcing $EMSDK_ENV from a
# shell will add emcc to the $PATH.
proc hwaci-check-emsdk {} {
  set emsdkHome [opt-val with-emsdk]
  define EMSDK_HOME ""
  define EMSDK_ENV ""
  define BIN_EMCC ""
  msg-checking "Emscripten SDK? "
  if {$emsdkHome eq ""} {
    # Fall back to checking the environment. $EMSDK gets set by
    # sourcing emsdk_env.sh.
    set emsdkHome [get-env EMSDK ""]
  }
  set rc 0
  if {$emsdkHome ne ""} {
    define EMSDK_HOME $emsdkHome
    set emsdkEnv "$emsdkHome/emsdk_env.sh"
    if {[file exists $emsdkEnv]} {
      msg-result "$emsdkHome"
      define EMSDK_ENV $emsdkEnv
      set rc 1
      set emcc "$emsdkHome/upstream/emscripten/emcc"
      if {[file exists $emcc]} {
        define BIN_EMCC $emcc
      }
    } else {
      msg-result "emsdk_env.sh not found in $emsdkHome"
    }
  } else {
    msg-result "not found"
  }
  define HAVE_EMSDK $rc
  return $rc
}

########################################################################
# Tries various approaches to handling the -rpath link-time
# flag. Defines LDFLAGS_RPATH to that/those flag(s) or an empty
# string. Returns 1 if it finds an option, else 0.
#
# Achtung: we have seen platforms which report that a given option
# checked here will work but then fails at build-time, and the current
# order of checks reflects that.
proc hwaci-check-rpath {} {
  set rc 1
  set lp "[get-define prefix]/lib"
  # If we _don't_ use cc-with {} here (to avoid updating the global
  # CFLAGS or LIBS or whatever it is that cc-check-flags updates) then
  # downstream tests may fail because the resulting rpath gets
  # implicitly injected into them.
  cc-with {} {
    if {[cc-check-flags "-rpath $lp"]} {
      define LDFLAGS_RPATH "-rpath $lp"
    } elseif {[cc-check-flags "-Wl,-rpath -Wl,$lp"]} {
      define LDFLAGS_RPATH "-Wl,-rpath -Wl,$lp"
    } elseif {[cc-check-flags -Wl,-R$lp]} {
      define LDFLAGS_RPATH "-Wl,-R$lp"
    } else {
      define LDFLAGS_RPATH ""
      set rc 0
    }
  }
  return $rc
}

########################################################################
# Check for libreadline functionality.  Linking in readline varies
# wildly by platform and this check does not cover all known options.
# This detection is known to fail under the following conditions:
#
# - (pkg-config readline) info is either unavailable for libreadline or
#   simply misbehaves.
#
# - Compile-and-link-with-default-path tests fail. This will fail for
#   platforms which store readline under, e.g., /usr/local.
#
# Defines the following vars:
#
# - HAVE_READLINE: 0 or 1
# - LDFLAGS_READLINE: "" or linker flags
# - CFLAGS_READLINE: "" or c-flags
#
# Returns the value of HAVE_READLINE.
proc hwaci-check-readline {} {
  define HAVE_READLINE 0
  define LDFLAGS_READLINE ""
  define CFLAGS_READLINE ""
  if {![opt-bool readline]} {
    msg-result "libreadline disabled via --disable-readline."
    return 0
  }

  if {[pkg-config-init 0] && [pkg-config readline]} {
    define HAVE_READLINE 1
    define LDFLAGS_READLINE [get-define PKG_READLINE_LDFLAGS]
    define-append LDFLAGS_READLINE [get-define PKG_READLINE_LIBS]
    define CFLAGS_READLINE [get-define PKG_READLINE_CFLAGS]
    return 1
  }

  # On OpenBSD on a Raspberry pi 4:
  #
  # $ pkg-config readline; echo $?
  # 0
  # $ pkg-config --cflags readline
  # Package termcap was not found in the pkg-config search path
  # $ echo $?
  # 1
  # $ pkg-config --print-requires readline; echo $?
  # 1
  #
  # i.e. there's apparently no way to find out that readline requires
  # termcap beyond parsing the error message.  It turns out it doesn't
  # want termcap, it wants -lcurses, but we don't get that info from
  # pkg-config either.

  set h "readline/readline.h"
  if {[cc-check-includes $h]} {
    if {[hwaci-check-function-in-lib readline readline]} {
      msg-result "Enabling libreadline."
      define HAVE_READLINE 1
      define LDFLAGS_READLINE [get-define lib_readline]
      undefine lib_readline
      return 1
    }
    # else TODO: look in various places and [define CFLAGS_READLINE
    # -I...]
  }
  # Numerous TODOs:
  # - Requires linking with [n]curses or similar on some platforms.
  # - Headers are in a weird place on some BSD systems.
  # - Add --with-readline=DIR
  # - Add --with-readline-lib=lib ==> pass lib file via LDFLAGS_READLINE
  # - Add --with-readline-inc=dir ==> pass -Idir via CFLAGS_READLINE
  msg-result "libreadline not found."
  return 0
}


########################################################################
# Internal helper for hwaci-dump-defs-json. Expects to be passed a
# [define] name and the variadic $args which are passed to
# hwaci-dump-defs-json. If it finds a pattern match for the given
# $name in the various $args, it returns the type flag for that $name,
# e.g. "-str" or "-bare", else returns an empty string.
proc hwaci-defs-type_ {name spec} {
  foreach {type patterns} $spec {
    foreach pattern $patterns {
      if {[string match $pattern $name]} {
        return $type
      }
    }
  }
  return ""
}

########################################################################
# Internal helper for hwaci-defs-format_: returns a JSON-ish quoted
# form of the given (JSON) string-type values.
proc hwaci-quote-str_ {value} {
  return \"[string map [list \\ \\\\ \" \\\"] $value]\"
}

########################################################################
# An internal impl detail of hwaci-dump-defs-json. Requires a data
# type specifier, as used by make-config-header, and a value. Returns
# the formatted value or the value $::hwaci_(defs-skip) if the caller
# should skip emitting that value.
set hwaci_(defs-skip) "-hwaci-defs-format_ sentinel"
proc hwaci-defs-format_ {type value} {
  switch -exact -- $type {
    -bare {
      # Just output the value unchanged
    }
    -none {
      set value $::hwaci_(defs-skip)
    }
    -str {
      set value [hwaci-quote-str_ $value]
    }
    -auto {
      # Automatically determine the type
      if {![string is integer -strict $value]} {
        set value [hwaci-quote-str_ $value]
      }
    }
    -array {
      set ar {}
      foreach v $value {
        set v [hwaci-defs-format_ -auto $v]
        if {$::hwaci_(defs-skip) ne $v} {
          lappend ar $v
        }
      }
      set value "\[ [join $ar {, }] \]"
    }
    "" {
      set value $::hwaci_(defs-skip)
    }
    default {
      hwaci-fatal "Unknown type in hwaci-dump-defs-json: $type"
    }
  }
  return $value
}

########################################################################
# This function works almost identically to autosetup's
# make-config-header but emits its output in JSON form. It is not a
# fully-functional JSON emitter, and will emit broken JSON for
# complicated outputs, but should be sufficient for purposes of
# emitting most configure vars (numbers and simple strings).
#
# In addition to the formatting flags supported by make-config-header,
# it also supports:
#
#  -array {patterns...}
#
# Any defines matching the given patterns will be treated as a list of
# values, each of which will be formatted as if it were in an -auto {...}
# set, and the define will be emitted to JSON in the form:
#
#  "ITS_NAME": [ "value1", ...valueN ]
#
# Achtung: if a given -array pattern contains values which themselves
# contains spaces...
#
#   define-append foo {"-DFOO=bar baz" -DBAR="baz barre"}
#
# will lead to:
#
#  ["-DFOO=bar baz", "-DBAR=\"baz", "barre\""]
#
# Neither is especially satisfactory (and the second is useless), and
# handling of such values is subject to change if any such values ever
# _really_ need to be processed by our source trees.
proc hwaci-dump-defs-json {file args} {
  file mkdir [file dirname $file]
  set lines {}
  lappend args -bare {SIZEOF_* HAVE_DECL_*} -auto HAVE_*
  foreach n [lsort [dict keys [all-defines]]] {
    set type [hwaci-defs-type_ $n $args]
    set value [hwaci-defs-format_ $type [get-define $n]]
    if {$::hwaci_(defs-skip) ne $value} {
      lappend lines "\"$n\": ${value}"
    }
  }
  set buf {}
  lappend buf [join $lines ",\n"]
  write-if-changed $file $buf {
    msg-result "Created $file"
  }
}
