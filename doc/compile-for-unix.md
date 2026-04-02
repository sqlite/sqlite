# Notes On Compiling SQLite On All Kinds Of Unix

Here are step-by-step instructions on how to build SQLite from
canonical source on any modern machine that isn't Windows.  These
notes are tested (on 2024-10-11) on Ubuntu and on MacOS, but they
are general and should work on most any modern unix platform.
See the companion document ([](./compile-for-windows.md>)) for
guidance on building for Windows.

  1.  Install a C-compiler.  GCC or Clang both work fine.  If you are
      reading this document, you've probably already done that.

  2.  *(Optional):* Install TCL development libraries.  In this note,
      we'll do a private install in the $HOME/local directory,
      but you can make adjustments to install TCL wherever you like.
      This document assumes you are working with TCL version 9.0.
      See also the [](./tcl-extension-testing.md) document that contains
      more details on compiling Tcl for use with SQLite.
      <ol type="a">
      <li>Get the TCL source archive, perhaps from
      <https://www.tcl.tk/software/tcltk/download.html>
      or <https://sqlite.org/tmp/tcl9.0.0.tar.gz>.
      <li>Untar the source archive.  CD into the "unix/" subfolder
          of the source tree.
      <li>Run: &ensp; `mkdir $HOME/local`
      <li>Run: &ensp; `./configure --prefix=$HOME/local`<br>
          SQLite deliverable builds add: &emsp; `--static CFLAGS=-Os`
      <li>Run: &ensp; `make install`
      </ol>
      <p>
      As of 2024-10-25, TCL is not longer required for many
      common build targets, such as "sqlite3.c" or the "sqlite3"
      command-line tool.  So you can skip this step if that is all
      you want to build.  TCL is still required to run "make test"
      and similar, or to build the TCL extension, of course.

  4.  Download the SQLite source tree and unpack it. CD into the
      toplevel directory of the source tree.

  5.  *(Optional):* Download Antirez's "linenoise" command-line editing
      library to provide command-line editing in the CLI.  You can find
      a suitable copy of the linenoise sources at
      <https://sqlite.org/linenoise.tar.gz> or at various other locations
      on the internet.  If you put the linenoise source tree in
      a directory named $HOME/linenoise or a directory "linenoise" which
      is a sibling of the SQLite source tree, then the SQLite ./configure
      script will automatically find and use that source code to provide
      command-line editing in the CLI. If you would rather use the readline
      or editline libraries or a precompiled linenoise library, there are
      ./configure options to accommodate that choice. The SQLite developers
      typically use $HOME/linenoise since linenoise is small, has no
      external dependencies, "just works", and the ./configure script
      will pick it up and use it automatically. But you do what works
      best for you.
      <p>
      You are not required to have any command-line editing support in
      order to use SQLite.  But command-line editing does make the
      interactive experience more enjoyable.

  6.  Run: `./configure --enable-all --with-tclsh=$HOME/local/bin/tclsh9.0`

      You do not need to use --with-tclsh if the tclsh you want to use is the
      first one on your PATH or if you are building without TCL.

      Lots of other options to ./configure are available.
      Run `./configure --help` for further guidance.

  7.  Run the "`Makefile`" makefile with an appropriate target.
      Examples:
      <ul>
      <li>  `make sqlite3.c`
      <li>  `make sqlite3`
      <li>  `make sqldiff`
      <li>  `make sqlite3_rsync`
      </ul>
      <p>None of the targets above require TCL.  TCL is needed
      for the following targets:
      <ul>
      <li>  `make tclextension-install`
      <li>  `make devtest`
      <li>  `make releasetest`
      <li>  `make sqlite3_analyzer`
      </ul>

      It is not required that you run the "tclextension-install" target prior to
      running tests.  However, the tests will run more smoothly if you do.
      The version of SQLite used for the TCL extension does *not* need to
      correspond to the version of SQLite under test.  So you can install the
      SQLite TCL extension once, and then use it to test many different versions
      of SQLite.


  8.  For a debugging build of the CLI, use `./configure --dev`.  A debugging
      build contains lots of extra debugging code, so it is slow and a lot
      bigger.  You probably do not want to deploy a debugging build.  But if
      you are working on the code, a debugging build works much better.
