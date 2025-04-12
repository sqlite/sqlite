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
# ----- @module teaish-checks.tcl -----
# @section TEA-ish collection of feature tests.
#
# Functions in this file with a prefix of teaish__ are
# private/internal APIs. Those with a prefix of teaish- are
# public APIs.

use pkg-config

# @teaish-check-cached@ ?-flags? msg script
#
# Under construction.
#
# A proxy for feature-test impls which handles chacheing of the
# feature flag check on a per-caller basis, using the calling scope's
# name as the cache key.
#
# The test is performed by $script. This function caches the result
# and checks for a chache hit before running $script. The value stored
# in the cache is the final value of $script (and this routine will
# intercept a 'return' from $script).
#
# Flags:
#
#   -nostatus = do not emit "ok" or "no" at the end. This presumes
#    that the caller will emit a newline before turning.
proc teaish-check-cached {args} {
  set quiet 0
  set xargs {}
  foreach arg $args {
    switch -exact -- $arg {
      -nostatus {
        incr quiet
      }
      default {
        lappend xargs $arg
      }
    }
  }
  lassign $xargs msg script
  if {"" eq $msg} {
    set msg [proj-current-scope 1]
  }
  if {[teaish-feature-cache-check 1 check]} {
    msg-checking "${msg} ... (cached) "
    if {$check} {msg-result "ok"} else {msg-result "no"}
    return $check
  } else {
    msg-checking "${msg} ... "
    set code [catch {uplevel 1 $script} rc xopt]
    #puts "***** ::teaish__fCache ="; parray ::teaish__fCache
    if {$code in {0 2}} {
      teaish-feature-cache-set 1 $rc
      if {!$quiet} {
        if {$rc} {
          msg-result "ok"
        } else {
          msg-result "no"
        }
      }
      #puts "**** code=$code rc=$rc xopt=$xopt"
    } else {
      return -options $xopt $rc
    }
  }
}


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
# Checks for whether dlopen() can be found and whether it requires -ldl
# for linking. If found, returns 1, defines LDFLAGS_DLOPEN to the linker flags
# (if any), and passes those flags to teaish-prepend-ldflags.
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

########################################################################
# Handles the --enable-math flag.
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


# @teaish-check-pkg-config-libssl
#
# Returns 1 if libssl is found via pkg-config, else 0.  If found,
# passes its link flags to teaish-prepend-ldflags. Defines LDFLAGS_SSL
# to the linker flags, if found, else "".
#
# If it returns true, the underlying pkg-config test will set several
# defines named PKG_LIBSSL_... (see the docs for [pkg-config] for
# details).
proc teaish-check-pkg-config-libssl {} {
  teaish-check-cached -nostatus "Checking for libssl via pkg-config" {
    msg-result "Looking for libssl ..."
    set lfl {}
    set rc 0
    if {[msg-quiet pkg-config-init 0] && [msg-quiet pkg-config libssl]} {
      lappend lfl [get-define PKG_LIBSSL_LDFLAGS] \
        [get-define PKG_LIBSSL_LIBS]
      incr rc
    } else {
      # TODO: port over the more elaborate checks from fossil.
    }
    if {$rc} {
      set lfl [string trim [join $lfl]]
      define LDFLAGS_SSL $lfl
      teaish-prepend-ldflags $lfl
    }
    define HAVE_LIBSSL $rc
    return [teaish-feature-cache-set $rc]
  }
}

# Under construction
#
# Helper for OpenSSL checking
proc teaish__check-openssl {msg {cflags {}} {libs {-lssl -lcrypto -lpthread}}} {
  msg-checking "Checking for $msg..."
  set rc 0
  set isMinGw [teaish-is-mingw]
  if {$isMinGw} {
    lappend libs -lgdi32 -lwsock32 -lcrypt32
  }
  if {[teaish-check-libz]} {
    lappend libs [get-define LDFLAGS_LIBZ]
  }
  msg-quiet cc-with [list -cflags $cflags -libs $libs] {
    if {[cc-check-includes openssl/ssl.h] && \
          [cc-check-functions SSL_new]} {
      incr rc
    }
  }
  # TODO
  if {!$rc && !$isMinGw} {
    # On some systems, OpenSSL appears to require -ldl to link.
    if {[teaish-check-dlopen]} {
      lappend libs [get-define LDFLAGS_DLOPEN ""]
      msg-quiet cc-with [list -cflags $cflags -libs $libs] {
        if {[cc-check-includes openssl/ssl.h] && \
              [cc-check-functions SSL_new]} {
          incr rc
        }
      }
    }
  }
  if {$rc} {
    msg-result "ok"
  } else {
    msg-result "no"
  }
  return $rc
}

# Under construction
proc teaish-check-libssl {} {
  # Goal: port in fossil's handle-with-openssl. It's a bit of a beast.
  if {![teaish-check-pkg-config-libssl]} {
    #teaish__check-openssl
  }
}
