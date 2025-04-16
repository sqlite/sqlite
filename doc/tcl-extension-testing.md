# Test Procedures For The SQLite TCL Extension

## 1.0 Background

The SQLite TCL extension logic (in the 
"[tclsqlite.c](/file/src/tclsqlite.c)" source
file) is statically linked into "textfixture" executable
which is the program used to do most of the testing
associated with "make test", "make devtest", and/or
"make releasetest".  So the functionality of the SQLite
TCL extension is thoroughly vetted during normal testing.  The
procedures below are designed to test the loadable extension
aspect of the SQLite TCL extension, and in particular to verify
that the "make tclextension-install" build target works and that
an ordinary tclsh can subsequently run "package require sqlite3".

This procedure can also be used as a template for how to set up
a local TCL+SQLite development environment.  In other words, it
can be be used as a guide on how to compile per-user copies of 
Tcl that are used to develop, test, and debug SQLite.  In that
case, perhaps make minor changes to the procedure such as:

  *  Make TCLBUILD directory is permanent.
  *  Enable debugging symbols on the Tcl library build.
  *  Reduce the optimization level to -O0 for easier debugging.
  *  Also compile "wish" to go with each "tclsh".


<a id="unix"></a>
## 2.0 Testing On Unix-like Systems (Including Mac)

See also the [](./compile-for-unix.md) document which provides another
perspective on how to compile SQLite on unix-like systems.

###  2.1 Setup

<ol type="1">
<li value="1">
    [Fossil][] installed.
<li> Check out source code and set environment variables:
   <ol type="a">
   <li> **TCLSOURCE** &rarr;
    The top-level directory of a [Fossil][] check-out of the
    [TCL source tree][tcl-fossil].
   <li> **SQLITESOURCE** &rarr;
    A Fossil check-out of the SQLite source tree.
   <li> **TCLHOME** &rarr;
    A directory that does not exist at the start of the test and which
    will be deleted at the end of the test, and that will contain the
    test builds of the TCL libraries and the SQLite TCL Extensions.
    It is the top-most installation directory, i.e. the one provided
    to Tcl's `./configure --prefix=/path/to/tcl`.
   <li> **TCLVERSION** &rarr;
    The `X.Y`-form version of Tcl being used: 8.6, 9.0, 9.1...
   </ol>
</ol>

### 2.2 Testing TCL 8.x and 9.x on unix

From a checked-out copy of [the core Tcl tree][tcl-fossil]

<ol type="1">
<li value="3">`TCLVERSION=8.6` <br>
      &uarr; A version of your choice. This process has been tested with
      values of 8.6, 9.0, and 9.1 (as of 2025-04-16). The out-of-life
      version 8.5 fails some of `make devtest` for undetermined reasons.
<li>`TCLHOME=$HOME/tcl/$TCLVERSION`
<li>`TCLSOURCE=/path/to/tcl/checkout`
<li>`SQLITESOURCE=/path/to/sqlite/checkout`
<li>`rm -fr $TCLHOME` <br>
      &uarr; Ensure that no stale Tcl installation is laying around.
<li> `cd $TCLSOURCE`
<li>  `fossil up core-8-6-branch` <br>
      &uarr; The branch corresponding to `$TCLVERSION`, e.g.
      `core-9-0-branch` or `trunk`.
<li>  `fossil clean -x`
<li>  `cd unix`
<li>  `./configure --prefix=$TCLHOME --disable-shared` <br>
      &uarr; The `--disable-shared` is to avoid the need to set `LD_LIBRARY_PATH`
      when using this Tcl build.
<li>  `make install`
<li> `cd $SQLITESOURCE`
<li> `fossil clean -x`
<li> `./configure --with-tcl=$TCLHOME --all`
<li> `make tclextension-install` <br>
     &uarr; Verify extension installed at
     `$TCLHOME/lib/tcl${TCLVERSION}/sqlite<SQLITE_VERSION>`.
<li> `make tclextension-list` <br>
     &uarr; Verify TCL extension correctly installed.
<li> `make tclextension-verify` <br>
     &uarr; Verify that the correct version is installed.
<li> `$TCLHOME/bin/tclsh[89].[0-9] test/testrunner.tcl release --explain` <br>
     &uarr; Verify thousands of lines of output with no errors. Or
     consider running "devtest" without --explain instead of "release".
</ol>

### 2.3 Cleanup

<ol type="1">
<li value="29"> `rm -rf $TCLHOME`
</ol>

