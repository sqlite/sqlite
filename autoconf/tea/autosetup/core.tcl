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

#
# API-internal settings and shared state.
array set teaish__Config [proj-strip-hash-comments {
  #
  # Teaish's version number, not to be confused with
  # teaish__PkgInfo(-version).
  #
  version 0.1-beta

  # set to 1 to enable some internal debugging output
  debug-enabled 0
  #
  # 0    = don't yet have extension's pkgindex
  # 0x01 = found TEAISH_EXT_DIR/pkgIndex.tcl.in
  # 0x02 = found srcdir/pkgIndex.tcl.in
  # 0x10 = found TEAISH_EXT_DIR/pkgIndex.tcl (static file)
  # 0x20 = static-pkgIndex.tcl pragma: behave as if 0x10
  #
  # Reminder: it's significant that the bottom 4 bits be
  # cases where teaish manages ./pkgIndex.tcl.
  #
  pkgindex-policy 0

  #
  # If 1+ then teaish__verbose will emit messages.
  #
  verbose 0

  #
  # Mapping of pkginfo -flags to their TEAISH_xxx define (if any).
  #
  pkginfo-f2d {
    -name TEAISH_NAME
    -pkgName TEAISH_PKGNAME
    -version TEAISH_VERSION
    -libDir TEAISH_LIBDIR_NAME
    -loadPrefix TEAISH_LOAD_PREFIX
    -vsatisfies TEAISH_VSATISFIES
    -options {}
    -pragmas {}
  }

  #
  # Queues for use with teaish-checks-queue and teaish-checks-run.
  #
  queued-checks-pre {}
  queued-checks-post {}

  # Whether or not "make dist" parts are enabled. They get enabled
  # when building from an extension's dir, disabled when building
  # elsewhere.
  dist-enabled 1

  # By default we enable compilation of a native extension but if the
  # extension has no native code or the user wants to take that over
  # via teaish.make.in or provide a script-only extension, we will
  # elide the default compilation rules if this is 0.
  dll-enabled 1

  # Files to include in the "make dist" bundle.
  dist-files {}

  # List of source files for the extension.
  extension-src {}

  # Path to the teaish.tcl file.
  teaish.tcl {}

  # Dir where teaish.tcl is found.
  extension-dir {}
}]
set teaish__Config(core-dir) $::autosetup(libdir)/teaish

#
# Array of info managed by teaish-pkginfo-get and friends.  Has the
# same set of keys as $teaish__Config(pkginfo-f2d).
#
array set teaish__PkgInfo {}

#
# Runs {*}$args if $lvl is <= the current verbosity level, else it has
# no side effects.
#
proc teaish__verbose {lvl args} {
  if {$lvl <= $::teaish__Config(verbose)} {
    {*}$args
  }
}

#
# @teaish-argv-has flags...
#
# Returns true if any arg in $::argv matches any of the given globs,
# else returns false.
#
proc teaish-argv-has {args} {
  foreach glob $args {
    foreach arg $::argv {
      if {[string match $glob $arg]} {
        return 1
      }
    }
  }
  return 0
}

if {[teaish-argv-has --teaish-verbose --t-v]} {
  # Check this early so that we can use verbose-only messages in the
  # pre-options-parsing steps.
  set ::teaish__Config(verbose) 1
  #teaish__verbose 1 msg-result "--teaish-verbose activated"
}

msg-quiet use system ; # Outputs "Host System" and "Build System" lines
if {"--help" ni $::argv} {
  teaish__verbose 1 msg-result "TEA(ish) Version = $::teaish__Config(version)"
  teaish__verbose 1 msg-result "Source dir    = $::autosetup(srcdir)"
  teaish__verbose 1 msg-result "Build dir     = $::autosetup(builddir)"
}

