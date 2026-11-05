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

.testcase 120
SELECT a FROM t WHERE +rowid = sqlar_uncompress(12345.0, 9);
.check 'row 12345'
