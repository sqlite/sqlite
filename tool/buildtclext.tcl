#
# Run this TCL script to build and optionally install the TCL interface
# library for SQLite.  Run the script with the specific "tclsh" for which
# the installation should occur.
#
# Options:
#
#    --build-only              Only build the extension, don't install it
#    --install-only            Install an extension previously build
#    --uninstall               Uninstall the extension
#
set installonly 0
set buildonly 0
set uninstall 0
for {set ii 0} {$ii<[llength $argv]} {incr ii} {
  set a0 [lindex $argv $ii]
  if {$a0=="--install-only"} {
    set installonly 1
  } elseif {$a0=="--build-only"} {
    set buildonly 1
  } elseif {$a0=="--uninstall"} {
    set uninstall 1
  } else {
    puts stderr "Unknown option: \"$a0\""
    exit 1
  }
}

# Find the root of the SQLite source tree
#
set srcdir [file normalize [file dir $argv0]/..]

# Get the SQLite version number into $VERSION
#
set fd [open $srcdir/VERSION]
set VERSION [string trim [read $fd]]
close $fd

# Figure out the location of the tclConfig.sh file used by the
# tclsh that is executing this script.
#
if {[catch {
  set LIBDIR [tcl::pkgconfig get libdir,install]
}]} {
  puts stderr "$argv0: tclsh does not support tcl::pkgconfig."
  exit 1
}
if {![file exists $LIBDIR]} {
  puts stderr "$argv0: cannot find the tclConfig.sh file."
  puts stderr "$argv0: tclsh reported library directory \"$LIBDIR\"\
               does not exist."
  exit 1
}
if {![file exists $LIBDIR/tclConfig.sh]} {
  set n1 $LIBDIR/tcl$::tcl_version
  if {[file exists $n1/tclConfig.sh]} {
    set LIBDIR $n1
  } else {
    puts stderr "$argv0: cannot find tclConfig.sh in either $LIBDIR or $n1"
    exit 1
  }
}

# Read the tclConfig.sh file into the $tclConfig variable
#
set fd [open $LIBDIR/tclConfig.sh rb]
set tclConfig [read $fd]
close $fd

# Extract parameter we will need from the tclConfig.sh file
#
set TCLMAJOR 8
regexp {TCL_MAJOR_VERSION='(\d)'} $tclConfig all TCLMAJOR
set SUFFIX so
regexp {TCL_SHLIB_SUFFIX='\.([^']+)'} $tclConfig all SUFFIX
set CC gcc
regexp {TCL_CC='([^']+)'} $tclConfig all CC
set CFLAGS -fPIC
regexp {TCL_SHLIB_CFLAGS='([^']+)'} $tclConfig all CFLAGS
set opt {}
regexp {TCL_CFLAGS_OPTIMIZE='([^']+)'} $tclConfig all opt
if {$opt!=""} {
  append CFLAGS " $opt"
}
set LIBS {}
regexp {TCL_STUB_LIB_SPEC='([^']+)'} $tclConfig all LIBS
set INC "-I$srcdir/src"
set inc {}
regexp {TCL_INCLUDE_SPEC='([^']+)'} $tclConfig all inc
if {$inc!=""} {
  append INC " $inc"
}
set cmd {}
regexp {TCL_SHLIB_LD='([^']+)'} $tclConfig all cmd
set LDFLAGS $INC
set CMD [subst $cmd]
if {$TCLMAJOR>8} {
  set OUT libtcl9sqlite$VERSION.$SUFFIX
} else {
  set OUT libsqlite$VERSION.$SUFFIX
}

# Uninstall the extension
#
if {$uninstall} {
  set cnt 0
  foreach dir $auto_path {
    if {[file isdirectory $dir/sqlite$VERSION]} {
      incr cnt
      if {![file writable $dir] || ![file writable $dir/sqlite$VERSION]} {
        puts "cannot uninstall $dir/sqlite$VERSION - permission denied"
      } else {
        puts "uninstalling $dir/sqlite$VERSION..."
        file delete -force $dir/sqlite$VERSION
      }
    }
  }
  if {$cnt==0} {
    puts "nothing to uninstall"
  }
  exit
}

# Figure out where the extension will be installed.
#
set DEST {}
foreach dir $auto_path {
  if {[file writable $dir]} {
    set DEST $dir
    break
  }
}
if {$DEST==""} {
  puts "None of the directories on $auto_path are writable by this process,"
  puts "so the installation cannot take place.  Consider running using sudo"
  puts "to work around this."
}

if {!$installonly} {
  # Generate the pkgIndex.tcl file
  #
  set fd [open pkgIndex.tcl w]
  puts $fd [subst -nocommands {# -*- tcl -*-
# Tcl package index file, version ???
#
package ifneeded sqlite3 $VERSION \\
    [list load [file join \$dir $OUT] sqlite3]
}]
  close $fd

  # Generate and execute the command with which to do the compilation.
  #
  set cmd "$CMD tclsqlite3.c -o $OUT $LIBS"
  puts $cmd
  exec {*}$cmd
}

# Install the extension
#
if {$DEST!="" && !$buildonly} {
  set DEST2 $DEST/sqlite$VERSION
  file mkdir $DEST2
  puts "installing $DEST2/pkgIndex.tcl"
  file copy -force pkgIndex.tcl $DEST2
  puts "installing $DEST2/$OUT"
  file copy -force $OUT $DEST2
}
