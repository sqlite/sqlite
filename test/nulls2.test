# 2026 June 4
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#    May you do good and not evil.
#    May you find forgiveness for yourself and forgive others.
#    May you share freely, never taking more than you give.
#
#***********************************************************************
# This file implements regression tests for SQLite library.
#

set testdir [file dirname $argv0]
source $testdir/tester.tcl
set testprefix nulls2

foreach {tn idx} {
  1 {}
  2 { CREATE INDEX i1 ON t1(a, b, c) }
  3 { CREATE INDEX i1 ON t1(b, c, a) }
  4 { CREATE INDEX i1 ON t1(c, a, b) }
  5 { CREATE INDEX i1 ON t1(c, b, a) }
} {
  reset_db

  do_execsql_test 1.$tn.0 {
    CREATE TABLE t1(a, b, c);

    INSERT INTO t1 VALUES(1, 1, NULL);
    INSERT INTO t1 VALUES(2, 2, NULL);

    CREATE TABLE t2(d NOT NULL, e NOT NULL, f);
    INSERT INTO t2 VALUES(1, 1, NULL);
    INSERT INTO t2 VALUES(2, 2, NULL);
  }

  execsql $idx

  # This should return zero rows, as "f" is always NULL.
  #
  do_execsql_test 1.$tn.1 {
    SELECT rowid FROM t2 WHERE (d, e, f) IN (
        SELECT a, b, c FROM t1
    )
  }
}

foreach {tn idx} {
  1 {}
  2 { CREATE INDEX i1 ON t1(a, b, c) }
  3 { CREATE INDEX i1 ON t1(b, c, a) }
  4 { CREATE INDEX i1 ON t1(c, a, b) }
  5 { CREATE INDEX i1 ON t1(c, b, a) }
} {
  reset_db

  do_execsql_test 2.$tn.0 {
    CREATE TABLE t1(a, b, c COLLATE nocase);
    INSERT INTO t1 VALUES('one', 'two', 'THREE');
    INSERT INTO t1 VALUES('four', 'five', 'SIX');
  }

  execsql $idx

  do_execsql_test 1.$tn.1 {
    SELECT (NULL, 'two', 'three') IN (
        SELECT a, b, c FROM t1
    ) IS NULL
  } 1
}


finish_test
