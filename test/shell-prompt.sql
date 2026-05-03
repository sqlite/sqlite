#!sqlite3
#
# 2026-04-11
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
# Tests for the .prompt command and prompt rendering.
#
#   ./sqlite3 test/shell-prompt.sql
#

.testcase setup
.open -new test.db
.mode list -quote off -escape ascii
.prompt --hard-reset
.check ''

.testcase 100
.prompt
.check ''

.testcase 110
.prompt --show
.check --glob <<END
Main prompt:  '*/A*/f*-> '
Continuation: '/B*/C*-> '
END
.testcase 111
.prompt 'abc> ' '123> ' -show
.check <<END
Main prompt:  'abc> '
Continuation: '123> '
END
.testcase 112
.prompt --  --first --second
.prompt --show
.check <<END
Main prompt:  '--first'
Continuation: '--second'
END
.testcase 113
.prompt --reset --show
.check --glob <<END
Main prompt:  '*/A*/f*> '
Continuation: '/B*/C*> '
END

.testcase 120 --error-prefix ERROR:
.prompt show
.check <<END
ERROR: .prompt show
ERROR:         ^--- use quotes around the prompt string
END

.testcase 121
.prompt 'show'
.check ''
.testcase 122
.prompt --show
.check --glob <<END
Main prompt:  'show'
Continuation: '/B*/C*> '
END

.testcase 130
.prompt --reset
.help prompt
.check <<END
.prompt MAIN CONTINUE    Replace the standard prompts
   --hard-reset              Unset SQLITE_PS1/2 and then --reset
   --reset                   Revert to default prompts
   --show                    Show the current prompt strings
   --                        No more options. Subsequent args are prompts
END

.testcase 140 --error-prefix ERROR:
.prompt --xyz
.check <<END
ERROR: .prompt --xyz
ERROR:         ^--- unknown option
END


.testcase 1000
SELECT shell_prompt_test(NULL);
.check --glob '*SQLite-3*test.db*-> ';
.testcase 1001
SELECT shell_prompt_test(NULL,'SELECT');
.check --glob ' *;*-> ';
.testcase 1002
SELECT shell_prompt_test(NULL,'SELECT ((("',null,2);
.check --glob ' *[ m]")));*-> ';
.testcase 1003
SELECT shell_prompt_test(NULL,'SELECT ((()[',null,2);
.check --glob ' *[ m]]));*-> ';
.testcase 1004
SELECT shell_prompt_test(NULL,'SELECT ''',null,2);
.check --glob " *[ m]';*-> ";
.testcase 1005
SELECT shell_prompt_test(NULL,'CREATE TRIGGER t1 BEGIN',null,2);
.check --glob " *[ m];END;*-> ";
.testcase 1006
SELECT shell_prompt_test(NULL,'CREATE TRIGGER t1 BEGIN SELECT ((([',null,2);
.check --glob " *[ m]])));END;*-> ";
.testcase 1007
SELECT shell_prompt_test(NULL,'CREATE TRIGGER t1 BEGIN SELECT ((/*a(((''bc',
                         null,2);
.check --glob " *[ m][*]/));END;*-> ";
.testcase 1008
SELECT shell_prompt_test(NULL,'CREATE TRIGGER t1 BEGIN SELECT 1;',null,2);
.check --glob " *[ m]END;*-> ";

.testcase 2000
.prompt 'SQLite/x-txn$/:>/; '
SELECT shell_prompt_test(NULL);
.check 'SQLite> ';
.testcase 2001
BEGIN;
SELECT shell_prompt_test(NULL);
.check 'SQLite-txn$ ';
.testcase 2002
ROLLBACK;
SELECT shell_prompt_test(NULL);
.check 'SQLite> ';
.testcase 2003
.prompt -- '--show '
SELECT shell_prompt_test(NULL);
.check '--show ';
.testcase 2004
.prompt --reset
SELECT shell_prompt_test(NULL);
.check --glob '*SQLite-3.# *test.db*-> ';

.testcase 3000
SELECT shell_prompt_test('(/A-/V)');
.check --glob '(SQLite-3.#.#)'
.testcase 3001
SELECT shell_prompt_test('(/A-/v)');
.check --glob '(SQLite-3.#)'
.testcase 3002
SELECT shell_prompt_test('(/A-/D%Y-%m-%dT%H:%M:%S/D)');
.check --glob '(SQLite-20#-#-#T#:#:#)'

.testcase 4000
.mode list -rowsep '' -escape ascii
SELECT shell_prompt_test('</myes/:no/;>',NULL,':memory:');
.check <yes>
.testcase 4001
.mode list -rowsep '' -escape ascii
SELECT shell_prompt_test('</myes/:no/;>',NULL,'t1.db');
.check <no>

.testcase 5000
SELECT shell_prompt_test('abc/e[0mxyz',NULL,NULL,1);
.check abc^A^[[0m^Bxyz
.testcase 5001
SELECT shell_prompt_test('abc/e[0mxyz',NULL,NULL,2);
.check abc^[[0mxyz
.testcase 5002
SELECT shell_prompt_test('/e[0mxyz',NULL,NULL,1);
.check ^A^[[0m^Bxyz
.testcase 5003
SELECT shell_prompt_test('abc/e[0m',NULL,NULL,1);
.check abc^A^[[0m^B
.testcase 5004
SELECT shell_prompt_test('abc/e[1;32m/e[0m/e[/exyz',NULL,NULL,1);
.check abc^A^[[1;32m^[[0m^B^[[^[xyz
.testcase 5005
SELECT shell_prompt_test('abc/e[1,32m/e[0m/e[/exyz',NULL,NULL,1);
.check abc^[[1,32m^A^[[0m^B^[[^[xyz
