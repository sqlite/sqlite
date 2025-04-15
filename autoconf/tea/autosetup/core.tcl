########################################################################
# 2025 April 5
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#  * May you do good and not evil.
#  * May you find forgiveness for yourself and forgive others.
#  * May you share freely, never taking more than you give.
#
########################################################################
# ----- @module teaish.tcl -----
# @section TEA-ish ((TCL Extension Architecture)-ish)
#
# Functions in this file with a prefix of teaish__ are
# private/internal APIs. Those with a prefix of teaish- are
# public APIs.
#
# Teaish has a hard dependency on proj.tcl, and any public API members
# of that module are considered legal for use by teaish extensions.
#
# Project home page: https://fossil.wanderinghorse.net/r/teaish

use proj

define TEAISH_VERSION 0.1-beta
use system ; # Will output "Host System" and "Build System" lines
if {"--help" ni $::argv} {
  proj-tweak-default-env-dirs
  msg-result "TEA(ish) Version = [get-define TEAISH_VERSION]"
  msg-result "Source dir    = $::autosetup(srcdir)"
  msg-result "Build dir     = $::autosetup(builddir)"
}

#
# API-internal settings and shared state.
array set teaish__Config [proj-strip-hash-comments {
  # set to 1 to enable some internal debugging output
  debug-enabled 0
  #
  # 0    = don't yet have extension's pkgindex
  # 0x01 = teaish__find_extension found TEAISH_DIR/pkgindex.tcl
  # 0x02 = teaish__find_extension found srcdir/pkgindex.tcl.in
  # 0x04 = teaish__find_extension found TEAISH_DIR/pkgindex.tcl (static file)
  # 0x10 = teaish-pragma was called: behave as if 0x04
  #
  pkgindex-policy 0
}]

#
# Returns true if any arg in $::argv matches any of the given globs,
# else returns false.
#
proc teaish__argv_has {args} {
  foreach glob $args {
    foreach arg $::argv {
      if {[string match $glob $arg]} {
        return 1
      }
    }
  }
  return 0
}

#
# Main entry point for the TEA-ish configure process. auto.def's primary
# (ideally only) job should be to call this.
#
proc teaish-configure-core {} {
  #
  # "Declare" some defines...
  #
  foreach {f v} {
    TEAISH_MAKEFILE ""
    TEAISH_MAKEFILE_IN ""
    TEAISH_DIST_FILES ""
    TEAISH_PKGINIT_TCL ""
    TEAISH_PKGINIT_TCL_IN ""
    TEAISH_PKGINDEX_TCL_IN ""
    TEAISH_PKGINDEX_TCL ""
    TEAISH_TCL ""
    TEAISH_CFLAGS ""
    TEAISH_LDFLAGS ""
    TEAISH_SRC ""
  } {
    define $f $v
  }

  set gotExt 0; # True if an extension config is found
  if {![teaish__argv_has --teaish-create-extension* --t-c-e*]} {
    # Don't look for an extension if we're in --t-c-e mode
    set gotExt [teaish__find-extension]
  }

  if {$gotExt} {
    set ttcl [get-define TEAISH_TCL]
    proj-assert {[file exists $ttcl]} "Expecting to have found teaish.tcl by now"
    uplevel 1 "source $ttcl"
    if {"" eq [get-define TEAISH_NAME ""]} {
      proj-fatal "$ttcl did not define TEAISH_NAME"
    } elseif {"" eq [get-define TEAISH_VERSION ""]} {
      proj-fatal "$ttcl did not define TEAISH_VERSION"
    }
    unset ttcl
  }; # sourcing extension's teaish.tcl

  #
  # Set up the --flags...
  #
  proj-options-add [proj-strip-hash-comments {
    with-tcl:DIR
      => {Directory containing tclConfig.sh or a directory one level up from
          that, from which we can derive a directory containing tclConfig.sh.}

    with-tclsh:PATH
      => {Full pathname of tclsh to use.  It is used for trying to find
          tclConfig.sh.  Warning: if its containing dir has multiple tclsh
          versions, it may select the wrong tclConfig.sh!}

    # TEA has --with-tclinclude but it appears to only be useful for
    # building an extension against an uninstalled copy of TCL's own
    # source tree. Either we get that info from tclConfig.sh or we
    # give up.
    #
    # with-tclinclude:DIR
    #   => {Specify the directory which contains the tcl.h. This should not
    #       normally be required, as that information comes from tclConfig.sh.}

    # We _generally_ want to reduce the possibility of flag collisions with
    # extensions, and thus use a teaish-... prefix on most flags. However,
    # --teaish-extension-dir is frequently needed, so...
    #
    # As of this spontaneous moment, we'll formalize using using
    # --t-X-Y to abbreviate teaish flags when doing so is
    # unambiguous...
    ted: t-e-d:
    teaish-extension-dir:DIR
      => {Looks for an extension in the given directory instead of the current dir.}

    t-c-e:
    teaish-create-extension:TARGET_DIRECTORY
      => {Writes stub files for creating an extension. Will refuse to overwrite
          existing files without --force.}

    t-f
    teaish-force
      => {Has a context-dependent meaning (autosetup defines --force for its own use)}

    t-d-d
    teaish-dump-defines => {Dump all configure-defined vars to config.defines.txt}

    t-d
    teaish-debug => {Enable teaish-specific debug output}
  }]; # main options.

  if {[llength [info proc teaish-options]] > 0} {
    # Add options defined by teaish-options, which is assumed to be
    # imported via TEAISH_TCL.
    proj-options-add [teaish-options]
  }
  set opts [proj-options-combine]
  #lappend opts teaish-debug => {x}; #testing dupe entry handling
  if {[catch {options $opts} msg xopts]} {
    # Workaround for <https://github.com/msteveb/autosetup/issues/73>
    # where [options] behaves oddly on _some_ TCL builds when it's
    # called from deeper than the global scope.
    dict incr xopts -level
    return {*}$xopts $msg
  }

  proj-xfer-options-aliases {
    t-c-e  => teaish-create-extension
    t-d    => teaish-debug
    t-d-d  => teaish-dump-defines
    ted    => teaish-extension-dir
    t-e-d  => teaish-extension-dir
    t-f    => teaish-force
  }

  set ::teaish__Config(debug-enabled) [opt-bool teaish-debug]

  if {[proj-opt-was-provided teaish-create-extension]} {
    teaish__create_extension [opt-val teaish-create-extension]
    return
  }
  proj-assert {1==$gotExt} "Else we cannot have gotten this far"

  teaish__configure-phase1
}


