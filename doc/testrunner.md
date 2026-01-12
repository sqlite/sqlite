

# The testrunner.tcl Script

<ul type=none>
  <li> 1. <a href=#overview>Overview</a>
<ul type=none>
  <li> 1.1. <a href=#runtr>Running testrunner.tcl</a>
  <li> 1.2. <a href=#runviamake>Run using "make"</a>
  <li> 1.3. <a href=#outputs>Outputs from testrunner.tcl</a>
  <li> 1.4. <a href=#help>Built-in help</a>
</ul>
  <li> 2. <a href=#binary_tests>Binary Tests</a>
<ul type=none>
  <li> 2.1. <a href=#organization_tests>Organization of Tcl Tests</a>
  <li> 2.2. <a href=#run_tests>Commands to Run Tests</a>
  <li> 2.3. <a href=#binary_test_failures>Investigating Binary Test Failures</a>
</ul>
  <li> 3. <a href=#source_code_tests>Source Tests</a>
<ul type=none>
  <li> 3.1. <a href=#commands_to_run_tests>Commands to Run SQLite Tests</a>
  <li> 3.2. <a href=#zipvfs_tests>Running ZipVFS Tests</a>
  <li> 3.3. <a href=#source_code_test_failures>Investigating Source Code Test Failures</a>
  <li> 3.4. <a href=#fuzzdb>External Fuzzcheck Databases</a>
</ul>
  <li> 4. <a href=#testrunner_options>Extra testrunner.tcl Options</a>
  <li> 5. <a href=#cpu_cores>Controlling CPU Core Utilization</a>
</ul>

<a name=overview></a>
# 1. Overview

The testrunner.tcl program is a Tcl script used to run multiple SQLite
tests in parallel, thus reducing testing time on multi-core machines.
The testrunner.tcl supports running tests that based on `testfixture`,
`sqlite3`, and `fuzzcheck`.

<a name="runtr"></a>
## 1.1 Running testrunner.tcl

The testrunner.tcl script is located in the "test" subdirectory of the
SQLite source tree.  So if your shell is current positioned at the top
of the source tree, you would normally run the script using the command:
"<tt>test/testrunner.tcl</tt>".  On Windows, you have to specify the
`tclsh` interpreter command first, like this:
"<tt>tclsh&nbsp;test/testrunner.tcl</tt>".

In this document, we will assume that you are on a unix-like OS
(not on Windows) and that your current directory is the root
of the SQLite source tree, and so all invocations of the testrunner.tcl
script will be of the form "<tt>test/testrunner.tcl</tt>".  If you
are in a different directory, then make appropriate adjustments to
the path.  On Windows, add the "<tt>tclsh</tt>" interpreter command
up front.

<a name="runviamake"></a>
## 1.2 Run using make

The standard Makefiles for SQLite include targets that invoke
testrunner.tcl.  So the following commands also run testrunner.tcl:

   *  `make devtest`
   *  `make releasetest`
   *  `make sdevtest`
   *  `make testrunner`

<a name="outputs"></a>
## 1.3 Outputs from testrunner.tcl

The testrunner.tcl program stores output of all tests and builds run in
log file **testrunner.log**, created in the current working directory.
Search this file to find details of errors.  Suggested search commands:

   *  `grep "^!" testrunner.log`
   *  `grep failed testrunner.log`

The testrunner.tcl program also populates SQLite database **testrunner.db**.
This database contains details of all tests run, running and to be run.
A useful query might be:

```
  SELECT * FROM script WHERE state='failed'
```

You can get a summary of errors in a prior run by invoking commands like
those shown below.  Note that the testrunner.tcl script can be run directly
on unix systems (including Macs) but you will need to add <tt>tclsh</tt>
to the front on Windows.

```
  test/testrunner.tcl errors
  test/testrunner.tcl errors -v
```

Running the command:

```
  test/testrunner.tcl status
```

in the directory containing the testrunner.db database runs various queries
to produce a succinct report on the state of a running testrunner.tcl script.
A good way to keep and eye on test progress is to run either of the two
following commands:

```
  watch test/testrunner.tcl status
  test/testrunner.tcl status -d 2
```

Both of the commands above accomplish about the same thing, but the second
one has the advantage of not requiring "watch" to be installed on your
system.

Sometimes testrunner.tcl uses the `testfixture` and `sqlite3` binaries that
are in the directory from which testrunner.tcl is run.
(see "Binary Tests" below). Sometimes it builds testfixture and
other binaries in specific configurations to test (see "Source Tests").

<a name=help></a>
## 1.4 Built-in help

Run this command:

```
  test/testrunner.tcl help
```

To get a summary of all of the various command-line options available
with testrunner.tcl


<a name=binary_tests></a>
# 2. Binary Tests

