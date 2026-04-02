#!sqlite3
#
# 2026-03-01
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
# Floating-point <-> text conversions
#
# FAILURES IN THIS SCRIPT ARE NOT NECESSARILY THE FAULT OF SQLITE.
#
# Some of the tests below use the system strtod() function as
# an oracle of truth.  These tests assume that the system strtod()
# is always correct.  That is the case for Win11, Macs, most Linux
# boxes and so forth.  But it possible to find a machine for which
# is not true.  (One example, is Macs from around 2005.)  On such
# machines, some of these tests might fail.
#
# So, in other words, a failure in any of the tests below does not
# necessarily mean that SQLite is wrong.  It might mean that the
# strtod() function in the standard library of the machine on which
# the test is running is wrong.
#

# Verify that binary64 -> text -> binary64 conversions round-trip
# successfully for 98,256 different edge-case binary64 values.  The
# query result is all cases that do not round-trip without change,
# and so the query result should be an empty set.
#
.testcase 100
.mode list
WITH
  i1(i) AS (VALUES(0) UNION ALL SELECT i+1 FROM i1 WHERE i<15),
  i2(j) AS (VALUES(0) UNION ALL SELECT j+1 FROM i2 WHERE j<0x7fe),
  i3(k) AS (VALUES(0x0000000000000000),
                  (0x000ffffffffffff0),
                  (0x0008080808080800)),
  fpint(n) AS (SELECT (j<<52)+i+k FROM i2, i1, i3),
  fp(n,r) AS (SELECT n, ieee754_from_int(n) FROM fpint)
SELECT n, r FROM fp WHERE r<>(0.0 + (r||''));
.check ''

# Another batch of 106,444 edge cases:  All postiive floating point
# values that have only a single bit set in the mantissa part of the
# number.
#
.testcase 110
WITH
  i1(i) AS (VALUES(0) UNION ALL SELECT i+1 FROM i1 WHERE i<51),
  i2(j) AS (VALUES(0) UNION ALL SELECT j+1 FROM i2 WHERE j<0x7fe),
  fpint(n) AS (SELECT (j<<52)+(1<<i) FROM i2, i1),
  fp(n,r) AS (SELECT n, ieee754_from_int(n) FROM fpint)
SELECT n, r FROM fp WHERE r<>(0.0 + (r||''));
.check ''

# Verify that text -> binary64 conversions agree with system strtod().
# for 98,256 different edge-cases.
#
.testcase 200
.mode list
WITH
  i1(i) AS (VALUES(0) UNION ALL SELECT i+1 FROM i1 WHERE i<15),
  i2(j) AS (VALUES(0) UNION ALL SELECT j+1 FROM i2 WHERE j<0x7fe),
  i3(k) AS (VALUES(0x0000000000000000),
                  (0x000ffffffffffff0),
                  (0x0008080808080800)),
  fpint(n) AS (SELECT (j<<52)+i+k FROM i2, i1, i3),
  fp(r) AS (SELECT ieee754_from_int(n) FROM fpint)
SELECT r FROM fp WHERE r<>strtod(r||'');
.check ''


# Another batch of 106,444 edge cases:  All postiive floating point
# values that have only a single bit set in the mantissa part of the
# number.
#
.testcase 210
WITH
  i1(i) AS (VALUES(0) UNION ALL SELECT i+1 FROM i1 WHERE i<51),
  i2(j) AS (VALUES(0) UNION ALL SELECT j+1 FROM i2 WHERE j<0x7fe),
  fpint(n) AS (SELECT (j<<52)+(1<<i) FROM i2, i1),
  fp(r) AS (SELECT ieee754_from_int(n) FROM fpint)
SELECT r FROM fp WHERE r<>strtod(r||'');
.check ''

