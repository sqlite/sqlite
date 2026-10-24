#!sqlite3
#
# 2026-10-24
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
# Test cases for the ".if", ".elseif", ".else", and ".endif" commands
# and the capable() SQL function of the CLI.
#
#   ./sqlite3 test/if.sql
#
# Many tests use ".read <<EOF" to run a script contained in a heredoc,
# so that a failure is confined to that script.  Heredocs within such
# scripts use a different end mark, usually END.
#
# Some tests deliberately leave the .if stack in a bad state when they
# fail.  Such tests are followed by ".if 1" and ".endif" before the
# ".check", so that a failure does not cause the rest of this script
# to be skipped.
#

# The following tests must come before any SQL statement, so that
# the database connection has not yet been opened.
#
.testcase 100
.if 0
.print 100-BUG
.endif
.print 100-end
.check 100-end

.testcase 110
.if capable('abs')
.print 110-yes
.else
.print 110-BUG
.endif
.check 110-yes

.testcase setup
.open :memory:
.mode list
.headers off
CREATE TABLE t1(a);
INSERT INTO t1 VALUES(1),(2),(3);
.check ''

#############################################################################
# Basic .if, .else, .endif
#
.testcase 200
.if 1
.print 200-then
.endif
.if 0
.print 200-BUG
.endif
.print 200-end
.check <<END
200-then
200-end
END

.testcase 210
.if 1
.print 210-then
.else
.print 210-BUG
.endif
.if 0
.print 210-BUG
.else
.print 210-else
.endif
.check <<END
210-then
210-else
END

# SQL statements are skipped as well as dot-commands
#
.testcase 220
.if 0
SELECT '220-BUG';
INSERT INTO t1 VALUES(99);
.endif
SELECT count(*) FROM t1;
.check 3

# SQL errors in skipped regions are not reported
#
.testcase 230
.if 0
SELECT * FROM no_such_table;
THIS IS NOT VALID SQL;
.endif
.print 230-end
.check 230-end

# A multi-line SQL statement in a skipped region is skipped in its
# entirety, even if it contains text that looks like a dot-command.
#
.testcase 240
.if 0
SELECT '240-BUG
.endif
.print 240-BUG
';
.print 240-BUG
.endif
.print 240-end
.check 240-end

# .exit within a skipped region is not run
#
.testcase 250
.if 0
.exit
.endif
.print 250-end
.check 250-end

# Skipped .testcase/.check pairs
#
.testcase 260
.if 0
.testcase 260-inner
SELECT 'x';
.check 'y'
.endif
.print 260-end
.check 260-end

# A ".check <<END" heredoc within a skipped region is skipped.
# The text of the heredoc is not treated as SQL or as dot-commands.
#
.testcase 270
.read <<EOF
.if 0
.testcase 270-inner
SELECT 1;
.check <<END
1
.print 270-BUG
END
.endif
.print 270-inner-end
EOF
.if 1
.endif
.check 270-inner-end

#############################################################################
# Truth values of the expression
#
.testcase 300
.if 1+1=2
.print 300-a
.endif
.if 1+1=3
.print 300-BUG
.endif
.if NULL
.print 300-BUG
.endif
.if (SELECT count(*) FROM t1)=3
.print 300-b
.endif
.if (SELECT a FROM t1 WHERE a>100)
.print 300-BUG
.endif
.if EXISTS(SELECT 1 FROM t1 WHERE a=2)
.print 300-c
.endif
.if -1
.print 300-d
.endif
.if 'abc'
.print 300-BUG
.endif
.if '5'
.print 300-e
.endif
.if 0.0
.print 300-BUG
.endif
.if 0.5
.print 300-f
.endif
.if 9223372036854775807
.print 300-g
.endif
.check <<END
300-a
300-b
300-c
300-d
300-e
300-f
300-g
END

# A trailing semicolon is allowed
#
.testcase 310
.if 1;
.print 310-a
.endif
.if 0;
.print 310-BUG
.endif
.check 310-a