########################################################################
# Internal config-time debugging output routine. It is not legal to
# call this from the global scope.
proc teaish-debug {msg} {
  if {$::teaish__Config(debug-enabled)} {
    puts stderr [proj-bold "** DEBUG: \[[proj-current-scope 1]\]: $msg"]
  }
}

proc teaish__configure-phase1 {} {

  msg-result \
    "Configuring extension [proj-bold [get-define TEAISH_NAME] [get-define TEAISH_VERSION]]..."

  uplevel 1 {
    use cc cc-db cc-shared cc-lib; # pkg-config
  }
  teaish__check-common-bins

  if {"" eq [get-define TEAISH_LIBDIR_NAME]} {
    define TEAISH_LIBDIR_NAME [get-define TEAISH_NAME]
  }

  teaish__check-tcl
  apply {{} {
    #
    # If --prefix or --exec-prefix are _not_ provided, use their
    # TCL_... counterpart from tclConfig.sh.  Caveat: by the time we can
    # reach this point, autosetup's system.tcl will have already done
    # some non-trivial amount of work with these to create various
    # derived values from them, so we temporarily end up with a mishmash
    # of autotools-compatibility var values. That will be straightened
    # out in the final stage of the configure script via
    # [proj-remap-autoconf-dir-vars].
    #
    foreach {flag uflag tclVar} {
      prefix      prefix      TCL_PREFIX
      exec-prefix exec_prefix TCL_EXEC_PREFIX
    } {
      if {![proj-opt-was-provided $flag]} {
        set v [get-define $tclVar "???"]
        proj-assert {"???" ne $v} "Expecting teach-check-tcl to have defined $tclVar"
        proj-opt-set $flag $v
        define $uflag $v
        # ^^^ As of here, all autotools-compatibility vars which derive
        # from --$flag, e.g. --libdir, still derive from the default
        # --$flag value which was active when system.tcl was
        # included. So long as those flags are not explicitly passed to
        # the configure script, those will be straightened out via
        # [proj-remap-autoconf-dir-vars].
        msg-result "Using \$$tclVar for --$flag=$v"
      }
    }
  }}; # --[exec-]prefix defaults

  proj-file-extensions
  apply {{} {
    set pkgname [get-define TEAISH_NAME]
    set pkgver [get-define TEAISH_VERSION]
    set libname "lib"
    if {[string match *-cygwin [get-define host]]} {
      set libname cyg
    }
    define TEAISH_DLL8_BASENAME $libname$pkgname$pkgver
    define TEAISH_DLL9_BASENAME ${libname}tcl9$pkgname$pkgver
    set ext [get-define TARGET_DLLEXT]
    define TEAISH_DLL8 [get-define TEAISH_DLL8_BASENAME]$ext
    define TEAISH_DLL9 [get-define TEAISH_DLL9_BASENAME]$ext
  }}

#  foreach ft [glob -nocomplain [get-define TEAISH_AUTOSETUP_DIR]/featuretest-*.tcl] {
#    puts "Loading external feature test: $ft"
#    upscope 1 "source $ft"
#  }

  if {[llength [info proc teaish-configure]] > 0} {
    # teaish-options is assumed to be imported via
    # TEAISH_TCL
    teaish-configure
  }

  if {[proj-looks-like-windows]} {
    # Without this, linking of an extension will not work on Cygwin or
    # Msys2.
    msg-result "Using USE_TCL_STUBS for Unix(ish)-on-Windows environment"
    teaish-add-cflags -DUSE_TCL_STUBS=1
  }

  define TEAISH_TEST_TCL \
    [join [glob -nocomplain [get-define TEAISH_DIR]/teaish.test.tcl]]

  #define AS_LIBDIR $::autosetup(libdir)
  define TEAISH_CORE_DIR $::autosetup(libdir)/teaish
  define TEAISH_MODULE_TEST_TCL $::autosetup(libdir)/teaish/tester.tcl
  define TEAISH_TESTER_TCL $::autosetup(builddir)/teaish.tester.tcl
  teaish__configure-finalize
}

