# Test Procedures For The SQLite TCL Extension

## 1.0 Background

The SQLite TCL extension logic (in the 
"[tclsqlite.c](/file/src/tclsqlite.c)" source
file) is statically linked into "textfixture" executable
which is the program used to do more of the testing
associated with "make test", "make devtest", and/or
"make releasetest".  So the functionality of the SQLite
TCL extension is thoroughly vetted during normal testing.  The
procedures below are designed to test the loadable extension
aspect of the SQLite TCL extension, and in particular to verify
that the "make tclextension-install" build target works and that
an ordinary tclsh can subsequently run "package require sqlite3".

## 2.0 Testing On Unix-like Systems (Including Mac)

###  2.1 Setup

<ol type="1">
<li value="1"> 
    [Fossil](https://fossil-scm.org/) installed.
<li>
    A Fossil check-out of the TCL source tree.  Let the directory
    of this check-out be called **$TCLCO** (mnemonic: "TCL Check-Out").
<li>
    A Fossil check-out of the SQLite source tree.  Let the directory
    of this check-out be called **$SRCCO** (mnemonic: "SouRCe Check-Out").
<li>
    Let **$TCLTD** (mnemonic: "TCL Test Directory") be the name of a directory
    that does not exist at the start of the test, and which will be 
    deleted at the end of the test, that will contain the test builds
    of the TCL libraries and the SQLite TCL Extensions.
</ol>

### 2.2 Testing TCL 8.6 on unix

<ol type="1">
<li value="5">  `mkdir $TCLTD/tcl86`
<li>  `cd $TCLCO/unix`
<li>  `fossil up core-8-6-16` <br>
      &uarr; Or some other version of Tcl8.6.
<li>  `fossil clean -x`
<li>  `./configure --prefix=$TCLTD/tcl86`
<li>  `make install`
<li> `cd $SRCCO`
<li> `fossil clean -x`
<li> `./configure --with-tclsh=$TCLTD/tcl86/bin/tclsh8.6`
<li> `make tclextension-install` <br>
     &uarr; Verify extension installed at $TCLTD/tcl86/lib/tcl8.6/sqlite3.*
<li> `makek tclextension-list` <br>
     &uarr; Verify TCL extension correctly installed.
<li> `make tclextension-verify` <br>
     &uarr; Verify that the correct version is installed.
<li> `$TCLTD/tcl86/bin/tclsh8.6 test/testrunner.tcl release --explain` <br>
     &uarr; Verify thousands of lines of output with no errors. Or
     consider running "devtest" without --explain instead of "release".
</ol>

### 2.3 Testing TCL 9.0 on unix

<ol>
<li value="18">  `mkdir $TCLTD/tcl90`
<li>  `fossil up core-9-0-0` <br>
      &uarr; Or some other version of Tcl9
<li>  `fossil clean -x`
<li>  `./configure --prefix=$TCLTD/tcl90`
<li>  `make install`
<li> `cd $SRCCO`
<li> `fossil clean -x`
<li> `./configure --with-tclsh=$TCLTD/tcl90/bin/tclsh9.0`
<li> `make tclextension-install` <br>
     &uarr; Verify extension installed at $TCLTD/tcl90/lib/sqlite3.*
<li> `makek tclextension-list` <br>
     &uarr; Verify TCL extension correctly installed.
<li> `make tclextension-verify`
<li> `$TCLTD/tcl90/bin/tclsh9.0 test/testrunner.tcl release --explain` <br>
     &uarr; Verify thousands of lines of output with no errors.  Or
     consider running "devtest" without --explain instead of "release".
</ol>

### 2.4 Cleanup

<ol type="1">
<li value="30"> `rm -rf $TCLTD`
</ol>

## 3.0 Testing On Windows

###  3.1 Setup for Windows

<ol type="1">
<li value="1"> 
    [Fossil](https://fossil-scm.org/) installed.
<li>
    A Fossil check-out of the TCL source tree.  Let the directory
    of this check-out be called **%TCLCO%** (mnemonic: "TCL Check-Out").
<li>
    A Fossil check-out of the SQLite source tree.  Let the directory
    of this check-out be called **%SRCCO%** (mnemonic: "SouRCe Check-Out").
<li>
    Let **%TCLTD%** (mnemonic: "TCL Test Directory") be the name of a directory
    that does not exist at the start of the test, and which will be 
    deleted at the end of the test, that will contain the test builds
    of the TCL libraries and the SQLite TCL Extensions.
<li>
    Unix-like command-line tools installed.  Example:
    [unxutils](https://unxutils.sourceforge.net/)
<li> [Visual Studio](https://visualstudio.microsoft.com/vs/community/)
     installed.  VS2015 or later required.
<li> `set ORIGINALPATH=%PATH%` <br>
     &uarr; remember the original %PATH% value
</ol>

### 3.2 Testing TCL 8.6 on Windows

<ol type="1">
<li value="8">  `mkdir %TCLTD%\tcl86`
<li>  `cd %TCLCO%\win`
<li>  `fossil up core-8-6-16` <br>
      &uarr; Or some other version of Tcl8.6.
<li>  `fossil clean -x`
<li>  `set INSTALLDIR=%TCLTD%\tcl86`
<li>  `nmake /f makefile.vc release` <br>
      &udarr; You *must* invoke the "release" and "install" targets
      using separate invocations of "nmake" or tclsh86t.exe won't be
      installed.
<li>  `nmake /f makefile.vc install`
<li> `cd %SRCCO%`
<li> `fossil clean -x`
<li> `set TCLDIR=%TCLTD%\tcl86`
<li> `set PATH=%TCLTD%\tcl86\bin;%ORIGINALPATH%`
<li> `set TCLSH_CMD=%TCLTD%\tcl86\bin\tclsh86t.exe`
<li> `nmake /f Makefile.msc tclextension-install` <br>
     &uarr; Verify extension installed at %TCLTD%\\tcl86\\lib\\tcl8.6\\sqlite3.*
<li> `nmake /f Makefile.msc tclextension-verify`
<li>`tclsh86t test/testrunner.tcl release --explain` <br>
     &uarr; Verify thousands of lines of output with no errors.  Or
     consider running "devtest" without --explain instead of "release".
</ol>

### 3.3 Testing TCL 9.0 on Windows

<ol>
<li value="23">  `mkdir %TCLTD%\tcl90`
<li>  `cd %TCLCO%\win`
<li>  `fossil up core-9-0-0` <br>
      &uarr; Or some other version of Tcl9
<li>  `fossil clean -x`
<li>  `set INSTALLDIR=%TCLTD%\tcl90`
<li>  `nmake /f makefile.vc release` <br>
      &udarr; You *must* invoke the "release" and "install" targets
      using separate invocations of "nmake" or tclsh90.exe won't be
      installed.
<li>  `nmake /f makefile.vc install`
<li> `cd %SRCCO%`
<li> `fossil clean -x`
<li> `set TCLDIR=%TCLTD%\tcl90`
<li> `set PATH=%TCLTD%\tcl90\bin;%ORIGINALPATH%`
<li> `set TCLSH_CMD=%TCLTD%\tcl90\bin\tclsh90.exe`
<li> `nmake /f Makefile.msc tclextension-install` <br>
     &uarr; Verify extension installed at %TCLTD%\\tcl90\\lib\\sqlite3.*
<li> `nmake /f Makefile.msc tclextension-verify`
<li> `tclsh90 test/testrunner.tcl release --explain` <br>
     &uarr; Verify thousands of lines of output with no errors.  Or
     consider running "devtest" without --explain instead of "release".
</ol>

### 3.4 Cleanup

<ol type="1">
<li value="38"> `rm -rf %TCLTD%`
</ol>
