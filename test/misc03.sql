#!sqlite3
#
# 2026-11-18
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
# A LIKE or GLOB prefix ending in U+D7FF must not use the index range
# optimization, because the upper bound would be the surrogate U+D800.
# That is not valid in UTF16BE and became U+FFFD, which admitted
# non-matching rows.
#
#   https://sqlite.org/bugs/info/3947fa76ab
#
# The patterns below contain a literal U+D7FF.  The optimization does not
# apply to patterns built using char().
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '

.testcase 100
PRAGMA encoding='UTF-16be';
PRAGMA case_sensitive_like=1;
CREATE TABLE t1(x TEXT);
INSERT INTO t1 VALUES
  (char(0xd7ff)),
  (char(0xd7ff,0x61)),
  (char(0xd7fe)),
  (char(0xe000)),
  (char(0xffff)),
  (char(0x10000));
CREATE INDEX t1x ON t1(x);
.check ''

.testcase 110
SELECT printf('U+%04X', unicode(x)) FROM t1
 WHERE x LIKE '퟿%';
.check 'U+D7FF U+D7FF'

.testcase 120
SELECT printf('U+%04X', unicode(x)) FROM t1 NOT INDEXED
 WHERE x LIKE '퟿%';
.check 'U+D7FF U+D7FF'

.testcase 130
SELECT printf('U+%04X', unicode(x)) FROM t1
 WHERE x GLOB '퟿*';
.check 'U+D7FF U+D7FF'

.testcase 140
SELECT printf('U+%04X', unicode(x)) FROM t1 NOT INDEXED
 WHERE x GLOB '퟿*';
.check 'U+D7FF U+D7FF'

.testcase 150
SELECT printf('U+%04X', unicode(x)) FROM t1
 WHERE x LIKE '퟿_';
.check 'U+D7FF'

.testcase 160
SELECT printf('U+%04X', unicode(x)) FROM t1
 WHERE x LIKE '퟿%' ESCAPE '^';
.check 'U+D7FF U+D7FF'


#***********************************************************************
#
# An index on a generated column must not change the type of a matching
# expression elsewhere in the query.  The index affinity string lumps all
# numeric affinities together, so a REAL column could replace an integer
# expression, or the reverse.
#
#   https://sqlite.org/bugs/info/c667229a15161954
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '

.testcase 200
CREATE TABLE t2(
  v INT,
  g REAL AS (CAST(v AS INT)) VIRTUAL,
  h INT AS (CAST(v AS REAL)) VIRTUAL,
  r REAL AS (v*1.0) VIRTUAL
);
INSERT INTO t2(v) VALUES(9);
CREATE INDEX t2g ON t2(g);
CREATE INDEX t2h ON t2(h);
CREATE INDEX t2r ON t2(r);
.check ''

.testcase 210
SELECT CAST(v AS INT), typeof(CAST(v AS INT)) FROM t2 NOT INDEXED WHERE g>0;
.check '9 integer'

.testcase 220
SELECT CAST(v AS INT), typeof(CAST(v AS INT)) FROM t2 WHERE g>0;
.check '9 integer'

.testcase 230
SELECT CAST(v AS REAL), typeof(CAST(v AS REAL)) FROM t2 NOT INDEXED WHERE h>0;
.check '9.0 real'

.testcase 240
SELECT CAST(v AS REAL), typeof(CAST(v AS REAL)) FROM t2 WHERE h>0;
.check '9.0 real'

.testcase 250
SELECT v*1.0, typeof(v*1.0) FROM t2 WHERE r>0;
.check '9.0 real'

#***********************************************************************
# Bug 2026-11-06T00:28:19Z  sqlite.org/bugs/info/53eb99b97a309be9
#
# The fullkey and path columns of json_each() and json_tree() must escape
# a double-quote character in an object label.  A label written in JSON5
# using single quotes can contain a raw double-quote, which must not be
# copied into the path unchanged.
#
.if capable('vtab')
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '
.testcase 500
SELECT fullkey FROM json_each('{''a"b'':1}');
.check '$."a\"b"'
.testcase 505
SELECT json_extract('{''a"b'':1}', fullkey)
  FROM json_each('{''a"b'':1}');
.check '1'
.testcase 510
SELECT fullkey FROM json_each('{"a\"b":1}');
.check '$."a\"b"'
.testcase 515
SELECT fullkey, path FROM json_tree('{''a"b'':[{''c"d'':2}]}')
 WHERE atom IS 2 OR key='a"b';
.check '$."a\"b" $ $."a\"b"[0]."c\"d" $."a\"b"[0]'
.testcase 520
SELECT json_extract('{''a"b'':[{''c"d'':2}]}', fullkey)
  FROM json_tree('{''a"b'':[{''c"d'':2}]}')
 WHERE atom=2;
.check '2'
.testcase 525
SELECT fullkey FROM json_each('{''a\"b'':1}');
.check '$."a\"b"'
.testcase 530
SELECT fullkey FROM json_each('{''a\\b"c'':1}');
.check '$."a\\b\"c"'
.testcase 535
SELECT json_extract('{''a\\b"c'':1}', fullkey)
  FROM json_each('{''a\\b"c'':1}');
.check '1'
.testcase 540
SELECT fullkey FROM json_each('{''"'':1}');
.check '$."\""'
.testcase 545
SELECT fullkey FROM json_each(jsonb('{''a"b'':1}'));
.check '$."a\"b"'
.testcase 550
SELECT fullkey FROM json_each('{''ab'':1,''a b'':2}');
.check '$.ab $."a b"'
.testcase 555
SELECT fullkey FROM json_each(x'6C3A615C621331');
.check '$."a\\b"'
.testcase 560
SELECT fullkey FROM json_each(x'5C29615C1331');
.check '$."a\\"'
.testcase 565
SELECT fullkey FROM json_each(x'5C28615C1331');
.check '$."a\\"'
.endif /* capable('vtab') */

