#!sqlite3
#
# 2025-12-16
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
# Test cases for the .imposter command.
#
.mode box -reset
.testcase 100
CREATE TABLE t1(a INTEGER PRIMARY KEY, b TEXT, c INT);
INSERT INTO t1 VALUES(1,'two',3),(4,'five',6);
CREATE INDEX t1bc ON t1(b,c);
.imposter T1BC x1
----------^^^^--- Different case that the original
SELECT * FROM x1;
.check <<END
CREATE TABLE "x1"("b","c","_ROWID_",PRIMARY KEY("b","c","_ROWID_"))WITHOUT ROWID;
╭──────┬───┬─────────╮
│  b   │ c │ _ROWID_ │
╞══════╪═══╪═════════╡
│ five │ 6 │       4 │
│ two  │ 3 │       1 │
╰──────┴───┴─────────╯
END
