#/usr/bin/tclsh
#
# This is a TCL script that copies multiple files into a common directory.
# The "cp" command will do this on unix, but no such command is available
# by default on Windows, so we have to use this script.
#
#   tclsh cp.tcl FILE1 FILE2 ... FILEN DIR
#
file copy -force -- {*}$argv