The commands described in this section all run various combinations of the Tcl
test scripts using whatever `testfixture` binary (and maybe also the `sqlite3`
binary, depending on the test) that is found in the directory from which
testrunner.tcl is launched.  So typically, one must first run something
like "`make testfixture sqlite3`" before launching binary tests.  In other
words, testrunner.tcl does not automatically build the binaries under test
for binary tests.  The testrunner.tcl expects the binaries to be available
already.

The following sub-sections describe the various options that can be
passed to testrunner.tcl to test binary testfixture builds.

<a name=organization_tests></a>
## 2.1. Organization of Tcl Tests

Tcl tests are stored in files that match the pattern *\*.test*. They are
found in both the $TOP/test/ directory, and in the various sub-directories
of the $TOP/ext/ directory of the source tree. Not all *\*.test* files
contain Tcl tests - a handful are Tcl scripts designed to invoke other
*\*.test* files.

The **veryquick** set of tests is a subset of all Tcl test scripts in the
source tree. In includes most tests, but excludes some that are very slow.
Almost all fault-injection tests (those that test the response of the library
to OOM or IO errors) are excluded. It is defined in source file 
*test/permutations.test*.

The **full** set of tests includes all Tcl test scripts in the source tree.
To run a "full" test is to run all Tcl test scripts that can be found in the
source tree.

File *permutations.test* defines various test "permutations". A permutation
consists of:

  *  A subset of Tcl test scripts, and 

  *  Runtime configuration to apply before running each test script 
     (e.g. enabling auto-vacuum, or disable lookaside).

Running **all** tests is to run all tests in the full test set, plus a dozen
or so permutations. The specific permutations that are run as part of "all"
are defined in file *testrunner_data.tcl*.

<a name=run_tests></a>
## 2.2. Commands to Run Tests

To run the "veryquick" test set, use either of the following:

```
  test/testrunner.tcl
  test/testrunner.tcl veryquick
```

To run the "full" test suite:

```
  test/testrunner.tcl full
```

To run the subset of the "full" test suite for which the test file name matches
a specified pattern (e.g. all tests that start with "fts5"), either of:

```
  test/testrunner.tcl fts5%
  test/testrunner.tcl 'fts5*'
```

Strictly speaking, for a test to be run the pattern must match the script
filename, not including the directory, using the rules of Tcl's 
\[string match\] command. Except that before the matching is done, any "%"
characters specified as part of the pattern are transformed to "\*".


To run "all" tests (full + permutations):

```
  test/testrunner.tcl all
```

<a name=binary_test_failures></a>
## 2.3. Investigating Binary Test Failures

If a test fails, testrunner.tcl reports name of the Tcl test script and, if
applicable, the name of the permutation, to stdout. This information can also
be retrieved from either *testrunner.log* or *testrunner.db*.

If there is no permutation, the individual test script may be run with:

```
  ./testfixture $PATH_TO_SCRIPT
```

Or, if the failure occured as part of a permutation:

```
  test/testrunner.tcl $PERMUTATION $PATH_TO_SCRIPT
```

One can also rerun all tests that failed or did not complete
in the previous invocation by typing:

```
  test/testrunner.tcl retest
```

<a name=source_code_tests></a>
# 3. Source Code Tests

The commands described in this section invoke the C compiler to build 
binaries from the source tree, then use those binaries to run Tcl and
other tests. The advantages of this are that:

  *  it is possible to test multiple build configurations with a single
     command, and 

  *  it ensures that tests are always run using binaries created with the
     same set of compiler options.

The testrunner.tcl commands described in this section do not require that
the testfixture and/or sqlite3 binaries be built ahead of time.  Those
binaries will be constructed automatically.

<a name=commands_to_run_tests></a>
## 3.1. Commands to Run SQLite Tests

The **mdevtest** command is equivalent to running the veryquick tests and
the `make fuzztest` target once for each of two --enable-all builds - one 
with debugging enabled and one without:

```
  test/testrunner.tcl mdevtest
```

In other words, it is equivalent to running:

```
  $TOP/configure --enable-all --enable-debug
  make fuzztest
  make testfixture
  $TOP/test/testrunner.tcl veryquick

  # Then, after removing files created by the tests above:
  $TOP/configure --enable-all OPTS="-O0"
  make fuzztest
  make testfixture
  $TOP/test/testrunner.tcl veryquick
```

The **sdevtest** command is identical to the mdevtest command, except that the
second of the two builds is a sanitizer build. Specifically, this means that
OPTS="-fsanitize=address,undefined" is specified instead of OPTS="-O0":

```
  test/testrunner.tcl sdevtest
```

The **release** command runs lots of tests under lots of builds. It runs
different combinations of builds and tests depending on whether it is run
on Linux, Windows or OSX. Refer to *testrunner\_data.tcl* for the details
of the specific tests run.

```
  test/testrunner.tcl release
```

