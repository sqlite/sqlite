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
# Test cases for foreign keys where the comparison between parent and
# child keys does not naturally use the affinity of the parent key.
# In that case an extra IN term is added to the comparison, but only if
# the child key is indexed by a suitable index - one whose left-most
# column is the child key, that is not a partial index, and that uses
# the same collation sequence as the parent key. See the
# fkChildKeyIsIndexed() routine in fkey.c.
#
# Each test case below creates a TEXT parent key and a BLOB (untyped)
# child key, plus some set of indexes on the child table. It then checks:
#
#   *  Whether or not an index on the child table is used
#      (EXPLAIN QUERY PLAN).
#
#   *  Whether or not the IN term is present in the trigger program
#      that implements the ON DELETE CASCADE action on table c. The IN
#      term is the only source of an OP_Cast opcode in that program.
#
#   *  That the correct child rows are deleted, whichever plan is used,
#      and that the no-action foreign key on table n is enforced.
#
# To run these tests:
#
#   ./sqlite3 test/fkey09.sql
#
#
.mode list
.if NOT capable('fkey')
.exit
.endif

#-------------------------------------------------------------------------
# No indexes on the child tables at all.
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (5, 'ABC'), (6, 'abc  ');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 100.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SCAN n
`--SCAN c
END

.if NOT capable('bytecode')
.exit
.endif

.testcase 100.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 0

.testcase 100.3
DELETE FROM p WHERE code='1001';
SELECT id FROM c ORDER BY id;
.check <<END
3
4
5
6
7
END

.testcase 100.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 100.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 100.6
PRAGMA foreign_key_check;
.check ''
#-------------------------------------------------------------------------
# An index on a column other than the child key. The left-most
# column of the index is not the child key (line 558 false).
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
CREATE INDEX cz ON c(z);
CREATE INDEX nz ON n(z);
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (5, 'ABC'), (6, 'abc  ');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 110.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SCAN n
`--SCAN c
END

.testcase 110.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 0

.testcase 110.3
DELETE FROM p WHERE code='1001';
SELECT id FROM c ORDER BY id;
.check <<END
3
4
5
6
7
END

.testcase 110.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 110.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 110.6
PRAGMA foreign_key_check;
.check ''
#-------------------------------------------------------------------------
# The child key is a column of the index, but not the left-most.
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
CREATE INDEX czk ON c(z, k);
CREATE INDEX nzk ON n(z, k);
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (5, 'ABC'), (6, 'abc  ');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 120.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SCAN n
`--SCAN c
END

.testcase 120.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 0

.testcase 120.3
DELETE FROM p WHERE code='1001';
SELECT id FROM c ORDER BY id;
.check <<END
3
4
5
6
7
END

.testcase 120.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 120.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 120.6
PRAGMA foreign_key_check;
.check ''
#-------------------------------------------------------------------------
# A partial index on the child key (line 559 false).
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
CREATE INDEX ckp ON c(k) WHERE z>0;
CREATE INDEX nkp ON n(k) WHERE z>0;
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (5, 'ABC'), (6, 'abc  ');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 130.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SCAN n
`--SCAN c
END

.testcase 130.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 0

.testcase 130.3
DELETE FROM p WHERE code='1001';
SELECT id FROM c ORDER BY id;
.check <<END
3
4
5
6
7
END

.testcase 130.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 130.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 130.6
PRAGMA foreign_key_check;
.check ''
#-------------------------------------------------------------------------
# An index on the child key with the wrong collation sequence
# (line 560 false).
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
CREATE INDEX ckn ON c(k COLLATE nocase);
CREATE INDEX nkn ON n(k COLLATE nocase);
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (5, 'ABC'), (6, 'abc  ');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 140.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SCAN n USING COVERING INDEX nkn
`--SCAN c USING COVERING INDEX ckn
END

.testcase 140.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 0

.testcase 140.3
DELETE FROM p WHERE code='1001';
SELECT id FROM c ORDER BY id;
.check <<END
3
4
5
6
7
END

.testcase 140.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 140.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 140.6
PRAGMA foreign_key_check;
.check ''
#-------------------------------------------------------------------------
# A suitable index on the child key (all three conditions true).
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
CREATE INDEX ck ON c(k);
CREATE INDEX nk ON n(k);
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (5, 'ABC'), (6, 'abc  ');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 150.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH n USING COVERING INDEX nk (k=?)
`--SEARCH c USING COVERING INDEX ck (k=?)
END

.testcase 150.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 1

.testcase 150.3
DELETE FROM p WHERE code='1001';
SELECT id FROM c ORDER BY id;
.check <<END
3
4
5
6
7
END

.testcase 150.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 150.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 150.6
PRAGMA foreign_key_check;
.check ''
#-------------------------------------------------------------------------
# A suitable index, with an explicit BINARY collation in a different
# case. Collation sequence names are compared without regard to case.
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
CREATE INDEX ck ON c(k COLLATE BiNaRy);
CREATE INDEX nk ON n(k COLLATE BiNaRy);
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (5, 'ABC'), (6, 'abc  ');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 160.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH n USING COVERING INDEX nk (k=?)
`--SEARCH c USING COVERING INDEX ck (k=?)
END

.testcase 160.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 1

.testcase 160.3
DELETE FROM p WHERE code='1001';
SELECT id FROM c ORDER BY id;
.check <<END
3
4
5
6
7
END

.testcase 160.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 160.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 160.6
PRAGMA foreign_key_check;
.check ''
#-------------------------------------------------------------------------
# Several unsuitable indexes, for each of the three reasons, followed
# by a suitable one. The loop in fkChildKeyIsIndexed() must skip each
# unsuitable index before it finds the suitable one.
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
CREATE INDEX ck ON c(k);
CREATE INDEX nk ON n(k);
CREATE INDEX cz ON c(z);
CREATE INDEX nz ON n(z);
CREATE INDEX czk ON c(z, k);
CREATE INDEX nzk ON n(z, k);
CREATE INDEX ckp ON c(k) WHERE z>0;
CREATE INDEX nkp ON n(k) WHERE z>0;
CREATE INDEX ckn ON c(k COLLATE nocase);
CREATE INDEX nkn ON n(k COLLATE nocase);
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (5, 'ABC'), (6, 'abc  ');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 170.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH n USING COVERING INDEX nk (k=?)
`--SEARCH c USING COVERING INDEX ck (k=?)
END

