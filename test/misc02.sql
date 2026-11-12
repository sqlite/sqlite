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
.mode quote
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

# Bug 2026-11-08T01:01:03Z sqlite.org/bugs/info/1dc46f0ed986553a
# RIGHT JOIN Drops a Matching Row After Nested FULL JOIN Flattening
# By Zhaokun Xiang
#
.open :memory:
.mode quote
.testcase 1100
CREATE TABLE h0base(a INT);
INSERT INTO h0base VALUES(0);
CREATE VIEW v1(c0, c1) AS
  SELECT 0, 0
    FROM (
      (SELECT 1 AS a WHERE 0) AS l
      FULL JOIN (SELECT 1 AS c0) AS r ON 0
    ) AS s;
CREATE TABLE h1(c1 INT);
INSERT INTO h1 VALUES(0);
.check

.testcase 1101
.testctrl opt -Flatten -q
SELECT ALL v1.c0
  FROM v1
       CROSS JOIN (SELECT * FROM h0base) AS h0
       RIGHT JOIN h1 ON v1.c1 = h1.c1
 WHERE h0.a = 0;
.check 0

.testcase 1102
.testctrl opt +All -q
SELECT ALL v1.c0
  FROM v1
       CROSS JOIN (SELECT * FROM h0base) AS h0
       RIGHT JOIN h1 ON v1.c1 = h1.c1
 WHERE h0.a = 0;
.check 0

# More tests for the 2026-11-08T07:03:28Z problem above: the term that
# was marked as an ON-clause term of the flattened subquery can be any
# kind of expression.
#
.open :memory:
.mode quote
.testcase 1010
CREATE TABLE rt0(c0 INT);
CREATE TABLE vt1(c1 INT);
INSERT INTO vt1 VALUES(1);
CREATE TABLE v0(c0 INT);
INSERT INTO v0 VALUES(1);
.check

.testcase 1011
.testctrl opt -Flatten -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE CASE WHEN s.c0=1 THEN 1 ELSE 0 END
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

.testcase 1012
.testctrl opt +All -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE CASE WHEN s.c0=1 THEN 1 ELSE 0 END
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

.testcase 1013
.testctrl opt -Flatten -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE s.c0 IN (1,2)
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

.testcase 1014
.testctrl opt +All -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE s.c0 IN (1,2)
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

.testcase 1015
.testctrl opt -Flatten -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE s.c0 BETWEEN 1 AND 2
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

.testcase 1016
.testctrl opt +All -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE s.c0 BETWEEN 1 AND 2
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

.testcase 1017
.testctrl opt -Flatten -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE coalesce(s.c0,0)=1
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

.testcase 1018
.testctrl opt +All -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE coalesce(s.c0,0)=1
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

.testcase 1019
.testctrl opt -Flatten -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE (s.c0,1) = (1,1)
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

.testcase 1020
.testctrl opt +All -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE (s.c0,1) = (1,1)
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

.testcase 1021
.testctrl opt -Flatten -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE s.c0=1 AND CASE s.c0 WHEN 1 THEN 1 END
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

.testcase 1022
.testctrl opt +All -q
SELECT vt1.c1
  FROM (
    SELECT s.c0
      FROM (
        SELECT v0.c0
          FROM v0 NATURAL FULL JOIN (SELECT NULL AS c0) AS r
      ) AS s
     WHERE s.c0=1 AND CASE s.c0 WHEN 1 THEN 1 END
  ) AS a
    CROSS JOIN vt1 ON 1
    FULL OUTER JOIN (SELECT c0 FROM rt0) AS b ON 0
WHERE vt1.c1 IS NOT NULL;
.check 1

# More tests for the 2026-11-08T01:01:03Z problem above.  When OUTER
# JOIN strength reduction makes the left operand of a RIGHT JOIN a
# plain inner-join operand, the TK_IF_NULL_ROW wrappers that the
# flattener previously added must be removed.
#
.open :memory:
.mode quote
.testcase 1200
CREATE TABLE h0base(a INT);
INSERT INTO h0base VALUES(0);
CREATE TABLE h1(c1 INT);
INSERT INTO h1 VALUES(0);
CREATE VIEW v1(c0, c1) AS
  SELECT 0, 0
    FROM (
      (SELECT 1 AS a WHERE 0) AS l
      FULL JOIN (SELECT 1 AS c0) AS r ON 0
    ) AS s;
