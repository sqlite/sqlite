# Run this TCL script to renumber the entries in the
# aSyscall[] thunk of the src/os_win.c file.  There should
# be one argument which is the filename of the src/os_win.c
# file.  Ex:
#
#    tclsh tool/renumber-win-thunk.tcl src/os_win.c
#
if {[llength $argv]!=1} {
  puts stderr "Usage: tclsh [info script] FILENAME"
  exit 1
}
set FILENAME [lindex $argv 0]
set fd [open $FILENAME rb]
set newtext {}
set ctr 0
set entryPending 0
set nocaps 0
while {![eof $fd]} {
  set line [gets $fd]
  if {$line eq "" && [eof $fd]} break
  if {[regexp {^#define os([A-Z][_a-zA-Z0-9]+) } $line all nm]} {
    if {$nm eq "Getenv"} {set nocaps 1}
    if {$nocaps} {set nm [string tolower $nm]}
    set syscall_name($ctr) $nm
    set syscall_idx($nm) $ctr
    set entryPending 1
  }
  if {$entryPending && [regexp {aSyscall\[(\d+)\]\.pCurrent} $line all n]} {
    regsub {aSyscall\[\d+\]\.pCurrent} $line "aSyscall\[$ctr\].pCurrent" line
    if {$n!=$ctr} {puts "$n -> $ctr"}
    incr ctr
    set entryPending 0
  }
  if {[regexp {ArraySize\(aSyscall\)==\d+} $line]} {
    regsub {ArraySize\(aSyscall\)==\d+} $line "ArraySize(aSyscall)==$ctr" line
    append newtext $line\n
    for {set i 0} {$i<$ctr} {incr i 8} {
      set nm $syscall_name($i)
      append newtext \
        "  assert( strcmp(aSyscall\[$i\].zName,\"$nm\")==0 );\n"
    }
    continue;
  }
  if {[regexp {^  assert\( strcmp\(aSyscall\[(\d+)\].zName,} $line n]} {
    continue
  }
  append newtext $line\n
}
close $fd
set fd [open $FILENAME wb]
puts -nonewline $fd $newtext
close $fd