proc teaish__configure-finalize {} {

  apply {{} {
    # Set up TEAISH_DIST_FILES
    set df {}
    foreach d {
      TEAISH_TCL
      TEAISH_MAKEFILE_IN
      TEAISH_TEST_TCL
    } {
      set x [get-define $d ""]
      if {"" ne $x} {
        lappend df [file tail $x]
      }
    }
    teaish-add-dist {*}$df
  }}

  #
  # Ensure we have a pkgIndex.tcl and don't have a stale generated one
  # when rebuilding for different --with-tcl=... values. Also take
  # care not to nuke it if pkgindex-policy is 0x10.
  #
  apply {{} {
    set policy $::teaish__Config(pkgindex-policy);
    set src ""; # input source for pkgIndex.tcl
    if {$policy & 0x10} {
      # teaish-pragma --have-own-pkgIndex.tcl override. This means
      # we have a static/non-generated pkgIndex.tcl.
      set tpt [get-define TEAISH_PKGINDEX_TCL ""]
      if {"" eq $tpt} {
        set tpt [file join [get-define TEAISH_DIR] pkgIndex.tcl]
        define TEAISH_PKGINDEX_TCL $tpt
      }
      set src $tpt
    } elseif {$policy & 0x04} {
      # Found TEAISH_DIR/pkgIndex.tcl but no pkgIndex.tcl.in, so
      # assume it's a hand-written one.
      set src [get-define TEAISH_PKGINDEX_TCL]
    } elseif {$policy} {
      # Found one of {TEAISH_DIR,srcdir}/pkgIndex.tcl.in
      set src [get-define TEAISH_PKGINDEX_TCL_IN]
    } else {
      proj-fatal "Cannot determine the pkgIndex.tcl to use"
    }
    msg-result "Using pkgIndex from $src"
  }}; # $::teaish__Config(pkgindex-policy)

  proj-dot-ins-append $::autosetup(srcdir)/Makefile.in
  proj-dot-ins-append $::autosetup(srcdir)/teaish.tester.tcl.in

  if {[get-define TEAISH_OUT_OF_EXT_TREE]} {
    define TEAISH_ENABLE_DIST 0
  }

  define TEAISH_AUTOSETUP_DIR $::autosetup(libdir)/teaish
  proj-setup-autoreconfig TEAISH_AUTORECONFIG
  foreach f {
    TEAISH_CFLAGS
    TEAISH_LDFLAGS
    TEAISH_SRC
    TEAISH_DIST_FILES
  } {
    define $f [join [get-define $f]]
  }
  define TEAISH__DEFINES_MAP [teaish__dump_defs_to_list]
  proj-dot-ins-process -validate; # do not [define] after this point
  proj-if-opt-truthy teaish-dump-defines {
    make-config-header config.defines.txt \
      -none {TEAISH__*} \
      -str {BIN_* CC LD AR INSTALL LDFLAG*} \
      -bare {HAVE_*} \
      -str {TEAISH_DIST_FILES} \
      -auto {*}
  }

  #
  # If these are set up before call [options], it triggers an
  # "option already defined" error.
  #
  #proj-opt-set teaish.tcl [get-define ]
  #proj-opt-set teaish.make.in [get-define ]

  #
  # $::autosetup(builddir)/.configured is a workaround to prevent
  # concurrent executions of TEAISH_AUTORECONFIG.  MUST come last in
  # the configure process.
  #
  #proj-file-write $::autosetup(builddir)/.configured ""
}


########################################################################
# Run checks for required binaries.
proc teaish__check-common-bins {} {
  if {"" eq [proj-bin-define install]} {
    proj-warn "Cannot find install binary, so 'make install' will not work."
    define BIN_INSTALL false
  }
  if {"" eq [proj-bin-define zip]} {
    proj-warn "Cannot find zip, so 'make dist.zip' will not work."
  }
  if {"" eq [proj-bin-define tar]} {
    proj-warn "Cannot find tar, so 'make dist.tgz' will not work."
  }
}

