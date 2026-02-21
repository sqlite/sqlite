# 2023-07-03
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
# This file contains a test that attempts to verify the claim that the
# floatpoint-to-text conversion routines built into SQLite maintain at
# least 15 significant digits of accuracy.
#

set testdir [file dirname $argv0]
source $testdir/tester.tcl


# Unusual rendering cases:
#
do_execsql_test fpconv1-1.0 {
  SELECT 1.23 - 2.34;
} {-1.1099999999999999}
#  ^---  Not -1.11 as you would expect.  -1.11 has a different bit pattern

do_execsql_test fpconv1-1.1 {
  SELECT 1.23 * 2.34;
} {2.8781999999999996}
#  ^---  Not 2.8782 as you would expect.  2.8782 has a different bit pattern

# Change significant digits to 15 and get a different result.
sqlite3_db_config db FP_DIGITS 15
do_execsql_test fpconv1-1.2 {
  SELECT 1.23 - 2.34;
} {-1.11}
do_execsql_test fpconv1-1.3 {
  SELECT 1.23 * 2.34;
} {2.8782}
sqlite3_db_config db FP_DIGITS 17


if {[catch {load_static_extension db decimal} error]} {
  puts "Skipping decimal tests, hit load error: $error"
  finish_test; return
}

sqlite3_create_function db
do_execsql_test fpconv1-2.0 {
  WITH RECURSIVE
       /* Number of random floating-point values to try.
       ** On a circa 2021 Ryzen 5950X running Mint Linux, and
       ** compiled with -O0 -DSQLITE_DEBUG, this test runs at
       ** about 150000 cases per second  ------------------vvvvvvv */
    c(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM c WHERE x<500_000),
    fp(y) AS MATERIALIZED (
       SELECT CAST( format('%+d.%019d0e%+03d',
                           random()%10,abs(random()),random()%200) AS real)
        FROM c
    )
  SELECT y FROM fp
   WHERE -log10(abs(decimal_sub(dtostr(y,24),format('%!.24e',y))/y))<17.0;
                     /* Number of digits of accuracy required -------^^^^ */
} {}
#  ^---- Expect a empty set as the result.  The output is all tested numbers
#        that fail to preserve at least 16 significant digits of accuracy.

########################################################################
# Random test to ensure that double -> text -> double conversions
# round-trip exactly.
#

load_static_extension db ieee754

do_execsql_test fpconv1-3.0 {
  WITH RECURSIVE
    c(x,s) AS MATERIALIZED (VALUES(1,random()&0xffefffffffffffff)
               UNION ALL
               SELECT x+1,random()&0xffefffffffffffff
                 FROM c WHERE x<1_000_000),
    fp(y,s) AS (
       SELECT ieee754_from_int(s),s FROM c
    ),
    fp2(yt,y,s) AS (
       SELECT y||'', y, s FROM fp
    )
  SELECT format('%#016x',s) aS 'orig-hex',
         format('%#016x',ieee754_to_int(CAST(yt AS real))) AS 'full-roundtrip',
         yt AS 'rendered-as',
         decimal_exp(yt,30) AS 'text-decimal',
         decimal_exp(ieee754_from_int(s),30) AS 'float-decimal'
    FROM fp2
   WHERE ieee754_to_int(CAST(yt AS real))<>s;
} {}
#  ^---- Values that fail to round-trip will be reported

finish_test