#
# @teaish-configure-core
#
# Main entry point for the TEA-ish configure process. auto.def's primary
# (ideally only) job should be to call this.
#
proc teaish-configure-core {} {
  proj-tweak-default-env-dirs

  set gotExt 0; # True if an extension config is found
  if {![teaish-argv-has --teaish-create-extension=* --t-c-e=*]} {
    # Don't look for an extension if we're in --t-c-e mode
    set gotExt [teaish__find_extension]
  }

  #
  # Set up the core --flags. This needs to come before teaish.tcl is
  # sourced so that that file can use teaish-pkginfo-set to append
  # options.
  #
  options-add [proj-strip-hash-comments {
    with-tcl:DIR
      => {Directory containing tclConfig.sh or a directory one level up from
          that, from which we can derive a directory containing tclConfig.sh.
          Defaults to the $TCL_HOME environment variable.}

    with-tclsh:PATH
      => {Full pathname of tclsh to use.  It is used for trying to find
          tclConfig.sh.  Warning: if its containing dir has multiple tclsh
          versions, it may select the wrong tclConfig.sh!
          Defaults to the $TCLSH environment variable.}

    # TEA has --with-tclinclude but it appears to only be useful for
    # building an extension against an uninstalled copy of TCL's own
    # source tree. The policy here is that either we get that info
    # from tclConfig.sh or we give up.
    #
    # with-tclinclude:DIR
    #   => {Specify the directory which contains the tcl.h. This should not
    #       normally be required, as that information comes from tclConfig.sh.}

    # We _generally_ want to reduce the possibility of flag collisions with
    # extensions, and thus use a teaish-... prefix on most flags. However,
    # --teaish-extension-dir is frequently needed, so...
    #
    # As of this spontaneous moment, we'll settle on using --t-A-X to
    # abbreviate --teaish-A...-X... flags when doing so is
    # unambiguous...
    ted: t-e-d:
    teaish-extension-dir:DIR
      => {Looks for an extension in the given directory instead of the current
          dir.}

    t-c-e:
    teaish-create-extension:TARGET_DIRECTORY
      => {Writes stub files for creating an extension. Will refuse to overwrite
          existing files without --teaish-force.}

    t-f
    teaish-force
      => {Has a context-dependent meaning (autosetup defines --force for its
          own use).}

    t-d-d
    teaish-dump-defines
      => {Dump all configure-defined vars to config.defines.txt}

    t-v
    teaish-verbose=0
      => {Enable more (often extraneous) messages from the teaish core.}

    t-d
    teaish-debug => {Enable teaish-specific debug output}
  }]; # main options.

  if {$gotExt} {
    proj-assert {"" ne [teaish-pkginfo-get -name]}
    proj-assert {[file exists $::teaish__Config(teaish.tcl)]} \
      "Expecting to have found teaish.tcl by now"
    uplevel 1 [list source $::teaish__Config(teaish.tcl)]
    # Set up some default values if the extension did not set them.
    # This must happen _after_ it's sourced but before
    # teaish-configure is called.
    foreach {pflag key type val} {
      - TEAISH_CFLAGS           -v ""
      - TEAISH_LDFLAGS          -v ""
      - TEAISH_MAKEFILE         -v ""
      - TEAISH_MAKEFILE_CODE    -v ""
      - TEAISH_MAKEFILE_IN      -v ""
      - TEAISH_PKGINDEX_TCL     -v ""
      - TEAISH_PKGINDEX_TCL_IN  -v ""
      - TEAISH_PKGINIT_TCL      -v ""
      - TEAISH_PKGINIT_TCL_IN   -v ""
      - TEAISH_TEST_TCL         -v ""
      - TEAISH_TEST_TCL_IN      -v ""

      -version    TEAISH_VERSION       -v 0.0.0
      -pkgName    TEAISH_PKGNAME       -e {teaish-pkginfo-get -name}
      -libDir     TEAISH_LIBDIR_NAME   -e {
        join [list \
                [teaish-pkginfo-get -pkgName] \
                [teaish-pkginfo-get -version]] ""
      }
      -loadPrefix TEAISH_LOAD_PREFIX   -e {
        string totitle [teaish-get -pkgName]
      }
      -vsatisfies TEAISH_VSATISFIES -v {{Tcl 8.5-}}
    } {
      set isDefOnly [expr {"-" eq $pflag}]
      if {!$isDefOnly &&  [info exists ::teaish__PkgInfo($pflag)]} {
        continue;
      }
      set got [get-define $key "<nope>"]
      if {$isDefOnly && "<nope>" ne $got} {
        continue
      }
      switch -exact -- $type {
        -v {}
        -e { set val [eval $val] }
        default { proj-error "Invalid type flag: $type" }
      }
      #puts "***** defining default $pflag $key {$val} isDefOnly=$isDefOnly got=$got"
      define $key $val
      if {!$isDefOnly} {
        set ::teaish__PkgInfo($pflag) $val
      }
    }
    unset isDefOnly pflag key type val
  }; # sourcing extension's teaish.tcl

  if {[llength [info proc teaish-options]] > 0} {
    # Add options defined by teaish-options, which is assumed to be
    # imported via [teaish-get -teaish-tcl].
    set o [teaish-options]
    if {"" ne $o} {
      options-add $o
    }
  }
  #set opts [proj-options-combine]
  #lappend opts teaish-debug => {x}; #testing dupe entry handling
  if {[catch {options {}} msg xopts]} {
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
    t-v    => teaish-verbose
  }

  set ::teaish__Config(verbose) [opt-bool teaish-verbose]
  set ::teaish__Config(debug-enabled) [opt-bool teaish-debug]

  if {[proj-opt-was-provided teaish-create-extension]} {
    teaish__create_extension [opt-val teaish-create-extension]
    return
  }
  proj-assert {1==$gotExt} "Else we cannot have gotten this far"

  teaish__configure_phase1
}


#
# Internal config-time debugging output routine. It is not legal to
# call this from the global scope.
#
proc teaish-debug {msg} {
  if {$::teaish__Config(debug-enabled)} {
    puts stderr [proj-bold "** DEBUG: \[[proj-current-scope 1]\]: $msg"]
  }
}