########################################################################
# TCL...
#
# teaish__check-tcl performs most of the --with-tcl and --with-tclsh
# handling. Some related bits and pieces are performed before and
# after that function is called.
#
# Important [define]'d vars:
#
#  - TCLSH_CMD is the path to the canonical tclsh or "".
#
#  - TCL_CONFIG_SH is the path to tclConfig.sh or "".
#
#  - TCLLIBDIR is the dir to which the extension library gets
#  - installed.
#
proc teaish__check-tcl {} {
  define TCLSH_CMD false ; # Significant is that it exits with non-0
  define TCLLIBDIR ""    ; # Installation dir for TCL extension lib
  define TCL_CONFIG_SH ""; # full path to tclConfig.sh

  # Clear out all vars which would harvest from tclConfig.sh so that
  # the late-config validation of @VARS@ works even if --disable-tcl
  # is used.
  proj-tclConfig-sh-to-autosetup ""

  # TODO: better document the steps this is taking.
  set srcdir $::autosetup(srcdir)
  msg-result "Checking for a suitable tcl... "
  set use_tcl 1
  set with_tclsh [opt-val with-tclsh]
  set with_tcl [opt-val with-tcl]
  if {0} {
    # This misinteracts with the $TCL_PREFIX default: it will use the
    # autosetup-defined --prefix default
    if {"prefix" eq $with_tcl} {
      set with_tcl [get-define prefix]
    }
  }
  teaish-debug "use_tcl ${use_tcl}"
  teaish-debug "with_tclsh=${with_tclsh}"
  teaish-debug "with_tcl=$with_tcl"
  if {"" eq $with_tclsh && "" eq $with_tcl} {
    # If neither --with-tclsh nor --with-tcl are provided, try to find
    # a workable tclsh.
    set with_tclsh [proj-first-bin-of tclsh9.1 tclsh9.0 tclsh8.6 tclsh]
    teaish-debug "with_tclsh=${with_tclsh}"
  }

  set doConfigLookup 1 ; # set to 0 to test the tclConfig.sh-not-found cases
  if {"" ne $with_tclsh} {
    # --with-tclsh was provided or found above. Validate it and use it
    # to trump any value passed via --with-tcl=DIR.
    if {![file-isexec $with_tclsh]} {
      proj-fatal "TCL shell $with_tclsh is not executable"
    } else {
      define TCLSH_CMD $with_tclsh
      #msg-result "Using tclsh: $with_tclsh"
    }
    if {$doConfigLookup &&
        [catch {exec $with_tclsh $::autosetup(libdir)/find_tclconfig.tcl} result] == 0} {
      set with_tcl $result
    }
    if {"" ne $with_tcl && [file isdir $with_tcl]} {
      msg-result "$with_tclsh recommends the tclConfig.sh from $with_tcl"
    } else {
      proj-warn "$with_tclsh is unable to recommend a tclConfig.sh"
      set use_tcl 0
    }
  }
  set cfg ""
  set tclSubdirs {tcl9.1 tcl9.0 tcl8.6 lib}
  while {$use_tcl} {
    if {"" ne $with_tcl} {
      # Ensure that we can find tclConfig.sh under ${with_tcl}/...
      if {$doConfigLookup} {
        if {[file readable "${with_tcl}/tclConfig.sh"]} {
          set cfg "${with_tcl}/tclConfig.sh"
        } else {
          foreach i $tclSubdirs {
            if {[file readable "${with_tcl}/$i/tclConfig.sh"]} {
              set cfg "${with_tcl}/$i/tclConfig.sh"
              break
            }
          }
        }
      }
      if {"" eq $cfg} {
        proj-fatal "No tclConfig.sh found under ${with_tcl}"
      }
    } else {
      # If we have not yet found a tclConfig.sh file, look in $libdir
      # which is set automatically by autosetup or via the --prefix
      # command-line option.  See
      # https://sqlite.org/forum/forumpost/e04e693439a22457
      set libdir [get-define libdir]
      if {[file readable "${libdir}/tclConfig.sh"]} {
        set cfg "${libdir}/tclConfig.sh"
      } else {
        foreach i $tclSubdirs {
          if {[file readable "${libdir}/$i/tclConfig.sh"]} {
            set cfg "${libdir}/$i/tclConfig.sh"
            break
          }
        }
      }
      if {![file readable $cfg]} {
        break
      }
    }
    msg-result "Using tclConfig.sh: $cfg"
    break
  }
  define TCL_CONFIG_SH $cfg
  # Export a subset of tclConfig.sh to the current TCL-space.  If $cfg
  # is an empty string, this emits empty-string entries for the
  # various options we're interested in.
  proj-tclConfig-sh-to-autosetup $cfg

  if {"" eq $with_tclsh && $cfg ne ""} {
    # We have tclConfig.sh but no tclsh. Attempt to locate a tclsh
    # based on info from tclConfig.sh.
    set tclExecPrefix [get-define TCL_EXEC_PREFIX]
    proj-assert {"" ne $tclExecPrefix}
    set tryThese [list \
                    $tclExecPrefix/bin/tclsh[get-define TCL_VERSION] \
                    $tclExecPrefix/bin/tclsh ]
    foreach trySh $tryThese {
      if {[file-isexec $trySh]} {
        set with_tclsh $trySh
        break
      }
    }
    if {![file-isexec $with_tclsh]} {
      proj-warn "Cannot find a usable tclsh (tried: $tryThese)"
    }
  }
  define TCLSH_CMD $with_tclsh
  if {$use_tcl} {
    # Set up the TCLLIBDIR
    #
    # 2024-10-28: calculation of TCLLIBDIR is now done via the shell
    # in main.mk (search it for T.tcl.env.sh) so that
    # static/hand-written makefiles which import main.mk do not have
    # to define that before importing main.mk. Even so, we export
    # TCLLIBDIR from here, which will cause the canonical makefile to
    # use this one rather than to re-calculate it at make-time.
    set tcllibdir [get-env TCLLIBDIR ""]
    set extDirName [get-define TEAISH_LIBDIR_NAME]
    if {"" eq $tcllibdir} {
      # Attempt to extract TCLLIBDIR from TCL's $auto_path
      if {"" ne $with_tclsh &&
          [catch {exec echo "puts stdout \$auto_path" | "$with_tclsh"} result] == 0} {
        foreach i $result {
          if {[file isdir $i]} {
            set tcllibdir $i/$extDirName
            break
          }
        }
      } else {
        proj-fatal "Cannot determine TCLLIBDIR."
      }
    }
    define TCLLIBDIR $tcllibdir
  }; # find TCLLIBDIR

  if {[file-isexec $with_tclsh]} {
    msg-result "Using tclsh: $with_tclsh"
    if {$cfg ne ""} {
      define HAVE_TCL 1
    } else {
      proj-warn "Found tclsh but no tclConfig.sh."
    }
  }
  show-notices
  # If TCL is not found: if it was explicitly requested then fail
  # fatally, else just emit a warning. If we can find the APIs needed
  # to generate a working JimTCL then that will suffice for build-time
  # TCL purposes (see: proc sqlite-determine-codegen-tcl).
  if {![file-isexec $with_tclsh]} {
    proj-fatal "Did not find tclsh"
  } elseif {"" eq $cfg} {
    proj-indented-notice -error {
      Cannot find a usable tclConfig.sh file.  Use
      --with-tcl=DIR to specify a directory where tclConfig.sh can be
      found.
    }
  }
}; # teaish-check-tcl

