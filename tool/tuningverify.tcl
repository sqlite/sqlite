#!/usr/bin/tclsh
#
# Verify that the Tuning(D,ID) macros in the SQLite source code agree
# with the sqlite3Tuning[] array in src/global.c.
#
# Usage:    tclsh tool/tuningverify.tcl ?SOURCE-ROOT?
#
# SOURCE-ROOT defaults to the parent of the directory that holds this
# script.  Source files under SOURCE-ROOT/src and SOURCE-ROOT/ext are
# scanned.
#
# The following checks are made:
#
#   (1) The declared size of sqlite3Tuning[] equals the number of
#       initializers.
#   (2) sqlite3Tuning[0] holds the index of the largest element in the
#       array, that is, the number of initializers minus 1.
#   (3) Every Tuning(D,ID) that is not on a line that begins with "**",
#       "#" or "/*" has no surplus whitespace, and both D and ID are
#       integers.  (ID must additionally be in the range 1..N.)
#   (4) All Tuning() macros that have the same ID have the same D.
#   (5) The D and ID of every Tuning() macro match the default value
#       held in the sqlite3Tuning[] array.
#
# Produce no output and exit 0 if all is well.  Otherwise, write error
# messages to stderr and exit 1.
#

set nErr 0
proc error_msg {msg} {
  global nErr
  puts stderr $msg
  incr nErr
}

if {[llength $argv]>1} {
  puts stderr "Usage: $argv0 ?SOURCE-ROOT?"
  exit 1
}
if {[llength $argv]==1} {
  set root [lindex $argv 0]
} else {
  set root [file dirname [file dirname [file normalize [info script]]]]
}

# Return a list of all source files below directory $dir.
#
proc find_sources {dir} {
  set res {}
  foreach f [lsort [glob -nocomplain -directory $dir/src *]] {
    if {[lsearch -exact {.c .h .y .in} [file extension $f]]>=0} {
      lappend res $f
    }
  }
  return $res
}

# Read a whole file.
#
proc readfile {fn} {
  set in [open $fn rb]
  set x [read $in]
  close $in
  return $x
}

# ---------------------------------------------------------------------
# Parse the sqlite3Tuning[] array out of src/global.c.
#
set globalc [file join $root src global.c]
if {![file exists $globalc]} {
  puts stderr "cannot find $globalc"
  exit 1
}
set content [readfile $globalc]
# (Tcl regular expressions take their greediness from the first quantifier,
# so locate the initializer using string operations instead.)
set body ""
if {![regexp -indices {int\s+sqlite3Tuning\s*\[\s*([^\]]*)\]\s*=\s*\{} \
        $content all sz]} {
  puts stderr "$globalc: cannot find the initialized sqlite3Tuning\[\] array"
  exit 1
}
set declSize [string trim [string range $content {*}$sz]]
set iStart [expr {[lindex $all 1]+1}]
set iEnd [string first \} $content $iStart]
if {$iEnd<0} {
  puts stderr "$globalc: unterminated sqlite3Tuning\[\] initializer"
  exit 1
}
set body [string range $content $iStart [expr {$iEnd-1}]]
regsub -all {/\*.*?\*/} $body {} body     ;# remove comments
set aDflt {}                              ;# Initializer values, by index
foreach term [split $body ,] {
  set term [string trim $term]
  if {$term eq ""} continue
  if {![regexp {^[-+]?\d+$} $term]} {
    error_msg "$globalc: sqlite3Tuning\[\] initializer \"$term\" is not\
               an integer"
    continue
  }
  lappend aDflt [scan $term %d]
}
set nElem [llength $aDflt]
set iLast [expr {$nElem-1}]

# (1) The array size must be correct.
if {![regexp {^\d+$} $declSize]} {
  error_msg "$globalc: size of sqlite3Tuning\[\] is \"$declSize\", which is\
             not an integer constant"
} elseif {$declSize != $nElem} {
  error_msg "$globalc: sqlite3Tuning\[$declSize\] has $nElem initializers"
}

# (2) Element 0 holds the index of the largest element.
if {$nElem==0} {
  error_msg "$globalc: sqlite3Tuning\[\] is empty"
} elseif {[lindex $aDflt 0] != $iLast} {
  error_msg "$globalc: sqlite3Tuning\[0\] is [lindex $aDflt 0] but the index\
             of the largest element is $iLast"
}

# ---------------------------------------------------------------------
# Scan the source for Tuning(D,ID) macros.
#
array set dOfId {}      ;# ID -> D value of the first macro seen with that ID
array set whereId {}    ;# ID -> "file:line" of the first such macro
set nMacro 0

foreach fn [concat [find_sources [file join $root src]] \
                   [find_sources [file join $root ext]]] {
  set lineno 0
  set rel [string range $fn [expr {[string length $root]+1}] end]
  foreach line [split [readfile $fn] \n] {
    incr lineno
    if {[string first Tuning( $line]<0} continue
    set t [string trimleft $line]
    if {[string match {\*\**} $t] || [string match {#*} $t]
        || [string match {/\**} $t]} continue
    set loc "$rel:$lineno"
    foreach {all} [regexp -all -inline {\mTuning\([^()]*\)} $line] {
      incr nMacro
      set arg [string range $all 7 end-1]
      # (3) no surplus whitespace, and both values are integers
      if {[regexp {\s} $arg]} {
        error_msg "$loc: surplus whitespace in $all"
        set arg [regsub -all {\s+} $arg {}]
      }
      if {![regexp {^([-+]?\d+),(\d+)$} $arg x D ID]} {
        error_msg "$loc: arguments of $all are not both integers"
        continue
      }
      set D [scan $D %d]
      set ID [scan $ID %d]
      if {$ID<1 || $ID>$iLast} {
        error_msg "$loc: ID $ID in $all is out of range 1..$iLast"
        continue
      }
      # (4) Same D for every macro with the same ID
      if {![info exists dOfId($ID)]} {
        set dOfId($ID) $D
        set whereId($ID) $loc
      } elseif {$dOfId($ID)!=$D} {
        error_msg "$loc: Tuning($D,$ID) disagrees with Tuning($dOfId($ID),$ID)\
                   at $whereId($ID)"
      }
      # (5) D must match the default in sqlite3Tuning[]
      if {$D != [lindex $aDflt $ID]} {
        error_msg "$loc: $all does not match sqlite3Tuning\[$ID\]=[lindex \
                   $aDflt $ID] in src/global.c"
      }
    }
  }
}

exit [expr {$nErr>0}]
