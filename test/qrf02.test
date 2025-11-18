# 2025-11-05
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
# These tests are for EXPLAIN and EXPLAIN QUERY PLAN formatting, the 
# output of which can change when enhancments are made to the query
# planner.  So expect to have to modify the expected results of these
# test cases in the future.
#

set testdir [file dirname $argv0]
source $testdir/tester.tcl
set testprefix qrf02

do_execsql_test 1.0 {
  CREATE TABLE t1(a);
  INSERT INTO t1 VALUES(1);
}

set result [db format {EXPLAIN SELECT * FROM t1}]
do_test 1.10 {
  set result
} {/*addr  opcode         p1    p2    p3    p4             p5  comment      
----  -------------  ----  ----  ----  -------------  --  -------------
0     Init           */}
regsub -all {\d+} $result {N} result2
do_test 1.11 {
  set result2
} "/.*\nN     Rewind .*\nN       Column .*/"

do_test 1.20 {
  set result "\n[db format {EXPLAIN QUERY PLAN SELECT * FROM t1}]"
} {
QUERY PLAN
`--SCAN t1
}

finish_test
