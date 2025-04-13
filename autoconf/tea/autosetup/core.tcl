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
  msg-result "Source dir  = $::autosetup(srcdir)"
  msg-result "Build dir   = $::autosetup(builddir)"
  msg-result "TEA-ish Version = [get-define TEAISH_VERSION]"
}

array set teaishConfig [proj-strip-hash-comments {
  # set to 1 to enable some internal debugging output
  debug-enabled 0
}]

#
# Returns true if any arg in $::argv matches the given glob, else
# returns false.
#
proc teaish__argv-has {glob} {
  foreach arg $::argv {
    if {[string match $glob $arg]} {
      return 1
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
  # "Declare" some defines for potential later use.
  #
  foreach f {
    TEAISH_MAKEFILE
    TEAISH_MAKEFILE_IN
    TEAISH_TCL
    TEAISH_CFLAGS
    TEAISH_LDFLAGS
    TEAISH_SRC
    TEAISH_DIST_FILES
    TEAISH_PKGINIT_TCL
    EXTRA_CFLAGS
  } {
    define $f {}
  }

  set gotExt 0; # True if an extension config is found
  if {![teaish__argv-has --teaish-create-extension*]} {
    set gotExt [teaish__find-extension]
  }

  if {$gotExt} {
    proj-assert {[file exists [get-define TEAISH_TCL]]}
    uplevel 1 {
      source [get-define TEAISH_TCL]
    }

    if {"" eq [get-define TEAISH_NAME ""]} {
      proj-fatal "[get-define TEAISH_TCL] did not define TEAISH_NAME"
    } elseif {"" eq [get-define TEAISH_VERSION ""]} {
      proj-fatal "[get-define TEAISH_TCL] did not define TEAISH_VERSION"
    }
  }

  set opts [proj-strip-hash-comments {
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
    t-e-d:
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
  }]; # $opts

  if {[llength [info proc teaish-options]] > 0} {
    # teaish-options is assumed to be imported via
    # TEAISH_TCL
    set opts [teaish-combine-option-lists $opts [teaish-options]]
  }

  #lappend opts "soname:=duplicateEntry => {x}"; #just testing
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

  set ::teaishConfig(debug-enabled) [opt-bool teaish-debug]

  if {[proj-opt-was-provided teaish-create-extension]} {
    teaish__create-extension [opt-val teaish-create-extension]
    return
  }
  proj-assert {1==$gotExt} "Else we cannot have gotten this far"

  teaish__configure-phase1
}


########################################################################
# Internal config-time debugging output routine. It is not legal to
# call this from the global scope.
proc teaish-debug {msg} {
  if {$::teaishConfig(debug-enabled)} {
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

  #
  # Ensure we don't have a stale pkgIndex.tcl when rebuilding for different
  # --with-tcl=... values.
  #
  file delete -force pkgIndex.tcl

  define TEAISH_TEST_TCL \
    [join [glob -nocomplain [get-define TEAISH_DIR]/teaish.test.tcl]]

  #define AS_LIBDIR $::autosetup(libdir)
  define TEAISH_LIBDIR $::autosetup(libdir)/teaish
  define TEAISH_TESTER_TCL [get-define TEAISH_LIBDIR]/tester.tcl
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
      TEAISH_PKGINIT_TCL
    } {
      set x [get-define $d ""]
      if {"" ne $x} {
        lappend df [file tail $x]
      }
    }
    teaish-add-dist {*}$df
  }}

  foreach f {
    TEAISH_CFLAGS
    TEAISH_LDFLAGS
    TEAISH_SRC
    TEAISH_DIST_FILES
  } {
    define $f [join [get-define $f]]
  }

  define TEAISH_AUTOSETUP_DIR $::autosetup(libdir)
  proj-setup-autoreconfig TEAISH_AUTORECONFIG
  proj-dot-ins-append $::autosetup(srcdir)/Makefile.in
  proj-dot-ins-append $::autosetup(srcdir)/pkgIndex.tcl.in
  proj-dot-ins-append $::autosetup(srcdir)/teaish.tester.tcl.in

  set dotIns [proj-dot-ins-list]
  #puts "*** dotIns = $dotIns"
  proj-dot-ins-process; # do not [define] after this point
  foreach e $dotIns {
    proj-validate-no-unresolved-ats [lindex $e 1]
  }

  proj-if-opt-truthy teaish-dump-defines {
    make-config-header config.defines.txt \
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
# teaish.make and teaish.tcl. Fails if it cannot find one of
# them. If it returns 0 then it did not find an extension but the
# --help flag was seen, in which case that's not an error.
#
proc teaish__find-extension {} {

  msg-result "Looking for teaish extension..."
  #
  # We have to handle some flags manually because the extension must
  # be loaded before [options] is run (so that the extension can
  # inject its own options).
  #
  set largv {}; # rewritten $::argv
  set extM ""; # teaish.make.in
  set extT ""; # teaish.tcl
  set lambdaM {{f} {
    if {[file isdir $f]} {
      set f [file join $f teaish.make.in]
    }
    if {[file readable $f]} {
      return [file-normalize $f]
    }
    return ""
  }}
  set lambdaT {{f} {
    if {[file isdir $f]} {
      set f [file join $f teaish.tcl]
    }
    if {![file readable $f]} {
      proj-fatal "extension tcl file is not readable: $f"
    }
    return [file-normalize $f]
  }}
#  set gotNonFlag 0
  foreach arg $::argv {
    #puts "*** arg=$arg"
    switch -glob -- $arg {
      --ted=* -
      --t-e-d=* -
      --teaish-extension-dir=* {
        regexp -- {--[^=]+=(.+)} $arg - extD
        set extD [file-normalize $extD]
        if {![file isdir $extD]} {
          proj-fatal "--teaish-extension-dir value is not a directory: $extD"
        }
        set extM [apply $lambdaM [file join $extD teaish.make.in]]
        set extT [apply $lambdaT [file join $extD teaish.tcl]]
        define TEAISH_DIR $extD
      }
      default {
        # We'd like to treat the first non-flag argument as
        # --teaish-extension-dir, but autosetup does not accept args
        # except in the form --flag or X=Y
        lappend largv $arg
#
#      --* {
#        lappend largv $arg
#      }
#      default {
#        if {$gotNonFlag || "" ne $extT} {
#          lappend largv $arg
#        } else {
#          incr gotNonFlag
#          msg-checking "Treating fist non-flag argument as --teaish-extension-dir ... "
#          if {[catch [set extD [file-normalize $arg]]]} {
#            msg-result "dir name not normalizable: $arg"
#            lappend largv $arg
#          } else {
#            set extM [apply $lambdaM [file join $arg teaish.make.in]]
#            set extT [apply $lambdaT [file join $arg teaish.tcl]]
#            define TEAISH_DIR $extD
#            msg-result "$arg"
#          }
#        }
#      }
      }
    }
  }
  set ::argv $largv
  set dbld $::autosetup(builddir)
  set dsrc $::autosetup(srcdir)
  set dext [get-define TEAISH_DIR $::autosetup(builddir)]

  #
  # teaish.tcl is a TCL script which implements various
  # interfaces described by this framework.
  #
  # We use the first one we find in the builddir or srcdir.
  #
  if {"" eq $extT} {
    set flist [list $dext/teaish.tcl $dsrc/teaish.tcl]
    if {![proj-first-file-found $flist extT]} {
      if {"--help" in $::argv} {
        return 0
      }
      proj-indented-notice -error "
Did not find any of: $flist

If you are attempting an out-of-tree build, be sure to
use --teaish-extension-dir=/path/to/extension"
    }
  }
  if {![file readable $extT]} {
    proj-fatal "extension tcl file is not readable: $extT"
  }
  msg-result "Extension config         = $extT"
  define TEAISH_TCL $extT
  if {"" eq [get-define TEAISH_DIR ""]} {
    # If this wasn't set via --teaish.dir then derive it from
    # --teaish.tcl.
    #puts "extT=$extT"
    define TEAISH_DIR [file dirname $extT]
  }

  #
  # teaish.make provides some of the info for the main makefile,
  # like which source(s) to build and their build flags.
  #
  # We use the first one of teaish.make.in we find in either
  # the builddir or the srcdir.
  #
  if {"" eq $extM} {
    set flist [list $dext/teaish.make.in $dsrc/teaish.make.in]
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
    #proj-warn "Did not find an teaish.make.in."
  }

  set extI $dext/teaish.pkginit.tcl
  if {[file exists $extI]} {
    define TEAISH_PKGINIT_TCL $extI
    msg-result "Extension post-load init = $extI"
    #teaish-add-install $extI
  }

  #
  # Set some sane defaults...
  #
  define TEAISH_NAME [file tail [file dirname $extT]]
  define TEAISH_PKGNAME [get-define TEAISH_NAME]
  define TEAISH_LIBDIR_NAME [get-define TEAISH_PKGNAME]
  define TEAISH_VERSION 0.0.0

  # THEAISH_OUT_OF_EXT_TREE = 1 if we're building from a dir other
  # than the extension's home dir.
  define THEAISH_OUT_OF_EXT_TREE \
    [expr {[file-normalize $::autosetup(builddir)] ne [file-normalize [get-define TEAISH_DIR]]}]

  #
  # Defines which extensions may optionally make but are not required
  # to.
  #
  foreach {optionalDef dflt} [subst {
    TEAISH_LOAD_PREFIX "[string totitle [get-define TEAISH_PKGNAME]]"
    TEAISH_MAKEFILE_CODE ""
  }] {
    define $optionalDef $dflt
  }
  return 1
}

# Internal helper to append $args to [define-append] $def
proc teaish__append_stuff {def args} {
  foreach a $args {
    if {"" ne $a} {
      define-append $def {*}$a
    }
  }
}

# @teaish-add-cflags ?-define? cflags...
#
# Appends all non-empty $args to TEAISH_CFLAGS
#
# If -define is used then each flag is assumed to be a [define]'d
# symbol name and [get-define X ""] used to fetch it.
proc teaish-add-cflags {args} {
  set isdefs 0
  if {[lindex $args 0] in {-d -define}} {
    set args [lassign $args -]
    set xargs [list]
    foreach arg $args {
      lappend xargs [get-define $arg ""]
    }
    set args $xargs
  }
  teaish__append_stuff TEAISH_CFLAGS {*}$args
}

# @teaish-add-cflags ?-p|-prepend? ?-define? ldflags...
#
# Appends all non-empty $args to TEAISH_LDFLAGS unless the first
# argument is one of (-p | -prepend), in which case it prepends all
# arguments, in their given order, to TEAISH_LDFLAGS.
#
# If -define is used then each argument is assumed to be a [define]'d
# flag and [get-define X ""] is used to fetch it.
#
# Typically, -lXYZ flags need to be in "reverse" order, with each -lY
# resolving symbols for -lX's to its left. This order is largely
# historical, and not relevant on all environments, but it is
# technically correct and still relevant on some environments.
#
# See: teaish-prepend-ldflags
proc teaish-add-ldflags {args} {
  set prepend 0
  set isdefs 0
  set xargs [list]
  foreach arg $args {
    switch -exact -- $arg {
      -p - -prepend { set prepend 1 }
      -d - -define {
        set isdefs 1
      }
      default {
        lappend xargs $arg
      }
    }
  }
  set args $xargs
  if {$isdefs} {
    set xargs [list]
    foreach arg $args {
      lappend xargs [get-define $arg ""]
    }
    set args $xargs
  }
  if {$prepend} {
    lappend args {*}[get-define TEAISH_LDFLAGS ""]
    define TEAISH_LDFLAGS [join $args]; # join to eliminate {} entries
  } else {
    teaish__append_stuff TEAISH_LDFLAGS {*}$args
  }
}

# @teaish-prepend-ldflags args...
#
# Functionally equivalent to [teaish-add-ldflags -p {*}$args]
proc teaish-prepend-ldflags {args} {
  teaish-add-ldflags -p {*}$args
}

# @teaish-add-cflags ?-dist? ?-dir? src-files...
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
        #lappend xargs $arg
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
      lappend xargs $d/$arg
    }
    set args $xargs
  }
  teaish__append_stuff TEAISH_SRC {*}$args
}

