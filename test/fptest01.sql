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
# Floating-point to text conversions
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
