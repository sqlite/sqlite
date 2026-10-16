#!sqlite3
#
# 2026-10-09
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
# Test cases for error handling in dot-commands.
#
#   ./sqlite3 test/clierrors.sql
#
#
.testcase 100
.import file table extra
.check --glob <<END
*: .import file table extra
*:                    ^--- unknown argument
END
.testcase 110
.import file table --schema
.check --glob <<END
*: .import file table --schema
*:                    ^--- missing argument
END
.testcase 120
.import file table --skip
.check --glob <<END
*: .import file table --skip
*:                    ^--- missing argument
END
.testcase 130
.import --esc
.check --glob <<END
*: .import --esc
*:         ^--- missing argument
END
.testcase 140
.import --qesc
.check --glob <<END
*: .import --qesc
*:         ^--- missing argument
END
.testcase 150
.import --rowsep
.check --glob <<END
*: .import --rowsep
*:         ^--- missing argument
END
.testcase 160
.import --colsep
.check --glob <<END
*: .import --colsep
*:         ^--- missing argument
END
.testcase 170
.import --xyzzy
.check --glob <<END
*: .import --xyzzy
*:         ^--- unknown option
END
.testcase 180
.import --colsep hi
.check --glob <<END
*: Missing FILE argument
END
