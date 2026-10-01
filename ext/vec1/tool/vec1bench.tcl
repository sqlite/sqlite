
package require sqlite3

set DEFAULT(--nthread)                16
set DEFAULT(--max-train)          100000
set DEFAULT(--train-function) vec1_train
set DEFAULT(--index)          {nbucket 1024 codesize 32 opq 1}
set DEFAULT(--query)          ""
set DEFAULT(--meta-data)      0
set DEFAULT(--svd-verify)     0
set DEFAULT(--report)         ""
set DEFAULT(--repeat)         1

proc usage {} {
  global DEFAULT
  puts stderr "usage: $::argv0 ?OPTIONS? DATABASE ?SHLIB1...?"
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


# Process command line arguments and open the database. And load the 
# vec1.so dynamic extension(s).
#
set lExt [process_args $argv]
if {[llength $lExt]==0} usage
set C(dbname) [lindex $lExt 0]
set C(lExt)   [lrange $lExt 1 end]

proc open_db {fname} {
  global C
  set lExt $C(lExt);

  sqlite3 db $fname
  db enable_load_extension 1
  if {[llength $lExt]==0 && [catch {db eval {SELECT vec1_info()}}]} {
    set shlib [file join [file dirname [info nameofexecutable]] vec1]
    db eval { SELECT load_extension($shlib) }
  } else {
    foreach shlib $lExt {
      db eval { SELECT load_extension($shlib) }
    }
  }
  db enable_load_extension 0
}

proc main { } {
  global C

  if {$C(dbname)=="release"} {
    if {[catch release_test msg]} {
      puts stderr "ERROR: $msg"
    }
    return
  }

  open_db $C(dbname)
  check_db_looks_ok
  summarize_db
  build_nearest_if_required

  set r $C(--repeat)
  set lResult [list]
  for {set i 0} {$i < $r} {incr i} {
    if {$C(--index) != ""} {
      test_build_index     $C(--index)
    }

    if {$C(--query) != ""} {
      lappend lResult [test_query_index   {*}$C(--query)]
    }
  }
  if {$C(--query) != ""} {
    if {$r==1} {
      print_query_timings
    } else {
      puts "recall: [lsort -integer $lResult]"
    }
    exit
  }

  array set I $C(--index)
  if {[info exists I(codesize)] && $I(codesize)==0} {
    # If the command-line specified a code-size of 0, then do not bother
    # varying K, as reranking won't help anyway. 
    foreach nprobe {16 32 48 64} {
      test_query_index      1  1 $nprobe
      test_query_index     10 10 $nprobe
    }
  } else {
    foreach nprobe {16 32 48 64} {
      test_query_index      1 1  $nprobe
      test_query_index     10 1  $nprobe
      test_query_index     50 1  $nprobe
      test_query_index    100 1  $nprobe
      test_query_index    200 1  $nprobe
      test_query_index    300 1  $nprobe
      test_query_index     10 10 $nprobe
      test_query_index     50 10 $nprobe
      test_query_index    100 10 $nprobe
      test_query_index    200 10 $nprobe
      test_query_index    300 10 $nprobe
      test_query_index    100 100 $nprobe
      test_query_index    200 100 $nprobe
      test_query_index    400 100 $nprobe
      test_query_index    600 100 $nprobe
      test_query_index    800 100 $nprobe
      test_query_index   1000 100 $nprobe
    }
  }

  print_report
}

proc release_test_one {varname name index lConfig} {
  global T
  upvar $varname lReport

  test_build_index $index
  foreach {K recall nprobe} $lConfig {
    test_query_index $K $recall $nprobe
  }

  set times [format_us $T(train)]/[format_us $T(build)]
  lappend lReport "$name {$index}: train/build: $times"
  foreach {K recall nprobe} $lConfig {
    set r $T(recall,$recall,$nprobe,$K)
    set q $T(qps,$recall,$nprobe,$K)
    set res "[format {% -6s} $nprobe/$K] -> ${r}@$recall ${q}qps "
    lappend lReport "        $res"
  }
}

proc release_test {} {
  global C
  set C(--nthread) 8

  set lReq [list \
      sift1m.db  \
      landmark-dino-768-cosine.db \
      agnews-mxbai-1024-euclidean.db \
  ]

  set nMinByte [expr 1024*1024]

  # Check that all required databases are present in cwd
  foreach f $lReq {
    if {[file exists $f]==0 || [file size $f]<$nMinByte} {
      error "missing required db file \"$f\""
    }
  }

  open_db sift1m.db
  build_nearest_if_required
  release_test_one lRep SIFT1M "nbucket 1024 codesize 16 opq 0" {
      1     1   16
      100   1   16
      1     1   48
      100   1   48
  }
  release_test_one lRep SIFT1M "nbucket 0 codesize 16 opq 0" {
      1     1   16
      100   1   16
  }
  release_test_one lRep SIFT1M "nbucket 1024 codesize 0 opq 0" {
      1     1   16
      1     1   48
  }

  open_db landmark-dino-768-cosine.db
  build_nearest_if_required
  release_test_one lRep LANDMARK "nbucket 1024 codesize 48 opq 1" {
      1     1   16
      100   1   48
  }

  open_db agnews-mxbai-1024-euclidean.db
  build_nearest_if_required
  release_test_one lRep AGNEWS "nbucket 1024 codesize 32 opq 1" {
      1     1   16
      100   1   48
  }

  foreach ln $lRep { puts $ln }

  set fd [open vec1.release.txt a+]
  puts $fd "--------------------------------------------------------------"
  foreach ln $lRep { puts $fd $ln }
  close $fd
}

proc format_cycles {c} {
  if {$c>(1000*1000*1000)} {
    set ret "[format %.2f [expr $c/1000000000.0]]G"
  } elseif {$c>(10*1000000)} {
    set ret "[format %d [expr $c/1000000]]M"
  } elseif {$c>(1000000)} {
    set ret "[format %.2f [expr $c/1000000.0]]M"
  } elseif {$c>(10*1000)} {
    set ret "[format %d [expr $c/1000]]K"
  } elseif {$c>1000} {
    set ret "[format %.2f [expr $c/1000.0]]K"
  } else {
    set ret $c
  }

  return $ret
}

# Read the qprofile information for table "v1" from eponymous virtual
# table vec1cat. Print a report to stdout based on the values read.
#
proc print_query_timings {} {

  set lTopic [list]
  db eval {
    SELECT fullkey, value FROM json_tree(
        (SELECT qprofile FROM vec1cat WHERE name='v1')
    ) WHERE type='integer';
  } {
    if {$value>0} {
      set key [string range $fullkey 2 end]
      set A($key) $value

      foreach {topic field} [split $key .] {
        if {$field=="cnt" && $topic!="total"} { lappend lTopic $topic }
      }
    }
  }

  set nTotal $A(total.cycle);
  set total_percent 0.0
  foreach name $lTopic {
    set cnt $A(${name}.cnt)
    set cycle $A(${name}.cycle)

    set percent [expr (100.0 * $cycle) / $nTotal]
    set total_percent [expr $total_percent + $percent]
    set c [format_cycles $cycle]
    puts "[format {% -12s%8s%6.2f%%} $name: $c $percent] ($cnt)"
  }
  puts [format {% -12s%6.2f%%} total: $total_percent]
}


# Argument $t is some number of microseconds. Return a string making
# this easy for humans to read - e.g. "123us" or "12.1ms".
#
proc format_us {t} {
  set ret "${t}us"
  if {$t>(10*1000000)} {
    set ret "[format %.1f [expr $t/1000000.0]]s"
  } elseif {$t>1000000} {
    set ret "[format %.2f [expr $t/1000000.0]]s"
  } elseif {$t>100000} {
    set ret "[format %d [expr $t/1000]]ms"
  } elseif {$t>10000} {
    set ret "[format %.1f [expr $t/1000.0]]ms"
  } elseif {$t>1000} {
    set ret "[format %.2f [expr $t/1000.0]]ms"
  } else {
    set ret "${t}us"
  }
  return $ret
}

# Add commas to integer argument $i to make it easier to read. 
# e.g. 12345 -> "12,345"
#
proc format_int {i} {
  set ret [list]
  while {$i>0} {
    if {$i>=1000} {
      set ret [concat [format %.3d [expr $i % 1000]] $ret]
    } else {
      set ret [concat $i $ret]
    }
    set i [expr $i / 1000]
  }
  join $ret ,
}

proc my_time {caption script} {
  puts -nonewline "$caption..."
  flush stdout
  set t [uplevel [list time $script]]
  set t [lindex $t 0]

  puts " [format_us $t]"

  return $t
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

proc get_distance_function {} {
  set dm [db one {SELECT value FROM meta WHERE field='distance'}]

  set f(cosine)     vec1_cos_distance
  set f(angular)    vec1_cos_distance
  set f(euclidean)  vec1_l2_distance
  set f(normalized) vec1_l2_distance

  if {[info exists f($dm)]==0} {
    puts "Unrecognized distance metric: $dm"
    exit 1
  }
  return $f($dm)
}

# Output a summary of the db for the user to read.
#
proc summarize_db {} {
  set nQuery [db one {SELECT count(*) FROM test}]
  set nVector [db one {SELECT count(*) FROM train}]
  set nDim [db one {SELECT length(vec)/4 FROM train LIMIT 1}]
  set dm [db one {SELECT 'dist=' || value FROM meta WHERE field='distance'}]
  set cmp "$dm -> [get_distance_function]"
  puts "Test is $nQuery queries against $nVector ${nDim}-d vectors ($cmp)"
}

proc vec_to_json_i64 {blob} {
  binary scan $blob w* L
  set ret "\[[join $L ,]\]"
  return $ret
}
proc vec_to_json_i32 {blob} {
  binary scan $blob i* L
  set ret "\[[join $L ,]\]"
  return $ret
}

proc myvar {var} {
  upvar $var v
  if {[info exists v]==0} { return "" }
  return $v
}

proc print_report {} {
  global T
  global C

  proc vkey_cmp {lhs rhs} {
    foreach {lr lp} [split $lhs ,] {}
    foreach {rr rp} [split $rhs ,] {}

    if {$rr==$lr} {
      if {$lp<$rp} { return -1 }
      if {$lp>$rp} { return +1 }
    }
    return [expr $rr - $lr]
  }

  foreach k [array names T qps*] {
    foreach {q recall nprobe k} [split $k ,] {}
    set RECALL($recall) 1
    set V($recall,$nprobe) 1
    set H($k) 1
  }

  if {$C(--report)==""} {
    set fd stdout
  } else {
    set fd [open $C(--report) a]
  }

  set nVector [format_int [db one {SELECT count(*) FROM train}]]
  set nSample [format_int $T(nTrain)]
  set nDim [db one {SELECT length(vec)/4 FROM train LIMIT 1}]
  set zDataset [file tail $C(dbname)]
  regsub {\..*} $zDataset {} zDataset
  
  puts $fd "<!-- Report starts here!!! -->"

  puts $fd "<h1>Dataset: $zDataset ($nVector ${nDim}d vectors)</h1>"
  puts $fd "<table striped=1>"
  puts $fd "<tr><th>Parameter<th>Value"
  puts $fd "<tr><td>Vec1 Version<td>[db one {SELECT vec1_info()}]"
  puts $fd "<tr><td>Index Parameters<td>$T(spec)"
  puts $fd "<tr><td>Training time<td>[format_us $T(train)] ($nSample samples, $C(--nthread) threads)"
  puts $fd "<tr><td>Build time<td>[format_us $T(build)] ($C(--nthread) threads)"
  puts $fd "</table>"
  puts $fd ""
  
  foreach r [lsort -integer [array names RECALL]] {
    puts $fd "<p>Recall@$r results:"
    puts $fd ""
    puts $fd "<table striped=1>"

    catch { array unset K }
    catch { array unset N }
    foreach key [array names T qps*] {
      foreach {q recall nprobe k} [split $key ,] {}
      if {$r==$recall} { 
        set K($k) 1 
        set N($nprobe) 1 
      }
    }

    puts -nonewline $fd "  <tr><th>"
    foreach k [lsort -integer [array names K]] {
      puts -nonewline $fd "<th colspan=2>K=$k"
    }
    puts $fd ""
    puts $fd "  <tr><th>nprobe"
    foreach k [lsort -integer [array names K]] {
      puts -nonewline $fd "<th>Recall<th>QPS"
    }
    puts $fd ""

    foreach nprobe [lsort -integer [array names N]] {
      puts -nonewline $fd "  <tr><td>$nprobe"
      foreach k [lsort -integer [array names K]] {
        puts -nonewline $fd "<td>[format %.3f $T(recall,$r,$nprobe,$k)]"
        puts -nonewline $fd "<td>$T(qps,$r,$nprobe,$k)"
      }
      puts $fd ""
    }
    puts $fd "</table>"
  }

  puts $fd "<!-- Report ends here!!! -->"
  puts $fd ""
  puts $fd ""

  if {$fd!="stdout"} {
    puts "Report output to $C(--report)"
    close $fd
  }
}

# See if the "nearest" table has been built yet. If not, create and
# populate it now. It contains one row per test vector.
#
proc build_nearest_if_required {} {
  set dm [db one {SELECT value FROM meta WHERE field='distance'}]

  set fmt [db one {SELECT dtype FROM __meta__ WHERE table_name='neighbors'}]
  if {$fmt=="int64"} {
    db func ivec_to_json vec_to_json_i64
  } else {
    db func ivec_to_json vec_to_json_i32
  }

  set func [get_distance_function]
  db eval BEGIN
  if {[db one {SELECT count(*) FROM sqlite_schema WHERE name='nearest'}]==0} {
    db eval "
      CREATE TABLE nearest(
          id INTEGER PRIMARY KEY, nn1 JSON, nn10 JSON, nn100 JSON
      );

      WITH groundtruth(qid, val, dist) AS (
        SELECT q.id, j.value, $func (q.vec, b.vec)
        FROM test q, 
             neighbors g, 
             json_each(ivec_to_json(g.vec)) j, 
             train b
        WHERE q.id=g.id /* AND j.key<20 */ AND b.id=j.value
      ), 
      ranked(qid, val, rank) AS (
        SELECT qid, val, rank() OVER (
          PARTITION BY qid ORDER BY dist
        ) FROM groundtruth
      )
      INSERT INTO nearest
        SELECT qid, 
            json_group_array(val) FILTER (WHERE rank=1),
            json_group_array(val) FILTER (WHERE rank<=10),
            json_group_array(val)
        FROM ranked 
        GROUP BY qid
    "
  }
  db eval COMMIT
}

proc test_build_index {lSpec} {
  global C
  global T

  set func [get_distance_function]
  set DIST(vec1_l2_distance) "\"L2\""
  set DIST(vec1_cos_distance) "\"cos\""

  set cols ""
  set values ""
  if {$C(--meta-data)==1} {
    set cols   ", m1"
    set values ", 100"
  } elseif {$C(--meta-data)==4} {
    set cols   ", m4"
    set values ", 10000"
  }

  # Drop any old "v1" table that may exist. And create the new one.
  #
  db eval " 
    DROP TABLE IF EXISTS v1;
    CREATE VIRTUAL TABLE v1 USING vec1(vector $cols);
    SELECT vec1_config('nthread', $C(--nthread) );
    INSERT INTO v1(rowid, vector $cols) SELECT id, vec $values FROM train;
  "

  if {$lSpec=="flat"} {
    db eval " 
      INSERT INTO v1(cmd, arg) 
      VALUES('rebuild', '{index:\"flat\",distance: $DIST($func)}')
    "
    return
  }

  set pairs [list]
  foreach {k v} $lSpec {
    set val $v
    if {[string is double $val]==0} {
      set val "\"$v\""
    }
    lappend pairs "$k:$val"
  }
  set showspec "{[join $pairs {, }]}"
  lappend pairs "svd_verify: $C(--svd-verify)"
  lappend pairs "progress: \"myprogress\""
  lappend pairs "profile: \"myprofile\""
  set spec "{[join $pairs {, }]}"

  # Command to handle vec1_train() progress callbacks.
  #
  unset -nocomplain ::myprogressms
  proc myprogress {percent msg} {
    catch {
      if {[info exists ::myprogressms]==0} {
        set ::myprogressms [clock milli]
      }
      set ms [expr [clock milli] - $::myprogressms]
      puts "${ms}ms: $percent%: $msg"
    } xyz
  }
  db func myprogress myprogress

  # Command to handle vec1_train() profile callbacks.
  #
  proc myprofile {p} {
    puts "profile: $p"
  }
  db func myprofile myprofile

  # Check if a "learn" table exists. If it does not, use data from "train"
  # to train the model.
  #
  if {[db one {SELECT count(*) FROM sqlite_schema WHERE name='learn'}]==0} {
    set learn "train"
  } else {
    set learn "learn"
  }
  set nTotal [db one "SELECT count(*) FROM $learn"]
  set nTrain [expr {$nTotal > $C(--max-train) ? $C(--max-train) : $nTotal}]
  
  set caption "$C(--train-function)($spec) on $nTrain/$nTotal \"$learn\" rows"

  set T(nTrain) $nTrain
  set T(spec) $showspec
  set T(train) [my_time "Training: $caption" {
    puts ""
    set model [db one [subst {
        SELECT $C(--train-function)(vec, '$spec') FROM $learn
        WHERE (
          (abs(random()) / 9223372036854775807.0) * $nTotal <= $nTrain
        )
    }]]
  }]

  set T(build) [my_time "Building index" {
    db eval {
      INSERT INTO v1(cmd, vector) VALUES('rebuild', $model);
    }
  }]
}

proc test_query_index {K {recall 1} {nprobe 0.05} } {
  global T
  global C

  set nMmap [db one { PRAGMA mmap_size = 20000000000 }]
  puts "Using [expr ${nMmap}/(1024*1024)]MB mmap region"
  db eval { 
    PRAGMA synchronous = off;
    SELECT rowid FROM v1 LIMIT 1;
    SELECT vec1_config('nprobe', $nprobe);
  }

  set func [get_distance_function]

  set nn "nn$recall"
  set sql1 [subst "
    SELECT vec, $nn AS nn, test.id AS id 
    FROM test JOIN nearest ON (test.id=nearest.id)
  "]

  #set spec "{K:$K,nprobe:$nprobe,nprobe_slack:0.9}"
  set spec "{K:$K,nprobe:$nprobe}"

  set tail ""
  if {$C(--meta-data)==1} {
    append tail "WHERE m1=100"
  } elseif {$C(--meta-data)==4} {
    append tail "WHERE m4=10000"
  }

  if {$K!=$recall} { 
    append tail [subst -novar {
      ORDER BY [set func] ($vec, v1.vector)
      LIMIT [set recall]}]
  }

  set sql2 [subst -novar {
    SELECT coalesce(sum( ans IN (SELECT value FROM json_each($nn) )), 0) FROM (
      SELECT v1.rowid AS ans 
      FROM v1($vec, $spec) [set tail]
    )
  }]

  set nQuery [db one {SELECT count(*) FROM test}]
  set caption "Testing $nQuery recall@$recall queries $spec" 

  set s 0
  set t [my_time $caption {
    set nQuery 0
    db eval $sql1 { 
      set nGood [db one $sql2]
      # if {$nGood!=$recall} { puts "id=$id nGood=$nGood" }
      incr s $nGood
      incr nQuery
    }
    puts -nonewline "($s / [expr $recall*$nQuery]) "
  }]

  set qps [expr ($nQuery*1000000 / $t)]
  set T(qps,$recall,$nprobe,$K) $qps
  set T(recall,$recall,$nprobe,$K) [expr (($s * 1.0) / ($nQuery * $recall))]

  return $s
}

main
exit

