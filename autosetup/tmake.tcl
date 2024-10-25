# Copyright (c) 2011 WorkWare Systems http://www.workware.net.au/
# All rights reserved

# @synopsis:
#
# The 'tmake' module makes it easy to support the tmake build system.
#
# The following variables are set:
#
## CONFIGURED  - to indicate that the project is configured

use system

options {}

define CONFIGURED

# @make-tmake-settings outfile patterns ...
#
# Examines all defined variables which match the given patterns (defaults to '*')
# and writes a tmake-compatible .conf file defining those variables.
# For example, if 'ABC' is '"3 monkeys"' and 'ABC' matches a pattern, then the file will include:
#
## define ABC {3 monkeys}
#
# If the file would be unchanged, it is not written.
#
# Typical usage is:
#
## make-tmake-settings [get-env BUILDDIR objdir]/settings.conf {[A-Z]*}
proc make-tmake-settings {file args} {
	file mkdir [file dirname $file]
	set lines {}

	if {[llength $args] == 0} {
		set args *
	}

	foreach n [lsort [dict keys [all-defines]]] {
		foreach p $args {
			if {[string match $p $n]} {
				set value [get-define $n]
				lappend lines "define $n [list $value]"
				break
			}
		}
	}
	set buf [join $lines \n]
	write-if-changed $file $buf {
		msg-result "Created $file"
	}
}
