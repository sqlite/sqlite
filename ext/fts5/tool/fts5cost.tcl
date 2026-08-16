#
# 2026 March 20
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#    May you do good and not evil.
#    May you find forgiveness for yourself and forgive others.
#    May you share freely, never taking more than you give.
#
#--------------------------------------------------------------------------
#
# This script extracts the documentation for the API used by fts5 auxiliary 
# functions from header file fts5.h. It outputs html text on stdout that
# is included in the documentation on the web.
# 


sqlite3 db fts5cost.db

# Create an IPK table with 1,000,000 entries. Short records.
#
set res [list [catch { db eval {SELECT count(*) FROM t1} } msg] $msg]
if {$res!="0 1000000"} {
  db eval {
    PRAGMA mmap_size = 1000000000;  -- 1GB
    DROP TABLE IF EXISTS t1;
    CREATE TABLE t1(a INTEGER PRIMARY KEY, b TEXT);
    WITH s(i) AS (
        SELECT 1 UNION ALL SELECT i+1 FROM s WHERE i<1_000_000
    )
    INSERT INTO t1 SELECT i, hex(randomblob(8)) FROM s;
  }
}

# Create an FTS5 table with 1,000,000 entries. Each row contains a single
# column containing a document of 100 terms chosen pseudo-randomly from
# a vocabularly of 2000.
set res [list [catch { db eval {SELECT count(*) FROM f1} } msg] $msg]
if {$res!="0 1000000"} {
  set nVocab 2000
  set nTerm   100
  db eval {
    BEGIN;
    DROP TABLE IF EXISTS vocab1;
    CREATE TABLE vocab1(w);
  }
  for {set ii 0} {$ii<$nVocab} {incr ii} {
    set word [format %06x [expr {int(abs(rand()) * 0xFFFFFF)}]]
    db eval { INSERT INTO vocab1 VALUES($word) }
    lappend lVocab $word
  }
  db func doc doc
  proc doc {} {
    for {set ii 0} {$ii<$::nTerm} {incr ii} {
      lappend ret [lindex $::lVocab [expr int(abs(rand())*$::nVocab)]]
    }
    set ret
  }
  db eval {
    DROP TABLE IF EXISTS f1;
    CREATE VIRTUAL TABLE f1 USING fts5(x);
    WITH s(i) AS (
        SELECT 1 UNION ALL SELECT i+1 FROM s WHERE i<1_000_000
    )
    INSERT INTO f1(rowid, x) SELECT i, doc() FROM s;
    COMMIT;
  }
} else {
  set lVocab [db eval { SELECT * FROM vocab1 }]
  set nVocab [llength $lVocab]
}

proc rowid_query {n} {
  set rowid 654
  for {set ii 0} {$ii<$n} {incr ii} {
    db eval { SELECT b FROM t1 WHERE a = $rowid }
    set rowid [expr {($rowid + 7717) % 1000000}]
  }
}

proc rowid_query_fts {n} {
  set rowid 654
  for {set ii 0} {$ii<$n} {incr ii} {
    db eval { SELECT * FROM f1 WHERE rowid = $rowid }
    set rowid [expr {($rowid + 7717) % 1000000}]
  }
}

proc match_query_fts {n} {
  set idx 654
  for {set ii 0} {$ii<$n} {incr ii} {
    set match [lrange $::lVocab $idx $idx+1]
    db eval { SELECT * FROM f1($match) }
    set idx [expr {($idx + 7717) % $::nVocab}]
  }
}

proc prefix_query_fts {n} {
  set idx 654
  for {set ii 0} {$ii<$n} {incr ii} {
    set match "[lindex $::lVocab $idx]*"
    db eval { SELECT * FROM f1($match) }
    set idx [expr {($idx + 7717) % $::nVocab}]
  }
}

proc match_rowid_query_fts {n} {
  set idx 654
  for {set ii 0} {$ii<$n} {incr ii} {
    set match "[lindex $::lVocab $idx]"
    db eval { SELECT * FROM f1($match) WHERE rowid=500000 }
    set idx [expr {($idx + 7717) % $::nVocab}]
  }
}

proc prefix_rowid_query_fts {n} {
  set idx 654
  for {set ii 0} {$ii<$n} {incr ii} {
    set match "[lindex $::lVocab $idx]*"
    db eval { SELECT * FROM f1($match) WHERE rowid=500000 }
    set idx [expr {($idx + 7717) % $::nVocab}]
  }
}


proc mytime {cmd div} {
  set tm [time $cmd]
  expr {[lindex $tm 0] / $div}
}

#set us [mytime { match_rowid_query_fts 1000 } 1000]
#puts "1000 match/rowid queries on fts5 table: ${us} per query"

set us [mytime { prefix_rowid_query_fts 1000 } 1000]
puts "1000 prefix/rowid queries on fts5 table: ${us} per query"

set us [mytime { match_query_fts 10 } 10]
puts "10 match queries on fts5 table: ${us} per query"

set us [mytime { prefix_query_fts 10 } 10]
puts "10 prefix queries on fts5 table: ${us} per query"

set us [mytime { prefix_rowid_query_fts 1000 } 1000]
puts "1000 prefix/rowid queries on fts5 table: ${us} per query"

set us [mytime { rowid_query 10000 } 10000]
puts "10000 by-rowid queries on normal table: ${us} per query"

set us [mytime { rowid_query_fts 10000 } 10000]
puts "10000 by-rowid queries on fts5 table: ${us} per query"


