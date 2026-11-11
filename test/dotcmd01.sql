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
.open :memory:
.mode -reset quote
.if NOT capable('vtab')
.testcase setup
.check ''
.exit
.endif

.testcase setup
.open :memory:
.mode tty
.check ''

# The ".eqp on" setting does not affect the output from .fullschema
# and similar.
#
.testctrl opt -Stat4
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
INSERT INTO sqlite_stat1 VALUES('t1','t1b','300 10'),
  ('t1','t1a','300 30');
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

# The default comparison for ".check" ignores leading and trailing
# whitespace.  Each interior whitespace character compares equal to exactly
# one whitespace character at the same position in the other string.  A
# whitespace character never matches a character that is not whitespace.
#
.mode --reset list
.testcase 200
SELECT 'a'||char(10)||'b';
.check 'a b'
.testcase 201
SELECT '  a'||char(9)||'b  '||char(10);
.check 'a b'
.testcase 202
SELECT 'a   b';
.check --not 'a b'
.testcase 203
SELECT 'a b';
.check --not 'a   b'
.testcase 204
SELECT 'a b';
.check --not 'a/b'
.testcase 205
SELECT 'a/b';
.check --not 'a b'
.testcase 206
SELECT 'ab';
.check --not 'a b'
.testcase 207
SELECT 'a b';
.check --not 'ab'
.testcase 208
SELECT 'a b';
.check --not 'a b c'
.testcase 209
SELECT 'a b c';
.check --not 'a b'

# The --glob option matches a pattern against any substring of the output.
# Leading and trailing whitespace on the pattern is ignored.
#
.testcase 220
SELECT 'abcdefghi';
.check --glob 'cde' --keep
.check --glob 'c?e' --keep
.check --glob 'a*i' --keep
.check --glob '[c]d' --keep
.check --glob '  cde  ' --keep
.check --not --glob 'x' --keep
.check --notglob 'ce'
.testcase 221
SELECT 'a   b'||char(10)||'c';
.check --glob 'a b' --keep
.check --glob 'b c' --keep
.check --not --glob 'a/b'
.testcase 222
SELECT 'x -12 y';
.check --glob 'x # y' --keep
.check --glob '# y' --keep
.check --not --glob 'x a y'

# The --allglob option matches a pattern against the entire output, except
# for leading and trailing whitespace.
#
.testcase 230
SELECT 'abcdefghi';
.check --allglob 'a*i' --keep
.check --allglob '  abc*  ' --keep
.check --not --allglob 'abcdefg?' --keep
.check --not --allglob '?bcdefgh' --keep
.check --not --allglob 'c*e'
.testcase 230a
SELECT 'abc';
.check --allglob 'abc' --keep
.check --not --allglob 'ab' --keep
.check --not --allglob 'bc'
.testcase 231
SELECT '  a   b'||char(10)||'c  '||char(10);
.check --allglob 'a b c' --keep
.check --allglob 'a b c*' --keep
.check --allglob 'a*c' --keep
.check --not --allglob 'a b' --keep
.check --not --allglob 'a b?' --keep
.check --not --allglob 'b c*'
.testcase 232
SELECT 'x +12 y';
.check --allglob 'x # y' --keep
.check --not --allglob 'x # z'

# A "*" in a GLOB pattern may be followed directly by whitespace or by "#".
#
.testcase 233
SELECT 'a'||char(9)||'b 12';
.check --allglob 'a* b #' --keep
.check --allglob 'a*#' --keep
.check --glob 'a*b' --keep
.check --not --allglob 'a*#x'

# The --nowhere option passes if the pattern is not a substring of the
# output.  It is the same as "--not --any".
#
.testcase 240
SELECT 'abcdefghi';
.check --nowhere 'x' --keep
.check --nowhere 'ce' --keep
.check --not --nowhere 'cde' --keep
.check --not --any 'x' --keep
.check --any 'cde'
.mode --reset quote
