# 2026-02-20
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
# Tests of the sqlite3AtoF() function.
#

set testdir [file dirname $argv0]
source $testdir/tester.tcl

# Rounding cases:
#
do_execsql_test atof2-1.0 {
  SELECT format('%g',192.496475);
} 192.496
do_execsql_test atof2-1.1 {
  SELECT format('%g',192.496501);
} 192.497

load_static_extension db ieee754
do_execsql_test atof2-2.1 {
  SELECT format('%!.30f',ieee754_inc(100.0,-1));
} 99.9999999999999858
do_execsql_test atof2-2.2 {
  SELECT format('%!.30f',ieee754_inc(100.0,-2));
} 99.9999999999999716

finish_test