#***********************************************************************
# Bug 2026-11-18T08:08:53Z  sqlite.org/bugs/info/dcf6e09adc1bfd53
#
# The excluded.* values of an UPSERT must have the affinity and collating
# sequence of the column they came from, just like the same values read
# from the table.
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '
.testcase 600
CREATE TABLE t7(a TEXT, n TEXT COLLATE NOCASE, u INT UNIQUE);
INSERT INTO t7 VALUES('2','ABC',1);
.check
.testcase 610
SELECT a=2, n='abc', n IN ('abc'), a BETWEEN 2 AND 2 FROM t7;
.check 1 1 1 1
.testcase 620
INSERT INTO t7 VALUES(2,'ABC',1) ON CONFLICT(u) DO UPDATE
  SET a = (excluded.a=2) || (excluded.n='abc') || (excluded.n IN ('abc'))
          || (excluded.a BETWEEN 2 AND 2);
SELECT a FROM t7;
.check 1111
.testcase 630
CREATE TABLE t8(n TEXT COLLATE NOCASE, u INT UNIQUE);
INSERT INTO t8 VALUES('ABC',1);
INSERT INTO t8 VALUES('ABC',1) ON CONFLICT(u) DO UPDATE
  SET n='updated' WHERE excluded.n='abc';
SELECT n FROM t8;
.check updated
.testcase 640
CREATE TABLE t9(x INT, u INT UNIQUE);
INSERT INTO t9 VALUES(5,1);
INSERT INTO t9 VALUES('5',1) ON CONFLICT(u) DO UPDATE
  SET x = typeof(excluded.x) || (excluded.x=5) || (excluded.x='5');
SELECT x FROM t9;
.check integer11
.testcase 650
CREATE TABLE t10(id INTEGER PRIMARY KEY, b, c TEXT);
INSERT INTO t10 VALUES(1,'x','y');
INSERT INTO t10 VALUES(1,'p','q') ON CONFLICT(id) DO UPDATE
  SET b = excluded.id || excluded.b, c = excluded.c;
SELECT id, b, c FROM t10;
.check 1 1p q
.testcase 660
CREATE TABLE t11(s TEXT, i INT, u INT UNIQUE);
INSERT INTO t11 VALUES('10',10,1);
INSERT INTO t11 VALUES(10,10,1) ON CONFLICT(u) DO UPDATE
  SET s = 'seen' WHERE t11.s=excluded.s AND t11.i=excluded.i;
SELECT s FROM t11;
.check seen

#***********************************************************************
# Bug 2026-11-06T00:28:57Z  sqlite.org/bugs/info/08c13eef95ba8fe2
#
# A BEFORE UPDATE trigger that reads a generated column via new.* must see
# the value computed from all of the columns, including those that the
# UPDATE does not assign.
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '
.testcase 700
CREATE TABLE t20(a INT, b INT, c INT AS (a+b));
INSERT INTO t20 VALUES(1,2);
CREATE TABLE log20(x);
CREATE TRIGGER tr20 BEFORE UPDATE ON t20 BEGIN
  INSERT INTO log20 VALUES(coalesce(new.c,'NULL'));
END;
UPDATE t20 SET a=5;
SELECT x FROM log20;
.check 7
.testcase 710
DELETE FROM log20;
UPDATE t20 SET b=10;
SELECT x FROM log20;
SELECT a, b, c FROM t20;
.check 15 5 10 15
.testcase 720
CREATE TABLE t21(a INT, b INT, c INT AS (a+b) STORED, d INT AS (c*2));
INSERT INTO t21 VALUES(1,2);
CREATE TABLE log21(x);
CREATE TRIGGER tr21 BEFORE UPDATE ON t21
  WHEN new.c>5 OR new.d>100
BEGIN
  INSERT INTO log21 VALUES(coalesce(new.c,'NULL') || '/' || new.d);
END;
UPDATE t21 SET a=5;
SELECT x FROM log21;
.check 7/14
.testcase 730
-- The generated column is beyond the 32nd column, and the column it
-- depends upon is not.
CREATE TABLE t22(
  c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14, c15,
  c16, c17, c18, c19, c20, c21, c22, c23, c24, c25, c26, c27, c28, c29,
  c30, c31, c32, c33 INT AS (c0+c1)
);
INSERT INTO t22(c0,c1,c2) VALUES(1,2,3);
CREATE TABLE log22(x);
CREATE TRIGGER tr22 BEFORE UPDATE ON t22 BEGIN
  INSERT INTO log22 VALUES(coalesce(new.c33,'NULL'));
END;
UPDATE t22 SET c0=5;
SELECT x FROM log22;
.check 7
.testcase 740
-- A BEFORE trigger that does not read generated columns still works, and
-- the row ends up with the right values.
CREATE TABLE t23(a INT, b INT, c INT AS (a+b) STORED);
INSERT INTO t23 VALUES(1,2);
CREATE TRIGGER tr23 BEFORE UPDATE ON t23 BEGIN SELECT new.a; END;
UPDATE t23 SET a=5;
SELECT a, b, c FROM t23;
.check 5 2 7

#***********************************************************************
# Bug 2026-11-04T20:22:42Z  sqlite.org/bugs/info/44ffe36c008ceff4
#
# OP_NewRowid moves the table cursor, so it must invalidate the cached
# result of any earlier seek.  Otherwise, in a multi-row UPSERT where a
# BEFORE UPDATE trigger deletes the conflicting row, the following row is
# inserted using a stale seek result and ends up out of rowid order.
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '
.testcase 800
CREATE TABLE t30(a, e UNIQUE);
CREATE TRIGGER t30r BEFORE UPDATE ON t30 BEGIN
  DELETE FROM t30 WHERE rowid=old.rowid;
END;
INSERT INTO t30 VALUES(1,1),(2,2),(3,3);
INSERT INTO t30 VALUES(10,2),(11,11) ON CONFLICT(e) DO UPDATE SET a=99;
PRAGMA integrity_check(t30);
.check ok
.testcase 810
SELECT rowid, a, e FROM t30 ORDER BY rowid;
.check 1 1 1 3 3 3 4 11 11
.testcase 820
-- The same with three new rows following the conflicting one.
CREATE TABLE t31(a, e UNIQUE);
CREATE TRIGGER t31r BEFORE UPDATE ON t31 BEGIN
  DELETE FROM t31 WHERE rowid=old.rowid;