As with <a href=#source code tests>source code tests</a>, one or more patterns
may be appended to any of the above commands (mdevtest, sdevtest or release).
Pattern matching is used for both Tcl tests and fuzz tests.

```
  test/testrunner.tcl release rtree%
```

<a name=zipvfs_tests></a>
## 3.2. Running ZipVFS Tests

testrunner.tcl can build a zipvfs-enabled testfixture and use it to run
tests from the Zipvfs project with the following command:

```
  test/testrunner.tcl --zipvfs $PATH_TO_ZIPVFS
```

This can be combined with any of "mdevtest", "sdevtest" or "release" to
test both SQLite and Zipvfs with a single command:

```
  test/testrunner.tcl --zipvfs $PATH_TO_ZIPVFS mdevtest
```

<a name=source_code_test_failures></a>
## 3.3. Investigating Source Code Test Failures

Investigating a test failure that occurs during source code testing is a
two step process:

  1. Recreating the build configuration in which the test failed, and

  2. Re-running the actual test.

To recreate a build configuration, use the testrunner.tcl **script** command
to create a build script. A build script is a bash script on Linux or OSX, or
a dos \*.bat file on windows. For example:

```
  # Create a script that recreates build configuration "Device-One" on 
  # Linux or OSX:
  test/testrunner.tcl script Device-One > make.sh 

  # Create a script that recreates build configuration "Have-Not" on Windows:
  test/testrunner.tcl script Have-Not > make.bat 
```

The generated bash or \*.bat file script accepts a single argument - a makefile
target to build. This may be used either to run a `make` command test directly,
or else to build a testfixture (or testfixture.exe) binary with which to
run a Tcl test script, as <a href=#binary_test_failures>described above</a>.

<a name=fuzzdb></a>
## 3.4 External Fuzzcheck Databases

Testrunner.tcl will also run fuzzcheck against an external (out of tree)
database, for example fuzzcheck databases generated by dbsqlfuzz.  To do
this, simply add the "`--fuzzdb` *FILENAME*" command-line option or set 
the FUZZDB environment variable to the name of the external
database.  For large external databases, testrunner.tcl will automatically use
the "`--slice`" command-line option of fuzzcheck to divide the work up into
multiple jobs, to increase parallelism.

Thus, for example, to run a full releasetest including an external
dbsqlfuzz database, run a command like one of these:

```
  test/testrunner.tcl releasetest --fuzzdb ../fuzz/20250415.db
  FUZZDB=../fuzz/20250415.db make releasetest
  nmake /f Makefile.msc FUZZDB=../fuzz/20250415.db releasetest
```

The patternlist option to testrunner.tcl will match against fuzzcheck
databases.  So if you want to run *only* tests involving the external
database, you can use a command something like this:

```
  test/testrunner.tcl releasetest 20250415 --fuzzdb ../fuzz/20250415.db
```

<a name=testrunner_options></a>
# 4. Extra testrunner.tcl Options

The testrunner.tcl script options in this section may be used with both source
code and binary tests.

The **--buildonly** option instructs testrunner.tcl just to build the binaries
required by a test, not to run any actual tests. For example:

```
  # Build binaries required by release test.
  test/testrunner.tcl --buildonly release"
```

The **--dryrun** option prevents testrunner.tcl from building any binaries
or running any tests. Instead, it just writes the shell commands that it
would normally execute into the testrunner.log file. Example:

```
  # Log the shell commmands that make up the mdevtest test.
  test/testrunner.tcl --dryrun mdevtest"
```

The **--explain** option is similar to --dryrun in that it prevents
testrunner.tcl from building any binaries or running any tests.  The
difference is that --explain prints on standard output a human-readable
summary of all the builds and tests that would have been run.

```
  # Show what builds and tests would have been run
  test/testrunner.tcl --explain mdevtest
```

The **--status** option uses VT100 escape sequences to display the test
status full-screen.  This is similar to running 
"`watch test/testrunner status`" in a separate window, just more convenient.
Unfortunately, this option does not work correctly on Windows, due to the
sketchy implementation of VT100 escapes on the Windows console.

<a name=cpu_cores></a>
# 5. Controlling CPU Core Utilization

When running either binary or source code tests, testrunner.tcl reports the
number of jobs it intends to use to stdout. e.g.

```
  $ test/testrunner.tcl
  splitting work across 16 jobs
  ... more output ...
```

By default, testfixture.tcl attempts to set the number of jobs to the number 
of real cores on the machine. This can be overridden using the "--jobs" (or -j)
switch:

```
  $ test/testrunner.tcl --jobs 8
  splitting work across 8 jobs
  ... more output ...
```

The number of jobs may also be changed while an instance of testrunner.tcl is
running by exucuting the following command from the directory containing the
testrunner.log and testrunner.db files:

```
  $ test/testrunner.tcl njob $NEW_NUMBER_OF_JOBS
```
