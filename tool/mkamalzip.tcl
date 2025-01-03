#!/usr/bin/tclsh
#
# Build a ZIP archive for the amalgamation source code found in the current
# directory.
#
set VERSION-file [file dirname [file dirname [file normalize $argv0]]]/VERSION
set fd [open ${VERSION-file} rb]
set vers [read $fd]
close $fd
scan $vers %d.%d.%d major minor patch
set numvers [format {3%02d%02d00} $minor $patch]
set cmd "zip sqlite-amalgamation-$numvers.zip\
         sqlite3.c sqlite3.h shell.c sqlite3ext.h"
puts $cmd
exec {*}$cmd
