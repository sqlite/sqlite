#!sqlite3
#
# 2026-11-13
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
# Bug 2026-10-26T12:58:59Z sqlite.org/bugs/info/0d4c9e9e9961f54c
# HAVING-to-WHERE and window push-down drop rows when a group holds both
# 1 and 1.0. By: Alpern Keles
#
# Bug 2026-10-27T07:19:03Z sqlite.org/bugs/info/913d06798baad5fb
# HAVING-to-WHERE and push-down past GROUP BY/PARTITION BY drop rows
# whose key is equal but distinguishable (2 vs 2.0) By: 0559jiangyc
#
#
.safe
.mode quote
.testcase 10
CREATE TABLE t(a, b);
INSERT INTO t VALUES (1, 10), (1.0, 20);
.check

.testcase 11
SELECT * FROM (SELECT a, sum(b) OVER (PARTITION BY a) AS s FROM t)
 WHERE typeof(a) = 'integer';
.check 1,30

.testcase 12
SELECT a, sum(b) FROM t GROUP BY a HAVING typeof(a) = 'integer';
.check 1,30

# Disable Having-to-Where and it works
.testctrl opt -q -HavingToWhere
.testcase 13
SELECT a, sum(b) FROM t GROUP BY a HAVING typeof(a) = 'integer';
.check 1,30
.testctrl opt -q +All

.testcase 20
CREATE TABLE q(k ANY, v INT) STRICT;
INSERT INTO q VALUES(2, 10), (2.0, 5);
.check

.testcase 21
SELECT k, sum(v) FROM q GROUP BY k;
.check 2,15

.testcase 22
SELECT k, sum(v) FROM q GROUP BY k HAVING typeof(k)='integer';
.check 2,15
.testcase 23
SELECT k, sum(v) FROM q GROUP BY k HAVING typeof(k)='real';
.check

# Disable Having-to-Where to get the correct answer
.testctrl opt -q -HavingToWhere
.testcase 24
SELECT k, sum(v) FROM q GROUP BY k HAVING typeof(k)='integer';
.check 2,15
.testcase 25
SELECT k, sum(v) FROM q GROUP BY k HAVING typeof(k)='real';
.check
.testctrl opt -q +All

# Coverage tests for exprGroupByOperandUnsafe() and its helpers in expr.c,
# which decide whether a term of a HAVING clause may be moved into the
# WHERE clause (and whether a term of a WHERE clause may be pushed down
# past a window function with PARTITION BY).
#
# Members of a group are equal but need not be identical: 1 and 1.0 are
# in the same group.  A term that can tell the two apart must not be moved.
#
# Part 1 (testcase 200-299) uses terms that must NOT be moved.  If such a
# term is moved anyway, the answer changes.  The expected answers are the
# ones obtained with the HAVING-to-WHERE and push-down optimizations
# disabled.
#
# Part 2 (testcase 300-399) uses terms that MUST be moved, since a wrongly
# conservative test would only cost performance and could not be found by
# looking at the answer.  Each query has a partial index whose WHERE clause
# is the HAVING term.  The planner can only use the partial index if the
# term was moved into the WHERE clause, so the query plan shows whether
# it was moved.
#
.mode --reset list

.testcase 100
CREATE TABLE t1(a, b TEXT, c, v INT);
INSERT INTO t1 VALUES(1,'x',1,10),(1.0,'x',2,20),(2,'y',3,30);
CREATE TABLE t2(n TEXT COLLATE NOCASE, v INT);
INSERT INTO t2 VALUES('x',1),('X',2),('y',4);
.check

# ---------------- Part 1: terms that must not be moved ---------------

# typeof() of a GROUP BY term that might be a number.  Rows 1 and 1.0
# are in one group.  Moving the term would drop the 1.0 row, giving 1|10.
.testcase 200
SELECT a, sum(v) FROM t1 GROUP BY a HAVING typeof(a)='integer';
.check <<END
1|30
2|30
END

# A function of a GROUP BY term that is not typeof().
.testcase 201
SELECT a, sum(v) FROM t1 GROUP BY a HAVING length(a)=1;
.check <<END
1|30
2|30
END

# An arithmetic operator applied to a GROUP BY term.  1/2 is 0 but
# 1.0/2 is 0.5.
.testcase 202
SELECT a, sum(v) FROM t1 GROUP BY a HAVING a/2=0;
.check '1|30'

# A comparison of a GROUP BY term that has no affinity (a+0) with an
# operand that has TEXT affinity.  The TEXT affinity converts 1 to '1'
# and 1.0 to '1.0'.
.testcase 203
SELECT a, sum(v) FROM t1 GROUP BY a+0 HAVING a+0=CAST('1' AS TEXT);
.check '1|30'

# A GROUP BY term that does not use the BINARY collating sequence.
.testcase 204
SELECT n, sum(v) FROM t2 GROUP BY n HAVING n='x' COLLATE binary;
.check 'x|3'

# A term that is not a GROUP BY term and is not constant.
.testcase 205
SELECT a, sum(v) FROM t1 GROUP BY a HAVING c=1;
.check '1|30'

# The same problem for the push-down of a WHERE term into a
# subquery with a window function that has PARTITION BY.  The window
# function sees all three rows.  The outer WHERE removes the 1.0 row.
.testcase 206
SELECT a, s FROM (SELECT a, sum(v) OVER (PARTITION BY a) AS s FROM t1)
 WHERE typeof(a)='integer';
.check <<END
1|30
2|30
END

# TK_NE comparing a GROUP BY term with no affinity against a TEXT operand.
.testcase 207
SELECT sum(v) FROM t1 GROUP BY a+0 HAVING a+0<>CAST('1' AS TEXT);
.check '30'