CREATE VIEW v2(c0, c1, c2) AS
  SELECT 0, 0, s.c0
    FROM (
      (SELECT 1 AS a WHERE 0) AS l
      FULL JOIN (SELECT 1 AS c0) AS r ON 0
    ) AS s;
.check

.testcase 1201
.testctrl opt -Flatten -q
SELECT v2.c0+1, v2.c1 FROM v2 CROSS JOIN (SELECT * FROM h0base) AS h0 RIGHT JOIN h1 ON v2.c1=h1.c1 WHERE h0.a=0;
.check 1,0

.testcase 1202
.testctrl opt +All -q
SELECT v2.c0+1, v2.c1 FROM v2 CROSS JOIN (SELECT * FROM h0base) AS h0 RIGHT JOIN h1 ON v2.c1=h1.c1 WHERE h0.a=0;
.check 1,0

.testcase 1203
.testctrl opt -Flatten -q
SELECT v1.c0 FROM v1 JOIN (SELECT * FROM h0base) AS h0 RIGHT JOIN h1 ON v1.c1=h1.c1 AND h0.a=0 WHERE h0.a=0;
.check 0

.testcase 1204
.testctrl opt +All -q
SELECT v1.c0 FROM v1 JOIN (SELECT * FROM h0base) AS h0 RIGHT JOIN h1 ON v1.c1=h1.c1 AND h0.a=0 WHERE h0.a=0;
.check 0

.testcase 1205
.testctrl opt -Flatten -q
SELECT count(*), max(v1.c0) FROM v1 CROSS JOIN (SELECT * FROM h0base) AS h0 RIGHT JOIN h1 ON v1.c1=h1.c1 WHERE h0.a=0;
.check 1,0

.testcase 1206
.testctrl opt +All -q
SELECT count(*), max(v1.c0) FROM v1 CROSS JOIN (SELECT * FROM h0base) AS h0 RIGHT JOIN h1 ON v1.c1=h1.c1 WHERE h0.a=0;
.check 1,0

.testcase 1207
.testctrl opt -Flatten -q
SELECT (SELECT v1.c0+1) FROM v1 CROSS JOIN (SELECT * FROM h0base) AS h0 RIGHT JOIN h1 ON v1.c1=h1.c1 WHERE h0.a=0;
.check 1

.testcase 1208
.testctrl opt +All -q
SELECT (SELECT v1.c0+1) FROM v1 CROSS JOIN (SELECT * FROM h0base) AS h0 RIGHT JOIN h1 ON v1.c1=h1.c1 WHERE h0.a=0;
.check 1

# A row-value IN whose subquery has DISTINCT and LIMIT/OFFSET must not
# lose rows when only some of its columns can drive an index.
#
#   https://sqlite.org/bugs/forumpost/51d85f8cae
#
.open :memory:
CREATE TABLE t(a INT, b INT, c INT);
INSERT INTO t VALUES(1,2,3),(2,1,3),(3,3,3);
CREATE INDEX i1 ON t(b);
CREATE TABLE s(p, q, r);
INSERT INTO s VALUES(2,2,2),(1,2,3),(2,1,3),(3,3,3);

.testcase 1300
SELECT a,b,c FROM t WHERE (a,b,c) IN (SELECT DISTINCT p,q,r FROM s ORDER BY r, p LIMIT 2 OFFSET 1) ORDER BY a;
.check "1,2,3\n2,1,3"

.testcase 1310
SELECT a,b,c FROM t NOT INDEXED WHERE (a,b,c) IN (SELECT DISTINCT p,q,r FROM s ORDER BY r, p LIMIT 2 OFFSET 1) ORDER BY a;
.check "1,2,3\n2,1,3"

