#!sqlite3
#
# 2026-03-01
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
# Bug report sqlite.org/forum/forumpost/efc9bc9cb3
#
.testcase 100
.mode quote
.intck 1
SELECT parse_create_index('CREATE IDEX i ON t("x',0);
.check <<END
1 steps, 0 errors
NULL
END