END;
INSERT INTO t31 VALUES(1,1),(2,2),(3,3);
INSERT INTO t31 VALUES(10,2),(11,11),(12,12),(13,13)
  ON CONFLICT(e) DO UPDATE SET a=99;
PRAGMA integrity_check(t31);
SELECT group_concat(rowid) FROM (SELECT rowid FROM t31);
.check ok 1,3,4,5,6
.testcase 830
-- An AFTER UPDATE trigger, and no trigger at all, must continue to work.
CREATE TABLE t32(a, e UNIQUE);
CREATE TRIGGER t32r AFTER UPDATE ON t32 BEGIN
  DELETE FROM t32 WHERE rowid=old.rowid;
END;
INSERT INTO t32 VALUES(1,1),(2,2),(3,3);
INSERT INTO t32 VALUES(10,2),(11,11) ON CONFLICT(e) DO UPDATE SET a=99;
CREATE TABLE t33(a, e UNIQUE);
INSERT INTO t33 VALUES(1,1),(2,2),(3,3);
INSERT INTO t33 VALUES(10,2),(11,11) ON CONFLICT(e) DO UPDATE SET a=99;
PRAGMA integrity_check;
SELECT group_concat(rowid) FROM t32;
SELECT group_concat(rowid||':'||a) FROM t33;
.check ok 1,3,4 1:1,2:99,3:3,4:11

#***********************************************************************
# Bug 2026-11-18T19:07:34Z  sqlite.org/bugs/info/706963ebcdd364c9
#
# Assertion fault following misuse of the modeof= query parameter.
#
.open :memory:
.if capable('unix')
.testcase 900
.shell rm -f no-such-file-misc03-900.db
VACUUM INTO 'file:no-such-file-misc03-900.db?modeof';
.check
.testcase 901
.shell rm -f no-such-file-misc03-900.db
VACUUM INTO 'file:no-such-file-misc03-900.db?modeof=nonsense';
.check
.endif /* capable('unix') */

#***********************************************************************
# Bug 2026-11-16T13:32:44Z  sqlite.org/bugs/info/b17e957ed57698be
#
# An UPSERT conflict target must match every column of the unique index
# that it selects, in both directions.  A unique index that repeats a
# column, such as UNIQUE(b,b), is not matched by a conflict target of (b,c).
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '
.testcase 900
CREATE TABLE t40(b, c, v, UNIQUE(b,c));
CREATE UNIQUE INDEX i40 ON t40(b,b);
INSERT INTO t40 VALUES(1,1,0);
INSERT INTO t40 VALUES(1,2,5) ON CONFLICT(b,c) DO UPDATE SET v=excluded.v;
SELECT b, c, v FROM t40;
.check --glob *UNIQUE?constraint?failed*1?1?0*
.testcase 910
-- No (b,c) constraint exists, so the conflict target is rejected.
CREATE TABLE t41(b, c, v);
CREATE UNIQUE INDEX i41 ON t41(b,b);
INSERT INTO t41 VALUES(1,1,0);
INSERT INTO t41 VALUES(1,2,5) ON CONFLICT(b,c) DO UPDATE SET v=excluded.v;
SELECT b, c, v FROM t41;
.check --glob *does?not?match?any*1?1?0*
.testcase 920
-- Targets that still match: the repeated column in the target as well,
-- reordered columns, and a conflict on the matching constraint.
CREATE TABLE t42(b, c, v, UNIQUE(b,c));
CREATE UNIQUE INDEX i42 ON t42(b,b);
INSERT INTO t42 VALUES(1,1,0);
INSERT INTO t42 VALUES(1,2,5) ON CONFLICT(b,b) DO UPDATE SET v=excluded.v;
CREATE TABLE t43(a, b, v, UNIQUE(b,a));
INSERT INTO t43 VALUES(1,1,0);
INSERT INTO t43 VALUES(1,1,7) ON CONFLICT(a,b) DO UPDATE SET v=excluded.v;
SELECT b, c, v FROM t42;
SELECT a, b, v FROM t43;
.check 1 1 5 1 1 7
.testcase 930
CREATE TABLE t50(c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,c14,c15,c16,
   c17,c18,c19,c20,c21,c22,c23,c24,c25,c26,c27,c28,c29,c30,c31,c32,c33,c34,
   c35,c36,c37,c38,c39,c40,c41,c42,c43,c44,c45,c46,c47,c48,c49,c50,c51,c52,
   c53,c54,c55,c56,c57,c58,c59,c60,c61,c62,c63,v,
   UNIQUE(c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,c14,c15,c16,
   c17,c18,c19,c20,c21,c22,c23,c24,c25,c26,c27,c28,c29,c30,c31,c32,c33,c34,
   c35,c36,c37,c38,c39,c40,c41,c42,c43,c44,c45,c46,c47,c48,c49,c50,c51,c52,
   c53,c54,c55,c56,c57,c58,c59,c60,c61,c62,c63));
INSERT INTO t50 VALUES(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,0);
INSERT INTO t50 VALUES(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,5)
     ON CONFLICT(c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,c14,c15,c16,
   c17,c18,c19,c20,c21,c22,c23,c24,c25,c26,c27,c28,c29,c30,c31,c32,c33,c34,
   c35,c36,c37,c38,c39,c40,c41,c42,c43,c44,c45,c46,c47,c48,c49,c50,c51,c52,
   c53,c54,c55,c56,c57,c58,c59,c60,c61,c62,c63) DO UPDATE SET v=excluded.v;
