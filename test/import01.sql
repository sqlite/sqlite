#!sqlite3
#
# 2025-12-28
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
# Test cases for the ".import" command of the CLI.
# To run these tests:
#
#   ./sqlite3 test/import01.sql
#

.testcase setup
.open :memory:
.mode tty
.check ''

.testcase 100
CREATE TABLE t1(a,b,c);
.import -csv <<END t1
111,222,333
abc,def,ghi
END
SELECT * FROM t1;
.check <<END
╭───────┬───────┬───────╮
│   a   │   b   │   c   │
╞═══════╪═══════╪═══════╡
│ '111' │ '222' │ '333' │
│ abc   │ def   │ ghi   │
╰───────┴───────┴───────╯
END

.testcase 110
DELETE FROM t1;
.import -colsep ";" <<END t1
this;is a;test
one;two;three
END
SELECT * FROM t1;
.check <<END
╭──────┬──────┬───────╮
│  a   │  b   │   c   │
╞══════╪══════╪═══════╡
│ this │ is a │ test  │
│ one  │ two  │ three │
╰──────┴──────┴───────╯
END

.testcase 120
DELETE FROM t1;
.import -colsep "," -rowsep ';' <<END t1
this,is a,test;one,two,three;
END
SELECT * FROM t1;
.check <<END
╭──────┬──────┬───────╮
│  a   │  b   │   c   │
╞══════╪══════╪═══════╡
│ this │ is a │ test  │
│ one  │ two  │ three │
╰──────┴──────┴───────╯
END

.testcase 130
DELETE FROM t1;
.import -csv -colsep "," -rowsep "\n" <<END t1
this,"is a","test ""with quotes"""
"second",,"line"
END
SELECT * FROM t1;
.check <<END
╭────────┬──────┬────────────────────╮
│   a    │  b   │         c          │
╞════════╪══════╪════════════════════╡
│ this   │ is a │ test "with quotes" │
│ second │      │ line               │
╰────────┴──────┴────────────────────╯
END
.testcase 131
DELETE FROM t1;
.import -ascii -colsep "," -rowsep "\n" <<END t1
this,"is a","test ""with quotes"""
"second",,"line"
END
SELECT * FROM t1;
.check <<END
╭──────────┬────────┬────────────────────────╮
│    a     │   b    │           c            │
╞══════════╪════════╪════════════════════════╡
│ this     │ "is a" │ "test ""with quotes""" │
│ "second" │        │ "line"                 │
╰──────────┴────────┴────────────────────────╯
END

.testcase 140
DROP TABLE t1;
.import -csv <<END t1
"abc","def","xy z"
"This","is","a"
"test","...",
END
SELECT * FROM t1;
.check <<END
╭──────┬─────┬──────╮
│ abc  │ def │ xy z │
╞══════╪═════╪══════╡
│ This │ is  │ a    │
│ test │ ... │      │
╰──────┴─────┴──────╯
END
.testcase 141
SELECT sql FROM sqlite_schema WHERE name='t1';
.check <<END
╭───────────────────────────────────╮
│                sql                │
╞═══════════════════════════════════╡
│ CREATE TABLE "t1"(                │
│ "abc" ANY, "def" ANY, "xy z" ANY) │
╰───────────────────────────────────╯
END

.testcase 150
DROP TABLE t1;
.import -csv -v <<END t1
"abc","def","xy z"
"This","is","a"
"test","...",
END
SELECT * FROM t1;
.check <<END
Column separator ",", row separator "\n"
CREATE TABLE "main"."t1"(
"abc" ANY, "def" ANY, "xy z" ANY)

Added 2 rows with 0 errors using 3 lines of input
╭──────┬─────┬──────╮
│ abc  │ def │ xy z │
╞══════╪═════╪══════╡
│ This │ is  │ a    │
│ test │ ... │      │
╰──────┴─────┴──────╯
END

.testcase 160
DROP TABLE t1;
.import -csv -schema TEMP <<END t2
"x"
"abcdef"
END
SELECT * FROM t2;
.tables
.check <<END
╭────────╮
│   x    │
╞════════╡
│ abcdef │
╰────────╯
temp.t2
END

.testcase 170
.import -csv -skip 2 <<END t3
a,b,c
d,e,f
g,h,i
j,k,l
m,n,o
END
SELECT * FROM t3;
.check <<END
╭───┬───┬───╮
│ g │ h │ i │
╞═══╪═══╪═══╡
│ j │ k │ l │
│ m │ n │ o │
╰───┴───┴───╯
END

.testcase 180
DELETE FROM t3;
.import -csv -skip 7 <<END t3
a,b,c
d,e,f
g,h,i
j,k,l
m,n,o
END
SELECT * FROM t3;
.check <<END
END

.testcase 200 --error-prefix ERROR:
.import -csv
.check 'ERROR: Missing FILE argument'
.testcase 201
.import -csv file1.csv
.check 'ERROR: Missing TABLE argument'
.testcase --error-prefix test-error: 202
.import -csvxyzzy file1.csv
.check <<END
test-error: .import -csvxyzzy file1.csv
test-error:         ^--- unknown option
END
.testcase 203
.import -csv file1.csv t4 -colsep
.check <<END
test-error: .import -csv file1.csv t4 -colsep
test-error:       missing argument ---^
END
