########################################################################
# 2025 April 7
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#  * May you do good and not evil.
#  * May you find forgiveness for yourself and forgive others.
#  * May you share freely, never taking more than you give.
#
########################################################################
# ----- @module feature-tests.tcl -----
# @section TEA-ish collection of feature tests.
#
# Functions in this file with a prefix of teaish__ are
# private/internal APIs. Those with a prefix of teaish- are
# public APIs.


# @teaish-check-libz
#
# Checks for zlib.h and the function deflate in libz. If found,
# prepends -lz to the extension's ldflags and returns 1, else returns
# 0. It also defines LDFLAGS_LIBZ to the libs flag.
#
proc teaish-check-libz {} {
  teaish-check-cached "Checking for libz" {
    set rc 0
    if {[msg-quiet cc-check-includes zlib.h] && [msg-quiet proj-check-function-in-lib deflate z]} {
      teaish-prepend-ldflags [define LDFLAGS_LIBZ [get-define lib_deflate]]
      undefine lib_deflate
      incr rc
    }
    expr $rc
  }
}

# @teaish-check-librt ?funclist?
#
# Checks whether -lrt is needed for any of the given functions.  If
# so, appends -lrt via [teaish-prepend-ldflags] and returns 1, else
# returns 0. It also defines LDFLAGS_LIBRT to the libs flag or an
# empty string.
#
# Some systems (ex: SunOS) require -lrt in order to use nanosleep.
#
proc teaish-check-librt {{funclist {fdatasync nanosleep}}} {
  teaish-check-cached -nostatus "Checking whether ($funclist) need librt" {
    define LDFLAGS_LIBRT ""
    foreach func $funclist {
      if {[msg-quiet proj-check-function-in-lib $func rt]} {
        set ldrt [get-define lib_${func}]
        undefine lib_${func}
        if {"" ne $ldrt} {
          teaish-prepend-ldflags -r [define LDFLAGS_LIBRT $ldrt]
          msg-result $ldrt
          return 1
        } else {
          msg-result "no lib needed"
          return 1
        }
      }
    }
    msg-result "not found"
    return 0
  }
}

# @teaish-check-stdint
#
# A thin proxy for [cc-with] which checks for <stdint.h> and the
# various fixed-size int types it declares. It defines HAVE_STDINT_T
# to 0 or 1 and (if it's 1) defines HAVE_XYZ_T for each XYZ int type
# to 0 or 1, depending on whether its available.
proc teaish-check-stdint {} {
  teaish-check-cached "Checking for stdint.h" {
    msg-quiet cc-with {-includes stdint.h} \
      {cc-check-types int8_t int16_t int32_t int64_t intptr_t \
         uint8_t uint16_t uint32_t uint64_t uintptr_t}
  }
}

# @teaish-is-mingw
#
# Returns 1 if building for mingw, else 0.
proc teaish-is-mingw {} {
  return [expr {
    [string match *mingw* [get-define host]] &&
    ![file exists /dev/null]
  }]
}

# @teaish-check-libdl
#
# Checks for whether dlopen() can be found and whether it requires
# -ldl for linking. If found, returns 1, defines LDFLAGS_DLOPEN to the
# linker flags (if any), and passes those flags to
# teaish-prepend-ldflags. It unconditionally defines HAVE_DLOPEN to 0
# or 1 (the its return result value).
proc teaish-check-dlopen {} {
  teaish-check-cached -nostatus "Checking for dlopen()" {
    set rc 0
    set lfl ""
    if {[cc-with {-includes dlfcn.h} {
      cctest -link 1 -declare "extern char* dlerror(void);" -code "dlerror();"}]} {
      msg-result "-ldl not needed"
      incr rc
    } elseif {[cc-check-includes dlfcn.h]} {
      incr rc
      if {[cc-check-function-in-lib dlopen dl]} {
        set lfl [get-define lib_dlopen]
        undefine lib_dlopen
        msg-result " dlopen() needs $lfl"
      } else {
        msg-result " - dlopen() not found in libdl. Assuming dlopen() is built-in."
      }
    } else {
      msg-result "not found"
    }
    teaish-prepend-ldflags [define LDFLAGS_DLOPEN $lfl]
    define HAVE_DLOPEN $rc
  }
}

#
# @teaish-check-libmath
#
# Handles the --enable-math flag. Returns 1 if found, else 0.
# If found, it prepends -lm (if needed) to the linker flags.
proc teaish-check-libmath {} {
  teaish-check-cached "Checking for libc math library" {
    set lfl ""
    set rc 0
    if {[msg-quiet proj-check-function-in-lib ceil m]} {
      incr rc
      set lfl [get-define lib_ceil]
      undefine lib_ceil
      teaish-prepend-ldflags $lfl
      msg-checking "$lfl "
    }
    define LDFLAGS_LIBMATH $lfl
    expr $rc
  }
}

# @teaish-import-features ?-no-options? feature-names...
#
# For each $name in feature-names... it invokes:
#
#   use teaish/feature/$name
#
# to load TEAISH_AUTOSETUP_DIR/feature/$name.tcl
#
# By default, if a proc named teaish-check-${name}-options is defined
# after sourcing a file, it is called and its result is passed to
# proj-append-options. This can be suppressed with the -no-options
# flag.
proc teaish-import-features {args} {
  set doOpt 1
  if {1} {
    set xopt [list]
    foreach arg $args {
      switch -exact -- $arg {
        -no-options {
          set doOpt 0
        }
        default {
          lappend xopt $arg
        }
      }
    }
    set args $xopt
  }
  foreach arg $args {
    uplevel "use teaish/feature/$arg"
    if {$doOpt} {
      set n "teaish-check-${arg}-options"
      if {[llength [info proc $n]] > 0} {
        set x [$n]
        if {"" ne $x} {
          options-add $x
        }
      }
    }
  }
}
