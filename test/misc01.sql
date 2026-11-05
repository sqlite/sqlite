# 7c7dc3813e784515 2026-11-05T18:09:41Z
# OP_SeekRowid converts a key that has both a number and its text from
# the text: assertion, and the wrong row with a short fp_d
# Alperen Keles
#
.open :memory:
.mode -reset list
.testcase 100
CREATE TABLE t(a);
INSERT INTO t(rowid, a) VALUES
  (12340,'row 12340'),
  (12345,'row 12345'),
  (12350,'row 12350');
.dbconfig fp_digits 4
.check

.testcase 110
SELECT a FROM t WHERE rowid = 12345.0;
.check 'row 12345'

# The sqlar_uncompress() function has the special property
# below that the sqlite3_value it returns has both a text
# and an equivalent numeric representation at the same time.
#
.testcase 120
SELECT a FROM t WHERE rowid = sqlar_uncompress(12345.0, 9);
.check 'row 12345'
.testcase 121
SELECT a FROM t WHERE rowid = dualrep(12345);
.check 'row 12345'
.testcase 122
SELECT a FROM t WHERE rowid = dualrep(12345.0);
.check 'row 12345'

.testcase 130
SELECT a FROM t WHERE +rowid = sqlar_uncompress(12345.0, 9);
.check 'row 12345'
.testcase 131
SELECT a FROM t WHERE +rowid = dualrep(12345);
.check 'row 12345'
.testcase 132
SELECT a FROM t WHERE +rowid = dualrep(12345.0);
.check 'row 12345'

# 45bcbf3580 2026-11-05T18:08:21Z
# A function result that carries a number and its text keeps
# the old text after REAL affinity.
#
.testcase 200
.mode quote
CREATE TABLE r(x REAL);
INSERT INTO r VALUES(dualrep(5)) RETURNING x || '';
.check "'5.0'"

.testcase 300
PRAGMA foreign_keys=ON;
CREATE TABLE p(k TEXT UNIQUE);
INSERT INTO p VALUES('5.0');
CREATE TABLE c(x REAL REFERENCES p(k));
INSERT INTO c VALUES(5);
.check ''
.testcase 301
INSERT INTO c VALUES(dualrep(5));
.check ''
.testcase 302
SELECT count(*) FROM c;
.check 2
