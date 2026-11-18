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


#***********************************************************************
#
# An index on a generated column must not change the type of a matching
# expression elsewhere in the query.  The index affinity string lumps all
# numeric affinities together, so a REAL column could replace an integer
# expression, or the reverse.
#
#   https://sqlite.org/bugs/info/c667229a15161954
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '

.testcase 200
CREATE TABLE t2(
  v INT,
  g REAL AS (CAST(v AS INT)) VIRTUAL,
  h INT AS (CAST(v AS REAL)) VIRTUAL,
  r REAL AS (v*1.0) VIRTUAL
);
INSERT INTO t2(v) VALUES(9);
CREATE INDEX t2g ON t2(g);
CREATE INDEX t2h ON t2(h);
CREATE INDEX t2r ON t2(r);
.check ''

.testcase 210
SELECT CAST(v AS INT), typeof(CAST(v AS INT)) FROM t2 NOT INDEXED WHERE g>0;
.check '9 integer'

.testcase 220
SELECT CAST(v AS INT), typeof(CAST(v AS INT)) FROM t2 WHERE g>0;
.check '9 integer'

.testcase 230
SELECT CAST(v AS REAL), typeof(CAST(v AS REAL)) FROM t2 NOT INDEXED WHERE h>0;
.check '9.0 real'

.testcase 240
SELECT CAST(v AS REAL), typeof(CAST(v AS REAL)) FROM t2 WHERE h>0;
.check '9.0 real'

.testcase 250
SELECT v*1.0, typeof(v*1.0) FROM t2 WHERE r>0;
.check '9.0 real'

#***********************************************************************
# 2026-11-06T00:28:19Z
# The fullkey and path columns of json_each() and json_tree() must escape
# a double-quote character in an object label.  A label written in JSON5
# using single quotes can contain a raw double-quote, which must not be
# copied into the path unchanged.
#
#   https://sqlite.org/bugs/info/53eb99b97a309be9
#
.open :memory:
.mode -reset list -colsep ' ' -rowsep ' '
.testcase 500
SELECT fullkey FROM json_each('{''a"b'':1}');
.check '$."a\"b"'
.testcase 505
SELECT json_extract('{''a"b'':1}', fullkey)
  FROM json_each('{''a"b'':1}');
.check '1'
.testcase 510
SELECT fullkey FROM json_each('{"a\"b":1}');
.check '$."a\"b"'
.testcase 515
SELECT fullkey, path FROM json_tree('{''a"b'':[{''c"d'':2}]}')
 WHERE atom IS 2 OR key='a"b';
.check '$."a\"b" $ $."a\"b"[0]."c\"d" $."a\"b"[0]'
.testcase 520
SELECT json_extract('{''a"b'':[{''c"d'':2}]}', fullkey)
  FROM json_tree('{''a"b'':[{''c"d'':2}]}')
 WHERE atom=2;
.check '2'
.testcase 525
SELECT fullkey FROM json_each('{''a\"b'':1}');
.check '$."a\"b"'
.testcase 530
SELECT fullkey FROM json_each('{''a\\b"c'':1}');
.check '$."a\\b\"c"'
.testcase 535
SELECT json_extract('{''a\\b"c'':1}', fullkey)
  FROM json_each('{''a\\b"c'':1}');
.check '1'
.testcase 540
SELECT fullkey FROM json_each('{''"'':1}');
.check '$."\""'
.testcase 545
SELECT fullkey FROM json_each(jsonb('{''a"b'':1}'));
.check '$."a\"b"'
.testcase 550
SELECT fullkey FROM json_each('{''ab'':1,''a b'':2}');
.check '$.ab $."a b"'
.testcase 555
SELECT fullkey FROM json_each(x'6C3A615C621331');
.check '$."a\\b"'
.testcase 560
SELECT fullkey FROM json_each(x'5C29615C1331');
.check '$."a\\"'
.testcase 565
SELECT fullkey FROM json_each(x'5C28615C1331');
.check '$."a\\"'
