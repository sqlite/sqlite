#!sqlite3
#
# 2026-10-10
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
# Test cases that only work if the CLI is run in --safe mode.
#
#   ./sqlite3 --safe test/clierrors.sql
#
#
.testcase 100
.mode list --null NULL
SELECT readfile('probe.txt');
.check --glob <<END
*no such function: readfile
  SELECT readfile('probe.txt');
         ^--- error here
END

# Bug 2026-10-10T13:15:05Z
.testcase 110
SELECT data FROM fsdir('probe.txt');
.check --glob '*no such table: fsdir'
