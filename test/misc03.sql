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
# 2026-11-06T00:28:19Z
# The fullkey and path columns of json_each() and json_tree() must escape
# a double-quote character in an object label.  A label written in JSON5
# using single quotes can contain a raw double-quote, which must not be
# copied into the path unchanged.
#
#   https://sqlite.org/bugs/info/53eb99b97a309be9
#
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

#***********************************************************************
# Bug 2026-11-18T08:08:53Z
#
# The excluded.* values of an UPSERT must have the affinity and collating
# sequence of the column they came from, just like the same values read
# from the table.
#
#   https://sqlite.org/bugs/info/dcf6e09adc1bfd53
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
# Bug 2026-11-06
#
# A BEFORE UPDATE trigger that reads a generated column via new.* must see
# the value computed from all of the columns, including those that the
# UPDATE does not assign.
#
#   https://sqlite.org/bugs/info/08c13eef95ba8fe2
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
# Bug 2026-11-04
#
# OP_NewRowid moves the table cursor, so it must invalidate the cached
# result of any earlier seek.  Otherwise, in a multi-row UPSERT where a
# BEFORE UPDATE trigger deletes the conflicting row, the following row is
# inserted using a stale seek result and ends up out of rowid order.
#
#   https://sqlite.org/bugs/info/44ffe36c008ceff4
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
# Bug 2026-11-04
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
