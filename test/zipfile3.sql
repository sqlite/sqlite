#!sqlite3
#
# 2026-11-14
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#    May you do good and not evil.
#    May you find forgiveness for yourself and forgive others.
#    May you share freely, never taking more than you give.
#
# Test cases for the ZIPFILE extension.
#

# The .shell command is used to run unix command.  And, of course we need
# the zipfile extension itself available.  If those preconditions are not
# met, do not run these tests.
#
.if NOT capable('unsafe') OR NOT capable('zipfile') OR NOT capable('unix')
.exit
.endif

.open :memory:
.mode quote -quote relaxed
.testcase 100
.shell rm -f test.zip
CREATE VIRTUAL TABLE t1 USING zipfile('test.zip');
INSERT INTO t1(name,data) VALUES('abc.txt','111'),('def.txt','222');
CREATE TABLE t2(x);
INSERT INTO t2 VALUES(readfile('test.zip')),('test.zip');
.check
.testcase 101
SELECT (SELECT name FROM zipfile(t2.x)) FROM t2;
.check abc.txt abc.txt

# At one point, valgrind would show a UAF on the following
.if capable('tryeval')
.open :memory:
.shell rm -f w.zip
.testcase
CREATE VIRTUAL TABLE zw USING zipfile('w.zip');
BEGIN;
INSERT INTO zw(name,data) VALUES('a.txt','abc'),('b.txt','def');
SELECT name, tryeval('COMMIT') FROM zw;
.check a.txt,NULL
.endif /* capable('tryeval') */