# An empty IN () list on the RHS of IS must evaluate to integer 0/1,
# not be treated as IS FALSE / IS TRUE.
#
#   https://sqlite.org/bugs/forumpost/fb5f5b2dac
#   https://sqlite.org/bugs/forumpost/fb2e7f646b
#
.testcase 1400
SELECT 'one' IS (1 IN ()), 'one' IS (1 IN (SELECT 1 WHERE 0)), 'one' IS 0;
.check 0,0,0

.testcase 1410
SELECT 2 IS (1 NOT IN ()), 2 IS 1;
.check 0,0

.testcase 1420
SELECT '' IS 0, '' IS (0 IN ()), '' IS FALSE;
.check 0,0,1

.testcase 1430
SELECT '' IS NOT 0, '' IS NOT (0 IN ()), '' IS NOT FALSE;
.check 1,1,0

.testcase 1440
SELECT '' IS (length('') IN ()), 0 IS (0 IN ()), typeof(0 IN ());
.check "0,1,'integer'"

.testcase 1450
SELECT (0 IN ()) + 1, (0 IN ()) = 0, CASE WHEN (0 IN ()) THEN 1 ELSE 0 END;
.check 1,1,0

.testcase 1460
SELECT x'' IS (0 IN ()), x'' IS NOT (0 IN ());
.check 0,1

.testcase 1470
SELECT '' IS (0 NOT IN ()), 'x' IS NOT (0 NOT IN ());
.check 0,1

.testcase 1480
CREATE TABLE t1480(c1 TEXT);
INSERT INTO t1480 VALUES('a');
SELECT count(*) FROM t1480 WHERE TRIM(c1,c1) IS (-693048757 IN ());
.check 0

# WHERE-clause constant propagation must not substitute a constant for
# a no-affinity column on the LHS of a comparison whose RHS has TEXT
# affinity. Here x=b compares with TEXT affinity, so x=1.0 must not
# be used to rewrite x=b as 1.0=b.
#
#   https://sqlite.org/bugs/forumpost/e4bbd1c242
#
.testcase 1500
CREATE TABLE t1500a(a INT);
CREATE TABLE t1500b(b TEXT);
INSERT INTO t1500a VALUES(1);
INSERT INTO t1500b VALUES('1.0'),('1');
WITH c AS MATERIALIZED (SELECT a+0 AS x FROM t1500a)
  SELECT x, b FROM c, t1500b WHERE x=b AND x=1.0;
.check 1,'1'

.testcase 1510
WITH c AS MATERIALIZED (SELECT a+0 AS x FROM t1500a)
  SELECT x, b FROM c, t1500b WHERE b=x AND x=1.0;
.check 1,'1'

.testcase 1520
SELECT x, b FROM (SELECT a+0 AS x FROM t1500a LIMIT 5), t1500b
  WHERE x=b AND x=1.0;
.check 1,'1'

# EXISTS(SELECT DISTINCT ... LIMIT ... OFFSET ...) must honor the DISTINCT
# when applying the OFFSET. Here the DISTINCT subquery returns no rows.
#
#   https://sqlite.org/bugs/forumpost/7b2bd78913
#
.testcase 1530
CREATE TABLE t1530(b);
INSERT INTO t1530 VALUES(1),(1);
SELECT EXISTS(SELECT DISTINCT b FROM t1530 LIMIT 1 OFFSET 1);
.check 0

.testcase 1540
SELECT count(*) FROM t1530 
 WHERE EXISTS(SELECT DISTINCT b FROM t1530 LIMIT 1 OFFSET 1);
.check 0

.testcase 1550
CREATE TABLE t1550(b, c);
INSERT INTO t1550 VALUES(1,1),(1,2),(2,3),(NULL,4),(NULL,5);
CREATE INDEX t1550b ON t1550(b);
SELECT EXISTS(SELECT DISTINCT b FROM t1550 LIMIT 1 OFFSET 2),
       EXISTS(SELECT DISTINCT b FROM t1550 LIMIT 1 OFFSET 3),
       EXISTS(SELECT DISTINCT b, c>1 FROM t1550 LIMIT 1 OFFSET 3),
       EXISTS(SELECT DISTINCT b, c>1 FROM t1550 LIMIT 1 OFFSET 4),
       EXISTS(SELECT DISTINCT count(*) FROM t1550 GROUP BY b LIMIT 1 OFFSET 1),
       EXISTS(SELECT DISTINCT count(*) FROM t1550 GROUP BY b LIMIT 1 OFFSET 2);
