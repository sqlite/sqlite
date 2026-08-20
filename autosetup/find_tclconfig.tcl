#
# Run this TCL script to find and print the pathname for the tclConfig.sh
# file.  Used by ../configure
#
if {[catch {
  set libdir [tcl::pkgconfig get libdir,install]
}]} {
  puts stderr "tclsh too old:  does not support tcl::pkgconfig"
  exit 1
}
if {![file exists $libdir]} {
  puts stderr "tclsh reported library directory \"$libdir\" does not exist"
  exit 1
}
set candidates [list $libdir $libdir/tcl$::tcl_version]
foreach d $::tcl_pkgPath {
  lappend candidates $d $d/tcl$::tcl_version
}
foreach d $candidates {
  if {[file exists $d/tclConfig.sh]} {
    puts $d
    exit 0
  }
}
puts stderr "cannot find tclConfig.sh in any of: [join $candidates {, }]"
exit 1
