#!sqlite3
#
# 2026-11-24
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#    May you do good and not evil.
#    May you find forgiveness for yourself and forgive others.
#    May you share freely, never taking more than you give.
#

# Enhancement to tryeval() so that if any argument is "FAIL" then an
# error in the argument that follows is propagated back up to the
# caller of tryeval().
#
.open :memory:
.mode list -null NULL
.testcase 100
CREATE TABLE t1(a,b,c);
INSERT INTO t1 VALUES(1,2,3),(4,5,6),(7,8,9);
SELECT a,b,c,if(a=4,tryeval('FAIL','DROP TABLE t1')) FROM t1;
.check --glob '1|2|3|NULL*database table is locked'
.testcase 110
SELECT a,b,c,if(a=4,tryeval('FAIL','SELECT count(*) FROM t2')) FROM t1;
.check --glob '1|2|3|NULL*no such table: t2'

# Bug 2026-11-20T00:59:12Z sqlite.org/bugs/info/2485e1ff59de3ded
# ALTER TABLE ADD COLUMN, ADD CHECK and SET NOT NULL must fail if another
# statement that is already running can write the table, since that
# statement will not enforce the new constraint.
#
.if capable('unsafe')
.open :memory:
.mode --reset list
CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT);
INSERT INTO t1 VALUES(1,10),(2,20);
UPDATE t1 SET b=NULL
 WHERE a=2 OR tryeval('ALTER TABLE t1 ALTER COLUMN b SET NOT NULL') IS NULL;
.testcase 200
PRAGMA integrity_check(t1);
.check ok
.testcase 201
SELECT count(*) FROM t1 WHERE b IS NULL;
.check 2
CREATE TABLE t2(c INTEGER PRIMARY KEY, d INT);
INSERT INTO t2
  VALUES(1, coalesce(tryeval('ALTER TABLE t2 ADD CHECK(d>0)'),5)), (2, -7);
.testcase 202
PRAGMA integrity_check(t2);
.check ok
CREATE TABLE t3(e INTEGER PRIMARY KEY, f INT);
CREATE TABLE t4(g);
INSERT INTO t4 VALUES(1),(2);
INSERT INTO t3(e)
  SELECT g + 0*length(coalesce(tryeval('ALTER TABLE t3 ADD n INT NOT NULL'),''))
    FROM t4;
.testcase 203
PRAGMA integrity_check(t3);
.check ok
.endif /* capable('unsafe') */

# Bug 2026-11-20T00:58:56Z sqlite.org/bugs/info/9e0321d2e6b03dc3
# CREATE INDEX run by a function while a statement is writing the same
# table must not leave the new index incomplete.  The tryeval() function
# ignores the SQLITE_LOCKED error that CREATE INDEX returns.
#
.if capable('unsafe')
.open :memory:
.mode --reset list
.testcase 210
CREATE TABLE t1(a INTEGER PRIMARY KEY, b);
INSERT INTO t1 VALUES(1,'x'),(2,'y');
.check
.testcase 211
UPDATE t1 SET b='z'
 WHERE if(a=1,tryeval('FAIL','CREATE INDEX i1 ON t1(b)')) IS NULL;
.check --glob 'database table is locked'
.testcase 212
SELECT * FROM t1;
.check 1|x 2|y
.testcase 213
UPDATE t1 SET b='z'
 WHERE tryeval(iif(a=1,'CREATE INDEX i1 ON t1(b)',NULL)) IS NULL;
.check
.testcase 214
PRAGMA integrity_check;
.check ok
.testcase 215
SELECT count(*) FROM t1 WHERE b='z';
.check 2
DROP TABLE t1;
CREATE TABLE t1(a INTEGER PRIMARY KEY, b);
INSERT INTO t1 VALUES(1,'x'),(2,'y');
INSERT INTO t1 SELECT a+10, 'w' FROM t1
 WHERE tryeval(iif(a=1,'CREATE INDEX i1 ON t1(b)',NULL)) IS NULL;