#
# Searches $::argv and/or the build dir and/or the source dir for
# teaish.tcl and friends. Fails if it cannot find teaish.tcl or if
# there are other irreconcilable problems. If it returns 0 then it did
# not find an extension but the --help flag was seen, in which case
# that's not an error.
#
# This does not _load_ the extension, it simply locates the files
# which make up an extension.
#
# This sets up lots of defines, e.g. TEAISH_DIR.
#
proc teaish__find-extension {} {

  msg-result "Looking for teaish extension..."
  #
  # We have to handle some flags manually because the extension must
  # be loaded before [options] is run (so that the extension can
  # inject its own options).
  #
  set extM ""; # teaish.make.in
  set extT ""; # teaish.tcl
  # Helper for the foreach loop below.
  set lambdaMT {{mustHave fid dir} {
    if {[file isdir $dir]} {
      set f [file join $dir $fid]
      if {[file readable $f]} {
        return [file-normalize $f]
      } elseif {$mustHave} {
        proj-fatal "Missing required $dir/$fid"
      }
    } elseif {$mustHave} {
      proj-fatal "--teaish-extension-dir=$dir does not reference a directory"
    }
    return ""
  }}
  set largv {}; # rewritten $::argv
  foreach arg $::argv {
    #puts "*** arg=$arg"
    switch -glob -- $arg {
      --ted=* -
      --t-e-d=* -
      --teaish-extension-dir=* {
        # Ensure that $extD refers to a directory and contains a
        # teaish.tcl.
        regexp -- {--[^=]+=(.+)} $arg - extD
        set extD [file-normalize $extD]
        if {![file isdir $extD]} {
          proj-fatal "--teaish-extension-dir value is not a directory: $extD"
        }
        set extM [apply $lambdaMT 0 teaish.make.in $extD]
        set extT [apply $lambdaMT 1 teaish.tcl $extD]
        define TEAISH_DIR $extD
      }
      default {
        lappend largv $arg
      }
    }
  }
  set ::argv $largv
  set dirBld $::autosetup(builddir); # dir we're configuring under
  set dirSrc $::autosetup(srcdir);   # where teaish's configure script lives
  set dirExt [get-define TEAISH_DIR $dirBld] ; # dir with the extension
  set extEqSrc [expr {$dirExt eq $dirSrc}]   ; # are we building in-tree vis-a-vis teaish core?

  #
  # teaish.tcl is a TCL script which implements various
  # interfaces described by this framework.
  #
  # We use the first one we find in the builddir or srcdir.
  #
  if {"" eq $extT} {
    set flist [list $dirExt/teaish.tcl]
    if {!$extEqSrc} {
      lappend flist $dirSrc/teaish.tcl
    }
    if {![proj-first-file-found $flist extT]} {
      if {"--help" in $::argv} {
        return 0; # signals teaish-configure-core to process --help
      }
      proj-indented-notice -error "
Did not find any of: $flist

If you are attempting an out-of-tree build, use
 --teaish-extension-dir=/path/to/extension"
    }
  }
  if {![file readable $extT]} {
    proj-fatal "extension tcl file is not readable: $extT"
  }
  define TEAISH_TCL $extT
  if {"" eq [get-define TEAISH_DIR ""]} {
    # If this wasn't set via --teaish-extension-dir then derive it from
    # $extT.
    #puts "extT=$extT dirExt=$dirExt"
    set dirExt [file dirname $extT]
    define TEAISH_DIR $dirExt
  }
  msg-result "Extension dir            = [get-define TEAISH_DIR]"
  msg-result "Extension config         = $extT"

  #
  # teaish.make provides some of the info for the main makefile,
  # like which source(s) to build and their build flags.
  #
  # We use the first one of teaish.make.in we find in either
  # the builddir or the srcdir.
  #
  if {"" eq $extM} {
    set flist [list $dirExt/teaish.make.in]
    if {!$extEqSrc} {
      lappend flist $dirSrc/teaish.make.in
    }
    proj-first-file-found $flist extM
  }
  if {"" ne $extM && [file readable $extM]} {
    define TEAISH_MAKEFILE_IN $extM
    define TEAISH_MAKEFILE [file rootname [file tail $extM]]
    proj-dot-ins-append $extM [get-define TEAISH_MAKEFILE]
    msg-result "Extension makefile       = $extM"
  } else {
    define TEAISH_MAKEFILE_IN ""
    define TEAISH_MAKEFILE ""
    #proj-warn "Did not find a teaish.make.in."
  }

  # Look for teaish.pkginit.tcl
  set flist [list $dirExt/teaish.pkginit.tcl.in $dirExt/teaish.pkginit.tcl]
  if {[proj-first-file-found $flist extI]} {
    if {[string match *.in $extI]} {
      proj-dot-ins-append $extI
      define TEAISH_PKGINIT_TCL_IN $extI
      define TEAISH_PKGINIT_TCL [file tail [file rootname $extI]]
    } else {
      define TEAISH_PKGINIT_TCL_IN ""
      define TEAISH_PKGINIT_TCL $extI
    }
    teaish-add-dist [file tail $extI]
    msg-result "Extension post-load init = $extI"
    define TEAISH_PKGINIT_TCL_TAIL [file tail [get-define TEAISH_PKGINIT_TCL]]; # for use in pkgIndex.tcl
  }

  # Look for pkgIndex.tcl[.in]...
  set piPolicy 0
  if {[proj-first-file-found $dirExt/pkgIndex.tcl.in extPI]} {
    # If $dirExt/pkgIndex.tcl.in exists, generate ./pkgIndex.tcl from
    # it.
    define TEAISH_PKGINDEX_TCL_IN $extPI
    define TEAISH_PKGINDEX_TCL [file rootname [file tail $extPI]]
    proj-dot-ins-append $extPI
    teaish-add-dist [file tail $extPI]
    set piPolicy 0x01
  } elseif {!$extEqSrc && [proj-first-file-found $dirSrc/pkgIndex.tcl.in extPI]} {
    # If $dirSrc/pkgIndex.tcl.in exists, generate ./pkgIndex.tcl from
    # it.
    define TEAISH_PKGINDEX_TCL_IN $extPI
    define TEAISH_PKGINDEX_TCL [file rootname [file tail $extPI]]
    proj-dot-ins-append $extPI
    set piPolicy 0x02
  } elseif {[proj-first-file-found $dirExt/pkgIndex.tcl extPI]} {
    # if TEAISH_DIR/pkgIndex.tcl exists, assume it's a static file
    # and use it.
    define TEAISH_PKGINDEX_TCL_IN ""
    define TEAISH_PKGINDEX_TCL $extPI
    proj-dot-ins-append $extPI
    set piPolicy 0x04
  }

  set ::teaish__Config(pkgindex-policy) $piPolicy

  #
  # Set some sane defaults...
  #
  define TEAISH_NAME [file tail [file dirname $extT]]
  define TEAISH_PKGNAME [get-define TEAISH_NAME]
  define TEAISH_LIBDIR_NAME [get-define TEAISH_PKGNAME]
  define TEAISH_VERSION 0.0.0

  # TEAISH_OUT_OF_EXT_TREE = 1 if we're building from a dir other
  # than the extension's home dir.
  set dteaish [file-normalize [get-define TEAISH_DIR]]
  define TEAISH_OUT_OF_EXT_TREE \
    [expr {[file-normalize $::autosetup(builddir)] ne $dteaish}]

  #
  # Defines which extensions may optionally make but are not required
  # to.
  #
  foreach {optionalDef dflt} [subst {
    TEAISH_LOAD_PREFIX "[string totitle [get-define TEAISH_PKGNAME]]"
    TEAISH_MAKEFILE_CODE ""
    TEAISH_ENABLE_DIST 1
  }] {
    define $optionalDef $dflt
  }

  return 1
}

