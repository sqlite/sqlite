#!sqlite3
#
# 2025-11-12
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
# Test cases for the ".mode" command of the CLI.
# To run these tests:
#
#   ./sqlite3 test/modeA.sql
#
#
.open :memory:
CREATE TABLE t1(a,b,c,d,e);
INSERT INTO t1 VALUES(1,2.5,'three',x'4444',NULL);
INSERT INTO t1 SELECT b,c,d,e,a FROM t1;
INSERT INTO t1 SELECT d,e,a,b,c FROM t1;
.mode box

.testcase 100
SELECT * FROM t1;
.check <<END
╭─────┬───────┬───────┬───────┬───────╮
│  a  │   b   │   c   │   d   │   e   │
╞═════╪═══════╪═══════╪═══════╪═══════╡
│   1 │   2.5 │ three │ DD    │       │
│ 2.5 │ three │ DD    │       │     1 │
│ DD  │       │     1 │   2.5 │ three │
│     │     1 │   2.5 │ three │ DD    │
╰─────┴───────┴───────┴───────┴───────╯
END

.testcase 110
.mode --null xyz
SELECT * FROM t1;
.check <<END
╭─────┬───────┬───────┬───────┬───────╮
│  a  │   b   │   c   │   d   │   e   │
╞═════╪═══════╪═══════╪═══════╪═══════╡
│   1 │   2.5 │ three │ DD    │ xyz   │
│ 2.5 │ three │ DD    │ xyz   │     1 │
│ DD  │ xyz   │     1 │   2.5 │ three │
│ xyz │     1 │   2.5 │ three │ DD    │
╰─────┴───────┴───────┴───────┴───────╯
END

# Default output mode is qbox --quote relaxed
#
.mode tty --wrap 10
CREATE TABLE t2(a,b,c,d);
INSERT INTO t2 VALUES(1,2.5,'three',x'4444');
INSERT INTO t2 VALUES('The quick fox jumps over the lazy brown dog',2,3,4);
INSERT INTO t2 VALUES('10','', -1.25,NULL);
INSERT INTO t2 VALUES('a,b,c','"Double-Quoted"','-1.25','NULL');
.testcase 120
SELECT * FROM t2;
.check <<END
╭────────────┬────────────┬─────────┬─────────╮
│     a      │     b      │    c    │    d    │
╞════════════╪════════════╪═════════╪═════════╡
│          1 │        2.5 │ three   │ x'4444' │
├────────────┼────────────┼─────────┼─────────┤
│ The quick  │          2 │       3 │       4 │
│ fox jumps  │            │         │         │
│ over the   │            │         │         │
│ lazy brown │            │         │         │
│ dog        │            │         │         │
├────────────┼────────────┼─────────┼─────────┤
│ '10'       │            │   -1.25 │ NULL    │
├────────────┼────────────┼─────────┼─────────┤
│ a,b,c      │ "Double-   │ '-1.25' │ 'NULL'  │
│            │ Quoted"    │         │         │
╰────────────┴────────────┴─────────┴─────────╯
END
.testcase 121
.mode tty --wrap 1 --limits off
SELECT 'xyz123' AS a, 2 AS b;
.check <<END
╭───┬───╮
│ a │ b │
╞═══╪═══╡
│ x │ 2 │
│ y │   │
│ z │   │
│ 1 │   │
│ 2 │   │
│ 3 │   │
╰───┴───╯
END
.testcase 130
.mode tty -wrap 10
.mode
.check <<END
.mode qbox --limits on --quote relaxed --sw auto --textjsonb on
END
.testcase 140
.mode -v
.check <<END
.mode qbox --align "" --border on --blob-quote auto --colsep "" --escape auto --intfmt default --limits on --multiinsert 3000 --null "NULL" --quote relaxed --realfmt default --rowcount off --rowsep "" --sw auto --tablename "" --textjsonb on --titles always --widths "" --wordwrap off --wrap 10
END
.testcase 150 --error-prefix "Error:"
.mode foo
.check <<END
Error: .mode foo
Error:       ^--- unknown mode
Error: Use ".help .mode" for more info
END

.testcase 160
.mode --null xyzzy -v
.output -glob ' --null "xyzzy"'
.testcase 170
.mode -null abcde -v
.output -glob ' --null "abcde"'

# Test cases for the ".explain off" command
.mode box -reset
.testcase 180
EXPLAIN SELECT * FROM t1;
.output --notglob *────* --keep
.output --notglob "* id │ parent │ notused │ detail *" --keep
.output --glob "*   Init  *"
.testcase 190
EXPLAIN QUERY PLAN SELECT * FROM t1;
.output --glob "*`--SCAN *"
.explain off
.testcase 200
EXPLAIN SELECT * FROM t1;
.output --glob *────*
.testcase 210
EXPLAIN QUERY PLAN SELECT * FROM t1;
.output --glob "* id │ parent │ notused │ detail *"
.explain auto

