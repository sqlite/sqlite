#!sqlite3
#
# 2026-10-21
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
# Test cases for comparisons between parent and child keys in foreign
# key processing.  Derived from test cases 8.0 and following of
# test/fkey8.test.  To run these tests:
#
#   ./sqlite3 test/fkey08.sql
#
#
.mode list
.if NOT capable('fkey')
.exit
.endif

#-------------------------------------------------------------------------
# Bug report: https://sqlite.org/bugs/forumpost/89bb27c659
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE parent(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE child(
    id INTEGER PRIMARY KEY,
    k REFERENCES parent(code) ON DELETE CASCADE
);
INSERT INTO parent VALUES(1, '1001'), (2, '1002');
INSERT INTO child VALUES(10, 1001), (11, 1002);

.testcase 8.1
INSERT INTO child VALUES(12, 1003);
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 8.2
DELETE FROM parent WHERE code = '1001';
SELECT id, quote(k) FROM child;
.check 11|1002

.testcase 8.3
PRAGMA foreign_key_check;
.check ''

# Parent columns with numeric affinity, child columns with BLOB affinity.
# The child keys are text values that only match the parent keys once
# the parent column affinity has been applied.
#
CREATE TABLE p2(id INTEGER PRIMARY KEY, n INTEGER UNIQUE, r REAL UNIQUE);
CREATE TABLE c1(k REFERENCES p2(n) ON DELETE CASCADE);
CREATE TABLE c2(k REFERENCES p2(n) ON UPDATE CASCADE);
CREATE TABLE c3(k REFERENCES p2(r) ON DELETE SET NULL);
CREATE TABLE c4(k REFERENCES p2(id) ON DELETE CASCADE);
INSERT INTO p2 VALUES(1, 5, 1.5), (2, 6, 2.5), (3, 7, 3.5), (4, 8, 4.5);
INSERT INTO c1 VALUES('5');
INSERT INTO c2 VALUES('6');
INSERT INTO c3 VALUES('3.5');
INSERT INTO c4 VALUES('4');

.testcase 8.5
DELETE FROM p2 WHERE n=5;
SELECT count(*) FROM c1;
.check 0

.testcase 8.6
UPDATE p2 SET n=60 WHERE n=6;
SELECT quote(k) FROM c2;
.check 60

.testcase 8.7
DELETE FROM p2 WHERE n=7;
SELECT quote(k) FROM c3;
.check NULL

.testcase 8.8
DELETE FROM p2 WHERE n=8;
SELECT count(*) FROM c4;
.check 0

.testcase 8.9
PRAGMA foreign_key_check;
.check ''

#-------------------------------------------------------------------------
# More cases where (parent_key = child_key) does not naturally use the
# affinity of the parent key. Tests 9.1.* are without an index on the
# child key and 9.2.* are the same tests with an index on the child key.
#
.open :memory:
PRAGMA foreign_keys=ON;

# TEXT parent, INTEGER child. Child value 1001 matches '1001' but
# not '01001'.
CREATE TABLE p1(code TEXT PRIMARY KEY);
CREATE TABLE c1(k INTEGER REFERENCES p1 ON DELETE CASCADE);
INSERT INTO p1 VALUES('01001'), ('1001');
INSERT INTO c1 VALUES(1001);

.testcase 9.1.2
DELETE FROM p1 WHERE code='01001';
SELECT quote(k) FROM c1;
.check 1001

.testcase 9.1.3
DELETE FROM p1 WHERE code='1001';
SELECT quote(k) FROM c1;
.check ''

# TEXT parent, REAL child. Child value 1001.0 matches '1001.0' but
# not '1001'.
CREATE TABLE p2(code TEXT PRIMARY KEY);
CREATE TABLE c2(k REAL REFERENCES p2 ON DELETE SET NULL);
INSERT INTO p2 VALUES('1001'), ('1001.0');
INSERT INTO c2 VALUES(1001);

.testcase 9.1.5
DELETE FROM p2 WHERE code='1001';
SELECT quote(k) FROM c2;
.check 1001.0

.testcase 9.1.6
DELETE FROM p2 WHERE code='1001.0';
SELECT quote(k) FROM c2;
.check NULL

# BLOB parent, INTEGER child. Child value 5 matches 5 but not '5'.
CREATE TABLE p3(code UNIQUE);
CREATE TABLE c3(k INTEGER REFERENCES p3(code) ON DELETE CASCADE);
INSERT INTO p3 VALUES('5'), (5);
INSERT INTO c3 VALUES(5);

.testcase 9.1.8
DELETE FROM p3 WHERE code='5';
SELECT quote(k) FROM c3;
.check 5

.testcase 9.1.9
DELETE FROM p3 WHERE code=5;
SELECT quote(k) FROM c3;
.check ''

# TEXT parent, BLOB child, ON UPDATE CASCADE.
CREATE TABLE p4(code TEXT PRIMARY KEY);
CREATE TABLE c4(k REFERENCES p4 ON UPDATE CASCADE);
INSERT INTO p4 VALUES('7'), ('07');
INSERT INTO c4 VALUES(7), ('07');

.testcase 9.1.11
UPDATE p4 SET code='8' WHERE code='7';
SELECT quote(k) FROM c4 ORDER BY 1;
.check <<END
'07'
'8'
END

# TEXT parent with NOCASE collation, BLOB child.
CREATE TABLE p5(code TEXT COLLATE nocase PRIMARY KEY);
CREATE TABLE c5(k REFERENCES p5 ON DELETE CASCADE);
INSERT INTO p5 VALUES('abc'), ('10');
INSERT INTO c5 VALUES('ABC'), (10);

.testcase 9.1.13
DELETE FROM p5;
SELECT count(*) FROM c5;
.check 0

# Immediate constraint (no action) on a TEXT parent with a BLOB child.
CREATE TABLE p6(code TEXT PRIMARY KEY);
CREATE TABLE c6(k REFERENCES p6);
INSERT INTO p6 VALUES('1'), ('01');
INSERT INTO c6 VALUES(1);

.testcase 9.1.15
DELETE FROM p6 WHERE code='01';
.check ''

.testcase 9.1.16
DELETE FROM p6 WHERE code='1';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 9.1.17
PRAGMA foreign_key_check;
.check ''

#-------------------------------------------------------------------------
# Repeat the 9.1.* tests with an index on each child key.
#
.open :memory:
PRAGMA foreign_keys=ON;

# TEXT parent, INTEGER child. Child value 1001 matches '1001' but
# not '01001'.
CREATE TABLE p1(code TEXT PRIMARY KEY);
CREATE TABLE c1(k INTEGER REFERENCES p1 ON DELETE CASCADE);
INSERT INTO p1 VALUES('01001'), ('1001');
INSERT INTO c1 VALUES(1001);
CREATE INDEX c1k ON c1(k);

.testcase 9.2.2
DELETE FROM p1 WHERE code='01001';
SELECT quote(k) FROM c1;
.check 1001

.testcase 9.2.3
DELETE FROM p1 WHERE code='1001';
SELECT quote(k) FROM c1;
.check ''

# TEXT parent, REAL child. Child value 1001.0 matches '1001.0' but
# not '1001'.
CREATE TABLE p2(code TEXT PRIMARY KEY);
CREATE TABLE c2(k REAL REFERENCES p2 ON DELETE SET NULL);
INSERT INTO p2 VALUES('1001'), ('1001.0');
INSERT INTO c2 VALUES(1001);
CREATE INDEX c2k ON c2(k);

.testcase 9.2.5
DELETE FROM p2 WHERE code='1001';
SELECT quote(k) FROM c2;
.check 1001.0

.testcase 9.2.6
DELETE FROM p2 WHERE code='1001.0';
SELECT quote(k) FROM c2;
.check NULL

# BLOB parent, INTEGER child. Child value 5 matches 5 but not '5'.
CREATE TABLE p3(code UNIQUE);
CREATE TABLE c3(k INTEGER REFERENCES p3(code) ON DELETE CASCADE);
INSERT INTO p3 VALUES('5'), (5);
INSERT INTO c3 VALUES(5);
CREATE INDEX c3k ON c3(k);

.testcase 9.2.8
DELETE FROM p3 WHERE code='5';
SELECT quote(k) FROM c3;
.check 5

.testcase 9.2.9
DELETE FROM p3 WHERE code=5;
SELECT quote(k) FROM c3;
.check ''

# TEXT parent, BLOB child, ON UPDATE CASCADE.
CREATE TABLE p4(code TEXT PRIMARY KEY);
CREATE TABLE c4(k REFERENCES p4 ON UPDATE CASCADE);
INSERT INTO p4 VALUES('7'), ('07');
INSERT INTO c4 VALUES(7), ('07');
CREATE INDEX c4k ON c4(k);

.testcase 9.2.11
UPDATE p4 SET code='8' WHERE code='7';
SELECT quote(k) FROM c4 ORDER BY 1;
.check <<END
'07'
'8'
END

# TEXT parent with NOCASE collation, BLOB child.
CREATE TABLE p5(code TEXT COLLATE nocase PRIMARY KEY);
CREATE TABLE c5(k REFERENCES p5 ON DELETE CASCADE);
INSERT INTO p5 VALUES('abc'), ('10');
INSERT INTO c5 VALUES('ABC'), (10);
CREATE INDEX c5k ON c5(k COLLATE nocase);

.testcase 9.2.13
DELETE FROM p5;
SELECT count(*) FROM c5;
.check 0

# Immediate constraint (no action) on a TEXT parent with a BLOB child.
CREATE TABLE p6(code TEXT PRIMARY KEY);
CREATE TABLE c6(k REFERENCES p6);
INSERT INTO p6 VALUES('1'), ('01');
INSERT INTO c6 VALUES(1);
CREATE INDEX c6k ON c6(k);

.testcase 9.2.15
DELETE FROM p6 WHERE code='01';
.check ''

.testcase 9.2.16
DELETE FROM p6 WHERE code='1';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 9.2.17
PRAGMA foreign_key_check;
.check ''

#-------------------------------------------------------------------------
# Check that an index on the child key can still be used when the
# comparison does not naturally use the affinity of the parent key.
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE parent(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE c1(id INTEGER PRIMARY KEY, k REFERENCES parent(code));
CREATE INDEX c1k ON c1(k);
CREATE TABLE c2(id INTEGER PRIMARY KEY,
  k REFERENCES parent(code) ON DELETE CASCADE
);
CREATE INDEX c2k ON c2(k);
CREATE TABLE c3(k INTEGER PRIMARY KEY REFERENCES parent(code)
  ON UPDATE CASCADE
);

.testcase 10.1
EXPLAIN QUERY PLAN DELETE FROM parent WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH parent USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH c3 USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH c2 USING COVERING INDEX c2k (k=?)
`--SEARCH c1 USING COVERING INDEX c1k (k=?)
END

.testcase 10.2
EXPLAIN QUERY PLAN UPDATE parent SET code='x' WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH parent USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH c3 USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH c2 USING COVERING INDEX c2k (k=?)
|--SEARCH c1 USING COVERING INDEX c1k (k=?)
|--SEARCH c3 USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH c2 USING COVERING INDEX c2k (k=?)
`--SEARCH c1 USING COVERING INDEX c1k (k=?)
END

.if capable('bytecode')
#-------------------------------------------------------------------------
# The extra IN term used to drive an index lookup on the child key is
# only generated if there is a suitable index on the child key. In
# this test the IN term, if present, would use an ephemeral table. So
# check for an OP_OpenEphemeral in the program that implements the
# no-action foreign key checks. The bytecode() table-valued function
# is used in place of EXPLAIN.
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE parent(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE c1(id INTEGER PRIMARY KEY, k REFERENCES parent(code));

.testcase 11.1
SELECT count(*)>0 FROM bytecode('DELETE FROM parent WHERE id=1')
 WHERE opcode='OpenEphemeral';
.check 0

# An index with the wrong collation sequence is not suitable.
CREATE INDEX c1k1 ON c1(k COLLATE nocase);
.testcase 11.3
SELECT count(*)>0 FROM bytecode('DELETE FROM parent WHERE id=1')
 WHERE opcode='OpenEphemeral';
.check 0

# Nor is a partial index, or one on which k is not the left-most column.
CREATE INDEX c1k2 ON c1(k) WHERE id>10;
CREATE INDEX c1k3 ON c1(id, k);
.testcase 11.5
SELECT count(*)>0 FROM bytecode('DELETE FROM parent WHERE id=1')
 WHERE opcode='OpenEphemeral';
.check 0

CREATE INDEX c1k4 ON c1(k);
.testcase 11.7
SELECT count(*)>0 FROM bytecode('DELETE FROM parent WHERE id=1')
 WHERE opcode='OpenEphemeral';
.check 1
.endif /* capable('bytecode') */

#-------------------------------------------------------------------------
# Check that an index on a composite child key can be used for all
# columns of the key, not just the left-most, when the comparisons do
# not naturally use the affinity of the parent key.
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE parent(id INTEGER PRIMARY KEY, x TEXT, y TEXT, UNIQUE(x, y));
CREATE TABLE c1(a, b, FOREIGN KEY(a, b) REFERENCES parent(x, y));
CREATE INDEX c1ab ON c1(a, b);
CREATE TABLE c2(a, b, FOREIGN KEY(a, b) REFERENCES parent(x, y));
CREATE INDEX c2ba ON c2(b, a);

.testcase 12.1
EXPLAIN QUERY PLAN DELETE FROM parent WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH parent USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH c2 USING COVERING INDEX c2ba (b=? AND a=?)
`--SEARCH c1 USING COVERING INDEX c1ab (a=? AND b=?)
END