#
# Runs "phase 1" of the configuration, immediately after processing
# --flags. This is what will import the client-defined teaish.tcl.
#
proc teaish__configure_phase1 {} {
  msg-result \
    [join [list "Configuring build of Tcl extension" \
       [proj-bold [teaish-pkginfo-get -name] \
          [teaish-pkginfo-get -version]] "..."]]

  uplevel 1 {
    use cc cc-db cc-shared cc-lib; # pkg-config
  }
  teaish__check_tcl
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
        if {"exec-prefix" eq $flag} {
          # If --exec-prefix was not used, ensure that --exec-prefix
          # derives from the --prefix we may have just redefined.
          set v {${prefix}}
        } else {
          set v [get-define $tclVar "???"]
          teaish__verbose 1 msg-result "Using \$$tclVar for --$flag=$v"
        }
        proj-assert {"???" ne $v} "Expecting teaish__check_tcl to have defined $tclVar"
        #puts "*** $flag $uflag $tclVar = $v"
        proj-opt-set $flag $v
        define $uflag $v

        # ^^^ As of here, all autotools-compatibility vars which derive
        # from --$flag, e.g. --libdir, still derive from the default
        # --$flag value which was active when system.tcl was
        # included. So long as those flags are not explicitly passed to
        # the configure script, those will be straightened out via
        # [proj-remap-autoconf-dir-vars].
      }
    }
  }}; # --[exec-]prefix defaults
  teaish__check_common_bins

  #
  # Set up library file names
  #
  proj-file-extensions
  apply {{} {
    set name [teaish-pkginfo-get -name]; # _not_ -pkgName
    set pkgver [teaish-pkginfo-get -version]
    set libname "lib"
    if {[string match *-cygwin [get-define host]]} {
      set libname cyg
    }
    define TEAISH_DLL8_BASENAME $libname$name$pkgver
    define TEAISH_DLL9_BASENAME ${libname}tcl9$name$pkgver
    set ext [get-define TARGET_DLLEXT]
    define TEAISH_DLL8 [get-define TEAISH_DLL8_BASENAME]$ext
    define TEAISH_DLL9 [get-define TEAISH_DLL9_BASENAME]$ext
  }}

  teaish-checks-run -pre
  if {[llength [info proc teaish-configure]] > 0} {
    # teaish-configure is assumed to be imported via
    # teaish.tcl
    teaish-configure
  }
  teaish-checks-run -post

  if {0} {
    # Reminder: we cannot do a TEAISH_VSATISFIES_TCL check like the
    # following from here because _this_ tcl instance is very possibly
    # not the one which will be hosting the extension. This part might
    # be running in JimTcl, which can't load the being-configured
    # extension.
    if {$::autosetup(istcl)} {
      # ^^^ this is a canonical Tcl, not JimTcl
      set vsat [get-define TEAISH_VSATISFIES_TCL ""]
      if {$vsat ne ""
          && ![package vsatisfies [package provide Tcl] $vsat]} {
        error [join [list "Tcl package vsatisfies failed for" \
                       [teaish-pkginfo-get -name] \
                       [teaish-pkginfo-get -version] \
                       ": expecting vsatisfies to match ($vsat)"]]
      }
      unset vsat
    }
  }

  if {[proj-looks-like-windows]} {
    # Without this, linking of an extension will not work on Cygwin or
    # Msys2.
    msg-result "Using USE_TCL_STUBS for Unix(ish)-on-Windows environment"
    teaish-cflags-add -DUSE_TCL_STUBS=1
  }

  #define AS_LIBDIR $::autosetup(libdir)
  define TEAISH_TESTUTIL_TCL $::teaish__Config(core-dir)/tester.tcl

  apply {{} {
    #
    # Ensure we have a pkgIndex.tcl and don't have a stale generated one
    # when rebuilding for different --with-tcl=... values.
    #
    if {!$::teaish__Config(pkgindex-policy)} {
      proj-error "Cannot determine which pkgIndex.tcl to use"
    }
    set tpi [proj-coalesce \
               [get-define TEAISH_PKGINDEX_TCL_IN] \
               [get-define TEAISH_PKGINDEX_TCL]]
    proj-assert {$tpi ne ""} \
      "TEAISH_PKGINDEX_TCL should have been set up by now"
    teaish__verbose 1 msg-result "Using pkgIndex from $tpi"
    if {0x0f & $::teaish__Config(pkgindex-policy)} {
      # Don't leave stale pkgIndex.tcl laying around yet don't delete
      # or overwrite a user-managed static pkgIndex.tcl.
      file delete -force -- [get-define TEAISH_PKGINDEX_TCL]
      proj-dot-ins-append [get-define TEAISH_PKGINDEX_TCL_IN]
    } else {
      teaish-dist-add [file tail $tpi]
    }
  }}; # $::teaish__Config(pkgindex-policy)

  set dEx $::teaish__Config(extension-dir)
  set dSrc $::autosetup(srcdir)

  proj-dot-ins-append $dSrc/Makefile.in
  proj-dot-ins-append $dSrc/teaish.tester.tcl.in

  apply {{} {
    set vs [get-define TEAISH_VSATISFIES ""]
    if {"" eq $vs} return
    # Treat as a list-of-lists {{Tcl 8.5-} {Foo 1.0- -3.0} ...} and
    # generate Tcl which will run package vsatisfies tests with that
    # info.
    set code {}
    set n 0
    foreach pv $vs {
      set n [llength $pv]
      if {$n < 2} {
        proj-error "-vsatisfies: {$pv} appears malformed. Whole list is: $vs"
      }
      set ifpv [list if \{ "!\[package vsatisfies \[package provide"]
      lappend ifpv [lindex $pv 0] "\]"
      for {set i 1} {$i < $n} {incr i} {
        lappend ifpv [lindex $pv $i]
      }
      lappend ifpv "\] \} \{\n"
      lappend ifpv \
        "error \{Package $::teaish__PkgInfo(-name) $::teaish__PkgInfo(-version) requires $pv\}\n" \
        "\}"
      lappend code [join $ifpv]
    }
    define TEAISH_VSATISFIES_CODE [join $code "\n"]
  }}

  define TEAISH_ENABLE_DIST    $::teaish__Config(dist-enabled)
  define TEAISH_ENABLE_DLL     $::teaish__Config(dll-enabled)
  define TEAISH_AUTOSETUP_DIR  $::teaish__Config(core-dir)
  define TEAISH_TCL            $::teaish__Config(teaish.tcl)
  define TEAISH_DIST_FILES     [join $::teaish__Config(dist-files)]
  define TEAISH_EXT_DIR        [join $::teaish__Config(extension-dir)]
  define TEAISH_EXT_SRC        [join $::teaish__Config(extension-src)]
  proj-setup-autoreconfig TEAISH_AUTORECONFIG
  foreach f {
    TEAISH_CFLAGS
    TEAISH_LDFLAGS
  } {
    # Ensure that any of these lists are flattened
    define $f [join [get-define $f]]
  }
  define TEAISH__DEFINES_MAP \
    [teaish__dump_defs_to_list]; # injected into teaish.tester.tcl
  proj-remap-autoconf-dir-vars
  proj-dot-ins-process -validate; # do not [define] after this point
  proj-if-opt-truthy teaish-dump-defines {
    make-config-header config.defines.txt \
      -none {TEAISH__* TEAISH_*_CODE} \
      -str {
        BIN_* CC LD AR INSTALL LDFLAG* CFLAGS* *_LDFLAGS *_CFLAGS
      } \
      -bare {HAVE_*} \
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

#
# Run checks for required binaries.
#
proc teaish__check_common_bins {} {
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

#
# TCL...
#
# teaish__check_tcl performs most of the --with-tcl and --with-tclsh
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
proc teaish__check_tcl {} {
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
  set with_tclsh [opt-val with-tclsh [proj-get-env TCLSH]]
  set with_tcl [opt-val with-tcl [proj-get-env TCL_HOME]]
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
      proj-error "TCL shell $with_tclsh is not executable"
    } else {
      define TCLSH_CMD $with_tclsh
      #msg-result "Using tclsh: $with_tclsh"
    }
    if {$doConfigLookup &&
        [catch {exec $with_tclsh $::autosetup(libdir)/find_tclconfig.tcl} result] == 0} {
      set with_tcl $result
    }
    if {"" ne $with_tcl && [file isdirectory $with_tcl]} {
      teaish__verbose 1 msg-result "$with_tclsh recommends the tclConfig.sh from $with_tcl"
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
        proj-error "No tclConfig.sh found under ${with_tcl}"
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
    teaish__verbose 1 msg-result "Using tclConfig.sh = $cfg"
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
    set tcllibdir [get-env TCLLIBDIR ""]
    set extDirName [get-define TEAISH_LIBDIR_NAME]
    if {"" eq $tcllibdir} {
      # Attempt to extract TCLLIBDIR from TCL's $auto_path
      if {"" ne $with_tclsh &&
          [catch {exec echo "puts stdout \$auto_path" | "$with_tclsh"} result] == 0} {
        foreach i $result {
          if {[file isdirectory $i]} {
            set tcllibdir $i/$extDirName
            break
          }
        }
      } else {
        proj-error "Cannot determine TCLLIBDIR."
      }
    }
    define TCLLIBDIR $tcllibdir
  }; # find TCLLIBDIR

  if {[file-isexec $with_tclsh]} {
    teaish__verbose 1 msg-result "Using tclsh        = $with_tclsh"
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
    proj-error "Did not find tclsh"
  } elseif {"" eq $cfg} {
    proj-indented-notice -error {
      Cannot find a usable tclConfig.sh file.  Use
      --with-tcl=DIR to specify a directory where tclConfig.sh can be
      found, or --with-tclsh=/path/to/tclsh to allow the tclsh binary
      to locate its tclConfig.sh.
    }
  }
  msg-result "Using Tcl [get-define TCL_VERSION]."
}; # teaish__check_tcl

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
# This sets up lots of defines, e.g. TEAISH_EXT_DIR.
#
proc teaish__find_extension {} {

  teaish__verbose 1 msg-result "Looking for teaish extension..."
  # Helper for the foreach loop below.
  set lambdaMT {{mustHave fid dir} {
    if {[file isdirectory $dir]} {
      set f [file join $dir $fid]
      if {[file readable $f]} {
        return [file-normalize $f]
      } elseif {$mustHave} {
        proj-error "Missing required $dir/$fid"
      }
    } elseif {$mustHave} {
      proj-error "--teaish-extension-dir=$dir does not reference a directory"
    }
    return ""
  }}
  #
  # We have to handle some flags manually because the extension must
  # be loaded before [options] is run (so that the extension can
  # inject its own options).
  #
  #set extM ""; # teaish.make.in
  set dirBld $::autosetup(builddir); # dir we're configuring under
  set dirSrc $::autosetup(srcdir);   # where teaish's configure script lives
  set extT ""; # teaish.tcl
  set largv {}; # rewritten $::argv
  set gotHelpArg 0; # got the --help
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
        if {![file isdirectory $extD]} {
          proj-error "--teaish-extension-dir value is not a directory: $extD"
        }
        set extT [apply $lambdaMT 1 teaish.tcl $extD]
        set ::teaish__Config(extension-dir) $extD
      }
      --help {
        incr gotHelpArg
      }
      default {
        lappend largv $arg
      }
    }
  }
  set ::argv $largv

  set dirExt [proj-coalesce \
                $::teaish__Config(extension-dir) \
                $dirBld]; # dir with the extension
  #
  # teaish.tcl is a TCL script which implements various
  # interfaces described by this framework.
  #
  # We use the first one we find in the builddir or srcdir.
  #
  if {"" eq $extT} {
    set flist [list $dirExt/teaish.tcl]
    if {$dirExt ne $dirSrc} {
      lappend flist $dirSrc/teaish.tcl
    }
    if {![proj-first-file-found extT $flist]} {
      if {$gotHelpArg} {
        # Tell teaish-configure-core that the lack of extension is not
        # an error when --help is used.
        return 0;
      }
      proj-indented-notice -error "
Did not find any of: $flist

If you are attempting an out-of-tree build, use
 --teaish-extension-dir=/path/to/extension"
    }
  }
  if {![file readable $extT]} {
    proj-error "extension tcl file is not readable: $extT"
  }
  set ::teaish__Config(teaish.tcl) $extT

  if {"" eq $dirExt} {
    # If this wasn't set via --teaish-extension-dir then derive it from
    # $extT.
    #puts "extT=$extT dirExt=$dirExt"
    set dirExt [file dirname $extT]
  }

  set ::teaish__Config(extension-dir) $dirExt
  set ::teaish__Config(blddir-is-extdir) [expr {$dirBld eq $dirExt}]
  set ::teaish__Config(dist-enabled) $::teaish__Config(blddir-is-extdir); # may change later

  set addDist {{file} {
    teaish-dist-add [file tail $file]
  }}
  apply $addDist $extT

  teaish__verbose 1 msg-result "Extension dir            = [teaish-get -dir]"
  teaish__verbose 1 msg-result "Extension config         = $extT"

  teaish-pkginfo-set -name [file tail [file dirname $extT]]

  #
  # teaish.make[.in] provides some of the info for the main makefile,
  # like which source(s) to build and their build flags.
  #
  # We use the first one of teaish.make.in or teaish.make we find in
  # $dirExt.
  #
  if {[proj-first-file-found extM \
         [list $dirExt/teaish.make.in $dirExt/teaish.make]]} {
    if {[string match *.in $extM]} {
      define TEAISH_MAKEFILE_IN $extM
      define TEAISH_MAKEFILE [file rootname [file tail $extM]]
      proj-dot-ins-append $extM [get-define TEAISH_MAKEFILE]
    } else {
      define TEAISH_MAKEFILE_IN ""
      define TEAISH_MAKEFILE $extM
    }
    apply $addDist $extM
    teaish__verbose 1 msg-result "Extension makefile      = $extM"
  } else {
    define TEAISH_MAKEFILE_IN ""
    define TEAISH_MAKEFILE ""
  }

  # Look for teaish.pkginit.tcl[.in]
  if {[proj-first-file-found extI \
         [list $dirExt/teaish.pkginit.tcl.in $dirExt/teaish.pkginit.tcl]]} {
    if {[string match *.in $extI]} {
      proj-dot-ins-append $extI
      define TEAISH_PKGINIT_TCL_IN $extI
      define TEAISH_PKGINIT_TCL [file tail [file rootname $extI]]
    } else {
      define TEAISH_PKGINIT_TCL_IN ""
      define TEAISH_PKGINIT_TCL $extI
    }
    apply $addDist $extI
    teaish__verbose 1 msg-result "Extension post-load init = $extI"
    define TEAISH_PKGINIT_TCL_TAIL \
      [file tail [get-define TEAISH_PKGINIT_TCL]]; # for use in pkgIndex.tcl.in
  }

  # Look for pkgIndex.tcl[.in]...
  set piPolicy 0
  if {[proj-first-file-found extPI $dirExt/pkgIndex.tcl.in]} {
    # Generate ./pkgIndex.tcl from it.
    define TEAISH_PKGINDEX_TCL_IN $extPI
    define TEAISH_PKGINDEX_TCL [file rootname [file tail $extPI]]
    apply $addDist $extPI
    set piPolicy 0x01
  } elseif {$dirExt ne $dirSrc
            && [proj-first-file-found extPI $dirSrc/pkgIndex.tcl.in]} {
    # Generate ./pkgIndex.tcl from it.
    define TEAISH_PKGINDEX_TCL_IN $extPI
    define TEAISH_PKGINDEX_TCL [file rootname [file tail $extPI]]
    set piPolicy 0x02
  } elseif {[proj-first-file-found extPI $dirExt/pkgIndex.tcl]} {
    # Assume it's a static file and use it.
    define TEAISH_PKGINDEX_TCL_IN ""
    define TEAISH_PKGINDEX_TCL $extPI
    apply $addDist $extPI
    set piPolicy 0x10
  }
  # Reminder: we have to delay removal of stale TEAISH_PKGINDEX_TCL
  # and the proj-dot-ins-append of TEAISH_PKGINDEX_TCL_IN until much
  # later in the process.
  set ::teaish__Config(pkgindex-policy) $piPolicy

  # Look for teaish.test.tcl[.in]
  proj-assert {"" ne $dirExt}
  set flist [list $dirExt/teaish.test.tcl.in $dirExt/teaish.test.tcl]
  if {[proj-first-file-found ttt $flist]} {
    if {[string match *.in $ttt]} {
      # Generate teaish.test.tcl from $ttt
      set xt [file rootname [file tail $ttt]]
      file delete -force -- $xt; # ensure no stale copy is used
      define TEAISH_TEST_TCL $xt
      define TEAISH_TEST_TCL_IN $ttt
      proj-dot-ins-append $ttt $xt
    } else {
      define TEAISH_TEST_TCL $ttt
      define TEAISH_TEST_TCL_IN ""
    }
    apply $addDist $ttt
  } else {
    define TEAISH_TEST_TCL ""
    define TEAISH_TEST_TCL_IN ""
  }

  # Look for teaish.tester.tcl[.in]
  set flist [list $dirExt/teaish.tester.tcl.in $dirSrc/teaish.tester.tcl.in]
  if {[proj-first-file-found ttt $flist]} {
    # Generate teaish.test.tcl from $ttt
    set xt [file rootname [file tail $ttt]]
    file delete -force -- $xt; # ensure no stale copy is used
    define TEAISH_TESTER_TCL $xt
    define TEAISH_TESTER_TCL_IN $ttt
    proj-dot-ins-append $ttt $xt
    if {[lindex $flist 0] eq $ttt} {
      apply $addDist $ttt
    }
  } else {
    set ttt [file join $dirSrc teaish.tester.tcl.in]
    set xt [file rootname [file tail $ttt]]
    proj-dot-ins-append $ttt $xt
    define TEAISH_TESTER_TCL $xt
    define TEAISH_TESTER_TCL_IN $ttt
  }
  unset flist xt ttt

  # TEAISH_OUT_OF_EXT_TREE = 1 if we're building from a dir other
  # than the extension's home dir.
  define TEAISH_OUT_OF_EXT_TREE \
    [expr {[file-normalize $::autosetup(builddir)] ne \
             [file-normalize $::teaish__Config(extension-dir)]}]
  return 1
}; # teaish__find_extension

