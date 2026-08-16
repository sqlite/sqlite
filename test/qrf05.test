# 2025-12-02
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
# Test cases for the Query Result Formatter (QRF)
#

set testdir [file dirname $argv0]
source $testdir/tester.tcl
set testprefix qrf05

do_execsql_test 1.0 {
  CREATE TABLE t1(a INT NOT NULL);
}
do_test 1.1 {
  set rc [catch {db format -style list \
                  {INSERT INTO t1 VALUES(123) RETURNING *}} msg]
  list $rc [string trim $msg]
} {0 123}
do_test 1.2 {
  set rc [catch {db format -style list \
                  {INSERT INTO t1 VALUES(NULL) RETURNING *}} msg]
  list $rc [string trim $msg]
} {1  {NOT NULL constraint failed: t1.a}}
do_test 1.3 {
  set rc [catch {db format -version 99 {SELECT * FROM t1}} msg]
  list $rc [string trim $msg]
} {1 {unusable sqlite3_qrf_spec.iVersion (99)}}

finish_test