# Test cases for limit settings in the .mode command.
.testcase 300
.mode box --reset
.mode
.check <<END
.mode box
END
.testcase 310
.mode --limits 5,300,20
.mode
.check <<END
.mode box --limits on
END
.testcase 320
.mode --limits 5,300,19
.mode
.check <<END
.mode box --limits 5,300,19
END
.testcase 330
.mode --limits 0,0,0
.mode -v
.check <<END
.mode box --align "" --border on --blob-quote auto --colsep "" --escape auto --intfmt default --limits off --multiinsert 0 --null "" --quote off --realfmt default --rowcount off --rowsep "" --sw 0 --tablename "" --textjsonb off --titles always --widths "" --wordwrap off
END

.testcase 400
.mode --linelimit 123
.mode
.check <<END
.mode box --limits 123,0,0
END

.testcase 410
.mode --linelimit 0 -charlimit 123
.mode
.check <<END
.mode box --limits 0,123,0
END

.testcase 420
.mode --charlimit 0 -titlelimit 123
.mode
.check <<END
.mode box --limits 0,0,123
END

.testcase 430
.mode list
.mode
.check <<END
.mode list
END

.testcase 440
.mode -limits 0,123,0
.mode
.check <<END
.mode list --limits 0,123,0
END

.testcase 450
.mode -limits 123,0,0
.mode
.check <<END
.mode list
END

# --titlelimit functionality
#
.testcase 500
.mode line --limits off --titlelimit 20
SELECT a AS 'abcdefghijklmnopqrstuvwxyz', b FROM t2 WHERE c=3;
.check <<END
abcdefghijklmnopq...: The quick fox jumps over the lazy brown dog
                   b: 2
END
.testcase 510
.mode line --titlelimit 10
SELECT a AS 'abcdefghijklmnopqrstuvwxyz', b FROM t2 WHERE c=3;
.check <<END
abcdefg...: The quick fox jumps over the lazy brown dog
         b: 2
END
.testcase 520
.mode line --titlelimit 2
SELECT a AS 'abcdefghijklmnopqrstuvwxyz', b FROM t2 WHERE c=3;
.check <<END
ab: The quick fox jumps over the lazy brown dog
 b: 2
END
.testcase 530
.mode line --titlelimit 4
SELECT a AS 'abcd', b FROM t2 WHERE c=3;
.check <<END
abcd: The quick fox jumps over the lazy brown dog
   b: 2
END
.testcase 540
.mode line --titlelimit 3
SELECT a AS 'abcd', b FROM t2 WHERE c=3;
.check <<END
...: The quick fox jumps over the lazy brown dog
  b: 2
END

# line --screenwidth and --colsep
#
.testcase 550
.mode line --sw 40 --colsep ":-hi-:"
SELECT a AS 'abc', b FROM t2 WHERE c=3;
.check <<END
abc:-hi-:The quick fox jumps over the
         lazy brown dog
  b:-hi-:2
END
.testcase 551
.mode line --sw 40 --colsep ":-hi-:" --wordwrap off
SELECT a AS 'abc', b FROM t2 WHERE c=3;
.check <<END
abc:-hi-:The quick fox jumps over the la
         zy brown dog
  b:-hi-:2
END
# 23456789 123456789 123456789 123456789

# https://sqlite.org/forum/forumpost/2025-12-31T19:14:24z
#
# For legacy compatibility, ".header" settings are not changed
# by ".mode" unless the --title or --reset option is used on .mode.
#
.testcase 600
DROP TABLE IF EXISTS t1;
CREATE TABLE t1(a,b,c);
INSERT INTO t1 VALUES(1,2,3);
.header on
.mode csv
SELECT * FROM t1;
.check --glob a,b,c*

.testcase 610
.mode csv -reset
SELECT * FROM t1;
.check 1,2,3

.testcase 620
.mode tty
.mode csv
.header on
SELECT * FROM t1;
.check --glob a,b,c*

.testcase 630
.mode tty
.mode csv --title on
SELECT * FROM t1;
.check --glob a,b,c*
.testcase 631
.mode tty
.mode csv --title off
SELECT * FROM t1;
.check 1,2,3

# Verification of claims about .insert mode in the climode.html
# documentation.
.testcase 700
CREATE TABLE tbl1(one,two);
INSERT INTO tbl1 VALUES('hello!',10),('goodbye',20);
.mode insert new_table --multiinsert 0
SELECT * FROM tbl1;
.check <<END
INSERT INTO new_table VALUES('hello!',10);
INSERT INTO new_table VALUES('goodbye',20);
END
.testcase 710
.mode insert new_table --titles on
SELECT * FROM tbl1;
.check <<END
INSERT INTO new_table(one,two) VALUES('hello!',10);
INSERT INTO new_table(one,two) VALUES('goodbye',20);
END
.testcase 720
.mode insert new_table --titles off
SELECT * FROM tbl1;
.check <<END
INSERT INTO new_table VALUES('hello!',10);
INSERT INTO new_table VALUES('goodbye',20);
END
.testcase 730
.mode insert new_table --titles always --rowcount on
SELECT * FROM tbl1;
.check <<END
INSERT INTO new_table(one,two) VALUES('hello!',10);
INSERT INTO new_table(one,two) VALUES('goodbye',20);
/* 2 rows inserted */
END
.testcase 740
.mode insert new_table --titles always --rowcount on
SELECT * FROM tbl1 WHERE two<0;
.check <<END
/* 0 rows inserted */
END
.testcase 750
.mode insert new_table --titles always --rowcount on
SELECT * FROM tbl1 WHERE two<15;
.check <<END
INSERT INTO new_table(one,two) VALUES('hello!',10);
/* 1 row inserted */
END