# @teaish-add-dist files-or-dirs...
# Appends all non-empty $args to TEAISH_DIST_FILES
proc teaish-add-dist {args} {
  teaish__append_stuff TEAISH_DIST_FILES {*}$args
}

# teaish-add-install files...
# Appends all non-empty $args to TEAISH_INSTALL_FILES
#proc teaish-add-install {args} {
#  teaish__append_stuff TEAISH_INSTALL_FILES {*}$args
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

# internal cache for feature checks.
array set teaish__fCache {}

# @teaish-feature-cache-set ?$depth? value
#
# Sets a feature-check cache entry with a key equal to
# [proj-current-scope [expr {$depth+1}]] and the given value.
proc teaish-feature-cache-set {{depth 0} val} {
  array set ::teaish__fCache [list [proj-current-scope [expr {$depth + 1}]] $val]
  return $val
}

# @teaish-feature-cache-check ?$depth? tgtVarName
#
# If the feature-check cache has an entry named [proj-current-scope
# [expr {$depth+1}]] then this function assigns its value to tgtVar and
# returns 1, else it assigns tgtVar to "" and returns 0.
#
proc teaish-feature-cache-check {{depth 0} tgtVar} {
  upvar $tgtVar tgt
  set scope [proj-current-scope [expr {$depth + 1}]]
  if {[info exists ::teaish__fCache($scope)]} {
    set tgt $::teaish__fCache($scope)
    return 1
  }
  set tgtVar ""
  return 0
}

# @teash-combine-option-lists list1 ?...listN?
#
# Expects each argument to be a list of options compatible with
# autosetup's [options] function. This function concatenates the
# contents of each list into a new top-level list, stripping the outer
# list part of each argument. The intent is that teaish-options
# implementations can use this to combine multiple lists, e.g. from
# functions teaish-check-openssl-options.
proc teaish-combine-option-lists {args} {
  set rv [list]
  foreach e $args {
    foreach x $e {
      lappend rv $x
    }
  }
  return $rv
}

#
# Handles --teaish-create-extension=TARGET-DIR
#
proc teaish__create-extension {dir} {
  set force [opt-bool teaish-force]
  file mkdir $dir
  set cwd [pwd]
  set dir [file-normalize [file join $cwd $dir]]
  msg-result "Created dir $dir"
  cd $dir
  set flist {teaish.tcl}
  foreach f $flist {
    if {!$force && [file exists $f]} {
      error "Cowardly refusing to overwrite $dir/$f. Use --teaish-force to overwrite."
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
