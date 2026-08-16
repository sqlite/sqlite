# 2026-01-17
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
# Test cases for json_array_insert().
#

set testdir [file dirname $argv0]
source $testdir/tester.tcl
set testprefix json109

do_execsql_test 1.1 {
  SELECT json_array_insert('[1,2,3]','$[0]',999,'$[0]',888);
} {{[888,999,1,2,3]}}
do_execsql_test 1.2 {
  SELECT json_array_insert('[1,2,3]','$[0]',999,'$[#]',888);
} {{[999,1,2,3,888]}}
do_execsql_test 1.3 {
  SELECT json_array_insert('[1,2,3]','$[1]',888);
} {{[1,888,2,3]}}
do_execsql_test 1.4 {
  SELECT json_array_insert('[1,2,3]','$[2]',888);
} {{[1,2,888,3]}}
do_execsql_test 1.5 {
  SELECT json_array_insert('[1,2,3]','$[3]',888);
} {{[1,2,3,888]}}
do_execsql_test 1.6 {
  SELECT json_array_insert('[1,2,3]','$[#-1]',888);
} {{[1,2,888,3]}}
do_execsql_test 1.7 {
  SELECT json_array_insert('[1,2,3]','$[#-2]',888);
} {{[1,888,2,3]}}
do_execsql_test 1.8 {
  SELECT json_array_insert('[1,2,3]','$[#-3]',888);
} {{[888,1,2,3]}}
do_execsql_test 1.9 {
  SELECT json_array_insert('[1,2,3]','$[#-4]',888);
} {{[1,2,3]}}

do_catchsql_test 2.1 {
  SELECT json_array_insert('{a:[1,2,3]}','$.a',888);
} {1 {not an array element: '$.a'}}
do_catchsql_test 2.2 {
  SELECT json_array_insert('{a:[1,2,3]}','$.b',888);
} {1 {not an array element: '$.b'}}
do_catchsql_test 2.3 {
  SELECT json_array_insert('{a:[1,2,3]}','$.b[0]',888);
} {0 {{{"a":[1,2,3],"b":[888]}}}}
do_catchsql_test 2.4 {
  SELECT json_array_insert('{a:[1,2,3]}','$.b.c.d[0]',888);
} {0 {{{"a":[1,2,3],"b":{"c":{"d":[888]}}}}}}
do_catchsql_test 2.5 {
  SELECT json_array_insert('{a:[1,2,3]}','$.b.c.d[0',888);
} {1 {not an array element: '$.b.c.d[0'}}
do_catchsql_test 2.6 {
  SELECT json_array_insert('{a:[1,2,3]}','$.b.c.d',888);
} {1 {not an array element: '$.b.c.d'}}
do_catchsql_test 2.7 {
  SELECT json_array_insert('{a:[1,2,3]}','$[0]',888);
} {0 {{{"a":[1,2,3]}}}}
do_catchsql_test 2.8 {
  SELECT json_array_insert('{a:[1,2,3]}','$.b[0]',888,'$.a[1]','999','$.c',0);
} {1 {not an array element: '$.c'}}

finish_test
