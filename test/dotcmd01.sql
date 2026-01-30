#!sqlite3
#
# 2026-01-30
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
# Miscellaneous tests for dot-commands
#
#   ./sqlite3 test/dotcmd01.sql
#

.testcase setup
.open :memory:
.mode tty
.check ''

# The ".eqp on" setting does not affect the output from .fullschema
# and similar.
#
.testcase 100
CREATE TABLE t1(a,b,c);
WITH RECURSIVE c(n) AS (VALUES(1) UNION ALL SELECT n+1 FROM c WHERE n<300)
  INSERT INTO t1(a,b,c)
    SELECT n%10, n%30, n%100 FROM c;
CREATE INDEX t1a ON t1(a);
CREATE INDEX t1b ON t1(b);
ANALYZE;
.eqp on
.fullschema
.check <<END
CREATE TABLE t1(a,b,c);
CREATE INDEX t1a ON t1(a);
CREATE INDEX t1b ON t1(b);
ANALYZE sqlite_schema;
INSERT INTO sqlite_stat1 VALUES('t1','t1b','300 10');
INSERT INTO sqlite_stat1 VALUES('t1','t1a','300 30');
ANALYZE sqlite_schema;
END

.testcase 110
.schema
.check <<END
CREATE TABLE t1(a,b,c);
CREATE INDEX t1a ON t1(a);
CREATE INDEX t1b ON t1(b);
CREATE TABLE sqlite_stat1(tbl,idx,stat);
END

.testcase 120
.tables
.check --glob "t1"

.testcase 130
.indexes
.check --glob t1a*t1b