.check 1,0,1,0,1,0

.testcase 1560
SELECT x.c, EXISTS(
  SELECT DISTINCT c>2 FROM t1550 WHERE t1550.b IS x.b LIMIT 1 OFFSET 1
) FROM t1550 AS x ORDER BY x.c;
.check '1,0 2,0 3,0 4,0 5,0'

# A single-row INSERT ... VALUES that fails an immediate FK constraint
# after a deferred FK on the same row has already been counted must not
# leave the deferred-constraint counter incremented.
#
#   https://sqlite.org/bugs/forumpost/5f8d8118f7
#
.testcase 1570
PRAGMA foreign_keys=ON;
CREATE TABLE p1570(a INTEGER PRIMARY KEY);
CREATE TABLE q1570(a INTEGER PRIMARY KEY);
CREATE TABLE c1570(x REFERENCES p1570,
                   y REFERENCES q1570 DEFERRABLE INITIALLY DEFERRED);
BEGIN;
INSERT INTO c1570 VALUES(1,1);
.check -glob "*rror near line *: FOREIGN KEY constraint failed*"

.testcase 1580
COMMIT;
SELECT count(*), 'ok' FROM c1570;
.check 0,'ok'

.testcase 1590
ROLLBACK;
PRAGMA foreign_keys=OFF;
.check -glob "*"

# A DELETE inside a transaction that fails with an FK error because a
# BEFORE trigger with RAISE(IGNORE) blocks part of an ON DELETE CASCADE
# must be rolled back, as it is in autocommit mode.
#
#   https://sqlite.org/bugs/forumpost/791eb054fd
#
.testcase 1600
PRAGMA foreign_keys=ON;
CREATE TABLE p1600(id INTEGER PRIMARY KEY);
CREATE TABLE c1600(id INTEGER PRIMARY KEY,
                   r REFERENCES p1600(id) ON DELETE CASCADE);
INSERT INTO p1600 VALUES(1),(2),(3),(4);
INSERT INTO c1600 VALUES(1,1),(2,2),(3,3),(4,4);
CREATE TRIGGER t1600 BEFORE DELETE ON c1600 WHEN old.id=3 BEGIN
  SELECT RAISE(IGNORE);
END;
BEGIN;
DELETE FROM p1600;
.check -glob "*rror near line *: FOREIGN KEY constraint failed*"

.testcase 1610
SELECT (SELECT count(*) FROM p1600), (SELECT count(*) FROM c1600);
PRAGMA foreign_key_check;
.check 4,4

.testcase 1620
COMMIT;
.check -glob "*"

# Same again with ON DELETE SET NULL and a BEFORE UPDATE trigger.
.testcase 1630
CREATE TABLE p1630(id INTEGER PRIMARY KEY);
CREATE TABLE c1630(id INTEGER PRIMARY KEY,
                   r REFERENCES p1630(id) ON DELETE SET NULL);
INSERT INTO p1630 VALUES(1),(2),(3),(4);
INSERT INTO c1630 VALUES(1,1),(2,2),(3,3),(4,4);
CREATE TRIGGER t1630 BEFORE UPDATE ON c1630 WHEN old.id=3 BEGIN
  SELECT RAISE(IGNORE);
END;
BEGIN;
DELETE FROM p1630;
.check -glob "*rror near line *: FOREIGN KEY constraint failed*"

.testcase 1640
SELECT (SELECT count(*) FROM p1630), (SELECT count(r) FROM c1630);
PRAGMA foreign_key_check;
.check 4,4

.testcase 1650
COMMIT;
PRAGMA foreign_keys=OFF;
.check -glob "*"