SELECT v FROM t50;
.check 5
.testcase 940
CREATE TABLE t51(c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,c14,c15,c16,
   c17,c18,c19,c20,c21,c22,c23,c24,c25,c26,c27,c28,c29,c30,c31,c32,c33,c34,
   c35,c36,c37,c38,c39,c40,c41,c42,c43,c44,c45,c46,c47,c48,c49,c50,c51,c52,
   c53,c54,c55,c56,c57,c58,c59,c60,c61,c62,c63,c64,v,
   UNIQUE(c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,c14,c15,c16,
   c17,c18,c19,c20,c21,c22,c23,c24,c25,c26,c27,c28,c29,c30,c31,c32,c33,c34,
   c35,c36,c37,c38,c39,c40,c41,c42,c43,c44,c45,c46,c47,c48,c49,c50,c51,c52,
   c53,c54,c55,c56,c57,c58,c59,c60,c61,c62,c63,c64));
INSERT INTO t51 VALUES(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,0);
INSERT INTO t51 VALUES(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,5)
     ON CONFLICT(c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,c14,c15,c16,
   c17,c18,c19,c20,c21,c22,c23,c24,c25,c26,c27,c28,c29,c30,c31,c32,c33,c34,
   c35,c36,c37,c38,c39,c40,c41,c42,c43,c44,c45,c46,c47,c48,c49,c50,c51,c52,
   c53,c54,c55,c56,c57,c58,c59,c60,c61,c62,c63,c64) DO UPDATE SET v=excluded.v;
SELECT v FROM t51;
.check 5
-- 70 terms with the target given in reverse order.
.testcase 950
CREATE TABLE t52(c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,c14,c15,c16,
   c17,c18,c19,c20,c21,c22,c23,c24,c25,c26,c27,c28,c29,c30,c31,c32,c33,c34,
   c35,c36,c37,c38,c39,c40,c41,c42,c43,c44,c45,c46,c47,c48,c49,c50,c51,c52,
   c53,c54,c55,c56,c57,c58,c59,c60,c61,c62,c63,c64,c65,c66,c67,c68,c69,v,
          UNIQUE(c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,c14,c15,c16,
   c17,c18,c19,c20,c21,c22,c23,c24,c25,c26,c27,c28,c29,c30,c31,c32,c33,c34,
   c35,c36,c37,c38,c39,c40,c41,c42,c43,c44,c45,c46,c47,c48,c49,c50,c51,c52,
   c53,c54,c55,c56,c57,c58,c59,c60,c61,c62,c63,c64,c65,c66,c67,c68,c69));
INSERT INTO t52 VALUES(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,0);
INSERT INTO t52 VALUES(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,5)
       ON CONFLICT(c69,c68,c67,c66,c65,c64,c63,c62,c61,c60,c59,c58,c57,c56,
   c55,c54,c53,c52,c51,c50,c49,c48,c47,c46,c45,c44,c43,c42,c41,c40,c39,c38,
   c37,c36,c35,c34,c33,c32,c31,c30,c29,c28,c27,c26,c25,c24,c23,c22,c21,c20,
   c19,c18,c17,c16,c15,c14,c13,c12,c11,c10,c9,c8,c7,c6,c5,c4,c3,c2,c1,c0)
       DO UPDATE SET v=excluded.v;
SELECT v FROM t52;
.check 5

#***********************************************************************
# Bug 2026-11-19 (https://sqlite.org/bugs/info/d34952d48e3e3570)
#
# A WHERE clause term of the outer query must not be pushed down into a
# subquery that uses DISTINCT, UNION, INTERSECT, or EXCEPT unless the term
# gives the same answer for every member of a set of equal rows.  The
# values 1 and 1.0 are equal but typeof() tells them apart.
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '
.testcase 1000
CREATE TABLE a(x COLLATE BINARY);
CREATE TABLE b(x COLLATE BINARY);
INSERT INTO a VALUES(1);
INSERT INTO b VALUES(1.0);
SELECT x, typeof(x) FROM (SELECT x FROM a EXCEPT SELECT x FROM b)
 WHERE typeof(x)='integer';
.check
.testcase 1010
-- INTERSECT returns the left value, which satisfies the term.
SELECT x, typeof(x) FROM (SELECT x FROM a INTERSECT SELECT x FROM b)
 WHERE typeof(x)='integer';
.check 1 integer
.testcase 1020
-- The same with the arms swapped.  The row is 1.0, which does not.
SELECT x, typeof(x) FROM (SELECT x FROM b INTERSECT SELECT x FROM a)
 WHERE typeof(x)='integer';
.check
.testcase 1030
-- Pushing the term into the right-hand arm of EXCEPT is wrong for every
-- function or operator, not just typeof().
SELECT x FROM (SELECT x FROM a EXCEPT SELECT x FROM b) WHERE x+0=1;
SELECT x FROM (SELECT x FROM a EXCEPT SELECT x FROM b) WHERE length(x)=1;
SELECT x FROM (SELECT x FROM a EXCEPT SELECT x FROM b) WHERE x||''='1';
.check
.testcase 1040
-- For UNION and DISTINCT it is not specified which of two equal rows is
-- kept.  Check that the answer is the same as when no push-down is possible.
CREATE TABLE c(x COLLATE BINARY);
INSERT INTO c VALUES(1.0),(1);
WITH q AS MATERIALIZED (SELECT DISTINCT x FROM c)
SELECT (SELECT count(*) FROM (SELECT DISTINCT x FROM c)
         WHERE typeof(x)='integer')
     = (SELECT count(*) FROM q WHERE typeof(x)='integer'),
       (SELECT count(*) FROM (SELECT DISTINCT x FROM c)
         WHERE typeof(x)='real')
     = (SELECT count(*) FROM q WHERE typeof(x)='real');
.check 1 1
.testcase 1050
WITH q AS MATERIALIZED (SELECT x FROM a UNION SELECT x FROM b)
SELECT (SELECT count(*) FROM (SELECT x FROM a UNION SELECT x FROM b)
         WHERE typeof(x)='integer')
     = (SELECT count(*) FROM q WHERE typeof(x)='integer'),
       (SELECT count(*) FROM (SELECT x FROM a UNION SELECT x FROM b)
         WHERE typeof(x)='real')
     = (SELECT count(*) FROM q WHERE typeof(x)='real');
