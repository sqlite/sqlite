#!/usr/bin/tclsh
#
# Use this script to combine multiple source code files into a single
# file.  Example:
#
#    tclsh mkcombo.tcl file1.c file2.c file3.c -o file123.c
#

set help {Usage: tclsh mkcombo.tcl [OPTIONS] [FILELIST]
 where OPTIONS is zero or more of the following with these effects:
   --linemacros=?  => Emit #line directives into output or not. (? = 1 or 0)
   --o FILE        => write to alternative output file named FILE
   --help          => See this.
}

set linemacros 0
set fname {}
set src [list]


for {set i 0} {$i<[llength $argv]} {incr i} {
  set x [lindex $argv $i]
  if {[regexp {^-?-linemacros(?:=([01]))?$} $x ma ulm]} {
    if {$ulm == ""} {set ulm 1}
    set linemacros $ulm
  } elseif {[regexp {^-o$} $x]} {
    incr i
    if {$i==[llength $argv]} {
      error "No argument following $x"
    }
    set fname [lindex $argv $i]
  } elseif {[regexp {^-?-((help)|\?)$} $x]} {
    puts $help
    exit 0
  } elseif {[regexp {^-?-} $x]} {
    error "unknown command-line option: $x"
  } else {
    lappend src $x
  }
}

# Open the output file and write a header comment at the beginning
# of the file.
#
if {![info exists fname]} {
  set fname sqlite3.c
  if {$enable_recover} { set fname sqlite3r.c }
}
set out [open $fname wb]

# Return a string consisting of N "*" characters.
#
proc star N {
  set r {}
  for {set i 0} {$i<$N} {incr i} {append r *}
  return $r
}

# Force the output to use unix line endings, even on Windows.
fconfigure $out -translation binary
puts $out "/[star 78]"
puts $out {** The following is an amalgamation of these source code files:}
puts $out {**}
foreach s $src {
  regsub {^.*/(src|ext)/} $s {\1/} s2
  puts $out "**     $s2"
}
puts $out {**}
puts $out "[star 78]/"

# Insert a comment into the code
#
proc section_comment {text} {
  global out s78
  set n [string length $text]
  set nstar [expr {60 - $n}]
  puts $out "/************** $text [star $nstar]/"
}

# Read the source file named $filename and write it into the
# sqlite3.c output file. The only transformation is the trimming
# of EOL whitespace.
#
proc copy_file_verbatim {filename} {
  global out
  set in [open $filename rb]
  set tail [file tail $filename]
  section_comment "Begin file $tail"
  while {![eof $in]} {
    set line [string trimright [gets $in]]
    puts $out $line
  }
  section_comment "End of $tail"
}
set taillist ""
foreach file $src {
  copy_file_verbatim $file
  append taillist ", [file tail $file]"
}

set taillist "End of the amalgamation of [string range $taillist 2 end]"
set n [string length $taillist]
set ns [expr {(75-$n)/2}]
if {$ns<3} {set ns 3}
puts $out "/[star $ns] $taillist [star $ns]/"
close $out