# Bug 2026-11-04T20:19:36Z sqlite.org/bugs/info/311551f464e58d18
# two-argument substr(X, -9223372036854775808) drops the last character of X
#
.mode quote -quote relaxed
.testcase 1700
SELECT substr('abc',-100), substr('abc',-9223372036854775807);
.check abc,abc
.testcase 1701
SELECT substr('abc',-9223372036854775808), substr(x'616263',-9223372036854775808);
.check abc,x'616263'

# Bug sqlite.org/bugs/forumpost/93e321362f
# A single-row REPLACE that deletes a row to resolve a UNIQUE conflict,
# and then fails while computing a later index key, must roll back the
# deletion when run inside a transaction.
#
.testcase 1800
CREATE TABLE t1800(k INTEGER PRIMARY KEY, a, b);
CREATE INDEX t1800b ON t1800(abs(b));
CREATE UNIQUE INDEX t1800a ON t1800(a);
INSERT INTO t1800 VALUES(1,1,1),(2,2,2);
BEGIN;
REPLACE INTO t1800(a,b) VALUES(1,-9223372036854775807-1);
.check -glob "*rror near line *: integer overflow*"
.testcase 1810
SELECT * FROM t1800;
.check "1,1,1\n2,2,2"
.testcase 1820
INSERT OR IGNORE INTO t1800 VALUES(1,1,1);
UPDATE OR REPLACE t1800 SET a=1, b=-9223372036854775807-1 WHERE k=2;
.check -glob "*rror near line *: integer overflow*"
.testcase 1830
SELECT * FROM t1800;
COMMIT;
.check "1,1,1\n2,2,2"

# Same again, but the error is raised by the WHERE clause of a partial index.
#
.testcase 1840
CREATE TABLE t1840(k INTEGER PRIMARY KEY, a, b);
CREATE INDEX t1840b ON t1840(b) WHERE abs(b)>0;
CREATE UNIQUE INDEX t1840a ON t1840(a);
INSERT INTO t1840 VALUES(1,1,1),(2,2,2);
BEGIN;
REPLACE INTO t1840(a,b) VALUES(1,-9223372036854775807-1);
.check -glob "*rror near line *: integer overflow*"
.testcase 1850
SELECT * FROM t1840;
COMMIT;
.check "1,1,1\n2,2,2"

# Bug sqlite.org/bugs/forumpost/d5684dd903
# A multi-row UPDATE or INSERT that fails inside an aggregate function must
# roll back the rows it has already changed when run inside a transaction.
#
.testcase 1900
CREATE TABLE t1900(k INTEGER PRIMARY KEY, b INT);
INSERT INTO t1900 VALUES(1,1),(2,2),(3,3),(4,4);
CREATE TABLE t1900b(k INT, x INT);
INSERT INTO t1900b VALUES(1,100),(2,200),(3,9223372036854775807),(3,1),(4,400);
BEGIN;
UPDATE t1900 SET b=(SELECT sum(x) FROM t1900b WHERE t1900b.k=t1900.k);
.check -glob "*rror near line *: integer overflow*"
.testcase 1901
SELECT * FROM t1900;
COMMIT;
.check "1,1\n2,2\n3,3\n4,4"

# INSERT ... SELECT with the same correlated aggregate subquery.
.testcase 1902
CREATE TABLE t1900c(k, s);
BEGIN;
INSERT INTO t1900c
  SELECT k, (SELECT sum(x) FROM t1900b WHERE t1900b.k=t1900.k) FROM t1900;
.check -glob "*rror near line *: integer overflow*"
.testcase 1903
SELECT count(*) FROM t1900c;
COMMIT;
.check 0

# INSERT ... SELECT with GROUP BY. The error occurs while finalizing the
# aggregate for the second group, after the first group has been inserted.
.testcase 1904
CREATE TABLE t1900d(k, s);
BEGIN;
INSERT INTO t1900d SELECT k, sum(x) FROM t1900b GROUP BY k ORDER BY k;
.check -glob "*rror near line *: integer overflow*"
.testcase 1905
SELECT count(*) FROM t1900d;
COMMIT;
.check 0

