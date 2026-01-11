#!sqlite3
#
# 2025-12-16
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
# Test cases for the oversized patterns in the REGEXP extension found
# at ext/misc/regexp.c.
#
.mode list
.testcase 100
--                       0         1         2         3         4
--                        123456789 123456789 123456789 123456789 123
SELECT 'abcdefg' REGEXP '((((((((((((((((((abcdefg))))))))))))))))))';
.check 1

.testcase 110
.limit like_pattern_length 42
SELECT 'abcdefg' REGEXP '((((((((((((((((((abcdefg))))))))))))))))))';
.check -glob "Error near line #: REGEXP pattern too big*"

.testcase 120
.limit like_pattern_length 43
SELECT 'abcdefg' REGEXP '((((((((((((((((((abcdefg))))))))))))))))))';
.check 1
