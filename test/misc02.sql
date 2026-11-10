#!sqlite3
#
# 2026-11-09
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
# A multi-row VALUES clause on the left of a compound operator should
# not override an explicit COLLATE on the right-hand operand. A
# multi-row VALUES is equivalent to SELECT ... UNION ALL SELECT ...
#
#   https://sqlite.org/bugs/forumpost/b8c24e4914
#
.open :memory:
.mode list

.testcase 100
SELECT 'a' UNION ALL SELECT 'A' EXCEPT SELECT 'A' COLLATE NOCASE;
.check ''

.testcase 110
VALUES('a') EXCEPT SELECT 'A' COLLATE NOCASE;
.check ''

.testcase 120
VALUES('a'),('A') EXCEPT SELECT 'A' COLLATE NOCASE;
.check ''

.testcase 130
VALUES('a'), ('A' COLLATE NOCASE) EXCEPT SELECT 'A';
.check ''

.testcase 140
VALUES('a' COLLATE NOCASE), ('A') EXCEPT SELECT 'A';
.check ''

.testcase 200
SELECT 'a' UNION ALL SELECT 'A' UNION SELECT 'A' COLLATE NOCASE;
.check A

.testcase 210
VALUES('a'),('A') UNION SELECT 'A' COLLATE NOCASE;
.check A

.testcase 220
VALUES('a'), ('A' COLLATE NOCASE) UNION SELECT 'A';
.check A

.testcase 230
VALUES('a' COLLATE NOCASE), ('A') UNION SELECT 'A';
.check A

.open :memory:

.testcase 300
CREATE TABLE t1(a INTEGER PRIMARY KEY, u INT UNIQUE, b INT, c);
INSERT INTO t1 VALUES(1, 1, 0, 'start');
INSERT INTO t1 VALUES(1, 2, '5.0', NULL) ON CONFLICT(a) DO UPDATE SET c=excluded.b;
SELECT typeof(c), c FROM t1;
.check integer|5

.testcase 310
INSERT INTO t1 VALUES(1, 2, '5.0', NULL)
  ON CONFLICT(a) DO UPDATE SET c='matched' WHERE excluded.b=5;
SELECT c FROM t1;
.check matched

.testcase 320
UPDATE t1 SET c='start';
INSERT INTO t1 VALUES(2, 1, '5.0', NULL) ON CONFLICT(u) DO UPDATE SET c=excluded.b;
SELECT typeof(c), c FROM t1;
.check integer|5

.testcase 330
INSERT INTO t1 VALUES(2, 1, '5.0', NULL)
  ON CONFLICT(u) DO UPDATE SET c='matched' WHERE excluded.b=5;
SELECT c FROM t1;
.check matched

# TEXT affinity: integer 5 should become text '5' in excluded.b
.testcase 400
CREATE TABLE t2(a INTEGER PRIMARY KEY, b TEXT, c);
INSERT INTO t2 VALUES(1, 'x', NULL);
INSERT INTO t2 VALUES(1, 5, NULL) ON CONFLICT(a) DO UPDATE SET c=excluded.b;
SELECT typeof(c), c FROM t2;
.check text|5

.if capable('rtree')
# R-Tree: if a node blob exceeds SQLITE_LIMIT_LENGTH, nodeWrite() ignores
# the SQLITE_TOOBIG from sqlite3_bind_blob() and stores a NULL node,
# corrupting the table.
#
#   https://sqlite.org/bugs/forumpost/38492ae5dc
#
.open :memory:

.testcase 500
CREATE VIRTUAL TABLE rt USING rtree(id, x0, x1);
INSERT INTO rt VALUES(1, 0, 10);
.limit length 100
INSERT INTO rt VALUES(2, 5, 6);
.check -glob "*rror near line *: string or blob too big*"

.testcase 510
.limit length 1000000
SELECT nodeno, typeof(data) FROM rt_node;
SELECT * FROM rt;
PRAGMA integrity_check;
.check <<END
1|blob
1|0.0|10.0
ok
END
.endif /* if capable('rtree') */

.if capable('geopoly')
# Geopoly: same problem as above for the _shape blob. A 200-vertex polygon is
# 1207 bytes as JSON but 1604 bytes as a blob, so with a 1500 byte limit
# the INSERT succeeds and silently stores a NULL _shape.
#
.open :memory:

.testcase 600
CREATE VIRTUAL TABLE gp USING geopoly();
.limit length 1500
WITH c(i) AS (VALUES(0) UNION ALL SELECT i+1 FROM c WHERE i<199)
INSERT INTO gp(_shape)
  SELECT '[' || group_concat('[' || (i%10) || ',' || (i/10%10) || ']')
             || ',[0,0]]' FROM c;
.check -glob "*rror near line *: string or blob too big*"

.testcase 610
.limit length 1000000
SELECT count(*) FROM gp;
.check 0
.endif /* if capable('geopoly') */



