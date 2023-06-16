# Introduction to the SQLite Extensible Shell #

This article introduces a set of enhancements to the SQLite "CLI" shell
which make it extensible as a shell and provide other useful features.

What "extensible" means as of June 2023 is that additional dot-commands
can be implemented by a dynamically loaded extension and integrated
into the shell's command repertoire and help for same. It also means
that scripting support can be provided by an extension and integrated
into the shell's input collection and dispatching facility.

In the future, "extensible" will also mean that additional forms of query
result output formatting (or other disposition), and additional methods
of data collection into tables (or other disposition), can be provided
by an extension and utilized via the shell's .mode and .import commands.
(This integration with .mode and .import commands is in-work.)

Scripting support for the shell is presently implemented by a
[Tcl Shell Extension](https://sqlite.org/src/file?name=doc/tcl_extension_intro.md&ci=cli_extension).

Some new features are available via these new or altered dot-commands:

**.eval** will interpret its arguments as shell input. This is mainly useful
from within a script to run fixed or formulated shell commands.

**.parameter** has several enhancements: The new "edit" subcommand can be
used to create or modify SQL binding parameters, either by name or from
being referenced in the most recently run SQL. The new "save" and "load"
subcommands will store or retrieve all or named parameters into/from a file.

**.shxload** is used to load shell extensions at runtime.

**.shxopts** either shows or alters extended shell features. Presently,
this allows shell input parsing to be enhanced as described below or to be
reverted to the traditional rule where dot-commands always occupy one line
and neither dot-commands nor #-prefixed comments may have leading whitespace.

**.tables** has new options to show only tables, views, or system tables.

**.vars** is used to create or modify "shell" variables. These are
key/value pairs which are associated with the shell session rather than
a specific database such as .databases would list. These variables may
be the subject of "edit", "set", "save" and "load" subcommands with
effect similar to what they do with the .parameter dot-command.

**.x** is a general purpose, "run these" command. By default, its arguments
are treated as shell variable names, values of which are interpreted as
shell input. Optionally, its arguments are interpreted similarly to the
.eval and .read subcommand. They differ in that interpretation of the
arguments stops upon any error.

Other new shell features are:

By default, when invoked as "sqlite3x", or after ".shxopts +parsing" is run,
the shell effects enhanced command parsing whereby quoted dot-command arguments
may span multiple lines or dot-command input lines may be spliced together
with trailing backslash, and all shell input may have leading whitespace.

When not invoked as "sqlite3x", or after ".shxopts -parsing" is run, any
dot-command arguments and the dot-command itself end at the first newline.
Also, input lines with leading whitespace will not be accepted as dot-commands
or #-prefixed comments.
This might be needed to run legacy shell scripts having some dot-command(s)
with a final argument that has an initial quote but no closing quote or
which happen to end with backslash.

The shell's handling of certain fatal conditions has been changed to make
it more suitable being embedded into other applications. It no longer calls
exit() for OOM conditions or -safe mode violations. This feature has not
yet been fully tested; work remains to be sure that the shell can be called,
then return to its caller, repeatedly without leaking resources or leaving
the console in an odd state. But the infrastructure is in place to make
that all work with only minor (or possibly no) revisions.
