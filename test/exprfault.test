# 2021 April 17
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
set testprefix exprfault

do_execsql_test 1.0 {
  CREATE TABLE t1(a);                 
  CREATE TABLE t2(d);                 
}
faultsim_save_and_close

do_faultsim_test 1.1 -faults oom* -prep {
  faultsim_restore_and_reopen
} -body {
  execsql {
    SELECT a = ( SELECT d FROM (SELECT d FROM t2) ) FROM t1 
  }
} -test {
  faultsim_test_result {0 {}}
}

do_faultsim_test 2 -faults oom* -prep {
  faultsim_restore_and_reopen
} -body {
  execsql {
    SELECT hex ( unhex('ABCDEF') );
  }
} -test {
  faultsim_test_result {0 ABCDEF}
}

#-------------------------------------------------------------------------
reset_db
do_execsql_test 3.0 {
  PRAGMA page_size=1024;
  CREATE TABLE t1(a INTEGER PRIMARY KEY, b);
  CREATE INDEX i1 ON t1( hex(b) );
  INSERT INTO t1 VALUES(10, randomblob(500));
}
faultsim_save_and_close

do_faultsim_test 3 -faults oom* -prep {
  faultsim_restore_and_reopen
} -body {
  execsql {
    UPDATE t1 SET b=randomblob(500);
  }
} -test {
  faultsim_test_result {0 {}}
}



finish_test
