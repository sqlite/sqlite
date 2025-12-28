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