# QRF reports an error if the string is too big.
#
.testcase 800
.mode box
.limit length 1000
WITH c(n) AS (VALUES(1) UNION ALL SELECT n+1 FROM c WHERE n<100)
SELECT hex(randomblob(100)) c;
.check -glob "*: string or blob too big"
.limit length 10000000

# "psql" mode.
#
.testcase 900
.mode --reset psql -v
.check <<END
.mode psql --align "" --border off --blob-quote auto --colsep "" --escape auto --intfmt default --limits off --multiinsert 0 --null "" --quote off --realfmt default --rowcount on --rowsep "" --sw 0 --tablename "" --textjsonb off --titles always --widths "" --wordwrap off
END
.testcase 901
.mode
.check <<END
.mode psql
END
.testcase 902
.mode --rowcount off
.mode
.check <<END
.mode psql --rowcount off
END
.testcase 910
.mode psql --reset
DROP TABLE IF EXISTS t1;
CREATE TABLE t1(ab INT, text_column TEXT, int_col INT);
SELECT * FROM t1;
.check <<END
 ab | text_column | int_col
----+-------------+---------
(0 rows)
END
.testcase 911
INSERT INTO t1 VALUES(31415926,'Hello',99);
SELECT * FROM t1;
.check <<END
    ab    | text_column | int_col
----------+-------------+---------
 31415926 | Hello       |      99
(1 row)
END
.testcase 912
INSERT INTO t1 VALUES(2,NULL,2);
SELECT * FROM t1;
.check <<END
    ab    | text_column | int_col
----------+-------------+---------
 31415926 | Hello       |      99
        2 |             |       2
(2 rows)
END

# Variations on ".mode -title"
#
DROP TABLE IF EXISTS t1;
CREATE TABLE t1("a b", "c'""<""'d", "xyz", "123");
.testcase 1000
.mode box -reset -title off -title plain
SELECT * FROM t1;
.check <<END
╭─────┬─────────┬─────┬─────╮
│ a b │ c'"<"'d │ xyz │ 123 │
╘═════╧═════════╧═════╧═════╛
END
.testcase 1001
.mode -title off -title sql
SELECT * FROM t1;
.check <<END
╭───────┬─────────────┬───────┬───────╮
│ 'a b' │ 'c''"<"''d' │ 'xyz' │ '123' │
╘═══════╧═════════════╧═══════╧═══════╛
END
.testcase 1002
.mode -title off -title csv
SELECT * FROM t1;
.check <<END
╭───────┬─────────────┬─────┬─────╮
│ "a b" │ "c'""<""'d" │ xyz │ 123 │
╘═══════╧═════════════╧═════╧═════╛
END
.testcase 1003
.mode -title off -title html
SELECT * FROM t1;
.check <<END
╭─────┬──────────────────────────────┬─────┬─────╮
│ a b │ c&#39;&quot;&lt;&quot;&#39;d │ xyz │ 123 │
╘═════╧══════════════════════════════╧═════╧═════╛
END
.testcase 1003b
.mode -title off -title html -title off --title always
SELECT * FROM t1;
.check <<END
╭─────┬──────────────────────────────┬─────┬─────╮
│ a b │ c&#39;&quot;&lt;&quot;&#39;d │ xyz │ 123 │
╘═════╧══════════════════════════════╧═════╧═════╛
END
.testcase 1003c
.show
.check --glob "*headers: always*"
.testcase 1004
.mode -title off -title tcl
SELECT * FROM t1;
.check <<END
╭───────┬─────────────┬───────┬───────╮
│ "a b" │ "c'\"<\"'d" │ "xyz" │ "123" │
╘═══════╧═════════════╧═══════╧═══════╛
END
.testcase 1005
.mode -title off -title json
SELECT * FROM t1;
.check <<END
╭───────┬─────────────┬───────┬───────╮
│ "a b" │ "c'\"<\"'d" │ "xyz" │ "123" │
╘═══════╧═════════════╧═══════╧═══════╛
END
.testcase 1006
.mode -title on -title json
SELECT * FROM t1;
.check ""
.testcase 1007
.mode -title on -title json -title auto
SELECT * FROM t1;
.check <<END
╭─────┬─────────┬─────┬─────╮
│ a b │ c'"<"'d │ xyz │ 123 │
╘═════╧═════════╧═════╧═════╛
END
.testcase 1008
.mode -title off -title json
.headers always
SELECT * FROM t1;
.check <<END
╭─────┬─────────┬─────┬─────╮
│ a b │ c'"<"'d │ xyz │ 123 │
╘═════╧═════════╧═════╧═════╛
END