#
# @teaish-cflags-add ?-p|prepend? ?-define? cflags...
#
# Equivalent to [proj-define-amend TEAISH_CFLAGS {*}$args].
#
proc teaish-cflags-add {args} {
  proj-define-amend TEAISH_CFLAGS {*}$args
}

#
# @teaish-define-to-cflag defineName...
#
# Uses [proj-define-to-cflag] to expand a list of [define] keys, each
# one a separate argument, to CFLAGS-style -D... form then appends
# that to the current TEAISH_CFLAGS.
#
proc teaish-define-to-cflag {args} {
  teaish-cflags-add [proj-define-to-cflag {*}$args]
}

#
# @teaish-ldflags-add ?-p|-prepend? ?-define? ldflags...
#
# Equivalent to [proj-define-amend TEAISH_LDFLAGS {*}$args].
#
# Typically, -lXYZ flags need to be in "reverse" order, with each -lY
# resolving symbols for -lX's to its left. This order is largely
# historical, and not relevant on all environments, but it is
# technically correct and still relevant on some environments.
#
# See: teaish-ldflags-prepend
#
proc teaish-ldflags-add {args} {
  proj-define-amend TEAISH_LDFLAGS {*}$args
}

#
# @teaish-ldflags-prepend args...
#
# Functionally equivalent to [teaish-ldflags-add -p {*}$args]
#
proc teaish-ldflags-prepend {args} {
  teaish-ldflags-add -p {*}$args
}