# A declared column type of 16 or more characters that ends in "always"
# (but not "generated always") should be recorded as declared.
#
#   https://sqlite.org/bugs/forumpost/b1d3b87a8c
#
.if capable('vtab')
.open :memory:
.mode list
.testcase 700
CREATE TABLE t7(a longertypealways, b shorttypealways, c generatedalways);
SELECT name, type FROM pragma_table_info('t7');
.check <<END
a|longertypealways
b|shorttypealways
c|generatedalways
END

.testcase 710
CREATE TABLE t8(a integer        always) STRICT;
.check -glob "*rror near line *: unknown datatype for t8.a: \"integer        always\"*"

.testcase 720
CREATE TABLE t9(a integergeneratedalways, b integer generatedalways,
                c xgeneratedalways, d integer xgenerated always,
                e xgenerated always, f integer generated always);
SELECT name, type FROM pragma_table_info('t9');
.check <<END
a|integergeneratedalways
b|integer generatedalways
c|xgeneratedalways
d|integer xgenerated always
e|xgenerated always
f|INTEGER
END

.testcase 730
CREATE TABLE t10(a integergeneratedalways) STRICT;
.check -glob "*rror near line *: unknown datatype for t10.a: \"integergeneratedalways\"*"

# A generated column with no declared type must still have an empty
# type. In a STRICT table it is rejected as missing a datatype.
#
.testcase 740
CREATE TABLE t11(x INT, a generated always as (x+1),
                 b GENERATED    ALWAYS AS (2));
SELECT name, type FROM pragma_table_xinfo('t11');
.check <<END
x|INT
a|
b|
END

.testcase 750
CREATE TABLE t12(x INT, a generated always as (x+1)) STRICT;
.check -glob "*rror near line *: missing datatype for t12.a*"
.endif /* capable('vtab') */

# Forum post 2026-11-09T04:18:04Z sqlite.org/forum/forumpost/f19d79a00a
# Correctly convert ridicuous AI-generated floating point literals.
#
.testcase 800
SELECT CAST( format('0.%.1000000c125e+1000001','0') AS real );
.check 1.25
.testcase 801
SELECT CAST( format('-125%.1000000c1e-1000001','0') AS real );
.check -125.0
.testcase 802
SELECT 1.234e+1000000000000000000000000000000000000000000000000000000000000000000;
.check Inf
.testcase 803
SELECT 1.234e-2000000000000000000000000000000000000000000000000000000000000000000;
.check 0.0
.testcase 804
SELECT 0.0e+4000000000000000000000000000000000000000000000000000000000000000000;
.check 0.0

# A BINARY index on t2.c must not be used to look up a value carried
# across the NOCASE equivalence t2.b=t2.c from t1.a=t2.b (BINARY).
#
#   https://sqlite.org/bugs/forumpost/f71ffd3ada
#
.open :memory:
CREATE TABLE t1(a TEXT);
CREATE TABLE t2(b TEXT COLLATE NOCASE, c TEXT COLLATE NOCASE);
CREATE INDEX t2x ON t2(c COLLATE BINARY);
INSERT INTO t1 VALUES('abc');
INSERT INTO t2 VALUES('abc','ABC');

.testcase 900
SELECT * FROM t1, t2 WHERE t1.a=t2.b AND t2.b=t2.c;
.check abc|abc|ABC

.testcase 910
SELECT count(*) FROM t1, t2 WHERE t1.a=t2.b AND t2.b=t2.c;
.check 1

.testcase 920
SELECT * FROM t1, t2 NOT INDEXED WHERE t1.a=t2.b AND t2.b=t2.c;
.check abc|abc|ABC

# A BINARY equivalence t2.b=t2.c may still be followed to seek a
# NOCASE index on t2.c.
#
.open :memory:
CREATE TABLE t1(a TEXT);
CREATE TABLE t2(b TEXT, c TEXT);
CREATE INDEX t2z ON t2(c COLLATE NOCASE);
INSERT INTO t1 VALUES('abc');
INSERT INTO t2 VALUES('ABC','ABC'), ('abc','ABC'), ('xyz','xyz');

.testcase 930
SELECT * FROM t1, t2 WHERE t2.b=t2.c AND t2.b=t1.a COLLATE NOCASE;
.check abc|ABC|ABC

# Bug 2026-11-08T07:03:28Z sqlite.org/bugs/info/f1c96924c04b74b1
# Unexpected FULL JOIN Result by Zhaokun Xiang
#
.open :memory:
.mode test
.testcase 1000
CREATE TABLE rt0(c0 INT);
CREATE TABLE vt1(c1 INT);
INSERT INTO vt1 VALUES(1);
CREATE TABLE v0(c0 INT);
INSERT INTO v0 VALUES(1);
.check
.testcase 1001
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE s.c0 = 1
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1
