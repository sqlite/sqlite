#!sqlite3
#
# 2026-07-29
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
# Test cases for the sha1(), sha1b() functions.
#
.mode list -reset
.testcase 100
SELECT sha1('this is a test');
.check fa26be19de6bff93f70bc2308434e4a440bbad02
.testcase 110
SELECT hex(sha1b('this is a test'));
.check FA26BE19DE6BFF93F70BC2308434E4A440BBAD02
.testcase 120
SELECT sha1(x'31323334353637383930');
.check 01b307acba4f54f55aafc33bb06bbbf6ca803e9a
.testcase 130
SELECT sha1(12345*100000 + 67890);
.check 01b307acba4f54f55aafc33bb06bbbf6ca803e9a
.testcase 140
SELECT sha1('');
.check da39a3ee5e6b4b0d3255bfef95601890afd80709
.testcase 150
SELECT sha1(x'');
.check da39a3ee5e6b4b0d3255bfef95601890afd80709

.testcase 200
CREATE TABLE t1(x TEXT);
INSERT INTO t1 VALUES('SELECT sha1_query((SELECT x FROM t1))');
.check ''
.testcase 210
SELECT sha1_query((SELECT x FROM t1));
.check -glob '*recursive use of sha1_query()*'

.testcase 300
DELETE FROM t1;
INSERT INTO t1 VALUES('SELECT sha3_query((SELECT x FROM t1))');
.check ''
.testcase 301
SELECT sha3_query((SELECT x FROM t1));
.check -glob '*recursive use of sha3_query()*'