# The expression may use parameters set by .parameter
#
.testcase 320
.parameter set $x 5
.if $x>3
.print 320-a
.else
.print 320-BUG
.endif
.if $x>10
.print 320-BUG
.endif
.parameter unset $x
.check 320-a

# An expression that has a syntax error or a run-time error is false.
# No error message is issued.
#
.testcase 330
.if 1 +
.print 330-BUG
.else
.print 330-else
.endif
.check 330-else

.testcase 331
.if
.print 331-BUG
.else
.print 331-else
.endif
.check 331-else

.testcase 332
.if (SELECT x FROM no_such_table)
.print 332-BUG
.else
.print 332-else
.endif
.check 332-else

.testcase 333
.if NOT (SELECT x FROM no_such_table)
.print 333-BUG
.endif
.print 333-end
.check 333-end

.testcase 340
.if abs(-9223372036854775808)
.print 340-BUG
.else
.print 340-else
.endif
.check 340-else

.testcase 341
.if 0
.print 341-BUG
.elseif abs(-9223372036854775808)
.print 341-BUG
.elseif no_such_function(1)
.print 341-BUG
.else
.print 341-else
.endif
.check 341-else

# A capable() test on a table that might not exist.  This is the
# intended use for the error-is-false rule.
#
.testcase 350
.if (SELECT count(*) FROM pragma_module_list WHERE name='no_such_module')
.print 350-BUG
.elseif (SELECT count(*) FROM no_such_vtab)>=0
.print 350-BUG
.else
.print 350-else
.endif
.check 350-else

#############################################################################
# .elseif
#
.testcase 400
.if 0
.print 400-BUG
.elseif 1
.print 400-a
.else
.print 400-BUG
.endif
.check 400-a

.testcase 410
.if 0
.print 410-BUG
.elseif 0
.print 410-BUG
.else
.print 410-a
.endif
.check 410-a

# At most one branch of an .if/.elseif/.else chain is run
#
.testcase 420
.if 1
.print 420-a
.elseif 1
.print 420-BUG
.endif
.check 420-a

.testcase 421
.if 1
.print 421-a
.elseif 0
.print 421-BUG
.else
.print 421-BUG
.endif
.check 421-a

.testcase 422
.if 0
.print 422-BUG
.elseif 0
.print 422-BUG
.elseif 1
.print 422-a
.elseif 1
.print 422-BUG
.else
.print 422-BUG
.endif
.check 422-a

.testcase 423
.if 0
.print 423-BUG
.elseif 1
.print 423-a
.elseif 0
.print 423-BUG
.else
.print 423-BUG
.endif
.check 423-a

# The chain state of one .if does not carry over to the next .if at
# the same level.
#
.testcase 424
.if 1
.endif
.if 0
.print 424-BUG
.elseif 1
.print 424-a
.endif
.if 1
.endif
.if 0
.print 424-BUG
.else
.print 424-b
.endif
.check <<END
424-a
424-b
END

#############################################################################
# Nesting
#
.testcase 500
.if 1
.print 500-a
.if 0
.print 500-BUG
.else
.print 500-b
.endif
.print 500-c
.else
.print 500-BUG
.endif
.check <<END
500-a
500-b
500-c
END

# An inner .else or .elseif within a skipped region does not cause
# anything to run
#
.testcase 510
.if 0
.if 0
.print 510-BUG
.else
.print 510-BUG
.endif
.if 0
.print 510-BUG
.elseif 1
.print 510-BUG
.endif
.if 1
.print 510-BUG
.endif
.else
.print 510-a
.endif
.check 510-a

# Nested .if/.elseif/.else chains within the branches of an outer chain
#
.testcase 511
.if 0
.print 511-BUG
.elseif 1
.if 0
.print 511-BUG
.elseif 1
.print 511-a
.else
.print 511-BUG
.endif
.else
.if 1
.print 511-BUG
.else
.print 511-BUG
.endif
.endif
.check 511-a

