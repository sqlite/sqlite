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

.testcase 300
UPDATE t1 SET c='start';
INSERT INTO t1 VALUES(2, 1, '5.0', NULL) ON CONFLICT(u) DO UPDATE SET c=excluded.b;
SELECT typeof(c), c FROM t1;
.check integer|5

.testcase 310
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