.check 1 1
.testcase 1060
-- Comparison and logical operators are safe, and the term is still
-- pushed down.  Results must be correct either way.
CREATE TABLE d(x COLLATE BINARY);
CREATE TABLE e(x COLLATE BINARY);
INSERT INTO d VALUES(1),(2),(3);
INSERT INTO e VALUES(2.0);
SELECT x FROM (SELECT x FROM d EXCEPT SELECT x FROM e) WHERE x>1 AND x<>3;
SELECT x FROM (SELECT x FROM d EXCEPT SELECT x FROM e) WHERE x=2;
SELECT x FROM (SELECT x FROM d INTERSECT SELECT x FROM e) WHERE x IN (2,5);
.check 2
.testcase 1070
-- Equal under a non-BINARY collating sequence but distinguishable under BINARY.
CREATE TABLE n(x TEXT COLLATE NOCASE);
INSERT INTO n VALUES('a'),('A');
WITH q AS MATERIALIZED (SELECT DISTINCT x FROM n)
SELECT (SELECT count(*) FROM (SELECT DISTINCT x FROM n)
         WHERE x='A' COLLATE BINARY)
     = (SELECT count(*) FROM q WHERE x='A' COLLATE BINARY);
.check 1
.testcase 1080
-- A DISTINCT arm of a UNION ALL compound.  The rows of the DISTINCT arm
-- are collapsed even though the compound operator is UNION ALL.
WITH q AS MATERIALIZED (SELECT DISTINCT x FROM c UNION ALL SELECT x FROM a)
SELECT (SELECT count(*) FROM (SELECT DISTINCT x FROM c UNION ALL SELECT x FROM a)
         WHERE typeof(x)='integer')
     = (SELECT count(*) FROM q WHERE typeof(x)='integer'),
       (SELECT count(*) FROM (SELECT DISTINCT x FROM c UNION ALL SELECT x FROM a)
         WHERE typeof(x)='real')
     = (SELECT count(*) FROM q WHERE typeof(x)='real');
.check 1 1
.testcase 1090
-- The DISTINCT arm is not the last arm.
WITH q AS MATERIALIZED (SELECT x FROM a UNION ALL SELECT DISTINCT x FROM c)
SELECT (SELECT count(*) FROM (SELECT x FROM a UNION ALL SELECT DISTINCT x FROM c)
         WHERE typeof(x)='integer')
     = (SELECT count(*) FROM q WHERE typeof(x)='integer'),
       (SELECT count(*) FROM (SELECT x FROM a UNION ALL SELECT DISTINCT x FROM c)
         WHERE typeof(x)='real')
     = (SELECT count(*) FROM q WHERE typeof(x)='real');
.check 1 1

# Bug 2026-11-19T21:24:04Z
# https://sqlite.org/bugs/info/9b101b2d7f859b65
#
# The "no match" optimization can make the inner-most outer loop that
# influences a key delete its auto-index entry when the key matches nothing.
# If that outer loop was an EXISTS-to-JOIN loop, the label for the
# deletion was never resolved, giving an infinite loop (and an assertion
# fault in debug builds).
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '
.testcase 1100
CREATE TABLE t0(a);
CREATE INDEX i0 ON t0(a);
CREATE TABLE t1(b TEXT);
INSERT INTO t1 VALUES('x');
CREATE TABLE t2(c, d TEXT);
INSERT INTO t2 VALUES('x','x');
SELECT t0.rowid, t1.rowid, t2.rowid
  FROM t2 CROSS JOIN t0 JOIN t1
 WHERE t0.a=t2.c AND t2.d=t1.b
   AND EXISTS(SELECT 1 FROM t1 AS z WHERE z.b=t2.c)
 ORDER BY 1;
.check
.testcase 1110
-- The same query with rows in t0, some of which match.  Several rows of
-- t2 use the same key so that auto-index entries get deleted and reused.
INSERT INTO t0 VALUES('x'),('y'),('x');
INSERT INTO t1 VALUES('y'),('x'),('z');
INSERT INTO t2 VALUES('y','y'),('x','y'),('z','z'),('y','x'),('x','x');
SELECT count(*), sum(t0.rowid*100+t1.rowid*10+t2.rowid)
  FROM t2 CROSS JOIN t0 JOIN t1
 WHERE t0.a=t2.c AND t2.d=t1.b
   AND EXISTS(SELECT 1 FROM t1 AS z WHERE z.b=t2.c);
.check 13 2906
.testcase 1120
-- Same answer when the EXISTS is written in a way that cannot be turned
-- into a join.
SELECT count(*), sum(t0.rowid*100+t1.rowid*10+t2.rowid)
  FROM t2 CROSS JOIN t0 JOIN t1
 WHERE t0.a=t2.c AND t2.d=t1.b
   AND (SELECT count(*) FROM t1 AS z WHERE z.b=t2.c)>0;
.check 13 2906

# Bug 2026-11-19T21:38:21Z
# https://sqlite.org/bugs/info/f8a6bebf87002694
#
# Transitive constraints.  The equivalence t2.c=t1.b is NOCASE, so t1.b
# can be substituted for t2.c only where the comparison is NOCASE too.
# The BINARY term t0.a=t2.c must not be recoded as t0.a=t1.b.
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '
.testcase 1200
CREATE TABLE t0(id INTEGER PRIMARY KEY, a);
CREATE TABLE t1(id INTEGER PRIMARY KEY, b COLLATE NOCASE);
CREATE TABLE t2(id INTEGER PRIMARY KEY, c COLLATE NOCASE);
CREATE TABLE t3(id INTEGER PRIMARY KEY, d COLLATE NOCASE);
INSERT INTO t0(a) VALUES('A');
INSERT INTO t1(b) VALUES('a');
INSERT INTO t2(c) VALUES('A');
INSERT INTO t3(d) VALUES('A');
ANALYZE;
SELECT t0.a = t2.c, t2.c = t1.b, t1.b = t3.d FROM t0, t1, t2, t3;
.check 1 1 1
.testcase 1210
SELECT t0.id, t1.id, t2.id, t3.id
  FROM t1 JOIN t2 JOIN t0 JOIN t3
 WHERE t0.a = t2.c AND t2.c = t1.b AND t1.b = t3.d
 ORDER BY 1, 2, 3, 4;
