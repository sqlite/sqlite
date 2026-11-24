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