#
# @teaish-src-add ?-dist? ?-dir? src-files...
#
# Appends all non-empty $args to the project's list of C/C++ source
# files.
#
# If passed -dist then it also passes each filename, as-is, to
# [teaish-dist-add].
#
# If passed -dir then each src-file has [teaish-get -dir] prepended to
# it for before they're added to the list. As often as not, that will
# be the desired behavior so that out-of-tree builds can find the
# sources, but there are cases where it's not desired (e.g. when using
# a source file from outside of the extension's dir).
#
proc teaish-src-add {args} {
  set i 0
  proj-parse-simple-flags args flags {
    -dist 0 {expr 1}
    -dir  0 {expr 1}
  }
  if {$flags(-dist)} {
    teaish-dist-add {*}$args
  }
  if {$flags(-dir)} {
    set xargs {}
    foreach arg $args {
      if {"" ne $arg} {
        lappend xargs [file join $::teaish__Config(extension-dir) $arg]
      }
    }
    set args $xargs
  }
  lappend ::teaish__Config(extension-src) {*}$args
}

#
# @teaish-dist-add files-or-dirs...
#
# Adds the given files to the list of files to include with the "make
# dist" rules.
#
# This is a no-op when the current build is not in the extension's
# directory, as dist support is disabled in out-of-tree builds.
#
# It is not legal to call this until [teaish-get -dir] has been
# reliably set (via teaish__find_extension).
#
proc teaish-dist-add {args} {
  if {$::teaish__Config(blddir-is-extdir)} {
    # ^^^ reminder: we ignore $::teaish__Config(dist-enabled) here
    # because the client might want to implement their own dist
    # rules.
    lappend ::teaish__Config(dist-files) {*}$args
  }
}