# Unknown dot-commands within a skipped region are not reported, even
# if they begin with "if", "else", "read", or "check".
#
.testcase 530
.if 0
.no_such_command
.iffy
.elsewhere
.endiff
.readx
.checkmate
.endif
.print 530-end
.check 530-end

# Ten levels of nesting are allowed.  The error message says the
# maximum is ten levels.
#
.testcase 540
.read <<EOF
.if 1
.if 1
.if 1
.if 1
.if 1
.if 1
.if 1
.if 1
.if 1
.if 1
.print 540-depth-10
.endif
.endif
.endif
.endif
.endif
.endif
.endif
.endif
.endif
.endif
.print 540-inner-end
EOF
.check <<END
540-depth-10
540-inner-end
END

# Nesting too deep is an error.  The outer script continues, and
# it is left in a sane state.
#
.testcase 550
.read <<EOF
.if 0
.if 1
.if 1
.if 1
.if 1
.if 1
.if 1
.if 1
.if 1
.if 1
.if 1
.print 550-BUG
EOF
.print 550-a
.if 1
.endif
.check --keep --glob "nested too deep"
.check --keep --notglob 550-BUG
.check --glob 550-a

#############################################################################
# Errors
#
.testcase 600
.else
.check --glob ".else without a .if"

.testcase 610
.endif
.check --glob ".endif without a .if"

# .elseif without .if is an error, but it does not stop the script
#
.testcase 620
.read <<EOF
.elseif 1
.print 620-inner-end
EOF
.check --keep --glob ".elseif with no .if"
.check --glob 620-inner-end

.testcase 630
.if 1
.print 630-a
.else
.else
.print 630-BUG
.endif
.check --keep --glob "already saw a .else"
.check --notglob 630-BUG

# .elseif after .else is an error.  It does not cause the body that
# follows it to run.
#
.testcase 640
.if 1
.print 640-a
.else
.print 640-BUG
.elseif 1
.print 640-BUG
.endif
.check --keep --glob ".elseif follows a .else"
.check --notglob 640-BUG

.testcase 641
.if 0
.print 641-BUG
.else
.print 641-a
.elseif 1
.print 641-BUG
.endif
.check --keep --glob ".elseif follows a .else"
.check --notglob 641-BUG

#############################################################################
# Interaction with .read
#
# A .read script that ends with an unterminated .if reports an error,
# and the caller is not affected.
#
.testcase 700
.read <<EOF
.print 700-inner
.if 0
.print 700-BUG
EOF
.print 700-after-read
.if 1
.endif
.check --keep --glob "<heredoc>:2: unterminated .if"
.check --keep --notglob 700-BUG
.check --glob "700-inner*700-after-read"

# Also with nested unterminated .ifs.  The .else at nesting level 2
# must not be remembered after the .read finishes.
#
.testcase 701
.read <<EOF
.if 1
.if 0
.else
.print 701-inner
EOF
.print 701-after-read
.if 1
.if 0
.else
.print 701-nested-else
.endif
.endif
.if 1
.if 1
.endif
.endif
.check --keep --glob "unterminated .if"
.check --keep --notglob "already saw a .else"
.check --glob "701-inner*701-after-read*701-nested-else"

# A true branch taken in an unterminated .if of a .read script must
# not be remembered after the .read finishes.
#
.testcase 702
.read <<EOF
.if 1
EOF
.if 0
.print 702-BUG
.elseif 1
.print 702-a
.endif
.if 1
.endif
.check --keep --glob "unterminated .if"
.check --glob 702-a