.check 1 1 1 1
.testcase 1220
-- The same with the tables in other orders.
SELECT t0.id, t1.id, t2.id, t3.id
  FROM t0 JOIN t1 JOIN t2 JOIN t3
 WHERE t0.a = t2.c AND t2.c = t1.b AND t1.b = t3.d;
.check 1 1 1 1
.testcase 1230
SELECT t0.id, t1.id, t2.id, t3.id
  FROM t3 JOIN t2 JOIN t1 JOIN t0
 WHERE t0.a = t2.c AND t2.c = t1.b AND t1.b = t3.d
 ORDER BY 1;
.check 1 1 1 1
.testcase 1240
-- A row in t0 that differs from t2.c in case does not match, even though
-- the NOCASE equivalences would let it match t1.b and t3.d.
INSERT INTO t0(a) VALUES('a');
SELECT t0.id, t1.id, t2.id, t3.id
  FROM t1 JOIN t2 JOIN t0 JOIN t3
 WHERE t0.a = t2.c AND t2.c = t1.b AND t1.b = t3.d
 ORDER BY 1, 2, 3, 4;
.check 1 1 1 1
.testcase 1250
-- When the chain is all NOCASE the transitive constraint is still valid.
CREATE TABLE t4(id INTEGER PRIMARY KEY, e COLLATE NOCASE);
INSERT INTO t4(e) VALUES('a'),('A'),('b');
SELECT t4.id, t1.id, t2.id
  FROM t4 JOIN t1 JOIN t2
 WHERE t4.e = t2.c AND t2.c = t1.b
 ORDER BY 1, 2, 3;
.check 1 1 1 2 1 1

#----------------------------------------------------------------------------
# Bug 2026-11-20T03:13:17Z sqlite.org/bugs/info/618ab5918003277e
#
# The URI decoder in the CLI and in the SQLite core should agree on what
# to do with invalid % escapes.
#
.if capable('unix') AND capable('unsafe') AND capable('vtab')
.mode quote --quote off --rowsep ' '
.testcase 1300
.shell rm -f victim victim%zz.db
.shell echo important >victim
.open --new file:victim%zz.db
CREATE TABLE t1(x);
.check
.testcase 1310
SELECT name FROM fsdir('.') WHERE name LIKE '%victim%' ORDER BY name;
.check ./victim ./victim%zz.db
.endif /* capable('unix') AND capable('unsafe') */

#-----------------------------------------------------------------------------
# Bug 2026-11-21T01:05:21Z sqlite.org/bugs/forumpost/6471ec2c5f
#
# Command Injection in sqlite3 CLI .fullschema Output
#
.open :memory:
.testcase 1400
CREATE TABLE t1(a,b,c,d);
CREATE INDEX t1a ON t1(a);
INSERT INTO t1 VALUES(111,'aaa',999,'zzz'),('bbb',222,'yyy',888);
ANALYZE;
UPDATE sqlite_stat1 SET stat = X'7827303027293b0a2e7368656c6c207072696e74662046554c4c534348454d415f45584543555445440a2d2d';
.fullschema
.check --glob 7827303027293b --keep
.check --not --glob .shell
.testcase 1410
.dump
.check --glob 7827303027293b --keep
.check --not --glob .shell

#-----------------------------------------------------------------------------
# Bug 2026-11-21T00:00:22Z sqlite.org/bugs/info/1b6dc0eaf614f8ef
# json_extract() misrounds 20-digit integers
#
.mode --reset quote
.testcase 1500
SELECT json_extract('[85432319487574911862]', '$[0]')
     = CAST('85432319487574911862' AS REAL);
.check 1
.testcase 1510
SELECT '[-85432319487574911862]' ->> '$[0]'
     = CAST('-85432319487574911862' AS REAL);
.check 1
.if capable('json_each')
.testcase 1520
SELECT value = CAST('+85432319487574911862' AS REAL)
  FROM json_each('[+85432319487574911862]');
.check 1
.endif
.testcase 1530
SELECT json_extract('[18446744073709551616]', '$[0]'),
       json_extract('[9223372036854775808]', '$[0]');
.check 1.8446744073709552e+19,9.2233720368547758e+18

#-----------------------------------------------------------------------------
# Bug 2026-11-21T15:01:57Z sqlite.org/bugs/forumpost/f5b4fcbc9e
# NULL cursor dereference in OP_Column: expression index, RIGHT JOIN,
# and an aggregate over an empty table.
#
.open :memory:
.mode --reset quote
.testcase 1600
CREATE TABLE t1(a);
CREATE TABLE t2(b);
CREATE INDEX i2 ON t2(-b);
.check
.testcase 1610
SELECT -b, count(*) FROM t1 AS p, t1 AS q RIGHT JOIN t2 ON -b=1;
.check NULL,0
INSERT INTO t1 VALUES(1);
INSERT INTO t2 VALUES(1), (-1);
.testcase 1620
SELECT -b, count(*) FROM t1 AS p, t1 AS q RIGHT JOIN t2 ON -b=1
 GROUP BY 1 ORDER BY 1;
.check -1,1 1,1

