
#
# This script edits a vec1bench database to add constant dimensions to
# the data, query and (if any) training vectors in the db. This should
# not change the results of any query, but did at one point confound
# the OPQ training code.
#

package require sqlite3

set DEFAULT(--ncol)          1
set DEFAULT(--zero)          0

proc usage {} {
  global DEFAULT
  puts stderr "usage: $::argv0 ?OPTIONS? DATABASE"
  puts stderr "options:"
  foreach o [array names DEFAULT] {
    set display $DEFAULT($o)
    if { [string is integer $display]==0 } { set display "\"$display\"" }
    puts stderr "      [format {% -20s} $o](default: $display)"
  }
  exit 1
}

proc process_args {lArg} {
  global C
  global DEFAULT
  array set C [array get DEFAULT]

  for {set i 0} {$i < [expr [llength $lArg]-1]} {incr i} {
    set a [lindex $lArg $i]
    if {[string range $a 0 0]!="-"} break
    if {[string range $a 1 1]!="-"} { set a "-$a" }
  
    unset -nocomplain m
    foreach o [array names C] {
      if {[string match ${a}* $o]} {
        if {[info exists m]} usage
        set m $o
      }
    }
    if {[info exists m]==0} usage

    incr i
    set C($m) [lindex $lArg $i]
  }

  lrange $lArg $i end
}


# Check that the __meta__ table looks ok. This command checks that there 
# are "train", "test", "neighbors" and "distances" tables, with types
# float32, float32, int64 and float64, respectively, in the db. If not,
# it prints out and error and exits.
#
proc check_db_looks_ok {} {
  db eval {
    WITH expect(nm, tp) AS (
      SELECT 'train',     'float32'      UNION ALL
      SELECT 'test',      'float32'      UNION ALL
      SELECT 'neighbors', 'int64'        UNION ALL
      SELECT 'neighbors', 'int32'       
    ),
    have(nm, tp) AS (
      SELECT table_name, dtype 
      FROM __meta__ 
      WHERE table_name IN ('train', 'test', 'neighbors')
    )
  
    SELECT (count(*)==3) as 'ok' FROM have WHERE (nm, tp) IN (
      SELECT nm, tp FROM expect
    );
  } {
    if {$ok==0} {
      puts stderr "database does not look right. bailing out..."
      exit -1
    }
  }
}


# Process command line arguments and open the database. And load the 
# vec1.so dynamic extension(s).
#
set lExt [process_args $argv]
if {[llength $lExt]!=1} usage
set C(dbname) [lindex $lExt 0]

proc main { } {
  global C

  sqlite3 db $C(dbname)

  check_db_looks_ok

  set nCol $C(--ncol)
  set zstr [string repeat 00000000 $nCol]

  db transaction {
    foreach tbl {test train learn} {
      if {$C(--zero)} {
        catch { db eval "UPDATE $tbl SET vec = unhex( hex(vec) || '$zstr')" }
      } else {
        catch { db eval "
          UPDATE $tbl SET vec = unhex( hex(vec) || substr(hex(vec), 1, 8*$nCol) )
        " }
      }
    }
  }
  if {$C(--zero)} {
    puts "Appended $nCol 0.0 floats to all vectors"
  } else {
    puts "Appended duplicate of first $nCol floats to all vectors"
  }
}

main
exit