<a id="windows"></a>
## 3.0 Testing On Windows

See also the [](./compile-for-windows.md) document which provides another
perspective on how to compile SQLite on Windows.

###  3.1 Setup for Windows

(These docs are not as up-to-date as the Unix docs, above.)

<ol type="1">
<li value="1">
    [Fossil][] installed.
<li>
    Unix-like command-line tools installed.  Example:
    [unxutils](https://unxutils.sourceforge.net/)
<li> [Visual Studio](https://visualstudio.microsoft.com/vs/community/)
     installed.  VS2015 or later required.
<li> Check out source code and set environment variables.
   <ol type="a">
   <li> **TCLSOURCE** &rarr;
    The top-level directory of a Fossil check-out of the TCL source tree.
   <li> **SQLITESOURCE** &rarr;
    A Fossil check-out of the SQLite source tree.
   <li> **TCLBUILD** &rarr;
    A directory that does not exist at the start of the test and which
    will be deleted at the end of the test, and that will contain the
    test builds of the TCL libraries and the SQLite TCL Extensions.
    <li> **ORIGINALPATH** &rarr;
    The original value of %PATH%.  In other words, set as follows:
    `set ORIGINALPATH %PATH%`
   </ol>
</ol>

### 3.2 Testing TCL 8.6 on Windows

<ol type="1">
<li value="5">  `mkdir %TCLBUILD%\tcl86`
<li>  `cd %TCLSOURCE%\win`
<li>  `fossil up core-8-6-16` <br>
      &uarr; Or some other version of Tcl8.6.
<li>  `fossil clean -x`
<li>  `set INSTALLDIR=%TCLBUILD%\tcl86`
<li>  `nmake /f makefile.vc release` <br>
      &udarr; You *must* invoke the "release" and "install" targets
      using separate "nmake" commands or tclsh86t.exe won't be
      installed.
<li>  `nmake /f makefile.vc install`
<li> `cd %SQLITESOURCE%`
<li> `fossil clean -x`
<li> `set TCLDIR=%TCLBUILD%\tcl86`
<li> `set PATH=%TCLBUILD%\tcl86\bin;%ORIGINALPATH%`
<li> `set TCLSH_CMD=%TCLBUILD%\tcl86\bin\tclsh86t.exe`
<li> `nmake /f Makefile.msc tclextension-install` <br>
     &uarr; Verify extension installed at %TCLBUILD%\\tcl86\\lib\\tcl8.6\\sqlite3.*
<li> `nmake /f Makefile.msc tclextension-verify`
<li>`tclsh86t test/testrunner.tcl release --explain` <br>
     &uarr; Verify thousands of lines of output with no errors.  Or
     consider running "devtest" without --explain instead of "release".
</ol>

### 3.3 Testing TCL 9.0 on Windows

<ol>
<li value="20">  `mkdir %TCLBUILD%\tcl90`
<li>  `cd %TCLSOURCE%\win`
<li>  `fossil up core-9-0-0` <br>
      &uarr; Or some other version of Tcl9
<li>  `fossil clean -x`
<li>  `set INSTALLDIR=%TCLBUILD%\tcl90`
<li>  `nmake /f makefile.vc release` <br>
      &udarr; You *must* invoke the "release" and "install" targets
      using separate "nmake" commands or tclsh90.exe won't be
      installed.
<li>  `nmake /f makefile.vc install`
<li> `cd %SQLITESOURCE%`
<li> `fossil clean -x`
<li> `set TCLDIR=%TCLBUILD%\tcl90`
<li> `set PATH=%TCLBUILD%\tcl90\bin;%ORIGINALPATH%`
<li> `set TCLSH_CMD=%TCLBUILD%\tcl90\bin\tclsh90.exe`
<li> `nmake /f Makefile.msc tclextension-install` <br>
     &uarr; Verify extension installed at %TCLBUILD%\\tcl90\\lib\\sqlite3.*
<li> `nmake /f Makefile.msc tclextension-verify`
<li> `tclsh90 test/testrunner.tcl release --explain` <br>
     &uarr; Verify thousands of lines of output with no errors.  Or
     consider running "devtest" without --explain instead of "release".
</ol>

### 3.4 Cleanup

<ol type="1">
<li value="35"> `rm -rf %TCLBUILD%`
</ol>

[Fossil]: https://fossil-scm.org/home
[tcl-fossil]: https://core.tcl-lang.org/tcl
