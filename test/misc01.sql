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

# f945de7c4eb73732 2026-11-06T04:16:54Z
# sqlite3's .recover output can smuggle shell commands into the next
# sqlite3 run
#
.testcase 400
.open --hexdb
| size 8192 pagesize 4096 filename unk.db
| page 1 offset 0
|      0: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00   SQLite format 3.
|     16: 10 00 01 01 00 40 20 20 00 00 00 00 00 00 00 02   .....@  ........
|     32: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04   ................
|     48: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00   ................
|     96: 00 00 00 00 0d 00 00 00 01 0f ca 00 0f ca 00 00   ................
|   4032: 00 00 00 00 00 00 00 00 00 00 34 01 06 17 11 11   ..........4.....
|   4048: 01 55 74 61 62 6c 65 74 31 74 31 02 43 52 45 41   .Utablet1t1.CREA
|   4064: 54 45 20 54 41 42 4c 45 20 74 31 28 61 2c 62 2c   TE TABLE t1(a,b,
|   4080: 63 29 3b 0a 2e 70 72 69 6e 74 20 68 65 6c 6c 6f   c);..print hello
| page 2 offset 4096
|      0: 0d 00 00 00 00 10 00 00 00 00 00 00 00 00 00 00   ................
| end unk.db
.recover
.check <<END
.dbconfig defensive off
BEGIN;
PRAGMA writable_schema = on;
PRAGMA foreign_keys = off;
PRAGMA encoding = 'UTF-8';
PRAGMA page_size = '4096';
PRAGMA auto_vacuum = '0';
PRAGMA user_version = '0';
PRAGMA application_id = '0';
CREATE TABLE t1(a,b,c);
PRAGMA writable_schema = off;
COMMIT;
END

# 959866cfe4ec0e16 2026-11-06T00:25:13Z 
# Wrong result: c0 GLOB 'ab?' OR c0 GLOB 'ab*' on an indexed column
# omits BLOB rows
#
.open :memory:
.mode -reset qbox
.testcase 500
CREATE TABLE t0(c0, c1);
INSERT INTO t0 VALUES('ab',1),('abc',2),('abd',3),(x'6162',4),
                     (x'616263',5),('b',6);
CREATE INDEX i0 ON t0(c0);
.check ''

.testcase 510
SELECT c1, c0 FROM t0
 WHERE c0 GLOB 'ab*'
 ORDER BY c1;
.check <<*END*
╭────┬───────────╮
│ c1 │    c0     │
╞════╪═══════════╡
│  1 │ 'ab'      │
│  2 │ 'abc'     │
│  3 │ 'abd'     │
│  4 │ x'6162'   │
│  5 │ x'616263' │
╰────┴───────────╯
*END*

.testcase 520
SELECT c1, c0 FROM t0
 WHERE c0 GLOB 'ab?' OR c0 GLOB 'ab*'
 ORDER BY c1;
.check <<*END*
╭────┬───────────╮
│ c1 │    c0     │
╞════╪═══════════╡
│  1 │ 'ab'      │
│  2 │ 'abc'     │
│  3 │ 'abd'     │
│  4 │ x'6162'   │
│  5 │ x'616263' │
╰────┴───────────╯
*END*

.testcase 530
SELECT c1, c0 FROM t0
 WHERE c0 GLOB 'ab*' OR c0 GLOB 'ab*'
 ORDER BY c1;
.check <<*END*
╭────┬───────────╮
│ c1 │    c0     │
╞════╪═══════════╡
│  1 │ 'ab'      │
│  2 │ 'abc'     │
│  3 │ 'abd'     │
│  4 │ x'6162'   │
│  5 │ x'616263' │
╰────┴───────────╯
*END*

.testcase 540
SELECT c1, c0 FROM t0 NOT INDEXED
 WHERE c0 GLOB 'ab?' OR c0 GLOB 'ab*' 
 ORDER BY c1;
.check <<*END*
╭────┬───────────╮
│ c1 │    c0     │
╞════╪═══════════╡
│  1 │ 'ab'      │
│  2 │ 'abc'     │
│  3 │ 'abd'     │
│  4 │ x'6162'   │
│  5 │ x'616263' │
╰────┴───────────╯
*END*