# A .read script cannot close, invert, or change a .if of its caller
#
.testcase 710
.read <<OUTER
.if 1
.read <<EOF
.endif
EOF
.read <<EOF
.else
EOF
.read <<EOF
.elseif 1
EOF
.read <<EOF
.elseif 0
EOF
.print 710-a
.else
.print 710-BUG
.endif
.print 710-end
OUTER
.if 1
.endif
.check --keep --glob ".endif without a .if"
.check --keep --glob ".else without a .if"
.check --keep --glob ".elseif with no .if"
.check --keep --notglob 710-BUG
.check --glob "710-a*710-end"

.testcase 711
.read <<OUTER
.if 0
.print 711-BUG
.else
.read <<EOF
.elseif 1
EOF
.print 711-a
.endif
OUTER
.if 1
.endif
.check --keep --glob ".elseif with no .if"
.check --glob 711-a

# .if and .endif within a .read script work normally when the
# caller is within a .if
#
.testcase 720
.if 1
.read <<EOF
.if 0
.print 720-BUG
.else
.print 720-inner
.endif
EOF
.print 720-a
.endif
.check <<END
720-inner
720-a
END

# A .read heredoc within a skipped region is not run, and its text is
# skipped in its entirety.  It is not treated as SQL or as dot-commands
# of the outer script.
#
.testcase 730
.read <<OUTER
.if 0
.read <<EOF
.print 730-BUG
.endif
.print 730-BUG
EOF
.print 730-a
.endif
.print 730-inner-end
OUTER
.if 1
.endif
.check <<END
730-inner-end
END

.testcase 731
.read <<OUTER
.if 0
.read <<EOF
.check <<END
.print 731-BUG
END
.print 731-BUG
EOF
.endif
.print 731-inner-end
OUTER
.if 1
.endif
.check 731-inner-end

# .read heredocs nest
#
.testcase 740
.read <<OUTER
.print 740-a
.read <<INNER
.if 1
.print 740-b
.endif
INNER
.print 740-c
OUTER
.check <<END
740-a
740-b
740-c
END

# A .read heredoc may be the last thing in an .if branch, and
# an .else that follows it is processed by the outer script.
#
.testcase 750
.read <<OUTER
.if 0
.read <<EOF
.print 750-BUG
EOF
.else
.read <<EOF
.print 750-a
EOF
.endif
OUTER
.if 1
.endif
.check 750-a

# Line numbers in error messages within the outer script are correct
# after a .read heredoc.  The .else on the line after the EOF mark is
# on line 802.
#
.testcase 760
.read <<EOF
.print 760-a
.print 760-b
.print 760-c
EOF
.else
.check --keep --glob "if.sql:802: .else without a .if"
.check --glob "760-c"

#############################################################################
# The capable() function
#
.testcase 800
SELECT capable('abs'), capable('ABS'), capable('capable'),
       capable('no_such_capability');
.check 1|1|1|0

.if capable('vtab')
.testcase 810
SELECT capable('generate_series')
     = EXISTS(SELECT 1 FROM pragma_module_list WHERE name='generate_series');
.check 1

# Every compile-time option, other than OMIT and DISABLE options, is
# a capability.
#
.testcase 820
SELECT compile_options FROM pragma_compile_options
 WHERE compile_options NOT GLOB 'OMIT_*'
   AND compile_options NOT GLOB 'DISABLE_*'
   AND NOT capable(compile_options);
.check ''

# The ENABLE_ prefix is optional and case does not matter
#
.testcase 830
SELECT capable('fts5')=capable('ENABLE_FTS5'),
       capable('fts5')=capable('enable_fts5'),
       capable('fts5')=EXISTS(SELECT 1 FROM pragma_compile_options
                               WHERE compile_options='ENABLE_FTS5');
.check 1|1|1
.endif /* capable(virtualtable) */

.testcase 850
.null NULL
SELECT capable(NULL);
.check NULL

# Self-reference of capable()
#
.testcase 860
SELECT capable('capable')
.check 1

.testcase 870
.if capable('no_such_capability')
.print 870-BUG
.elseif capable('abs')
.print 870-a
.endif
.check 870-a