# TK_LT comparing a GROUP BY term with no affinity against a TEXT operand.
.testcase 208
SELECT sum(v) FROM t1 GROUP BY a+0 HAVING a+0<CAST('1.0' AS TEXT);
.check '30'

# TK_LE comparing a GROUP BY term with no affinity against a TEXT operand.
.testcase 209
SELECT sum(v) FROM t1 GROUP BY a+0 HAVING a+0<=CAST('1' AS TEXT);
.check '30'

# TK_GT comparing a GROUP BY term with no affinity against a TEXT operand.
.testcase 210
SELECT sum(v) FROM t1 GROUP BY a+0 HAVING a+0>CAST('1' AS TEXT);
.check '30'

# TK_GE comparing a GROUP BY term with no affinity against a TEXT operand.
.testcase 211
SELECT sum(v) FROM t1 GROUP BY a+0 HAVING a+0>=CAST('1.0' AS TEXT);
.check '30'

# TK_IS comparing a GROUP BY term with no affinity against a TEXT operand.
.testcase 212
SELECT sum(v) FROM t1 GROUP BY a+0 HAVING a+0 IS CAST('1' AS TEXT);
.check '30'

# TK_ISNOT comparing a GROUP BY term with no affinity against a TEXT operand.
.testcase 213
SELECT sum(v) FROM t1 GROUP BY a+0 HAVING a+0 IS NOT CAST('1' AS TEXT);
.check '30'

# TK_BETWEEN comparing a GROUP BY term with no affinity against TEXT
# operands.
.testcase 214
SELECT sum(v) FROM t1 GROUP BY a+0
 HAVING a+0 BETWEEN CAST('1' AS TEXT) AND CAST('1' AS TEXT);
.check '30'

# ---------------- Part 2: terms that must be moved ---------------

# Each test creates a partial index p1 whose WHERE clause is the HAVING
# term, runs EXPLAIN QUERY PLAN, and requires that p1 is used.

# A GROUP BY term as an operand of TK_EQ.
.testcase 300
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a=1;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a=1;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as an operand of TK_NE.
.testcase 301
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a<>1;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a<>1;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as an operand of TK_LT.
.testcase 302
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a<1;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a<1;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as an operand of TK_LE.
.testcase 303
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a<=1;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a<=1;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as an operand of TK_GT.
.testcase 304
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a>1;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a>1;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as an operand of TK_GE.
.testcase 305
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a>=1;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a>=1;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as an operand of TK_IS.
.testcase 306
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a IS 1;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a IS 1;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as an operand of TK_ISNOT.
.testcase 307
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a IS NOT 1;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a IS NOT 1;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as an operand of TK_IN.
.testcase 308
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a IN (1,2);
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a IN (1,2);
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as an operand of TK_BETWEEN.
.testcase 309
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a BETWEEN 1 AND 2;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a BETWEEN 1 AND 2;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as an operand of TK_ISNULL.
.testcase 310
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a ISNULL;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a ISNULL;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as an operand of TK_NOTNULL.
.testcase 311
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a NOTNULL;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a NOTNULL;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term as the operand of NOT.
.testcase 312
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE NOT a;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING NOT a;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# GROUP BY terms as the operands of OR.
.testcase 313
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a OR b;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a, b
 HAVING a OR b;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# GROUP BY terms as the operands of AND.
.testcase 314
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE NOT (a AND b);
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a, b
 HAVING NOT (a AND b);
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term with no affinity compared with something that has no
# TEXT affinity.
.testcase 315
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a+0=1;
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a+0
 HAVING a+0=1;
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term that is a column with no affinity compared with
# a TEXT operand.  Two operands that look like columns are compared
# without conversion.
.testcase 316
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a=CAST('1' AS TEXT);
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a=CAST('1' AS TEXT);
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A logical operator with operands that are a GROUP BY term with no
# affinity and a TEXT operand.  This is not a comparison.
.testcase 317
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE NOT ((a+0) AND CAST('1' AS TEXT));
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a+0
 HAVING NOT ((a+0) AND CAST('1' AS TEXT));
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# TK_OR with a GROUP BY term with no affinity and a TEXT operand.  This
# is not a comparison.
.testcase 318
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE (a+0) OR CAST('1' AS TEXT);
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a+0
 HAVING (a+0) OR CAST('1' AS TEXT);
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A function of a GROUP BY column with TEXT affinity.
.testcase 319
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE lower(b)='x';
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY b
 HAVING lower(b)='x';
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A function of the || operator, which always yields TEXT.
.testcase 320
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE upper(b||'!')='X!';
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY b||'!'
 HAVING upper(b||'!')='X!';
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A function of constants compared with a GROUP BY term.
.testcase 321
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a=length('abc');
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a
 HAVING a=length('abc');
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# A GROUP BY term that is a column with TEXT affinity.
.testcase 322
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE b='x';
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY b
 HAVING b='x';
DROP INDEX p1;
.check --glob '*COVERING INDEX p1*'

# ---------------- Part 3: terms that are not moved ---------------

# The test for TK_IN is conservative: x IN (list) applies no affinity to
# a TEXT operand of the list, so moving this term would be safe.  (A list
# with a single term is converted to x=+y, which hides the TEXT affinity.)
# It is not moved, and the test makes sure that TK_IN is treated as a
# comparison.

.testcase 400
CREATE INDEX p1 ON t1(a,b,c,v)
 WHERE a+0 IN (CAST('1' AS TEXT), CAST('7' AS TEXT));
EXPLAIN QUERY PLAN SELECT sum(v) FROM t1 GROUP BY a+0
 HAVING a+0 IN (CAST('1' AS TEXT), CAST('7' AS TEXT));
DROP INDEX p1;
.check --notglob '*COVERING INDEX p1*'