#-----------------------------------------------------------------------------
# Bug 2026-11-21T03:33:40Z sqlite.org/bugs/info/91f1a3bee82ea420
# An index on a generated column must not replace CAST(x AS NUMERIC),
# because storing a large REAL in the column converts it to INTEGER.
#
.open :memory:
.mode --reset quote
CREATE TABLE t1(n, g NUMERIC AS (CAST(n AS NUMERIC)) VIRTUAL);
INSERT INTO t1(n) VALUES(9007199254740992.0);
CREATE INDEX t1g ON t1(g, n);
.testcase 1700
SELECT typeof(CAST(n AS NUMERIC)), CAST(n AS NUMERIC)/3 FROM t1 NOT INDEXED;
.check "'real',3002399751580330.5"
.testcase 1710
SELECT typeof(CAST(n AS NUMERIC)), CAST(n AS NUMERIC)/3 FROM t1 INDEXED BY t1g;
.check "'real',3002399751580330.5"
CREATE TABLE t2(n, g INTEGER AS (CAST(n AS NUMERIC)) VIRTUAL);
INSERT INTO t2(n) VALUES(9007199254740992.0);
CREATE INDEX t2g ON t2(g, n);
.testcase 1720
SELECT typeof(CAST(n AS NUMERIC)), CAST(n AS NUMERIC)/3 FROM t2 INDEXED BY t2g;
.check "'real',3002399751580330.5"