.testcase 170.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 1

.testcase 170.3
DELETE FROM p WHERE code='1001';
SELECT id FROM c ORDER BY id;
.check <<END
3
4
5
6
7
END

.testcase 170.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 170.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 170.6
PRAGMA foreign_key_check;
.check ''
#-------------------------------------------------------------------------
# Several unsuitable indexes, for each of the three reasons, and no
# suitable one.
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
CREATE INDEX cz ON c(z);
CREATE INDEX nz ON n(z);
CREATE INDEX czk ON c(z, k);
CREATE INDEX nzk ON n(z, k);
CREATE INDEX ckp ON c(k) WHERE z>0;
CREATE INDEX nkp ON n(k) WHERE z>0;
CREATE INDEX ckn ON c(k COLLATE nocase);
CREATE INDEX nkn ON n(k COLLATE nocase);
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (5, 'ABC'), (6, 'abc  ');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 180.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SCAN n USING COVERING INDEX nkn
`--SCAN c USING COVERING INDEX ckn
END

.testcase 180.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 0

.testcase 180.3
DELETE FROM p WHERE code='1001';
SELECT id FROM c ORDER BY id;
.check <<END
3
4
5
6
7
END

.testcase 180.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 180.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 180.6
PRAGMA foreign_key_check;
.check ''
#-------------------------------------------------------------------------
# A parent key with NOCASE collation. An index on the child key with
# BINARY collation is not suitable (line 560 false).
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT COLLATE NOCASE UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
CREATE INDEX ck ON c(k);
CREATE INDEX nk ON n(k);
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (6, 'abc  ');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 200.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SCAN n USING COVERING INDEX nk
`--SCAN c USING COVERING INDEX ck
END

.testcase 200.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 0

.testcase 200.3
DELETE FROM p WHERE code='ABC';
SELECT id FROM c ORDER BY id;
.check <<END
1
2
3
4
7
END

.testcase 200.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 200.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 200.6
PRAGMA foreign_key_check;
.check ''
#-------------------------------------------------------------------------
# A parent key with NOCASE collation and an index on the child key
# with the same collation, named in a different case (line 560 true).
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT COLLATE NOCASE UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
CREATE INDEX ckn ON c(k COLLATE nocase);
CREATE INDEX nkn ON n(k COLLATE nocase);
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (6, 'abc  ');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 210.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH n USING COVERING INDEX nkn (k=?)
`--SEARCH c USING COVERING INDEX ckn (k=?)
END

.testcase 210.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 1

.testcase 210.3
DELETE FROM p WHERE code='ABC';
SELECT id FROM c ORDER BY id;
.check <<END
1
2
3
4
7
END

.testcase 210.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 210.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 210.6
PRAGMA foreign_key_check;
.check ''
#-------------------------------------------------------------------------
# A parent key with RTRIM collation. Both an unsuitable index (NOCASE)
# and a suitable one (RTRIM).
#
.open :memory:
PRAGMA foreign_keys=ON;
CREATE TABLE p(id INTEGER PRIMARY KEY, code TEXT COLLATE RTRIM UNIQUE);
CREATE TABLE c(id INTEGER PRIMARY KEY, z,
  k REFERENCES p(code) ON DELETE CASCADE
);
CREATE TABLE n(id INTEGER PRIMARY KEY, z, k REFERENCES p(code));
CREATE INDEX ckn ON c(k COLLATE nocase);
CREATE INDEX nkn ON n(k COLLATE nocase);
CREATE INDEX ckr ON c(k COLLATE rtrim);
CREATE INDEX nkr ON n(k COLLATE rtrim);
INSERT INTO p VALUES(1, '1001'), (2, '01001'), (3, 'abc'), (4, '1001.0'),
  (5, 'ABC');
INSERT INTO c(id, z, k) VALUES
  (1, 1, 1001), (2, 2, '1001'), (3, 3, '01001'),
  (4, 4, 1001.0), (5, 5, 'abc'), (6, 6, 'ABC'), (7, 7, 'abc  ');

.testcase 220.1
EXPLAIN QUERY PLAN DELETE FROM p WHERE id=1;
.check <<END
QUERY PLAN
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH n USING COVERING INDEX nkr (k=?)
`--SEARCH c USING COVERING INDEX ckr (k=?)
END

.testcase 220.2
SELECT count(*)>0 FROM bytecode('DELETE FROM p WHERE id=1')
 WHERE opcode='Cast' AND subprog IS NOT NULL;
.check 1

.testcase 220.3
DELETE FROM p WHERE code='abc';
SELECT id FROM c ORDER BY id;
.check <<END
1
2
3
4
6
END

.testcase 220.4
INSERT INTO n(id, z, k) VALUES(1, 1, 1001.0);
DELETE FROM p WHERE code='1001.0';
.check --glob "*: FOREIGN KEY constraint failed"

.testcase 220.5
DELETE FROM p WHERE code<>'1001.0';
SELECT count(*) FROM p;
.check 1

.testcase 220.6
PRAGMA foreign_key_check;
.check ''