# @teaish-add-cflags ?-p|prepend? ?-define? cflags...
#
# Equivalent to [proj-define-amend TEAISH_CFLAGS {*}$args].
proc teaish-add-cflags {args} {
  proj-define-amend TEAISH_CFLAGS {*}$args
}

# @teaish-add-ldflags ?-p|-prepend? ?-define? ldflags...
#
# Equivalent to [proj-define-amend TEAISH_LDFLAGS {*}$args].
#
# Typically, -lXYZ flags need to be in "reverse" order, with each -lY
# resolving symbols for -lX's to its left. This order is largely
# historical, and not relevant on all environments, but it is
# technically correct and still relevant on some environments.
#
# See: teaish-prepend-ldflags
proc teaish-add-ldflags {args} {
  proj-define-amend TEAISH_LDFLAGS {*}$args
}

# @teaish-prepend-ldflags args...
#
# Functionally equivalent to [teaish-add-ldflags -p {*}$args]
proc teaish-prepend-ldflags {args} {
  teaish-add-ldflags -p {*}$args
}

# @teaish-add-src ?-dist? ?-dir? src-files...
#
# Appends all non-empty $args to TEAISH_SRC.
#
# If passed -dist then it also passes each filename, as-is, to
# [teaish-add-dist].
#
# If passed -dir then each src-file has the TEAISH_DIR prepended to
# it for before they're added to TEAISH_SRC. As often as not, that
# will be the desired behavior so that out-of-tree builds can find the
# sources, but there are cases where it's not desired (e.g. when using
# a source file from outside of the extension's dir).
proc teaish-add-src {args} {
  set dist 0
  set xdir 0
  set i 0
  foreach arg $args {
    switch -exact -- $arg {
      -dist {
        set dist 1
        set args [lassign $args -]
      }
      -dir {
        set xdir 1
        set args [lassign $args -]
      }
      default {
        break;
      }
    }
  }
  if {$dist} {
    teaish-add-dist {*}$args
  }
  if {$xdir} {
    set xargs {}
    set d [get-define TEAISH_DIR]
    foreach arg $args {
      if {"" ne $arg} {
        lappend xargs $d/$arg
      }
    }
    set args $xargs
  }
  proj-define-append TEAISH_SRC {*}$args
}