#-------------------------------------------------------------------------------
# Bug 2026-11-20T23:57:26Z sqlite.org/bugs/info/a07990ac94d08c35
#
# Numeric affinity should not accept "Inf" as a valid REAL number.
#
.open --hexdb
| size 20480 pagesize 4096 filename unk.db
| page 1 offset 0
|      0: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00   SQLite format 3.
|     16: 10 00 01 01 00 40 20 20 00 00 00 00 00 00 00 05   .....@  ........
|     32: 00 00 00 00 00 00 00 00 00 00 00 06 00 00 00 04   ................
|     48: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00   ................
|     96: 00 00 00 00 0d 00 00 00 04 0f 1c 00 0f bf 0f 70   ...............p
|    112: 0f 46 0f 1c 00 00 00 00 00 00 00 00 00 00 00 00   .F..............
|   3856: 00 00 00 00 00 00 00 00 00 00 00 00 28 04 06 17   ............(...
|   3872: 11 11 01 3d 69 6e 64 65 78 69 32 74 32 05 43 52   ...=indexi2t2.CR
|   3888: 45 41 54 45 20 49 4e 44 45 58 20 69 32 20 4f 4e   EATE INDEX i2 ON
|   3904: 20 74 32 28 65 29 28 03 06 17 11 11 01 3d 69 6e    t2(e)(......=in
|   3920: 64 65 78 69 31 74 31 04 43 52 45 41 54 45 20 49   dexi1t1.CREATE I
|   3936: 4e 44 45 58 20 69 31 20 4f 4e 20 74 31 28 62 29   NDEX i1 ON t1(b)
|   3952: 4d 02 07 17 11 11 01 81 05 74 61 62 6c 65 74 32   M........tablet2
|   3968: 74 32 03 43 52 45 41 54 45 20 54 41 42 4c 45 20   t2.CREATE TABLE 
|   3984: 74 32 28 64 20 49 4e 54 45 47 45 52 20 50 52 49   t2(d INTEGER PRI
|   4000: 4d 41 52 59 20 4b 45 59 2c 20 65 20 52 45 41 4c   MARY KEY, e REAL
|   4016: 20 44 45 46 41 55 4c 54 20 27 69 6e 66 27 29 3f    DEFAULT 'inf')?
|   4032: 01 06 17 11 11 01 6b 74 61 62 6c 65 74 31 74 31   ......ktablet1t1
|   4048: 02 43 52 45 41 54 45 20 54 41 42 4c 45 20 74 31   .CREATE TABLE t1
|   4064: 28 61 20 49 4e 54 45 47 45 52 20 50 52 49 4d 41   (a INTEGER PRIMA
|   4080: 52 59 20 4b 45 59 2c 62 20 52 45 41 4c 2c 63 29   RY KEY,b REAL,c)
| page 2 offset 4096
|      0: 0d 00 00 00 01 0f f7 00 0f f7 00 00 00 00 00 00   ................
|   4080: 00 00 00 00 00 00 00 07 01 04 00 13 08 69 6e 66   .............inf
| page 3 offset 8192
|      0: 0d 00 00 00 01 0f fc 00 0f fc 00 00 00 00 00 00   ................
|   4080: 00 00 00 00 00 00 00 00 00 00 00 00 02 01 02 00   ................
| page 4 offset 12288
|      0: 0a 00 00 00 01 0f f9 00 0f f9 00 00 00 00 00 00   ................
|   4080: 00 00 00 00 00 00 00 00 00 06 03 13 09 69 6e 66   .............inf
| page 5 offset 16384
|      0: 0a 00 00 00 01 0f f9 00 0f f9 00 00 00 00 00 00   ................
|   4080: 00 00 00 00 00 00 00 00 00 06 03 13 09 69 6e 66   .............inf
| end unk.db
.mode --reset list --colsep ,
.testcase 1800
PRAGMA integrity_check;
.check ok
.testcase 1810
UPDATE t1 SET c=1;
SELECT b, typeof(b) FROM t1 NOT INDEXED;
.check inf,text
.testcase 1820
DELETE FROM t1 WHERE a=1;
DELETE FROM t2 WHERE d=1;
SELECT (SELECT count(*) FROM t1), (SELECT count(*) FROM t2);
.check 0,0

#------------------------------------------------------------------------------
# Bug 2026-11-20T13:00:22Z sqlite.org/bugs/info/73ca911605f0ac09
# Two %_idx entries with page number INT_MAX should be reported as corruption
# without signed integer overflow.  The overflow shows up only with UBSan.
#
.if capable('unsafe') AND capable('fts5')
.open :memory:
.dbconfig defensive off
.mode --reset quote
.testcase 1830
CREATE VIRTUAL TABLE x USING fts5(a);
INSERT INTO x VALUES('one two');
UPDATE x_data SET block=x'0000000001010100010187fffffe0087fffffe00' WHERE id=10;
UPDATE x_data SET id=(1<<37)+2147483647 WHERE id=(1<<37)+1;
WITH c(i) AS (SELECT 2147483392 UNION ALL SELECT i+1 FROM c WHERE i<2147483646)
  INSERT INTO x_data SELECT (1<<37)+i, x'00000004' FROM c;
DELETE FROM x_idx;
INSERT INTO x_idx VALUES(1,x'01',4294967294),(1,x'02',4294967294);
.check
.testcase 1831
SELECT tryeval('PRAGMA integrity_check') LIKE '%corruption found%';
.check 1
.endif /* capable('unsafe') AND capable('fts5') */

# Bug 2026-11-20T23:56:37Z sqlite.org/bugs/info/d239fb99e67aa678
# A correlated subquery in an UPDATE sees the rows that earlier iterations
# of the UPDATE have written.  A Bloom filter or an automatic-index deletion
# that was computed on the first iteration must not hide those rows.
#
.open :memory:
.mode --reset list --colsep ,
CREATE TABLE s(c,d);
CREATE TABLE t(id INTEGER PRIMARY KEY,g,k,n);
CREATE INDEX tk ON t(k);
INSERT INTO s VALUES(1,1),(1,2),(1,11),(1,12);
INSERT INTO t VALUES(1,1,1,0),(2,1,2,0),(3,1,3,0);
ANALYZE;
DELETE FROM sqlite_stat1;
INSERT INTO sqlite_stat1 VALUES('s',NULL,'1000000'),('t','tk','10 1'),
                               ('t',NULL,'10');
ANALYZE sqlite_schema;
.testcase 1900
UPDATE t SET k=CASE WHEN id=1 THEN 11 ELSE k END,
  n=(SELECT count(*) FROM s JOIN t AS x ON x.k=s.d WHERE s.c=t.g AND x.g=t.g);
SELECT group_concat(n,'/') FROM (SELECT n FROM t ORDER BY id);
.check 2/2/2
.testcase 1910
CREATE TABLE t0(b);
CREATE TABLE t1(a,z);
CREATE TABLE x(id INTEGER PRIMARY KEY,k,v,w);
CREATE INDEX xk ON x(k);
INSERT INTO t0 VALUES(1);
INSERT INTO t1 VALUES(1,50);
INSERT INTO x VALUES(1,0,0,0),(2,0,0,0);
UPDATE x SET k=CASE WHEN id=1 THEN 50 ELSE k END,
  v=(SELECT count(*) FROM t0 CROSS JOIN t1 CROSS JOIN x AS y
      WHERE t0.b=t1.a AND y.k=t1.z AND x.id>0),
  w=(SELECT count(*) FROM x AS y WHERE y.k=50 AND x.id>0);
SELECT group_concat(v||'-'||w,'/') FROM (SELECT v,w FROM x ORDER BY id);
.check 0-0/1-1

# The same UPDATE as test case 1900, but running inside a trigger.  The
# write is detected on the top-level Parse, not on the trigger sub-program.
#
.testcase 1920
DROP TABLE t;
CREATE TABLE t(id INTEGER PRIMARY KEY,g,k,n);
CREATE INDEX tk ON t(k);
INSERT INTO t VALUES(1,1,1,0),(2,1,2,0),(3,1,3,0);
INSERT INTO sqlite_stat1 VALUES('t','tk','10 1'),('t',NULL,'10');
ANALYZE sqlite_schema;
CREATE TABLE trig(a);
CREATE TRIGGER r1 AFTER INSERT ON trig BEGIN
  UPDATE t SET k=CASE WHEN id=1 THEN 11 ELSE k END,
    n=(SELECT count(*) FROM s JOIN t AS x ON x.k=s.d WHERE s.c=t.g AND x.g=t.g);
END;
INSERT INTO trig VALUES(1);
SELECT group_concat(n,'/') FROM (SELECT n FROM t ORDER BY id);
.check 2/2/2

# Bug 2026-10-21T17:15:30Z sqlite.org/bugs/info/b0c03988e8947412
# FTS5 stores terms in UTF-8 order.  In a UTF-16 database fts5vocab must
# leave range constraints and ORDER BY on the term column to the core.
#
.if capable('fts5')
.open :memory:
.mode --reset list
PRAGMA encoding = 'UTF-16le';
CREATE VIRTUAL TABLE ft USING fts5(a);
INSERT INTO ft VALUES('apple '||char(307)||'s b');
CREATE VIRTUAL TABLE v USING fts5vocab(ft, 'row');
CREATE TABLE t(term TEXT);
INSERT INTO t SELECT term FROM v;
.testcase 1930
SELECT (SELECT count(*) FROM v WHERE term<'b')
     = (SELECT count(*) FROM v WHERE +term<'b');
.check 1
.testcase 1931
SELECT (SELECT group_concat(term,'/') FROM
          (SELECT term FROM v ORDER BY term LIMIT 100))
     = (SELECT group_concat(term,'/') FROM
          (SELECT term FROM t ORDER BY term LIMIT 100));
.check 1
.testcase 1932
SELECT count(*) FROM v WHERE term>='b';
.check 1
.endif /* capable('fts5') */

# Bug 2026-11-20T01:00:44Z sqlite.org/bugs/info/e86d30379622da23
# With secure-delete, a position list whose size field is a multi-byte
# varint must still be recognized as a delete when it is flushed.
#
.if capable('fts5')
.open :memory:
.mode --reset list
CREATE VIRTUAL TABLE ft USING fts5(x);
INSERT INTO ft(ft,rank) VALUES('secure-delete',1);
INSERT INTO ft(rowid,x) VALUES(1,'a b'),(2,'a c');
WITH c(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM c WHERE n<128)
UPDATE ft SET x=(SELECT group_concat('a',' ') FROM c) WHERE rowid=2;
UPDATE ft SET x='c d' WHERE rowid=2;
.testcase 1940
SELECT group_concat(rowid) FROM ft WHERE ft MATCH 'a';
.check 1
.testcase 1941
INSERT INTO ft(ft) VALUES('integrity-check');
.check
DELETE FROM ft WHERE ft MATCH 'a';
.testcase 1942
SELECT rowid, x FROM ft;
.check 2|c d
.endif /* capable('fts5') */
