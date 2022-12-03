/*
** 2022-12-01
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This source allows multiple SQLite extensions to be either: (a) combined
** into a single runtime-loadable library; (b) built into the SQLite shell
** using a preprocessing convention set by src/shell.c.in (and shell.c), or
** (b) statically linked with a customized SQLite3 library build.
**
** The customized library build may be effected by appending this source
** to the sqlite3.c amalgamation to form a modified translation unit. How
** this concatenation is accomplished is immaterial.
*/

#ifndef INCLUDE
# define INCLUDE(fn)
#endif

INCLUDE(cli_extend.c)
#include "cli_extend.c"