.testcase 216
PRAGMA integrity_check;
.check ok
.testcase 217
SELECT count(*) FROM t1 WHERE b='w';
.check 2
DROP TABLE t1;
CREATE TABLE t1(a INTEGER PRIMARY KEY, b);
INSERT INTO t1 VALUES(1,'x'),(2,'y'),(3,'x');
DELETE FROM t1
 WHERE tryeval(iif(a=1,'CREATE INDEX i1 ON t1(b)',NULL)) IS NULL;
.testcase 218
PRAGMA integrity_check;
.check ok
.testcase 219
SELECT count(*) FROM t1 WHERE b='x';
.check 0
DROP TABLE t1;
# Only a trigger program of the running statement writes the indexed table.
CREATE TABLE t1(a INTEGER PRIMARY KEY);
CREATE TABLE t2(x INTEGER PRIMARY KEY, y);
CREATE TRIGGER r1 AFTER INSERT ON t1 BEGIN
  INSERT INTO t2 VALUES(new.a, 'w');
END;
INSERT INTO t1 SELECT column1 FROM (VALUES(1),(2))
 WHERE tryeval(iif(column1=1,'CREATE INDEX i2 ON t2(y)',NULL)) IS NULL;
.testcase 220
PRAGMA integrity_check;
.check ok
.testcase 221
SELECT count(*) FROM t2 WHERE y='w';
.check 2

# The OP_OpenWrite of CREATE TABLE AS names a new root page in a register
# (OPFLAG_P2ISREG).  It must not be mistaken for a write cursor on the
# existing table whose root page equals the register number, here page 2.
.open :memory:
CREATE TABLE t1(a INTEGER PRIMARY KEY, b);
INSERT INTO t1 VALUES(1,'x'),(2,'y');
CREATE TABLE t3 AS SELECT a, b FROM t1
 WHERE tryeval(iif(a=1,'CREATE INDEX t1b ON t1(b)',NULL)) IS NULL;
.testcase 230
SELECT count(*) FROM sqlite_schema WHERE name='t1b';
.check 1
.testcase 231
SELECT count(*) FROM t3;
.check 2
.endif /* capable('unsafe') */

# Bug 2026-11-23T10:18:04Z sqlite.org/bugs/info/e3c6cf1958519524
# PRAGMA quick_check must detect WITHOUT ROWID rows that are out of
# PRIMARY KEY order, just as PRAGMA integrity_check does.
#
.open :memory:
.dbconfig defensive off
.mode --reset list
CREATE TABLE t1(k TEXT COLLATE BINARY PRIMARY KEY, v) WITHOUT ROWID;
INSERT INTO t1 VALUES('Z','upper'),('a','lower');
PRAGMA writable_schema=ON;
UPDATE sqlite_schema SET sql=replace(sql,'COLLATE BINARY','COLLATE NOCASE')
 WHERE type='table' AND name='t1';
PRAGMA writable_schema=RESET;
.testcase 240
PRAGMA integrity_check;
.check row not in PRIMARY KEY order for t1
.testcase 250
PRAGMA quick_check;
.check row not in PRIMARY KEY order for t1

# Bug 2026-10-26T16:29:54Z sqlite.org/bugs/info/8ca14621a38e3a0b
# A window-function sum() whose frame sum fits in 64 bits must not fail with
# an integer overflow just because adding a new row to the frame overflowed
# before the old rows were removed.
#
.open :memory:
.mode --reset list
CREATE TABLE t(a, c);
INSERT INTO t VALUES(1, 9223372036854775807), (10, 1);
.testcase 300
SELECT a, sum(c) OVER (ORDER BY a RANGE BETWEEN 5 PRECEDING AND 0 PRECEDING)
  FROM t;
.check <<END
1|9223372036854775807
10|1
END
DELETE FROM t;
INSERT INTO t VALUES(1,9223372036854775807),(10,1),(20,9223372036854775807),
                    (30,-1),(40,-9223372036854775807-1),(50,-5);
.testcase 310
SELECT a, sum(c) OVER (ORDER BY a RANGE BETWEEN 5 PRECEDING AND 0 PRECEDING)
  FROM t;
.check <<END
1|9223372036854775807
10|1
20|9223372036854775807
30|-1
40|-9223372036854775808
50|-5
END

# Sums that really overflow are still reported as errors.
#
DELETE FROM t;
INSERT INTO t VALUES(1,9223372036854775807),(2,9223372036854775807);
.testcase 320
SELECT sum(c) OVER (ORDER BY a ROWS BETWEEN 1 PRECEDING AND CURRENT ROW)
  FROM t;
.check --glob '*integer overflow*'
.testcase 330
SELECT sum(c) FROM (SELECT 9223372036854775807 AS c UNION ALL SELECT 1
                    UNION ALL SELECT -1);
.check --glob '*integer overflow*'

# The frame sum is still out of range after the inverse step, in both
# directions, so sum() must keep reporting the overflow.
#
DELETE FROM t;
INSERT INTO t VALUES(1,-9223372036854775807),(4,9223372036854775807),
                    (8,9223372036854775807);
.testcase 340
SELECT sum(c) OVER (ORDER BY a RANGE BETWEEN 5 PRECEDING AND 0 PRECEDING)
  FROM t;
.check --glob '-9223372036854775807*0*integer overflow*'
DELETE FROM t;
INSERT INTO t VALUES(1,9223372036854775807),(4,-9223372036854775807),
                    (8,-9223372036854775807);
.testcase 350
SELECT sum(c) OVER (ORDER BY a RANGE BETWEEN 5 PRECEDING AND 0 PRECEDING)
  FROM t;
.check --glob '9223372036854775807*0*integer overflow*'

# Bug 2026-10-26T16:31:22Z sqlite.org/bugs/info/b9dfb3ca9dfc0438
# Rows that are in no window frame must not be passed to xStep or xInverse
# when the frame starts with "<expr> FOLLOWING".
#
.open :memory:
.mode --reset list
CREATE TABLE t(a, c);
INSERT INTO t VALUES(1, 'x'), (2, x''), (3, 'y');
.testcase 400
SELECT a, json_group_array(c)
  OVER (ORDER BY a ROWS BETWEEN 2 FOLLOWING AND 3 FOLLOWING) FROM t;
.check <<END
1|["y"]
2|[]
3|[]
END
.testcase 410
SELECT a, json_group_array(c)
  OVER (ORDER BY a ROWS BETWEEN 5 FOLLOWING AND UNBOUNDED FOLLOWING) FROM t;
.check <<END
1|[]
2|[]
3|[]
END
.testcase 420
SELECT a, json_group_array(c)
  OVER (ORDER BY a ROWS BETWEEN 1 FOLLOWING AND UNBOUNDED FOLLOWING) FROM t;
.check --glob '*JSON cannot hold BLOB values*'

# Leading rows are skipped separately in each partition.
#
CREATE TABLE u(p, a, c);
INSERT INTO u VALUES(1,1,'x'),(1,2,x''),(1,3,'y'),(2,1,'z'),(2,2,x''),(2,3,'w');
.testcase 430
SELECT p, a, json_group_array(c) OVER (
    PARTITION BY p ORDER BY a ROWS BETWEEN 2 FOLLOWING AND 3 FOLLOWING
  ) FROM u;
.check <<END
1|1|["y"]
1|2|[]
1|3|[]
2|1|["w"]
2|2|[]
2|3|[]
END

# Bug 2026-11-23T10:03:39Z sqlite.org/bugs/info/a8ad4f0ac1ec7999
# ALTER TABLE RENAME COLUMN refuses to rename a column that appears in a
# USING clause or in a NATURAL join of a view or trigger, since the
# rename could change the meaning of the join.
#
.open :memory:
.mode --reset list
CREATE TABLE t(c INTEGER, v INTEGER);
INSERT INTO t VALUES(1, 7);
CREATE VIEW v1 AS
  SELECT a.v AS av, b.v AS bv FROM t AS a JOIN t AS b USING(c);
.testcase 500
ALTER TABLE t RENAME COLUMN c TO c_new;
.check --glob 'cannot rename c because it appears in a USING clause of VIEW v1'
.testcase 510
SELECT name FROM pragma_table_info('t') ORDER BY cid;
.check <<EOT
c
v
EOT
.testcase 520
SELECT av+bv FROM v1;
.check 14

# The same check applies to a SELECT in a trigger.
#
DROP VIEW v1;
CREATE TABLE sink(out);
CREATE TABLE fire(x);
CREATE TRIGGER tr AFTER INSERT ON fire BEGIN
  INSERT INTO sink SELECT a.v+b.v FROM t AS a JOIN t AS b USING(c);
END;
.testcase 530
ALTER TABLE t RENAME COLUMN c TO c_new;
.check --glob 'cannot rename c because it appears in a USING clause within TRIGGER tr'
.testcase 540
ALTER TABLE t RENAME COLUMN v TO v_new;
.check
DROP TRIGGER tr;

# Renaming the column on either side of a join between different tables.
#
CREATE TABLE p(k, a);
CREATE TABLE q(k, b);
CREATE VIEW vpq AS SELECT a, b FROM p JOIN q USING(k);
.testcase 550
ALTER TABLE p RENAME COLUMN k TO kk;
.check --glob 'cannot rename k because it appears in a USING clause of VIEW vpq'
.testcase 560
ALTER TABLE q RENAME COLUMN k TO kk;
.check --glob 'cannot rename k because it appears in a USING clause of VIEW vpq'

# A table that does not have the USING column is not part of the join.
#
CREATE TABLE r(k);
.testcase 570
ALTER TABLE r RENAME COLUMN k TO kk;
.check
.testcase 580
SELECT sql FROM sqlite_schema WHERE name='vpq';
.check CREATE VIEW vpq AS SELECT a, b FROM p JOIN q USING(k)

# An earlier table in the FROM clause that does not have the column is
# passed over when looking for the left side of the USING.
#
CREATE TABLE o1(k, m);
CREATE TABLE o2(z);
CREATE VIEW vo AS SELECT 1 AS x FROM o2 JOIN o1 AS a JOIN o1 AS b USING(k);
.testcase 590
ALTER TABLE o1 RENAME COLUMN k TO kk;
.check --glob 'cannot rename k because it appears in a USING clause of VIEW vo'

# The left side of the USING can be a table after another one that lacks
# the column.
#
CREATE TABLE u0(z);
CREATE TABLE u1(k);
CREATE TABLE u2(k);
CREATE VIEW vu AS SELECT 1 AS x FROM u0 JOIN u1 JOIN u2 USING(k);
.testcase 595
ALTER TABLE u1 RENAME COLUMN k TO kk;
.check --glob 'cannot rename k because it appears in a USING clause of VIEW vu'

# Only the first earlier table that has the column is on the left side
# of the USING.  The middle table can be renamed.
#
CREATE TABLE l0(k);
CREATE TABLE l1(k);
CREATE TABLE l2(k);
CREATE VIEW vl AS SELECT 1 AS x FROM l0 JOIN l1 JOIN l2 USING(k);
.testcase 600
ALTER TABLE l1 RENAME COLUMN k TO kk;
.check
.testcase 610
ALTER TABLE l0 RENAME COLUMN k TO kk;
.check --glob 'cannot rename k because it appears in a USING clause of VIEW vl'

# Joins with an ON clause are not affected.  Only the names in the USING
# list matter.
#
CREATE TABLE w1(k, m, z, u);
CREATE VIEW vw AS
  SELECT 1 AS x FROM w1 AS a JOIN w1 AS b ON a.k=b.k JOIN w1 AS c USING(m,z);
.testcase 620
ALTER TABLE w1 RENAME COLUMN k TO kk;
.check
.testcase 630
ALTER TABLE w1 RENAME COLUMN u TO uu;
.check
.testcase 640
ALTER TABLE w1 RENAME COLUMN z TO zz;
.check --glob 'cannot rename z because it appears in a USING clause of VIEW vw'

# A subquery with its own USING clause after another one has been found.
#
CREATE TABLE s1(k);
CREATE VIEW vsq AS SELECT 1 AS x FROM s1 AS a JOIN s1 AS b USING(k)
  JOIN (SELECT 1 FROM s1 AS c JOIN s1 AS d USING(k)) AS e;
.testcase 650
ALTER TABLE s1 RENAME COLUMN k TO kk;
.check --glob 'cannot rename k because it appears in a USING clause of VIEW vsq'

# NATURAL joins, in a view and in a trigger.
#
CREATE TABLE m1(c, x);
CREATE TABLE m2(c, y);
CREATE VIEW nv AS SELECT x, y FROM m1 NATURAL JOIN m2;
.testcase 660
ALTER TABLE m1 RENAME COLUMN c TO cc;
.check --glob 'cannot rename c because it participates in a NATURAL JOIN in VIEW nv'
.testcase 670
ALTER TABLE m2 RENAME COLUMN c TO cc;
.check --glob 'cannot rename c because it participates in a NATURAL JOIN in VIEW nv'
.testcase 680
ALTER TABLE m1 RENAME COLUMN x TO xx;
.check
DROP VIEW nv;
CREATE TRIGGER ntr AFTER INSERT ON fire BEGIN
  INSERT INTO sink SELECT y FROM m1 NATURAL JOIN m2;
END;
.testcase 690
ALTER TABLE m1 RENAME COLUMN c TO cc;
.check --glob 'cannot rename c because it participates in a NATURAL JOIN within TRIGGER ntr'

# The ON clause of an UPDATE-FROM trigger step does not see the UPDATE
# target.  Unqualified "p" in the ON clause is ua.p, not ut.p.
#
CREATE TABLE ut(p, q);
CREATE TABLE ua(p, k);
CREATE TABLE ub(y);
CREATE TABLE uz(c);
CREATE TRIGGER utr AFTER INSERT ON ub BEGIN
  UPDATE ut SET q = ua.k FROM ua JOIN ub ON ub.y = p WHERE ut.p = ua.k;
END;
INSERT INTO ut VALUES(1, 0);
INSERT INTO ua VALUES(1, 1);
INSERT INTO ub VALUES(1);
.testcase 700
SELECT q FROM ut;
.check 1
.testcase 710
ALTER TABLE uz RENAME COLUMN c TO d;
.check
.testcase 720
ALTER TABLE uz RENAME TO uz2;
.check
# Renaming ua.p changes the ON clause.  Renaming ut.p does not.
.testcase 730
ALTER TABLE ua RENAME COLUMN p TO pa;
.check
.testcase 740
SELECT sql FROM sqlite_schema WHERE name='utr';
.check <<EOT
CREATE TRIGGER utr AFTER INSERT ON ub BEGIN
  UPDATE ut SET q = ua.k FROM ua JOIN ub ON ub.y = pa WHERE ut.p = ua.k;
END
EOT
.testcase 750
ALTER TABLE ut RENAME COLUMN p TO pt;
.check
.testcase 760
SELECT sql FROM sqlite_schema WHERE name='utr';
.check <<EOT
CREATE TRIGGER utr AFTER INSERT ON ub BEGIN
  UPDATE ut SET q = ua.k FROM ua JOIN ub ON ub.y = pa WHERE ut.pt = ua.k;
END
EOT

# USING and NATURAL joins in an UPDATE-FROM step ignore the target too.
#
CREATE TABLE vt(p, q);
CREATE TABLE va(p, k);
CREATE TABLE vb(p);
CREATE TABLE vs(x);
CREATE TRIGGER vtr1 AFTER INSERT ON vs BEGIN
  UPDATE vt SET q = va.k FROM va JOIN vb USING(p) WHERE vt.p = va.k;
END;
CREATE TRIGGER vtr2 AFTER DELETE ON vs BEGIN
  UPDATE vt SET q = va.k FROM va NATURAL JOIN vb WHERE vt.p = va.k;
END;
CREATE TRIGGER vtr3 AFTER UPDATE ON vs BEGIN
  UPDATE vt SET q = 5 WHERE p=new.x;
  INSERT INTO vb SELECT x FROM vs;
  DELETE FROM vb WHERE p=old.x;
END;
.testcase 770
ALTER TABLE vs RENAME TO vs2;
.check
.testcase 780
ALTER TABLE vt RENAME COLUMN q TO qq;
.check
.testcase 790
INSERT INTO vt VALUES(2,0);
INSERT INTO va VALUES(2,2);
INSERT INTO vb VALUES(2);
INSERT INTO vs2 VALUES(0);
SELECT qq FROM vt;
.check 2

# Bug 321efb6baaa6d6b3: ALTER TABLE RENAME COLUMN rewrites references
# in the arguments of a table-valued function in the FROM clause of an
# UPDATE-FROM trigger step.  The arguments can see the target table, the
# earlier FROM terms, and OLD and NEW.
#
CREATE TABLE tg(id INTEGER, payload TEXT);
CREATE TABLE tf(x INTEGER);
CREATE TRIGGER tgr AFTER INSERT ON tf BEGIN
  UPDATE tg SET id=tg.id FROM json_each(tg.payload) AS j WHERE j.value=new.x;
END;
.testcase 800
ALTER TABLE tg RENAME COLUMN payload TO payload2;
.check
.testcase 810
SELECT sql FROM sqlite_schema WHERE name='tgr';
.check <<EOT
CREATE TRIGGER tgr AFTER INSERT ON tf BEGIN
  UPDATE tg SET id=tg.id FROM json_each(tg.payload2) AS j WHERE j.value=new.x;
END
EOT
CREATE TABLE sg(j);
CREATE TABLE dg(y);
CREATE TRIGGER sgr AFTER INSERT ON sg BEGIN
  UPDATE dg SET y=je.value FROM json_each(new.j) AS je;
END;
.testcase 820
ALTER TABLE sg RENAME COLUMN j TO jj;
.check
.testcase 830
SELECT sql FROM sqlite_schema WHERE name='sgr';
.check <<EOT
CREATE TRIGGER sgr AFTER INSERT ON sg BEGIN
  UPDATE dg SET y=je.value FROM json_each(new.jj) AS je;
END
EOT
.testcase 840
INSERT INTO dg VALUES(0);
INSERT INTO sg VALUES('[5,6]');
SELECT y FROM dg;
.check 6

# The arguments can also name an earlier FROM term.
#
CREATE TABLE ag(x);
CREATE TABLE bg(y);
CREATE TRIGGER agr AFTER DELETE ON ag BEGIN
  UPDATE bg SET y=je.value FROM ag AS q, json_each(q.x) AS je;
END;
.testcase 850
ALTER TABLE ag RENAME COLUMN x TO xx;
.check
.testcase 860
SELECT sql FROM sqlite_schema WHERE name='agr';
.check <<EOT
CREATE TRIGGER agr AFTER DELETE ON ag BEGIN
  UPDATE bg SET y=je.value FROM ag AS q, json_each(q.xx) AS je;
END
EOT

# Bugs f0f9b448c47ae1b0 and b262313c7e78437a: A datatype error on a STRICT
# table in the middle of a multi-row INSERT or UPDATE inside a transaction
# rolls back the statement, but not the entire transaction.
#
.open :memory:
.mode --reset list
CREATE TABLE s1(a INTEGER) STRICT;
CREATE TABLE s2(b);
INSERT INTO s2 VALUES(1), ('x');
BEGIN;
.testcase 1000
INSERT INTO s1 SELECT b FROM s2 ORDER BY rowid;
.check --glob '*cannot store TEXT value in INTEGER column s1.a*'
.testcase 1010
SELECT count(*) FROM s1;
.check 0
COMMIT;
CREATE TABLE s3(c INTEGER) STRICT;
INSERT INTO s3 VALUES(1), (2);
BEGIN;
.testcase 1020
UPDATE s3 SET c = iif(c=1, 10, 'x');
.check --glob '*cannot store TEXT value in INTEGER column s3.c*'
.testcase 1030
SELECT group_concat(c) FROM s3;
.check 1,2
COMMIT;

