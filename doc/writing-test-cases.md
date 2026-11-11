# Hints For Writing Test Cases For SQLite

## SQL scripts that run using the "sqlite3" command-line interface (CLI)

  *  This is preferred.  Use this technique unless the test
     requires capabilities not available in the CLI.

  *  If possible, the test case should be runable with the --safe command-line
     option of the CLI.  If not possible, externally submitted test scripts
     should explain why not in a comment near the top of the file.

  *  The test case should use an in-memory database only, if possible.
     If external filesystem resources are required, they should be specified
     using relative pathnames.  A concise comment explaining why external
     resources are required should appear near the top of the script.

  *  Format the SQL for readability, with indentation that shows
     structure.  Avoid lines of text longer than 80 characters.

  *  SQL keywords in uppercase.  Identifiers in lowercase.

  *  Identifiers should be short - two or three characters - unless a long
     identifier name is required to demonstrate the desired behavior.

  *  Column names should be unique across all table, unless shared
     column names are required to demonstrate the desired behavior (for
     example the USING clause or NATURAL joins).

  *  The test case should be as simple as possible without compromising
     legibility.  Omit unnecessary SQL clauses and other complications.
     Populate test databases with the minimum amount of content needed
     to demonstrate the desired behavior.

  *  Concise comments are ok.  Prolix comments are discouraged.

  *  Avoid unnecessary ".print" commands as these can be annoying to
     users of the script.  Commentary should go in comments.

  *  Use ".testcase" and ".check" pairs to enclose individual tests as
     the argument to ".check" clearly shows the reader what the result
     should be which is helpful in understanding the script.

  *  Rules for quoting arguments to dot-command:
     <https://sqlite.org/cli.html#dot_command_arguments>

  *  Use ".help" to see a list of available dot-commands in the CLI.
     Use ".help COMMAND" for more information about a particular command.

  *  Detailed documentation on operating the CLI:
     <https://sqlite.org/cli.html>

  *  Use files in the SQLite source tree named "test/\*.sql" as examples.

## TCL scripts run with "testfixture"

  *  SQL scripts are preferred.  Use a TCL test script only if an SQL
     script is unable to easily demonstrate the desired behavior.
     Externally submitted scripts should include a one- or two-sentence
     explanation of why TCL is necessary in a comment near the top of
     the script.

  *  All applicable rules of SQL test scripts apply to TCL scripts too.

  *  Use files in the SQLite source tree named "\*.test" as examples.

## C programs that link against the "sqlite3.c" amalgamated source code.

  *  SQL scripts and TCL scripts are preferred over C code  Only use
     C code if pure SQL or a TCL script is inadequate.  Include a short
     comment near the top of the C source file explaining why C code is
     necessary.

  *  Portable C99.  Should compile with gcc and/or clang on Linux.
     Use C code written for "cl" on Windows only for Windows-specific tests.

  *  All applicable rules of SQL and TCL test scripts apply to C code
     tests too.
