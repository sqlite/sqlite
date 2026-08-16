# 2026-03-16
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#    May you do good and not evil.
#    May you find forgiveness for yourself and forgive others.
#    May you share freely, never taking more than you give.
#
#***********************************************************************
#
# This file contains test cases for handling stale expression indexes -
# expression indexes for which the value of the expression is different
# from (though usually very close to) the value stored on disk.
#
set testdir [file dirname $argv0]
source $testdir/tester.tcl
set testprefix expridx1

# Check that OP_IdxDelete works when:
#
# 1.1.* The real value in the index is greater than that in the table,
# 1.2.* The real value in the index is smaller than that in the table,
# 1.3.* Duplicate distorted real values.
#

do_execsql_test 1.0.1 {
  CREATE TABLE t1(a INTEGER PRIMARY KEY, b REAL);
  INSERT INTO t1 VALUES(10, 10.0);
  INSERT INTO t1 VALUES(15, 15.0);
  INSERT INTO t1 VALUES(20, 20.0);
  INSERT INTO t1 VALUES(25, 25.0);
  INSERT INTO t1 VALUES(30, 30.0);
  CREATE INDEX i1 ON t1((b+0.0));
}

# Return a list of rowids from table t1 for which there is no exact
# match in the index.
set idxcheck {
  SELECT rowid FROM t1 AS o NOT INDEXED 
  WHERE NOT EXISTS (SELECT 1 FROM t1 WHERE +a=o.a AND b+0.0=o.b+0.0)
}

set root [db one {SELECT rootpage FROM sqlite_schema WHERE name='i1'}]

do_test 1.0.2 {
  # Create an imposter table for index i1
  sqlite3_test_control SQLITE_TESTCTRL_IMPOSTER db main 1 $root
  db eval {
    CREATE TABLE x1(b, rowid, PRIMARY KEY(b, rowid)) WITHOUT ROWID;
  }
  sqlite3_test_control SQLITE_TESTCTRL_IMPOSTER db main 0 0

  db one { PRAGMA integrity_check }
} ok

do_execsql_test 1.1.1 {
  UPDATE x1 SET b=21.0 WHERE rowid=20;
}
do_execsql_test 1.1.1b {
  PRAGMA integrity_check;
} {{row 3 missing from index i1}}
do_execsql_test 1.1.1c $idxcheck {20}

do_execsql_test 1.1.2 {
  DELETE FROM t1 WHERE a=20;
} {}
do_execsql_test 1.1.3 {
  PRAGMA integrity_check;
} {ok}
do_execsql_test 1.1.4 $idxcheck {}

do_execsql_test 1.2.1 {
  UPDATE x1 SET b=26.0 WHERE rowid=25;
  PRAGMA integrity_check;
} {
  {row 3 missing from index i1}
}
do_execsql_test 1.2.2 $idxcheck {25}

do_execsql_test 1.2.3 {
  DELETE FROM t1 WHERE a=25;
} {}
do_execsql_test 1.2.4 {
  PRAGMA integrity_check;
} {ok}
do_execsql_test 1.2.5 $idxcheck {}

do_execsql_test 1.3.1 {
  DELETE FROM t1;
  INSERT INTO t1 VALUES(5,  15.0);
  INSERT INTO t1 VALUES(10, 20.0);
  INSERT INTO t1 VALUES(15, 20.0);
  INSERT INTO t1 VALUES(20, 20.0);
  INSERT INTO t1 VALUES(25, 20.0);
  INSERT INTO t1 VALUES(30, 20.0);
  INSERT INTO t1 VALUES(35, 25.0);

  UPDATE x1 SET b=19.0 WHERE b=20.0;
  PRAGMA integrity_check;
} {
  {row 2 missing from index i1} 
  {row 3 missing from index i1} 
  {row 4 missing from index i1} 
  {row 5 missing from index i1}
  {row 6 missing from index i1}
}

do_execsql_test 1.3.2 $idxcheck {10 15 20 25 30}

foreach {tn a} {
  1 15   2 30   3 20   4 10   5 25
} {
  do_execsql_test 1.3.3.$tn {
    DELETE FROM t1 WHERE a=$a
  }
}
do_execsql_test 1.3.4 {
  PRAGMA integrity_check
} {ok}
do_execsql_test 1.3.5 $idxcheck {}

#-------------------------------------------------------------------------
reset_db
set nRow [expr 1000]
do_execsql_test 2.0 {
  CREATE TABLE t1(a, b, c, PRIMARY KEY(a, b)) WITHOUT ROWID;
  WITH s(i) AS (
    SELECT 1 UNION ALL SELECT i+1 FROM s WHERE i<$nRow
  )
  INSERT INTO t1 SELECT i, random(), hex(randomblob(50)) FROM s;
  CREATE INDEX t1c ON t1(+c);
}

set idxcheck {
  SELECT a, b FROM t1 AS o NOT INDEXED 
  WHERE NOT EXISTS (SELECT 1 FROM t1 WHERE +a=o.a AND +b=o.b AND +c=o.c)
}

set root [db one {SELECT rootpage FROM sqlite_schema WHERE name='t1c'}]

do_test 2.1 { 
  sqlite3_test_control SQLITE_TESTCTRL_IMPOSTER db main 1 $root
  db eval {
    CREATE TABLE x1(a, b, c, PRIMARY KEY(c, a, b)) WITHOUT ROWID;
  }
  sqlite3_test_control SQLITE_TESTCTRL_IMPOSTER db main 0 0
} {}