# A window function.
.testcase 1906
CREATE TABLE t1900e(k, s);
BEGIN;
INSERT INTO t1900e SELECT k, sum(x) OVER (ORDER BY k) FROM t1900b;
.check -glob "*rror near line *: integer overflow*"
.testcase 1907
SELECT count(*) FROM t1900e;
COMMIT;
.check 0

# Bug sqlite.org/bugs/forumpost/83b29c74b2
# A multi-row UPDATE or INSERT that fails inside json_each() or another
# table-valued function must roll back the rows it has already changed
# when run inside a transaction.
#
.testcase 1950
CREATE TABLE t1950(k INTEGER PRIMARY KEY, j TEXT, n INT);
INSERT INTO t1950 VALUES(1,'[1]',0),(2,'[1,2]',0),(3,'[1,2',0);
BEGIN;
UPDATE t1950 SET n=(SELECT count(*) FROM json_each(t1950.j));
.check -glob "*rror near line *: malformed JSON*"
.testcase 1951
SELECT * FROM t1950;
COMMIT;
.check "1,[1],0\n2,[1,2],0\n3,[1,2,0"

# INSERT ... SELECT from a join with json_each().
.testcase 1952
CREATE TABLE t1950b(v);
BEGIN;
INSERT INTO t1950b SELECT e.value FROM t1950, json_each(t1950.j) e;
.check -glob "*rror near line *: malformed JSON*"
.testcase 1953
SELECT count(*) FROM t1950b;
COMMIT;
.check 0

# json_tree().
.testcase 1954
UPDATE t1950 SET n=0;
BEGIN;
UPDATE t1950 SET n=(SELECT count(*) FROM json_tree(t1950.j));
.check -glob "*rror near line *: malformed JSON*"
.testcase 1955
SELECT * FROM t1950;
COMMIT;
.check "1,[1],0\n2,[1,2],0\n3,[1,2,0"

# pragma_table_info() with an unknown schema argument on the last row.
.testcase 1956
CREATE TABLE t1950c(k INTEGER PRIMARY KEY, s TEXT, n INT);
INSERT INTO t1950c VALUES(1,'main',0),(2,'main',0),(3,'nosuchdb',0);
BEGIN;
UPDATE t1950c SET n=(SELECT count(*) FROM pragma_table_info('t1950c', s));
.check -glob "*rror near line *: unknown database*"
.testcase 1957
SELECT * FROM t1950c;
COMMIT;
.check "1,main,0\n2,main,0\n3,nosuchdb,0"

# Bug sqlite.org/bugs/forumpost/1a7eb564ec
# An "INSERT INTO parent SELECT * FROM" statement must not use the xfer
# optimization if FKs are enabled and "parent" is a parent table.
#
.testcase 1960
PRAGMA foreign_keys=ON;
CREATE TABLE p1960(a INTEGER PRIMARY KEY, b);
CREATE TABLE c1960(x REFERENCES p1960(a) DEFERRABLE INITIALLY DEFERRED);
CREATE TABLE s1960(a INTEGER PRIMARY KEY, b);
INSERT INTO s1960 VALUES(5,'five');
BEGIN;
INSERT INTO c1960 VALUES(5);
INSERT INTO p1960 SELECT * FROM s1960;
COMMIT;
SELECT * FROM p1960, c1960;
.check "5,five,5"

.open :memory:
.testcase 1961
CREATE TABLE p1961(a PRIMARY KEY, b) WITHOUT ROWID;
CREATE TABLE c1961(x REFERENCES p1961(a));
CREATE TABLE s1961(a PRIMARY KEY, b) WITHOUT ROWID;
INSERT INTO s1961 VALUES(5,'five');
PRAGMA foreign_keys=ON;
PRAGMA defer_foreign_keys=ON;
BEGIN;
INSERT INTO c1961 VALUES(5);
INSERT INTO p1961 SELECT * FROM s1961;
COMMIT;
SELECT * FROM p1961, c1961;
.check "5,five,5"
.open :memory:
