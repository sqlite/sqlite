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