# A multi-row write to one table that reads a virtual generated column of
# a STRICT table needs a statement journal, since the type check of the
# column can raise an error.  This case checks the debugging assert() that
# compares Parse.mayAbort against the generated code.
#
CREATE TABLE s4(a INTEGER, b INTEGER AS (a+1) VIRTUAL) STRICT;
INSERT INTO s4(a) VALUES(1),(3);
CREATE TABLE s5(x);
BEGIN;
INSERT INTO s5 SELECT b FROM s4 ORDER BY a;
.testcase 1040
SELECT group_concat(x) FROM s5;
.check 2,4
COMMIT;

# Bug 2026-11-25T23:49:29Z  sqlite.org/bugs/info/5d0bc13581e5cbc6
# XSS via BLOB content (regression from 3.51.0)
#
.open :memory:
.testcase 1100
PRAGMA encoding=UTF8;
CREATE TABLE t1(p);
INSERT INTO t1 VALUES('xyzzy');
.check
.testcase 1101
.mode html
SELECT p, CAST('<script>document.body.dataset.xss="executed"</script>' AS BLOB) AS q
  FROM t1;
.check <<END
<tr>
<th>p
<th>q
</tr>
<tr>
<td>xyzzy
<td>&lt;script&gt;document.body.dataset.xss=&quot;executed&quot;&lt;/script&gt;
</tr>
END

# Bug 2026-11-26  sqlite.org/bugs/info/4c331d636710d5c8
# Recovery from a journal that names a missing super-journal must truncate
# or zero the journal in TRUNCATE and PERSIST modes.  Otherwise a later
# ROLLBACK can be silently skipped.  Simulate the crash by saving a journal
# in mid-transaction, committing, and appending a super-journal name to the
# saved journal.
#
.if capable('unix')
.mode list
.open --new misc004-pj.db
PRAGMA journal_mode=PERSIST;
CREATE TABLE t1(id INTEGER PRIMARY KEY, k TEXT, val TEXT);
CREATE INDEX t1k ON t1(k);
WITH c(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM c WHERE x<25)
  INSERT INTO t1 SELECT x, printf('k%03d',x), hex(zeroblob(750)) FROM c;
PRAGMA cache_size=5;
BEGIN;
UPDATE t1 SET k=printf('c%03d',id), val='COMMITTED'||hex(zeroblob(750));
.system cp misc004-pj.db-journal misc004-pj.saved
COMMIT;
.open :memory:
.testcase 1200
SELECT writefile('misc004-pj.saved', readfile('misc004-pj.saved')
  || unhex('00040001'
           || hex(CAST('misc004-pj.db-mj1234569AB' AS BLOB))
           || '0000001900000730d9d505f920a163d7'))>0;
.check 1

.open misc004-pj.db
PRAGMA journal_mode=PERSIST;
.system cp misc004-pj.saved misc004-pj.db-journal
.testcase 1210
SELECT count(*) FROM t1 WHERE val LIKE 'COMMITTED%';
.check 25
.testcase 1211
SELECT length(readfile('misc004-pj.db-journal'))>0;
.check 0
.testcase 1212
BEGIN IMMEDIATE;
UPDATE t1 SET k=printf('D%03d',id), val='DIRTY'||hex(zeroblob(750))
 WHERE id%2==0;
ROLLBACK;
SELECT count(*) FROM t1 WHERE val LIKE 'DIRTY%';
.check 0
.testcase 1213
PRAGMA integrity_check;
.check ok

.open misc004-pj.db
PRAGMA journal_mode=TRUNCATE;
.system cp misc004-pj.saved misc004-pj.db-journal
.testcase 1220
SELECT count(*) FROM t1 WHERE val LIKE 'COMMITTED%';
.check 25
.testcase 1221
SELECT count(*) FROM t1 WHERE length(readfile('misc004-pj.db-journal'))>0;
.check 0
.testcase 1222
BEGIN IMMEDIATE;
UPDATE t1 SET k=printf('D%03d',id), val='DIRTY'||hex(zeroblob(750))
 WHERE id%2==0;
ROLLBACK;
SELECT count(*) FROM t1 WHERE val LIKE 'DIRTY%';
.check 0
.testcase 1223
PRAGMA integrity_check;
.check ok
.open :memory:
.system rm -f misc004-pj.db misc004-pj.db-journal misc004-pj.saved
.endif
