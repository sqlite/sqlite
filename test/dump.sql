#!sqlite3
#
# 2026-10-20
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
# Test cases for the .dump CLI command
#


.if NOT capable('fkey') OR NOT capable('vtab')
.testcase setup
.check ''
.exit
.endif

.open :memory:
.testcase 100
CREATE TABLE customer(RowID TEXT, name TEXT);
INSERT INTO customer VALUES('C-1001','Ann');
.dump --preserve-rowids
.check <<END
PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE customer(RowID TEXT, name TEXT);
INSERT INTO customer(_rowid_,RowID,name) VALUES(1,'C-1001','Ann');
COMMIT;
END
