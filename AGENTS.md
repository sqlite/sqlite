# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project nature

SQLite is a self-contained, serverless SQL database engine written in C. The
source is **public domain** — no copyright or license header should ever be
added to any file. The blessing comment that appears at the top of each source
file is intentional and should be preserved unchanged.

SQLite does not accept pull requests without prior agreement and/or
accompanying legal paperwork that places the pull request in the public domain.
However, the human SQLite developers will review a concise and well-written
pull request as a proof-of-concept prior to reimplementing the changes
themselves.

SQLite does not accept agentic code.  However the project
will accept agentic bug reports that include a reproducible test case.
Patches or pull requests demonstrating a possible fix, for documentation
purposes, are welcomed.

SQLite uses the [Fossil](https://fossil-scm.org/) for version control,
not Git.  The canonical repository is at <https://sqlite.org/src>.

## Build

The configure script uses [autosetup](https://msteveb.github.io/autosetup/),
not GNU Autoconf.

```bash
apt install gcc make tcl-dev   # prerequisites (Debian/Ubuntu)

./configure --dev              # debug build
make sqlite3                   # CLI shell
make sqlite3d                  # Debugging variant of the CLI shell
make sqlite3.c                 # amalgamation (single-file distribution form)
make testfixture               # test runner binary (requires tcl-dev)
make tclextension-install      # install TCL extension before running tests
```

## Testing

Tests are TCL scripts run through the `testfixture` enhanced interpreter.

```bash
# From the build directory:
./testfixture test/main.test   # single test file
test/testrunner.tcl            # quick suite
test/testrunner.tcl full       # full suite
test/testrunner.tcl fts5%      # pattern match

# Check for failures:
grep '!' testrunner.log
```

`make devtest` is the fastest way to run a representative subset. Always run
at least `devtest` after any change to `src/`.

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

These files are produced by scripts and must not be edited by hand:

| File | Regenerate with |
|---|---|
| `sqlite3.h` | `tool/mksqlite3h.tcl` |
| `parse.c`, `parse.h` | build lemon (`tool/lemon.c`) then run on `src/parse.y` |
| `opcodes.h` | `tool/mkopcodeh.tcl` (reads `src/vdbe.c`) |
| `opcodes.c` | `tool/mkopcodec.tcl` (reads `opcodes.h`) |
| `keywordhash.h` | `tool/mkkeywordhash.c` |
| `pragma.h` | `tool/mkpragmatab.tcl` |
| `sqlite3.c` | `tool/mksqlite3c.tcl` (the amalgamation) |

Editing rules:
- To add a PRAGMA: edit `tool/mkpragmatab.tcl`, then regenerate `pragma.h`.
- To add a VDBE opcode: add the `case OP_Xxx:` handler in `src/vdbe.c`; the
  opcode number and name are extracted automatically by `mkopcodeh.tcl`.
- To change the SQL grammar: edit `src/parse.y`, not `parse.c`.

## Coding conventions

- C89/C99 compatible C only. No C++, no STL, no exceptions, no VLAs.
- All memory allocation goes through `sqlite3Malloc` / `sqlite3_malloc64`
  (never raw `malloc`). The `sqlite3MallocZero` variant zero-initializes.
- Integer widths: use `i64` (`sqlite3_int64`) for 64-bit values, `u32`/`u64`
  for unsigned. Avoid bare `long` or `int` for values that could exceed 2G.
- Error propagation: functions return `SQLITE_OK` (0) on success and a
  `SQLITE_*` error code on failure. Many routines also set `db->mallocFailed`
  on OOM, allowing deferred error checking.
- Assert liberally for invariants that must hold in correct code; use
  `ALWAYS(x)` / `NEVER(x)` for conditions that are logically always
  true/false but that the compiler cannot prove.

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