# teaish-install-add files...
# Equivalent to [proj-define-apend TEAISH_INSTALL_FILES ...].
#proc teaish-install-add {args} {
#  proj-define-amend TEAISH_INSTALL_FILES {*}$args
#}

#
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
proc teaish-make-add {args} {
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

#
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
#
proc teaish-make-config-header {filename} {
  make-config-header $filename \
    -none {HAVE_CFLAG_* LDFLAGS_* SH_* TEAISH__* TEAISH_*_CODE} \
    -auto {SIZEOF_* HAVE_* TEAISH_*  TCL_*} \
    -none *
  proj-touch $filename; # help avoid frequent unnecessary auto-reconfig
}

#
# @teaish-feature-cache-set ?$key? value
#
# Sets a feature-check cache entry with the given key.
# See proj-cache-set for the key's semantics.
#
proc teaish-feature-cache-set {{key 0} val} {
  proj-cache-set $key 1 $val
}

#
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
#
proc teaish-feature-cache-check {{key 0} tgtVar} {
  upvar $tgtVar tgt
  proj-cache-check $key 1 tgt
}

#
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
#
proc teaish-check-cached {args} {
  proj-parse-simple-flags args flags {
    -nostatus 0 {expr 1}
  }
  lassign $args msg script
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
      if {!$flags(-nostatus)} {
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

#
# Internal helper for teaish__defs_format_: returns a JSON-ish quoted
# form of the given string-type values.
#
# If $asList is true then the return value is in {$value} form.  If
# $asList is false it only performs the most basic of escaping and
# the input must not contain any control characters.
#
proc teaish__quote_str {asList value} {
  if {$asList} {
    return [join [list "\{" $value "\}"] ""]
  }
  return \"[string map [list \\ \\\\ \" \\\"] $value]\"
}

#
# Internal helper for teaish__dump_defs_to_list. Expects to be passed
# a name and the variadic $args which are passed to
# teaish__dump_defs_to_list.. If it finds a pattern match for the
# given $name in the various $args, it returns the type flag for that
# $name, e.g. "-str" or "-bare", else returns an empty string.
#
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

#
# An internal impl detail. Requires a data type specifier, as used by
# make-config-header, and a value. Returns the formatted value or the
# value $::teaish__Config(defs-skip) if the caller should skip
# emitting that value.
#
# In addition to -str, -auto, etc., as defined by make-config-header,
# it supports:
#
#  -list {...} will cause non-integer values to be quoted in {...}
#  instead of quotes.
#
#  -autolist {...} works like -auto {...} except that it falls back to
#   -list {...} type instead of -str {...} style for non-integers.
#
#  -array {...} emits the output in something which, for conservative
#   inputs, will be a valid JSON array. It can only handle relatively
#   simple values with no control characters in them.
#
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
      set value [teaish__quote_str 0 $value]
    }
    -auto {
      # Automatically determine the type
      if {![string is integer -strict $value]} {
        set value [teaish__quote_str 0 $value]
      }
    }
    -autolist {
      if {![string is integer -strict $value]} {
        set value [teaish__quote_str 1 $value]
      }
    }
    -list {
      set value [teaish__quote_str 1 $value]
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
      proj-error \
        "Unknown [project-current-scope] -type ($type) called from" \
        [proj-current-scope 1]
    }
  }
  return $value
}

#
# Returns Tcl code in the form of code which evaluates to a list of
# configure-time DEFINEs in the form {key val key2 val...}. It may
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
      AM_* AS_*
    } \
    -auto {
      SIZEOF_* HAVE_*
    } \
    -autolist *
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

#
# @teaish__pragma ...flags
#
# Offers a way to tweak how teaish's core behaves in some cases, in
# particular those which require changing how the core looks for an
# extension and its files.
#
# Accepts the following flags. Those marked with [L] are safe to use
# during initial loading of tclish.tcl (recall that most teaish APIs
# cannot be used until [teaish-configure] is called).
#
#    static-pkgIndex.tcl [L]: Tells teaish that ./pkgIndex.tcl is not
#    a generated file, so it will not try to overwrite or delete
#    it. Errors out if it does not find pkgIndex.tcl in the
#    extension's dir.
#
#    no-dist [L]: tells teaish to elide the 'make dist' recipe
#    from the generated Makefile.
#
#    no-dll [L]: tells teaish to elide the DLL-building recipe
#    from the generated Makefile.
#
# Emits a warning message for unknown arguments.
#
proc teaish__pragma {args} {
  foreach arg $args {
    switch -exact -- $arg {

      static-pkgIndex.tcl {
        set tpi [file join $::teaish__Config(extension-dir) pkgIndex.tcl]
        if {[file exists $tpi]} {
          define TEAISH_PKGINDEX_TCL_IN ""
          define TEAISH_PKGINDEX_TCL $tpi
          set ::teaish__Config(pkgindex-policy) 0x20
        } else {
          proj-error "$arg: found no package-local pkgIndex.tcl\[.in]"
        }
      }

      no-dist {
        set ::teaish__Config(dist-enabled) 0
      }

      no-dll {
        set ::teaish__Config(dll-enabled) 0
      }

      default {
        proj-error "Unknown flag: $arg"
      }
    }
  }
}

