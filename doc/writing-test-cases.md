# Hints For Writing Test Cases For SQLite

## Test Cases Written In SQL To Be Run Using The CLI

  *  The preferred format for new test scripts is SQL text that can be
     run using the [CLI](https://sqlite.org/cli.html).

  *  The test case should be runnable with the --safe command-line
     option of the CLI.  Check using "`./sqlite3 --safe test.sql`".
     If that is not possible, externally submitted test scripts
     should explain why not in a comment near the top of the file.  Core
     developers will be reluctant to run a test script received from a
     semi-anonymous forum post or similar if the script cannot be run
     in safe mode, for fear of trojan-horse attacks.  (Many of the older
     scripts in the "test/" directory do not run in safe mode.  They are
     not a model to follow in this respect.)

  *  The test case should use an in-memory database, if possible.  (This is
     a corollary of the previous point.)  If external filesystem resources
     are required, they should be specified using relative pathnames.
     A concise comment explaining why external resources are required
     should appear near the top of the script.

  *  Format the SQL for readability, with indentation that shows
     structure.  Avoid lines of text longer than 80 characters.

  *  SQL keywords in uppercase.  Identifiers in lowercase.

  *  Identifiers should be short - two or three characters - unless a long
     identifier name is required to demonstrate the desired behavior.

  *  Column names should be unique across all tables, unless shared
     column names are required to demonstrate the desired behavior (for
     example the USING clause or NATURAL joins).

  *  The test case should be as simple as possible without compromising
     legibility.  Omit unnecessary SQL clauses and other complications.
     Populate test databases with the minimum amount of content needed
     to demonstrate the desired behavior.

  *  Concise comments are fine.  Prolix comments are discouraged.

  *  Avoid unnecessary ".print" commands as these can be annoying to
     users of the script.  Commentary should go in comments.

  *  Use ".testcase" and ".check" pairs to enclose individual tests.

### A complete example

This script runs in safe mode and reports "3 tests run with 0 errors":

> ~~~
> -- Demonstrates: NULL handling in ORDER BY output.
> .mode --reset quote
> CREATE TABLE t1(a, b);
> INSERT INTO t1 VALUES(1, NULL), (2, 'x');
> 
> .testcase 100
> SELECT a, b FROM t1 ORDER BY a;
> .check <<END
> 1,NULL
> 2,'x'
> END
> 
> .testcase 110
> SELECT a FROM nosuch;
> .check --glob 'Parse error*no such table: nosuch*'
> 
> .testcase 120
> SELECT count(*) FROM t1 WHERE b IS NULL;
> .check 1
> ~~~
 
Run it using "`./sqlite3 --safe test.sql`".  If a ".check" fails, the CLI
shows the expected pattern and the text actually obtained, for example:

> ~~~
> test.sql:19: .check failed for testcase 120
> Expected: [2]
> ╭────────────── Got ───────────────╮
> 1
> ╰────────────── Got ───────────────╯
> 3 tests run with 1 error
> ~~~

and the exit status of the CLI is 1.  Look for the "N tests run with M
errors" line, and check the exit status, to know whether the script passed.

### The ".testcase" and ".check" commands

  *  The .testcase command begins capturing stdout and stderr into
     a memory buffer.  The subsequent .check command then comparies
     that captured output against a PATTERN to determine if the
     intervening SQL statements worked correctly.

  *  After .check, captured output is discarded and subsequent output
     goes to the console again (or whereever else it was going).  Except,
     if the --keep option is used, the captured output is retained and
     subsequent output is added to the capture.  The --keep option therefore
     allows multiple .check commands to occur in a row, testing various
     aspects of the captured output.  When there are multiple .check
     commands in a row, all but the last should use --keep.

  *  By default, captured output and PATTERN are compared like this:
     (1) leading and trailing whitespace is removed from both output
     and PATTERN.  (2) A whitespace character in PATTERN match a
     single whitespace character in the output.  (3) Non-whitespace
     characters in the PATTERN match that exact same character in the output.

  *  Use the --glob option to do glob matching:  (1) leading and trailing
     whitespace are removed from both output and PATTERN.  (2) the test
     passes if any substring of the output matches PATTERN.  (3) Within
     PATTERN, '`*`' matches any string of zero or more characters, "`?`" matches
     any single character, "`#`" matches any string of one or more digits
     with an optional "`+`" or "`-`" prefix, " " (a space) matches any sequence
     of one or more whitespace characters, "`[...]`" (a list of characters
     in square brackets) matches any of the characters in the list, and
     "`[^...]`" (a list starting with "`^`") matches any character not in the
     list.

  *  With --any, the check passes if any substring of the output exactly
     matches PATTERN, after removing leading and trailing whitespace from both.
     With --exact, the check passes if the output and PATTERN are byte-for-byte
     the same, modulo leading and trailing whitespace.

  *  The --not option inverts the search.  In other words, with --not the
     check passes if PATTERN is *not* in the output.  The --nowhere option is
     shorthand for --not --any.

  *  There are other options.  Those described above are the most used.
     Read about the others in the built-in help:
     "`./sqlite3 /dev/null '.help check'`"

  *  The PATTERN of a ".check" command carries the *correct* result.  When
     a test accompanies a bug report, the test must fail on the version
     of SQLite that has the bug, and pass once the bug is fixed.  Never
     write the incorrect output into the PATTERN in order to make the
     test pass.  When a fix is submitted, say that you confirmed the test
     fails without the fix and passes with it.

  *  Error messages are captured too (stdout and stderr both).  Use "--glob"
     with "\*" to ignore variable text such as line numbers, as in
     "`.check --glob 'Parse error*no such table: nosuch*'`".  Use
     "--exact" only when the exact bytes, including any line-number
     prefix, matter.  The ".testcase --error-prefix" option changes the
     prefix text on error messages.

  *  A ".testcase" and its ".check" should be adjacent.  Keep commands
     that produce output between them to those the test intends to check.

  *  ".testcase" identifiers can be anything, but traditionally they
     are numbers starting with 100 and incrementing by 10.  The identifiers
     in one script must be unique.

  *  When inserting new testcases into the middle of an existing test
     script, choose identifiers that go in increasing order.  Add suffixes
     to test identifiers if necessary.  For example to insert a new test
     in between "100" and "101", perhaps call the new test "100.1".

  *  Documentation on these or any other dot-command *COMMAND*,
     including details not described here,
     can be seen using: "`./sqlite3 /dev/null '.help COMMAND'`".


### State that carries from one test to the next

  *  Settings made by dot-commands persist until changed.  This includes
     ".mode", ".headers", ".nullvalue", ".width", ".timer", ".echo",
     ".dbconfig", and ".open".  Use ".show" to display the current
     settings.  A test should set every setting that it depends on,
     and should not leave behind settings that a later test could trip over.

  *  Consider using ".mode --reset quote" to set the output mode to the
     ["quote" style](https://sqlite.org/climode.html#dotmodequote).
     The --reset option ensures prior tests and/or command-line options
     have not left the output format settings in an unusual state. See
     <https://sqlite.org/climode.html> for additional documentation on
     the ".mode" command and its many options.

### Corrupt databases and optional features

  *  To test with a corrupt or specially constructed database, embed it
     in the script as a hex dump using "`.open --hexdb`", followed by the
     lines of the dump (the format of the output of the ".dbtotxt"
     command).  See the tests numbered 400 and later in "test/misc01.sql"
     for examples.

  *  Guard tests that need an optional feature using ".if" and ".endif",
     for example "`.if capable('vtab')`" on one line and
     "`.endif`" after the last test that depends on it.  See "test/misc01.sql"
     and "test/dump.sql" for examples.  This keeps the script from reporting
     false failures on builds that omit the feature.

### Where new test cases go

  *  When writing test cases to go with a new patch, put the new test cases
     in a new file in the "test/" subdirectory and the core SQLite developers
     will move them into an existing test file or give your new file an
     appropriate name, as part of their review process.  (Core developers
     put tests in the existing file for the relevant area when there is one.)

  *  No particular header-comment license boilerplate is required for
     externally submitted test scripts.  The core developers will add
     appropriate boilerplate text if and when the test script is
     approved to enter the official source tree.

### Reference

  *  Rules for quoting arguments to dot-commands are at
     <https://sqlite.org/cli.html#dot_command_arguments>.  Here is
     a copy:

     Dot-commands can be followed by zero or more space-separated arguments.
     Arguments are parsed according to these rules:

      1.  Any trailing whitespace and a final ";" are removed, if present.
      2.  Arguments are separated from each other and from the initial
          dot-command itself by whitespace.
      3.  Text within '...' is treated as a single argument with the '
          delimiters removed, even if the text contains white space.
      4.  Text within "..." is treated as a single argument with the "
          delimiters removed.
      5.  C-style backslash escapes (ex: `\\`, `\n`, `\r`, `\"`, `\033`, etc.)
          work only within double-quoted arguments.

  *  Use ".help" to see a list of available dot-commands in the CLI.
     Use ".help COMMAND" for more information about a particular command.

  *  Detailed documentation on operating the CLI:
     <https://sqlite.org/cli.html>

  *  Use files in the SQLite source tree named `test/*.sql` as examples
     of style (but see the note about safe mode above).

## TCL scripts run with "testfixture"

  *  SQL scripts are preferred.  Use a TCL test script only if an SQL
     script is unable to easily demonstrate the desired behavior.
     Externally submitted scripts should include a one- or two-sentence
     explanation of why TCL is necessary in a comment near the top of
     the script.

  *  All applicable rules of SQL test scripts apply to TCL scripts too.

  *  Run a script using "`./testfixture test/NAME.test`".  Build
     "testfixture" first using "`make testfixture`".

  *  Most TCL tests use "do_execsql_test", which runs SQL and compares the
     result list, or "do_catchsql_test", which compares a
     two-element result: 0 for success or 1 for an error, then the result
     or error message.  Test identifiers are strings, usually with the
     script's `testprefix` prepended.  In a script that begins
     with `set testprefix myprefix` (as "test/aggnested.test" does with
     its own prefix), identifier 1.1 below becomes "myprefix-1.1":

> ~~~
>   do_execsql_test 1.1 {
>     CREATE TABLE t1(a, b);
>     INSERT INTO t1 VALUES(1, 2);
>     SELECT a+b FROM t1;
>   } {3}
>
>   do_catchsql_test 1.2 {
>     SELECT a FROM t2;
>   } {1 {no such table: t2}}
> ~~~

  *  Use files in the SQLite source tree named `*.test` as examples.

## C programs that link against the "sqlite3.c" amalgamated source code

  *  SQL scripts and TCL scripts are preferred over C code.  Only use
     C code if pure SQL or a TCL script is inadequate.  Include a short
     comment near the top of the C source file explaining why C code is
     necessary.

  *  Portable C99.  Should compile with gcc and/or clang on Linux.
     Use C code written for "cl" on Windows only for Windows-specific tests.

  *  All applicable rules of SQL and TCL test scripts apply to C code
     tests too.
