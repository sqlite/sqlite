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
  msg-checking "${msg} ... "
  if {[teaish-feature-cache-check 1 check]} {
    msg-checking "(cached) "
    if {$check} {msg-result "ok"} else {msg-result "no"}
    return $check
  } else {
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
    } else {
      #puts "**** code=$code rc=$rc xopt=$xopt"
      teaish-feature-cache-set 1 0
    }
    return -options $xopt $rc
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

# @teaish-check-pkg-config-openssl
#
# Returns 1 if openssl is found via pkg-config, else 0.  If found,
# passes its link flags to teaish-prepend-ldflags.
#
# It defines LDFLAGS_OPENSSL to the linker flags and CFLAGS_OPENSSL to
# the CFLAGS, or "" if it's not found.
#
# Defines HAVE_OPENSSL to 0 or 1 (its return value).
#
# If it returns true, the underlying pkg-config test will set several
# defines named PKG_OPENSSL_... (see the docs for [pkg-config] for
# details).
proc teaish-check-pkg-config-openssl {} {
  use pkg-config
  teaish-check-cached -nostatus "Checking for openssl via pkg-config" {
    set rc 0
    if {[msg-quiet pkg-config-init 0] && [msg-quiet pkg-config openssl]} {
      incr rc
      set lfl [string trim "[get-define PKG_OPENSSL_LDFLAGS] [get-define PKG_OPENSSL_LIBS]"]
      define CFLAGS_OPENSSL [get-define PKG_OPENSSL_CFLAGS]
      define LDFLAGS_OPENSSL $lfl
      teaish-prepend-ldflags $lfl
      msg-result "ok ($lfl)"
    } else {
      define CFLAGS_OPENSSL ""
      define LDFLAGS_OPENSSL ""
      msg-result "no"
    }
    define HAVE_OPENSSL $rc
    return [teaish-feature-cache-set $rc]
  }
}

# Internal helper for OpenSSL checking using cc-with to check if the
# given $cflags, $ldflags, and list of -l libs can link an
# application.
#
# For a system-level check, use empty $cflags and $ldflags.
#
# On success, it defines CFLAGS_OPENSSL to $cflags and LDFLAGS_OPENSSL
# to a combination of $ldflags and any required libs (which may be
# amended beyond those provided in $libs). It then returns 1.
#
# On failure it defines the above-mentioned flags to ""
# and returns 0.
#
# Defines HAVE_OPENSSL to its return value.
#
# Derived from https://fossil-scm.org/file/auto.def
proc teaish__check-openssl {msg cflags ldflags {libs {-lssl -lcrypto -lpthread}}} {
  msg-checking "$msg ... "
  set rc 0
  set isMinGw [teaish-is-mingw]
  if {$isMinGw} {
    lappend libs -lgdi32 -lwsock32 -lcrypt32
  }
  set prefix msg-quiet
  #set prefix ""
  set lz ""
  if {[{*}$prefix teaish-check-libz]} {
    set lz [get-define LDFLAGS_LIBZ]
  }
  set libs2 $libs
  if {$lz ne ""} {
    lappend libs2 $lz
  }
  {*}$prefix cc-with [list -link 1 -cflags "$cflags $ldflags" -libs $libs2] {
    if {[cc-check-includes openssl/ssl.h] && \
          [cc-check-functions SSL_new]} {
      incr rc
    }
  }
  if {!$rc && !$isMinGw} {
    # On some systems, OpenSSL appears to require -ldl to link.
    if {[{*}$prefix teaish-check-dlopen]} {
      lappend libs2 [get-define LDFLAGS_DLOPEN ""]
      {*}$prefix cc-with [list  -link 1 -cflags "$cflags $ldflags" -libs $libs2] {
        if {[cc-check-includes openssl/ssl.h] && \
              [cc-check-functions SSL_new]} {
          incr rc
        }
      }
    }
  }
  #puts "*???? cflags=$cflags ldflags=$ldflags libs2=$libs2"
  if {$rc} {
    msg-result "ok"
    define CFLAGS_OPENSSL "$cflags"
    define LDFLAGS_OPENSSL "$ldflags $libs2"
  } else {
    define CFLAGS_OPENSSL ""
    define LDFLAGS_OPENSSL ""
    msg-result "no"
  }
  define HAVE_OPENSSL $rc
  return $rc
}

