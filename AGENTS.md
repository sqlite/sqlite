# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project nature

SQLite is a self-contained, serverless SQL database engine written in C. The
source is **public domain**.  Therefore, external contributions should also
be in the public domain, with well-documented and unambiguous provenance.
It is OK to submit patches that are not in the public domain or where the
provenance is unsubstantiated, however the developers will only be able to use
such patches as a proof-of-concept and will need to rewrite them from scratch
before committing them into the source tree, and that might delay their
adoption.

All submitted code should be indistinguishable from the original SQLite
code with respect to coding style, variable naming style, commenting style,
and so forth.  There are no hard rules regarding coding style, only the
requirement that new code should blend seamlessly and match the appearance
of what came before.  Make minimal changes: do not reformat or rename
unrelated code, and do not add external dependencies.

New source files carry no copyright or license header.  Instead they begin
with the SQLite blessing comment, copied from an existing file such as
`src/sqliteInt.h`.  Never remove or alter the blessing in an existing file.

SQLite uses [Fossil](https://fossil-scm.org/) for version control,
not Git.  The GitHub repository for SQLite is a mirror and is not
canonical.  The canonical repository is at <https://sqlite.org/src>.

To send patches to the SQLite developers, the ideal format is a Fossil
patch created using the
"[fossil patch create](https://fossil-scm.org/home/help/patch)" command.
The second best approach is "diff -u".  If neither Fossil nor a pristine
copy of the original tree is available, say so rather than guessing at
a diff, and do not run Git commands in a Fossil checkout.  Do not commit
or check in changes unless asked to.

## Reporting Bugs

Send bug reports to <https://sqlite.org/bugs>, not the SQLite user forum.
Reports are public, so they must not contain private or confidential data.

Bug reports should include a reproducible test case, written according
to the guidelines in
[doc/writing-test-cases.md](https://sqlite.org/src/doc/trunk/doc/writing-test-cases.md)
(in a local checkout: `doc/writing-test-cases.md`).  The test should fail
on the version that exhibits the bug and pass once the bug is fixed.
A report should describe one problem, and say what output was expected
and what was actually seen.

Treat the database file as untrusted input.  Many SQLite bugs are triggered
by corrupt or deliberately malformed database files, and fixes must be
robust against them.  Run scripts received from others only with the
CLI's `--safe` option, in an empty scratch directory.

Bug reports should identify the particular version of SQLite that exhibits
the problem, either by version number (ex: 3.54.0) or (better) by the value
of SQLITE_SOURCE_ID in the sqlite3.h header file or the return value from
the "sqlite_source_id()" SQL function
(ex: "2026-11-11 15:30:27 509c3506542a77d628dbab8b6ba5f7189b4712fcd8365b0b7873b875fc7ffd2a").

Report the platform, compiler, and build options if any of that seems
likely to make a difference.  (Usually it does not.)

Include suggested fixes and/or patches with bug reports, if possible.

## Build

The configure script uses [autosetup](https://msteveb.github.io/autosetup/),
not GNU Autoconf.

```bash
apt install gcc make tcl-dev   # prerequisites (Debian/Ubuntu)

./configure --dev              # debug build
make sqlite3                   # CLI shell
make sqlite3d                  # Debugging variant of the CLI shell (asserts on)
make sqlite3.c                 # amalgamation (single-file distribution form)
make testfixture               # test runner binary (requires tcl-dev)
make tclextension-install      # install TCL extension before running tests
```

All of the "make" commands above work on Windows as well, as long as MSVC
is installed, thanks to the `make.bat` file at the top of the source tree
that translates each command into its corresponding "nmake" command.

Built programs do not rebuild themselves.  After changing any source file,
rerun the corresponding `make` command (for example `make sqlite3`) before
testing.  Reproduce crashes and assertion failures using `sqlite3d` or
another build with `-DSQLITE_DEBUG`, since `assert()` is disabled otherwise.

## Testing

Test cases in the SQLite source tree are written as SQL scripts, TCL
scripts, or separate C programs.  There are many thousands of tests in
the canonical source tree.

Run individual SQL tests using the [CLI](https://sqlite.org/cli.html).
Run individual TCL test scripts using the "testfixture" program.
Use the test/testrunner.tcl script to run collections
of tests, including SQL tests, TCL scripts, and C test programs,
in parallel across multiple cores.

```bash
# From the build directory:
./sqlite3 test/misc01.sql      # single SQL test script
./testfixture test/main.test   # single TCL test script
test/testrunner.tcl            # "veryquick" suite (the default)
test/testrunner.tcl full       # full suite
test/testrunner.tcl fts5%      # pattern match
```

Check for failures from testrunner.tcl using:

  *  `grep '!' testrunner.log`
  *  `test/testrunner.tcl errors`
  *  `test/testrunner.tcl errors -v`

"`make test`" (a synonym for "`make devtest`") is an easy way to run a
reasonably complete subset of tests.  After changing anything under `src/`,
run at least `make devtest` before submitting a patch.  Report only the test
results you actually obtained, and say which tests were not run.

Where tests live: `test/*.test` are TCL scripts run by `testfixture`;
`test/*.sql` are SQL scripts run by the CLI; most extension keeps tests
in their own directory (for example `ext/fts5/test/`).  The test runner
picks up new `test/*.sql` files automatically.  Put a new test in the
existing file for the same area when one exists.  Details on writing tests
are in `doc/writing-test-cases.md`; see also `doc/testrunner.md`.

## Architecture

SQL text → **tokenizer** (`tokenize.c`) → **parser** (`parse.y` / Lemon) →
**code generator** (`build.c`, `select.c`, `insert.c`, `update.c`, `delete.c`,
`expr.c`) → **optimizer** (`where*.c`) → **VDBE** (`vdbe.c`) → **B-Tree**
(`btree.c`) → **Pager** (`pager.c`) → **WAL** (`wal.c`) → **VFS**
(`os_unix.c`, `os_win.c`).

The master internal header is `src/sqliteInt.h`. Major subsystems have private
headers: `vdbeInt.h`, `btreeInt.h`, `whereInt.h`. The public API is defined in
`src/sqlite.h.in` (template) which generates `sqlite3.h`.

## Do not edit generated files

These files are produced by scripts and ought not be edited by hand:

| File | Regenerate with |
|---|---|
| `sqlite3.h` | `tool/mksqlite3h.tcl` |
| `parse.c`, `parse.h` | build lemon (`tool/lemon.c`) then run on `src/parse.y` |
| `opcodes.h` | `tool/mkopcodeh.tcl` (reads `src/vdbe.c`) |
| `opcodes.c` | `tool/mkopcodec.tcl` (reads `opcodes.h`) |
| `keywordhash.h` | `tool/mkkeywordhash.c` |
| `pragma.h` | `tool/mkpragmatab.tcl` |
| `shell.c` | `tool/mkshellc.tcl` |
| `sqlite3.c` | `tool/mksqlite3c.tcl` (the amalgamation) |

All of the generated files can be built using "`make` *FILENAME*".

The primary source file for `shell.c` is `src/shell.c.in`, though the
`tool/mkshellc.tcl` pulls in lots of code from other sources as well.
Grep for "`^INCLUDE`" in `src/shell.c.in` to find all code sources
that go into building `shell.c`.  When searching the source, look in `src/`
and `ext/` rather than in generated files such as `sqlite3.c` and `shell.c`,
which only duplicate the hits.  Likewise the documentation for the public
API is the comment text in `src/sqlite.h.in`; edit that, not `sqlite3.h`.

Editing rules:
- To add a PRAGMA: edit `tool/mkpragmatab.tcl`, then regenerate `pragma.h`.
- To add a VDBE opcode: add the `case OP_Xxx:` handler in `src/vdbe.c`; the
  opcode number and name are extracted automatically by `mkopcodeh.tcl`.
- To change the SQL grammar: edit `src/parse.y`, not `parse.c`.

## Coding conventions

- C89/C99 compatible C only. No C++, no STL, no exceptions, no VLAs.
  Use `/* ... */` comments, never `//`.  Declare all local variables at
  the start of a block, before any statement.  Keep lines to 80 columns.
- In the SQLite core, memory is allocated using `sqlite3Malloc()` and
  its relatives such as `sqlite3MallocZero()` and `sqlite3DbMallocRaw()`,
  never raw `malloc()`.  Extensions and the CLI use the public
  `sqlite3_malloc64()` and `sqlite3_free()` instead.  Never release memory
  from one allocator using the other's free routine.  Every allocation can
  fail, and the failure must be handled.
- Integer widths: use `i64` (`sqlite3_int64`) for 64-bit values, `u32`/`u64`
  for unsigned. Avoid bare `long` or `int` for values that could exceed 2GiB.
- Error propagation: functions return `SQLITE_OK` (0) on success and a
  `SQLITE_*` error code on failure. Many routines also set `db->mallocFailed`
  on OOM, allowing deferred error checking.
- Assert liberally for invariants that must hold in correct code.  Think of
  an assert() as an executable comment that proves something about the
  state of the program to the reader.  Remember that in SQLite, assert() is
  a no-op unless compiled with -DSQLITE_DEBUG, so an assert() expression
  must never have side effects.
- New code should come with sufficient test cases to show that every
  condition in that code (every branch operation at the machine code level)
  can be both true and false.  Or, if some conditions that are always true
  or always false (as far as anyone knows) and are included only for safety,
  those conditions should be enclosed in ALWAYS() or NEVER(), respectively.
  Use testcase(X) to mark boundary values that tests need to exercise.
  Never use ALWAYS() or NEVER() on a condition that a corrupt database
  file or other untrusted input could make true (or false): such a
  condition needs real error handling.

## Extensions (`ext/`)

Extensions are compiled separately from the core and are not part of the
amalgamation by default (except where explicitly included). Major subsystems:

- `ext/fts5/` — Full-Text Search 5 (current)
- `ext/rtree/` — R-Tree spatial indexing
- `ext/session/` — Changesets and sessions
- `ext/recover/` — Database file recovery
- `ext/misc/` — Single-file utility extensions (many included in the CLI)
- `ext/qrf/` — Query Result Formatter utility library

## Useful references

- Architecture: <https://sqlite.org/arch.html>
- File format: <https://sqlite.org/fileformat2.html>
- VDBE opcodes: <https://sqlite.org/opcode.html>
- Query planner: <https://sqlite.org/optoverview.html>
- Lemon parser generator: <https://sqlite.org/doc/trunk/doc/lemon.html>
- Compile-time options: <https://sqlite.org/compile.html>