# @teaish-add-dist files-or-dirs...
#
# Equivalent to [proj-define-apend TEAISH_DIST_FILES ...].
proc teaish-add-dist {args} {
  proj-define-amend TEAISH_DIST_FILES {*}$args
}

# teaish-add-install files...
# Equivalent to [proj-define-apend TEAISH_INSTALL_FILES ...].
#proc teaish-add-install {args} {
#  proj-define-amend TEAISH_INSTALL_FILES {*}$args
#}

# @teash-append-make args...
#
# Appends makefile code to the TEAISH_MAKEFILE_CODE define. Each
# arg may be any of:
#
# -tab: emit a literal tab
# -nl: emit a literal newline
# -nltab: short for -nl -tab
# -eol: emit a backslash-escaped end-of-line
# -eoltab: short for -eol -tab
#
# Anything else is appended verbatim. This function adds no additional
# spacing between each argument nor between subsequent invocations.
#
proc teaish-add-make {args} {
  set out [get-define TEAISH_MAKEFILE_CODE ""]
  foreach a $args {
    switch -exact -- $a {
      -eol    { set a " \\\n" }
      -eoltab { set a " \\\n\t" }
      -tab    { set a "\t" }
      -nl     { set a "\n" }
      -nltab  { set a "\n\t" }
    }
    append out $a
  }
  define TEAISH_MAKEFILE_CODE $out
}

# @teaish-make-config-header filename
#
# Invokes autosetup's [make-config-header] and passes it $filename and
# a relatively generic list of options for controlling which defined
# symbols get exported. Clients which need more control over the
# exports can copy/paste/customize this.
#
# The exported file is then passed to [proj-touch] because, in
# practice, that's sometimes necessary to avoid build dependency
# issues.
proc teaish-make-config-header {filename} {
  make-config-header $filename \
    -bare {} \
    -none {HAVE_CFLAG_* LDFLAGS_* SH_*} \
    -auto {SIZEOF_* HAVE_* TEAISH_*  TCL_*} \
    -none *
  proj-touch $filename; # help avoid frequent unnecessary auto-reconfig
}

# @teaish-feature-cache-set ?$key? value
#
# Sets a feature-check cache entry with the given key.
# See proj-cache-set for the key's semantics.
proc teaish-feature-cache-set {{key 0} val} {
  proj-cache-set $key 1 $val
}

# @teaish-feature-cache-check ?$key? tgtVarName
#
# Checks for a feature-check cache entry with the given key.
# See proj-cache-set for the key's semantics.
#
# If the feature-check cache has a matching entry then this function
# assigns its value to tgtVar and returns 1, else it assigns tgtVar to
# "" and returns 0.
#
# See proj-cache-check for $key's semantics.
proc teaish-feature-cache-check {{key 0} tgtVar} {
  upvar $tgtVar tgt
  proj-cache-check $key 1 tgt
}

# @teaish-check-cached@ ?-nostatus? msg script
#
# A proxy for feature-test impls which handles caching of a feature
# flag check on per-function basis, using the calling scope's name as
# the cache key.
#
# It emits [msg-checking $msg]. If $msg is empty then it defaults to
# the name of the caller's scope. At the end, it will [msg-result "ok"]
# [msg-result "no"] unless -nostatus is used, in which case the caller
# is responsible for emitting at least a newline when it's done.
#
# This function checks for a cache hit before running $script and
# caching the result. If no hit is found then $script is run in the
# calling scope and its result value is stored in the cache. This
# routine will intercept a 'return' from $script.
#
# Flags:
#
#   -nostatus = do not emit "ok" or "no" at the end. This presumes
#    that the caller will emit at least one newline before turning.
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
    #puts "***** cached-check got code=$code rc=$rc"
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
    #puts "**** code=$code rc=$rc"
    return {*}$xopt $rc
  }
}

########################################################################
# Internal helper for teaish__defs_format_: returns a JSON-ish quoted
# form of the given string-type values. It only performs the most
# basic of escaping. The input must not contain any control
# characters.
proc teaish__quote_str {value} {
  return \"[string map [list \\ \\\\ \" \\\"] $value]\"
}