# Comparing SQLite's text-to-double conversion against strtod()
# for 200,000 random floating-point literals.
#
.param set $N 50_000
.testcase 300
WITH RECURSIVE fp(n,x) AS MATERIALIZED (
  VALUES(1,'1234.5789')
  UNION ALL
  SELECT n+1, format('%.*s.%.*se%+d',
                     (n%3)+1,
                     random()%1000,
                     abs(random()%16)+1,
                     abs(random()),
                     random()%308)
    FROM fp WHERE n<$N
) SELECT x FROM fp WHERE (x+0.0)<>strtod(x);
.check ''
.testcase 301
WITH RECURSIVE fp(n,x) AS MATERIALIZED (
  VALUES(1,'1234.5789')
  UNION ALL
  SELECT n+1, format('%.*s.%.*s',
                     (n%3)+1,
                     random()%1000,
                     abs(random()%16)+1,
                     abs(random()))
    FROM fp WHERE n<$N
) SELECT x FROM fp WHERE (x+0.0)<>strtod(x);
.check ''
.testcase 302
WITH RECURSIVE fp(n,x) AS MATERIALIZED (
  VALUES(1,'1234.5789')
  UNION ALL
  SELECT n+1, format('%.*s.%.*s',
                     abs(random()%7)+1,
                     random(),
                     abs(random()%10)+1,
                     abs(random()))
    FROM fp WHERE n<$N
) SELECT x FROM fp WHERE (x+0.0)<>strtod(x);
.check ''
.testcase 303
WITH RECURSIVE fp(n,x) AS MATERIALIZED (
  VALUES(1,'1234.5789')
  UNION ALL
  SELECT n+1, format('%de%+03d',
                     random(),
                     random()%308)
    FROM fp WHERE n<$N
) SELECT x FROM fp WHERE (x+0.0)<>strtod(x);
.check ''

# Another 500,000 comparisions between SQLite and strtod() from completely
# random floating point literals.
#
.testcase 310
WITH RECURSIVE fp(n,x) AS MATERIALIZED (
  SELECT 1,   format('%+.*g',abs(random()%18), ieee754_from_int(random()))
  UNION ALL
  SELECT n+1, format('%+.*g',abs(random()%18), ieee754_from_int(random()))
    FROM fp WHERE n<$N*2
) SELECT x FROM fp WHERE (x+0.0)<>strtod(x);
.check ''
.testcase 311
WITH RECURSIVE fp(n,x) AS MATERIALIZED (
  SELECT 1,   format('%+.*g',abs(random()%18), ieee754_from_int(random()))
  UNION ALL
  SELECT n+1, format('%+.*g',abs(random()%18), ieee754_from_int(random()))
    FROM fp WHERE n<$N*2
) SELECT x FROM fp WHERE (x+0.0)<>strtod(x);
.check ''
.testcase 312
WITH RECURSIVE fp(n,x) AS MATERIALIZED (
  SELECT 1,   format('%+.*g',abs(random()%18), ieee754_from_int(random()))
  UNION ALL
  SELECT n+1, format('%+.*g',abs(random()%18), ieee754_from_int(random()))
    FROM fp WHERE n<$N*2
) SELECT x FROM fp WHERE (x+0.0)<>strtod(x);
.check ''
.testcase 313
WITH RECURSIVE fp(n,x) AS MATERIALIZED (
  SELECT 1,   format('%+.*g',abs(random()%18), ieee754_from_int(random()))
  UNION ALL
  SELECT n+1, format('%+.*g',abs(random()%18), ieee754_from_int(random()))
    FROM fp WHERE n<$N*2
) SELECT x FROM fp WHERE (x+0.0)<>strtod(x);
.check ''
.testcase 314
WITH RECURSIVE fp(n,x) AS MATERIALIZED (
  SELECT 1,   format('%+.*g',abs(random()%18), ieee754_from_int(random()))
  UNION ALL
  SELECT n+1, format('%+.*g',abs(random()%18), ieee754_from_int(random()))
    FROM fp WHERE n<$N*2
) SELECT x FROM fp WHERE (x+0.0)<>strtod(x);
.check ''
