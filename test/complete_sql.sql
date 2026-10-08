#!sqlite3
#
# 2026-10-08
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
# Test cases for the "shell_complete_sql()" function in the CLI.
#
#   ./sqlite3 test/complete_sql.sql
#
#
.testcase 100
.mode line --limits off
SELECT shell_complete_sql('CREATE TABLE t1(a) /*') AS out;
.check <<END
out: CREATE TABLE t1(a) /**/
END

.testcase 110
SELECT shell_complete_sql('CREATE TABLE t1(a) --') AS out;
.check <<END
out: CREATE TABLE t1(a) --

END

.testcase 120
SELECT shell_complete_sql('CREATE TABLE t1(a CHECK(a<>''/*''))') AS out;
.check <<END
out: CREATE TABLE t1(a CHECK(a<>'/*'))
END

.testcase 130
SELECT shell_complete_sql('CREATE TABLE t1(a) ''one') AS out;
.check <<END
out: CREATE TABLE t1(a) 'one'
END

.testcase 140
SELECT shell_complete_sql('CREATE TABLE t1(a) "one') AS out;
.check <<END
out: CREATE TABLE t1(a) "one"
END

.testcase 150
SELECT shell_complete_sql('CREATE TABLE t1(a) `one') AS out;
.check <<END
out: CREATE TABLE t1(a) `one`
END

.testcase 160
SELECT shell_complete_sql('CREATE TABLE t1(a) [one') AS out;
.check <<END
out: CREATE TABLE t1(a) [one]
END

.testcase 170
SELECT shell_complete_sql('CREATE TRIGGER t1(a) [one') AS out;
.check <<END
out: CREATE TRIGGER t1(a) [one];END
END

.testcase 180
SELECT shell_complete_sql('CREATE TRIGGER t1(a) [one]') AS out;
.check <<END
out: CREATE TRIGGER t1(a) [one];END
END

.testcase 190
SELECT shell_complete_sql('CREATE TRIGGER t1(a) [one];') AS out;
.check <<END
out: CREATE TRIGGER t1(a) [one];END
END

.testcase 200
SELECT shell_complete_sql('CREATE TABLE t1(a);') AS out;
.check <<END
out: CREATE TABLE t1(a);
END

.testcase 210
SELECT shell_complete_sql(' ') AS out;
.check <<END
out:  
END

.testcase 220
SELECT shell_complete_sql(' /* only') AS out;
.check <<END
out:  /* only*/
END

.testcase 300
SELECT shell_complete_sql('SELECT (((''',1) AS out;
.check <<END
out: SELECT ((('')));
END

.testcase 310
SELECT shell_complete_sql('SELECT (((/*',1) AS out;
.check <<END
out: SELECT (((/**/)));
END

.testcase 320
SELECT shell_complete_sql('SELECT (((/*',3) AS out;
.check <<END
out: */)));
END