# @teaish-check-openssl
#
# Jumps through some provierbial hoops to look for OpenSSL dev pieces.
#
# $where must be one of the following:
#
# - "pkg-config": check only pkg-config for it, but also verify that
#   the results from pkg-config seem to work.
#
# - "system": look in standard(ish) system paths, starting with
#   a lookup requiring no -L flag.
#
# - "auto" or "": try (pkg-config, system), in that order.
#
# - "none": do no lookup, define vars (see below), and return 0.
#
# - Any other value is assumed to be a directory name suitable for
#   finding OpenSSL, but how this lookup is run is not currently
#   well-defined.
#
# It defines LDFLAGS_OPENSSL and CFLAGS_OPENSSL to ldflags
# resp. cflags needed for compiling and linking, and updates teaish's
# internal ldflags/cflags lists. If OpenSSL is not found, they're
# defined to "".
#
# It defines HAVE_OPENSSL to 0 or 1 (its return value).
#
# If $where is empty then it defaults to auto. If $where is not empty
# _and_ OpenSSL is not found
#
# If the --with-openssl=... config flag is defined (see
# teaish-check-openssl-options) then an empty $where value will use
# the value of the --with-openssl flag, defaulting to "auto" if that
# flag also has an empty value. If that flag is provided, and has a
# value other than "none", then failure to find the library is
# considered fatal.
#
# Derived from https://fossil-scm.org/file/auto.def
proc teaish-check-openssl {{where ""}} {
  teaish-check-cached -nostatus "Looking for openssl" {
    if {$where eq ""} {
      if {[proj-opt-exists with-openssl]} {
        set where [join [opt-val with-openssl auto]]
      }
    }

    set notfound {{checkWithFlag msg} {
      if {$checkWithFlag && [proj-opt-was-provided with-openssl]} {
        proj-fatal "--with-openssl " \
          "found no working installation. Try --with-openssl=none"
      }
      define LDFLAGS_OPENSSL ""
      define CFLAGS_OPENSSL ""
      define HAVE_OPENSSL 0
      msg-result $msg
    }}

    switch -exact -- $where {
      none {
        apply $notfound 0 none
        return 0
      }
      "" {
        set where auto
      }
    }
    if {$where in {pkg-config auto}} {
      # Check pkg-config
      if {[teaish-check-pkg-config-openssl]} {
        set cflags [get-define PKG_OPENSSL_CFLAGS ""]
        set ldflags [get-define LDFLAGS_OPENSSL ""]
        if {[teaish__check-openssl "Verifying openssl pkg-config values" \
               "$cflags $ldflags"]} {
          teaish-prepend-ldflags $ldflags
          return 1
        }
      }
      if {$where eq "pkg-config"} {
        apply $notfound 1 "not found"
        return 0
      }
    }

    # Determine which dirs to search...
    set ssldirs {}
    if {$where in {auto system}} {
      set ssldirs {
        {} /usr/sfw /usr/local/ssl /usr/lib/ssl /usr/ssl
        /usr/pkg /usr/local /usr /usr/local/opt/openssl
        /opt/homebrew/opt/openssl
      }
    } elseif {$where ne ""} {
      lappend ssldirs $where
    }

    foreach dir $ssldirs {
      set msg "in $dir"
      set cflags "-I$dir/include"
      if {$dir eq ""} {
        set msg "without -L/path"
        set ldflags ""
        set cflags ""
      } elseif {![file isdir $dir]} {
        continue
      } elseif {[file readable $dir/libssl.a]} {
        set ldflags -L$dir
      } elseif {[file readable $dir/lib/libssl.a]} {
        set ldflags -L$dir/lib
      } elseif {[file isdir $dir/lib]} {
        set ldflags [list -L$dir -L$dir/lib]
      } else {
        set ldflags -L$dir
      }
      if {[teaish__check-openssl $msg $cflags $ldflags]} {
        teaish-add-cflags [get-define CFLAGS_OPENSSL]
        teaish-prepend-ldflags [get-define LDFLAGS_OPENSSL]
        return 1
      }
      if {$dir ne ""} {
        # Look for a static build under $dir...
        set ldflags ""
        set libs [list $dir/libssl.a $dir/libcrypto.a]
        set foundLibs 0
        foreach x $libs {
          if {![file readlable $x]} break;
          incr foundLibs
        }
        if {$foundLibs != [llength $libs]} {
          continue
        }
        set cflags "-I$dir/include"
        lappend libs -lpthread
        if {[teaish__check-openssl "Checking for static openssl build in $dir" \
               $cflags $ldflags $libs]} {
          teaish-add-cflags [get-define CFLAGS_OPENSSL]
          teaish-prepend-ldflags [get-define LDFLAGS_OPENSSL]
          return 1
        }
      }
    }

    apply $notfound 1 no
    return 0
  }
}; # teaish-check-openssl

# @teaish-check-openssl-options
#
# teaish.tcl files which use teaish-check-openssl should
# include this function's result from their teaish-options
# impl, so that configure --help can include the --with-openssl
# flag that check exposes.
#
# Returns a list of options for the teaish-check-openssl feature test.
#
# Example usage:
#
# proc teaish-options {} {
#  use teaish-feature-tests
#  return [teaish-combine-option-lists \
#            [teaish-check-openssl-options] \
#            { hell-world => {just testing} } \
#          ]
# }
proc teaish-check-openssl-options {} {
  return {
    with-openssl:see-the-help =>
    {Checks for OpenSSL development libraries in a variety of ways.
      "pkg-config" only checks the system's pkg-config.
      "system" checks only for a system-level copy.
      "auto" checks the prior options and a list of
      likely candidate locations. "none" disables the check
      altogether and causes the check to not fail if it's
      not found. Any other value is a directory in which a
      _static_ copy of libssl.a can be found, e.g. a locally-built
      copy of the OpenSSL source tree. If this flag is explicitly provided,
      and has a value other than "none", failure to find OpenSSL
      is fatal.}
  }
}