#
# @teaish-pkginfo-set ...flags
#
# The way to set up the initial package state. Used like:
#
#   teaish-pkginfo-set -name foo -version 0.1.2
#
# Or:
#
#   teaish-pkginfo-set ?-vars|-subst? {-name foo -version 0.1.2}
#
# The latter may be easier to write for a multi-line invocation.
#
# For the second call form, passing the -vars flag tells it to perform
# a [subst] of (only) variables in the {...} part from the calling
# scope. The -subst flag will cause it to [subst] the {...} with
# command substitution as well (but no backslash substitution). When
# using -subst for string concatenation, e.g.  with -libDir
# foo[get-version-number], be sure to wrap the value in braces:
# -libDir {foo[get-version-number]}.
#
# Each pkginfo flag corresponds to one piece of extension package
# info.  Teaish provides usable default values for all of these flags,
# but at least the -name and -version should be set by clients.
# e.g. the default -name is the directory name the extension lives in,
# which may change (e.g. when building it from a "make dist" bundle).
#
# The flags:
#
#    -name theName: The extension's name. It defaults to the name of the
#     directory containing the extension. (In TEA this would be the
#     PACKAGE_NAME, not to be confused with...)
#
#    -pkgName pkg-provide-name: The extension's name for purposes of
#     Tcl_PkgProvide(), [package require], and friends. It defaults to
#     the `-name`, and is normally the same, but some projects (like
#     SQLite) have a different name here than they do in their
#     historical TEA PACKAGE_NAME.
#
#    -version version: The extension's package version. Defaults to
#     0.0.0.
#
#    -libDir dirName: The base name of the directory into which this
#     extension should be installed. It defaults to a concatenation of
#     `-pkgName` and `-version`.
#
#    -loadPrefix prefix: For use as the second argument passed to
#     Tcl's `load` command in the package-loading process. It defaults
#     to title-cased `-pkgName` because Tcl's `load` plugin system
#     expects it in that form.
#
#    -options {...}: If provided, it must be a list compatible with
#     Autosetup's `options-add` function. These can also be set up via
#     `teaish-options`.
#
#    -vsatisfies {{...} ...}: Expects a list-of-lists of conditions
#     for Tcl's `package vsatisfies` command: each list entry is a
#     sub-list of `{PkgName Condition...}`.  Teaish inserts those
#     checks via its default pkgIndex.tcl.in and teaish.tester.tcl.in
#     templates to verify that the system's package dependencies meet
#     these requirements. The default value is `{{Tcl 8.5-}}` (recall
#     that it's a list-of-lists), as 8.5 is the minimum Tcl version
#     teaish will run on, but some extensions may require newer
#     versions or dependencies on other packages. As a special case,
#     if `-vsatisfies` is given a single token, e.g. `8.6-`, then it
#     is transformed into `{Tcl $thatToken}`, i.e. it checks the Tcl
#     version which the package is being run with.  If given multiple
#     lists, each `package provides` check is run in the given
#     order. Failure to meet a `vsatisfies` condition triggers an
#     error.
#
#    -pragmas {...}  A list of infrequently-needed lower-level
#     directives which can influence teaish, including:
#
#      static-pkgIndex.tcl: tells teaish that the client manages their
#      own pkgIndex.tcl, so that teaish won't try to overwrite it
#      using a template.
#
#      no-dist: tells teaish to elide the "make dist" recipe from the
#      makefile so that the client can implement it.
#
#      no-dll: tells teaish to elide the makefile rules which build
#      the DLL, as well as any templated test script and pkgIndex.tcl
#      references to them. The intent here is to (A) support
#      client-defined build rules for the DLL and (B) eventually
#      support script-only extensions.
#
# Unsupported flags or pragmas will trigger an error.
proc teaish-pkginfo-set {args} {
  set doVars 0
  set doCommands 0
  set xargs $args
  foreach arg $args {
    switch -exact -- $arg {
      -vars {
        incr doVars
        set xargs [lassign $xargs -]
      }
      -subst {
        incr doVars
        incr doCommands
        set xargs [lassign $xargs -]
      }
      default {
        break
      }
    }
  }
  set args $xargs
  unset xargs
  if {1 == [llength $args] && [llength [lindex $args 0]] > 1} {
    # Transform a single {...} arg into the canonical call form
    set a [list {*}[lindex $args 0]]
    if {$doVars || $doCommands} {
      set sflags -nobackslashes
      if {!$doCommands} {
        lappend sflags -nocommands
      }
      set a [uplevel 1 [list subst {*}$sflags $a]]
    }
    set args $a
  }
  set sentinel "<nope>"
  set flagDefs [list]
  foreach {f d} $::teaish__Config(pkginfo-f2d) {
    lappend flagDefs $f => $sentinel
  }
  proj-parse-simple-flags args flags $flagDefs
  if {[llength $args]} {
    proj-error -up "Too many (or unknown) arguments to [proj-current-scope]: $args"
  }
  foreach {f d} $::teaish__Config(pkginfo-f2d) {
    if {$sentinel ne [set v $flags($f)]} {
      switch -exact -- $f {
        -options {
          proj-assert {"" eq $d}
          options-add $v
        }
        -pragmas {
          foreach p $v {
            teaish__pragma $p
          }
        }
        -vsatisfies {
          if {1 == [llength $v] && 1 == [llength [lindex $v 0]]} {
            # Transform X to {Tcl $X}
            set v [list [join [list Tcl $v]]]
          }
          define $d $v
        }
        default {
          define $d $v
        }
      }
      set ::teaish__PkgInfo($f) $v
    }
  }
}