########################################################################
# Internal helper for teaish__dump_defs_to_list. Expects to be passed a
# [define] name and the variadic $args which are passed to
# teaish__dump_defs_to_list.. If it finds a pattern match for the given
# $name in the various $args, it returns the type flag for that $name,
# e.g. "-str" or "-bare", else returns an empty string.
proc teaish__defs_type {name spec} {
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
# An internal impl detail. Requires a data type specifier, as used by
# make-config-header, and a value. Returns the formatted value or the
# value $::teaish__Config(defs-skip) if the caller should skip
# emitting that value.
set teaish__Config(defs-skip) "-teaish__defs_format sentinel"
proc teaish__defs_format {type value} {
  switch -exact -- $type {
    -bare {
      # Just output the value unchanged
    }
    -none {
      set value $::teaish__Config(defs-skip)
    }
    -str {
      set value [teaish__quote_str_ $value]
    }
    -auto {
      # Automatically determine the type
      if {![string is integer -strict $value]} {
        set value [teaish__quote_str $value]
      }
    }
    -array {
      set ar {}
      foreach v $value {
        set v [teaish__defs_format -auto $v]
        if {$::teaish__Config(defs-skip) ne $v} {
          lappend ar $v
        }
      }
      set value "\[ [join $ar {, }] \]"
    }
    "" {
      set value $::teaish__Config(defs-skip)
    }
    default {
      proj-fatal "Unknown type in [proj-current-scope 1]: $type"
    }
  }
  return $value
}

#
# Returns Tcl code in the form of code which evaluates to a list of
# configure-time DEFINEs in the form {key val key2 val...}. It will
# misbehave for values which are not numeric or simple strings.
#
proc teaish__dump_defs_to_list {args} {
  set lines {}
  lappend lines "\{"
  set skipper $::teaish__Config(defs-skip)
  lappend args \
    -none {
      TEAISH__*
      TEAISH_MAKEFILE_CODE
      TEAISH_AUTORECONFIGURE
      AM_* AS_*
    } \
    -bare {SIZEOF_* HAVE_DECL_*} \
    -auto *
  foreach n [lsort [dict keys [all-defines]]] {
    set type [teaish__defs_type $n $args]
    set value [teaish__defs_format $type [get-define $n]]
    if {$skipper ne $value} {
      lappend lines "$n $value"
    }
  }
  lappend lines "\}"
  return [join $lines "\n"]
}

# @teaish-pragma ...flags
#
# Offers a way to tweak how teaish's core behaves in some cases, in
# particular those which require changing how the core looks for an
# extension and its files.
#
# Accepts the following flags. Those marked with [L] are safe to use
# during initial loading of tclish.tcl (recall that most teaish APIs
# cannot be used until [teaish-configure] is called).
#
#   --have-own-pkgIndex.tcl [L]: Tells teaish that ./pkgIndex.tcl is
#    not a generated file, so it will not try to overwrite or delete
#    it.
#
#   --disable-dist [L]: disables the "dist" parts of the filtered
#     Makefile.  May be used during initial loading of teaish.tcl.
#
# Emits a warning message for unknown arguments.
proc teaish-pragma {args} {
  foreach arg $args {
    switch -exact -- $arg {

      --disable-dist {
        define TEAISH_ENABLE_DIST 0
      }

      --have-own-pkgIndex.tcl {
        set pi [file join [get-define TEAISH_DIR] pkgIndex.tcl]
        if {![file exists $pi]} {
          proj-fatal "teaish-pragmas $arg found no pkgIndex.tcl"
        }
        define TEAISH_PKGINDEX_TCL $pi
        set ::teaish__Config(pkgindex-policy) 0x10
      }

      default {
        proj-warn "Unknown [proj-current-scope] flag: $arg"
      }
    }
  }
}


# @teaish-enable-dist ?yes?
#
# Explicitly enables or disables the "dist" rules in the default
# Makefile.in. This is equivalent to defining TEAISH_ENABLE_DIST
# to $yes (which must be 0 or 1).
#
# By default, dist creation is enabled.
#
proc teaish-enable-dist {{yes 1}} {
  define TEAISH_ENABLE_DIST $yes
}


#
# Handles --teaish-create-extension=TARGET-DIR
#
proc teaish__create_extension {dir} {
  set force [opt-bool teaish-force]
  if {"" eq $dir} {
    proj-fatal "--teaish-create-extension=X requires a directory name."
  }
  file mkdir $dir
  set cwd [pwd]
  #set dir [file-normalize [file join $cwd $dir]]
  msg-result "Created dir $dir"
  cd $dir
  if {!$force} {
    # Ensure that we don't blindly overwrite anything
    foreach f {
      teaish.c
      teaish.tcl
      teaish.make.in
      teaish.test.tcl
    } {
      if {[file exists $f]} {
        error "Cowardly refusing to overwrite $dir/$f. Use --teaish-force to overwrite."
      }
    }
  }

  set name [file tail $dir]
  set pkgName $name
  set version 0.0.1
  set loadPrefix [string totitle $pkgName]
  set content "define TEAISH_NAME ${name}
define TEAISH_VERSION ${version}
# define TEAISH_PKGNAME ${pkgName}
# define TEAISH_LIBDIR_NAME ${name}
define TEAISH_LOAD_PREFIX ${loadPrefix}
proc teaish-options {} {}
proc teaish-configure {} {
  set d \[get-define TEAISH_DIR]
  teaish-add-src \$d/teaish.c
  teaish-add-dist teaish.c
}
"
  proj-file-write teaish.tcl $content
  msg-result "Created teaish.tcl"

  set content [subst -nocommands -nobackslashes {
#include <tcl.h>
static int
${loadPrefix}_Cmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]){
  Tcl_SetObjResult(interp, Tcl_NewStringObj("this is the ${name} extension", -1));
  return TCL_OK;
}

extern int DLLEXPORT ${loadPrefix}_Init(Tcl_Interp *interp){
  if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
    return TCL_ERROR;
  }
  if (Tcl_PkgProvide(interp, "${name}", "${version}") == TCL_ERROR) {
    return TCL_ERROR;
  }
  Tcl_CreateObjCommand(interp, "${name}", ${loadPrefix}_Cmd, NULL, NULL);
  return TCL_OK;
}
}]
  proj-file-write teaish.c $content
  msg-result "Created teaish.c"

  set content "# teaish makefile for the ${name} extension
# tx.src      = \$(tx.dir)/teaish.c
# tx.LDFLAGS  =
# tx.CFLAGS   =
"
  proj-file-write teaish.make.in $content
  msg-result "Created teaish.make.in"

  msg-result "Created new extension $name in \[$dir]"

  cd $cwd
}
