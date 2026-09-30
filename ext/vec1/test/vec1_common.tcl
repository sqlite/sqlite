
if {![info exists testdir]} {
  set testdir [file join [file dirname [info script]] .. .. .. test]
}
source $testdir/tester.tcl

set dir [file dirname [info nameofexecutable]]
set vec1_shlib [file join $dir vec1]


proc vec1_load_only {db} {
  if {[catch {$db one {SELECT vec1_info()}}]} {
    global vec1_shlib
    $db enable_load_extension 1
    if {[catch { sqlite3_load_extension $db $vec1_shlib }]} {
      $db eval { SELECT load_extension($vec1_shlib) }
    }
    $db enable_load_extension 0
  }
}

sqlite3 db_test_vec1 ""
set res [catch { vec1_load_only db_test_vec1 }]
db_test_vec1 close
if {$res} {
  finish_test
  return -code return
}

proc vec1_load {db} {
  if {[catch {$db one {SELECT vec1_info()}}]} {
    global vec1_shlib
    $db enable_load_extension 1
    $db eval { SELECT load_extension($vec1_shlib) }
    $db enable_load_extension 0
  }
  $db func random_vector random_vector
  $db func random_vector2 random_vector2
  $db func rvec rvec
}

proc vec1_load_scalar {db} {
  global vec1_shlib
  $db enable_load_extension 1
  set scalar "${vec1_shlib}testscalar"
  sqlite3_load_extension $db $scalar
  $db enable_load_extension 0
}

proc random_vector {n {scale 1.0}} {
  set v [list]
  for {set i 0} {$i < $n} {incr i} {
    lappend v [expr {((rand() * 2.0) - 1.0) * $scale}]
  }
  binary format f$n $v
}

proc random_vector2 {n} {
  set v [list]
  for {set i 0} {$i < $n} {incr i} {
    lappend v [expr {abs(rand() * 10.0)}]
  }
  binary format f$n $v
}

proc rvec {n i} {
  set lPrime {
      23 29 31 37 41 43 47 53 
      59 61 67 71 73 79 83 89 
      97 101 103 107 109 113 127 131 
      137 139 149 151 157 163 167 173 
      179 181 191 193 197 199 211 223 
      227 229 233 239 241 251 257 263 
      269 271 277 281 283 293 307 311 
      313 317 331 337 347 349 353 359 
      367 373 379 383 389 397 401 409 
      419 421 431 433 439 443 449 457 
      461 463 467 479 487 491 499 503 
      509 521 523 541 547 557 563 569 
      571 577 587 593 599 601 607 613 
      617 619 631 641 643 647 653 659 
      661 673 677 683 691 701 709 719 
      727 733 739 743 751 757 761 769
  }

  if {$n>[llength $lPrime]} {
    error "n value too large ($n)"
  }

  set i [expr $i * 769]

  set lDim [list]
  for {set iDim 0} {$iDim<$n} {incr iDim} {
    set x [lindex $lPrime $iDim]
    lappend lDim [expr ($i % $x) - $x/2]
  }

  binary format r* $lDim
}
db func rvec rvec

proc model2tbl {nm vec1tbl} {
  set model [db one "SELECT val FROM ${vec1tbl}_model WHERE id=1"]

  binary scan $model IIIIII iVersion flags nElem nCodebook nBucket eDistance

  set nBytePerVector [expr $nElem*4]
  set iOff [expr 4 * 6]
  if {($flags & 0x08)==0 && $nCodebook>0} {
    incr iOff [expr 4 * $nElem * 256]
  }
  #puts "nBytePerVector=$nBytePerVector"
  #puts "nBucket=$nBucket"
  #puts "sz=[string length $model]"
  #puts "iOff=$iOff"

  db eval "CREATE TABLE $nm (ibucket INTEGER PRIMARY KEY, coarse BLOB)"
  for {set iBucket 0} {$iBucket<$nBucket} {incr iBucket} {
    set vec [string range $model $iOff [expr $iOff+$nBytePerVector-1]]
    db eval "INSERT INTO $nm VALUES(\$iBucket, \$vec)"
    incr iOff $nBytePerVector
  }
}


