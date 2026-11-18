#!sqlite3
#
# 2026-11-18
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
# A LIKE or GLOB prefix ending in U+D7FF must not use the index range
# optimization, because the upper bound would be the surrogate U+D800.
# That is not valid in UTF16BE and became U+FFFD, which admitted
# non-matching rows.
#
#   https://sqlite.org/bugs/info/3947fa76ab
#
# The patterns below contain a literal U+D7FF.  The optimization does not
# apply to patterns built using char().
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '

.testcase 100
PRAGMA encoding='UTF-16be';
PRAGMA case_sensitive_like=1;
CREATE TABLE t1(x TEXT);
INSERT INTO t1 VALUES
  (char(0xd7ff)),
  (char(0xd7ff,0x61)),
  (char(0xd7fe)),
  (char(0xe000)),
  (char(0xffff)),
  (char(0x10000));
CREATE INDEX t1x ON t1(x);
.check ''

.testcase 110
SELECT printf('U+%04X', unicode(x)) FROM t1
 WHERE x LIKE '퟿%';
.check 'U+D7FF U+D7FF'

.testcase 120
SELECT printf('U+%04X', unicode(x)) FROM t1 NOT INDEXED
 WHERE x LIKE '퟿%';
.check 'U+D7FF U+D7FF'

.testcase 130
SELECT printf('U+%04X', unicode(x)) FROM t1
 WHERE x GLOB '퟿*';
.check 'U+D7FF U+D7FF'

.testcase 140
SELECT printf('U+%04X', unicode(x)) FROM t1 NOT INDEXED
 WHERE x GLOB '퟿*';
.check 'U+D7FF U+D7FF'

.testcase 150
SELECT printf('U+%04X', unicode(x)) FROM t1
 WHERE x LIKE '퟿_';
.check 'U+D7FF'

.testcase 160
SELECT printf('U+%04X', unicode(x)) FROM t1
 WHERE x LIKE '퟿%' ESCAPE '^';
.check 'U+D7FF U+D7FF'