do_execsql_test 2.2 {
  UPDATE x1 SET c=hex(randomblob(50)) WHERE (a%2)!=0
}

do_execsql_test 2.3 "
  SELECT count(*) FROM ( $idxcheck )
" [expr $nRow/2]

do_test 2.4 {
  for {set ii 1} {$ii<$nRow} {incr ii 2} {
    execsql { DELETE FROM t1 WHERE a=$ii }
  }
  execsql {PRAGMA integrity_check}
} {ok}

do_execsql_test 2.5 "
  SELECT count(*) FROM ( $idxcheck )
" 0

#-------------------------------------------------------------------------
reset_db
do_execsql_test 3.0 {
  CREATE TABLE y1(a, b, c GENERATED ALWAYS AS (a*b) VIRTUAL);
  CREATE INDEX i1 ON y1(c);
  INSERT INTO y1 VALUES(2, 3);
  INSERT INTO y1 VALUES(4, 5);
}

set idxcheck {
  SELECT rowid FROM y1 AS o NOT INDEXED 
  WHERE NOT EXISTS (SELECT 1 FROM y1 WHERE +rowid=o.rowid AND c=o.c)
}

set root [db one {SELECT rootpage FROM sqlite_schema WHERE name='i1'}]

do_test 3.1 { 
  sqlite3_test_control SQLITE_TESTCTRL_IMPOSTER db main 1 $root
  db eval { CREATE TABLE x1(c, rowid, PRIMARY KEY(c, rowid)) WITHOUT ROWID; }
  sqlite3_test_control SQLITE_TESTCTRL_IMPOSTER db main 0 0
} {}

do_execsql_test 3.2 {
  UPDATE x1 SET c=19 WHERE rowid=2;
}

do_execsql_test 3.3 $idxcheck 2 

do_execsql_test 3.4 {
  DELETE FROM y1 WHERE a=4;
}
do_execsql_test 3.5 $idxcheck {}

#-------------------------------------------------------------------------
reset_db
do_execsql_test 4.0 {
  CREATE TABLE z1(a INTEGER PRIMARY KEY, b);
  CREATE INDEX z1b ON z1(b+0.0);
  INSERT INTO z1 VALUES(1, 1.0);
  INSERT INTO z1 VALUES(2, 4.0);
  INSERT INTO z1 VALUES(3, 4.0);
  INSERT INTO z1 VALUES(4, 4.0);
  INSERT INTO z1 VALUES(5, 4.0);
  INSERT INTO z1 VALUES(6, 4.0);
  INSERT INTO z1 VALUES(7, 4.0);
  INSERT INTO z1 VALUES(8, 1.0);
}

set root [db one {SELECT rootpage FROM sqlite_schema WHERE name='z1b'}]
do_test 4.1 { 
  sqlite3_test_control SQLITE_TESTCTRL_IMPOSTER db main 1 $root
  db eval { CREATE TABLE x1(b, a, PRIMARY KEY(b, a)) WITHOUT ROWID; }
  sqlite3_test_control SQLITE_TESTCTRL_IMPOSTER db main 0 0
} {}

do_execsql_test 4.2 {
  UPDATE x1 SET b=4.000000000000001 WHERE a=2;          -- 1 ULP
  UPDATE x1 SET b=4.000000000000002 WHERE a=3;          -- 2 ULP
  UPDATE x1 SET b=4.000000000000003 WHERE a=4;          -- 3 ULP
  UPDATE x1 SET b=3.9999999999999996 WHERE a=5;         -- -1 ULP
  UPDATE x1 SET b=3.9999999999999992 WHERE a=6;         -- -2 ULP
  UPDATE x1 SET b=3.9999999999999988 WHERE a=7;         -- -3 ULP
}

do_execsql_test 4.3 {
  PRAGMA integrity_check
} {
  {index z1b stores an imprecise floating-point value for row 2}
  {index z1b stores an imprecise floating-point value for row 3}
  {row 4 missing from index z1b}
  {index z1b stores an imprecise floating-point value for row 5}
  {index z1b stores an imprecise floating-point value for row 6}
  {row 7 missing from index z1b}
}

do_execsql_test 4.4 {
  UPDATE z1 SET b=-4.0 WHERE b=4.0;
  PRAGMA integrity_check;
} {ok}

do_execsql_test 4.5 {
  UPDATE x1 SET b=-4.000000000000001 WHERE a=2;          -- -1 ULP
  UPDATE x1 SET b=-4.000000000000002 WHERE a=3;          -- -2 ULP
  UPDATE x1 SET b=-4.000000000000003 WHERE a=4;          -- -3 ULP
  UPDATE x1 SET b=-3.9999999999999996 WHERE a=5;         -- 1 ULP
  UPDATE x1 SET b=-3.9999999999999992 WHERE a=6;         -- 2 ULP
  UPDATE x1 SET b=-3.9999999999999988 WHERE a=7;         -- 3 ULP
}

do_execsql_test 4.6 {
  PRAGMA integrity_check
} {
  {index z1b stores an imprecise floating-point value for row 2}
  {index z1b stores an imprecise floating-point value for row 3}
  {row 4 missing from index z1b}
  {index z1b stores an imprecise floating-point value for row 5}
  {index z1b stores an imprecise floating-point value for row 6}
  {row 7 missing from index z1b}
}





finish_test