#
# @teaish-pkginfo-get ?arg?
#
# If passed no arguments, it returns the extension config info in the
# same form accepted by teaish-pkginfo-set.
#
# If passed one -flagname arg then it returns the value of that config
# option.
#
# Else it treats arg as the name of caller-scoped variable to
# which this function assigns an array containing the configuration
# state of this extension, in the same structure accepted by
# teaish-pkginfo-set. In this case it returns an empty string.
#
proc teaish-pkginfo-get {args} {
  set cases {}
  set argc [llength $args]
  set rv {}
  switch -exact $argc {
    0 {
      # Return a list of (-flag value) pairs
      lappend cases default {{
        if {[info exists ::teaish__PkgInfo($flag)]} {
          lappend rv $flag $::teaish__PkgInfo($flag)
        } else {
          lappend rv $flag [get-define $defName]
        }
      }}
    }

    1 {
      set arg $args
      if {[string match -* $arg]} {
        # Return the corresponding -flag's value
        lappend cases $arg {{
          if {[info exists ::teaish__PkgInfo($flag)]} {
            return $::teaish__PkgInfo($flag)
          } else {
            return [get-define $defName]
          }
        }}
      } else {
        # Populate target with an array of (-flag value).
        upvar $arg tgt
        array set tgt {}
        lappend cases default {{
          if {[info exists ::teaish__PkgInfo($flag)]} {
            set tgt($flag) $::teaish__PkgInfo($flag)
          } else {
            set tgt($flag) [get-define $defName]
          }
        }}
      }
    }

    default {
      proj-error "invalid arg count from [proj-current-scope 1]"
    }
  }

  foreach {flag defName} $::teaish__Config(pkginfo-f2d) {
    switch -exact -- $flag [join $cases]
  }
  if {0 == $argc} { return $rv }
}

#
# @teaish-checks-queue -pre|-post args...
#
# Queues one or more arbitrary "feature test" functions to be run when
# teaish-checks-run is called. $flag must be one of -pre or -post to
# specify whether the tests should be run before or after
# teaish-configure is run. Each additional arg is the name of a
# feature-test proc.
#
proc teaish-checks-queue {flag args} {
  if {$flag ni {-pre -post}} {
    proj-error "illegal flag: $flag"
  }
  lappend ::teaish__Config(queued-checks${flag}) {*}$args
}

#
# @teaish-checks-run -pre|-post
#
# Runs all feature checks queued using teaish-checks-queue
# then cleares the queue.
#
proc teaish-checks-run {flag} {
  if {$flag ni {-pre -post}} {
    proj-error "illegal flag: $flag"
  }
  #puts "*** running $flag: $::teaish__Config(queued-checks${flag})"
  set foo 0
  foreach f $::teaish__Config(queued-checks${flag}) {
    if {![teaish-feature-cache-check $f foo]} {
      set v [$f]
      teaish-feature-cache-set $f $v
    }
  }
  set ::teaish__Config(queued-checks${flag}) {}
}

#
# A general-purpose getter for various teaish state. Requires one
# flag, which determines its result value. Flags marked with [L] below
# are safe for using at load-time, before teaish-pkginfo-set is called
#
#   -dir [L]: returns the extension's directory, which may differ from
#    the teaish core dir or the build dir.
#
#   -teaish-home [L]: the "home" dir of teaish itself, which may
#   differ from the extension dir or build dir.
#
#   -build-dir [L]: the build directory (typically the current working
#   -dir).
#
#   Any of the teaish-pkginfo-get/get flags: returns the same as
#   teaish-pkginfo-get. Not safe for use until teaish-pkginfo-set has
#   been called.
#
# Triggers an error if passed an unknown flag.
#
proc teaish-get {flag} {
  switch -exact -- $flag {
    -dir {
      return $::teaish__Config(extension-dir)
    }
    -teaish-home {
      return $::autosetup(srcdir)
    }
    -build-dir {
      return $::autosetup(builddir)
    }
    -teaish.tcl {
      return $::teaish__Config(teaish.tcl)
    }
    default {
      if {[info exists ::teaish__PkgInfo($flag)]} {
        return $::teaish__PkgInfo($flag)
      }
    }
  }
  proj-error "Unhandled flag: $flag"
}

#
# Handles --teaish-create-extension=TARGET-DIR
#
proc teaish__create_extension {dir} {
  set force [opt-bool teaish-force]
  if {"" eq $dir} {
    proj-error "--teaish-create-extension=X requires a directory name."
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
  set content "teaish-pkginfo-set \
  -name ${name} \
  -pkgName ${pkgName} \
  -version ${version} \
  -loadPrefix $loadPrefix \
  -libDir ${name}${version}
  -vsatisfies {{Tcl 8.5-}} \
  -options { foo=1 => {Disable foo} }

#proc teaish-options {} {
  # Return a list and/or use \[options-add\] to add new
  # configure flags. This is called before teaish's
  # bootstrapping is finished, so only teaish-*
  # APIs which are explicitly noted as being safe
  # early on may be used here. Any autosetup-related
  # APIs may be used here.
  #
  # Return an empty string if there are no options to
  # add or if they are added using \[options-add\].
  #
  # If there are no options to add, this proc need
  # not be defined.
#}

proc teaish-configure {} {
  teaish-src-add -dir -dist teaish.c
  # TODO: your code goes here..
}
"
  proj-file-write teaish.tcl $content
  msg-result "Created teaish.tcl"

  set content "# Teaish test script.
# When this tcl script is invoked via 'make test' it will have loaded
# the package, run any teaish.pkginit.tcl code, and loaded
# autosetup/teaish/tester.tcl.
"
  proj-file-write teaish.test.tcl $content
  msg-result "Created teaish.test.tcl"

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
