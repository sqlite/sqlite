# 2018 May 19
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

source [file join [file dirname $argv0] pg_common.tcl]

#=========================================================================


start_test literal2 "2024 Jan 23"

execsql_test  1.0 { SELECT 123_456 }
errorsql_test 1.1 { SELECT 123__456 }

execsql_float_test 2.1 { SELECT 1.0e1_2 }

finish_test


