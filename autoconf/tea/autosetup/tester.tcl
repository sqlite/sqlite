########################################################################
# 2025 April 5
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#  * May you do good and not evil.
#  * May you find forgiveness for yourself and forgive others.
#  * May you share freely, never taking more than you give.
#
########################################################################
#
# Helper routines for running automated tests on teaish extensions
#
########################################################################
# ----- @module teaish-tester.tcl -----
#
# @section TEA-ish Testing APIs.
#
# Though these are part of the autosup dir hierarchy, they are not
# intended to be run from autosetup code. Rather, they're for
# use with/via teaish.tester.tcl.

########################################################################
# @test-current-scope ?lvl?
#
# Returns the name of the _calling_ proc from ($lvl + 1) levels up the
# call stack (where the caller's level will be 1 up from _this_
# call). If $lvl would resolve to global scope "global scope" is
# returned and if it would be negative then a string indicating such
# is returned (as opposed to throwing an error).
proc test-current-scope {{lvl 0}} {
  #uplevel [expr {$lvl + 1}] {lindex [info level 0] 0}
  set ilvl [info level]
  set offset [expr {$ilvl  - $lvl - 1}]
  if { $offset < 0} {
    return "invalid scope ($offset)"
  } elseif { $offset == 0} {
    return "global scope"
  } else {
    return [lindex [info level $offset] 0]
  }
}

# @test-msg
#
# Emits all arugments to stdout.
proc test-msg {args} {
  puts "$args"
}

# @test-warn
#
# Emits all arugments to stderr.
proc test-warn {args} {
  puts stderr "WARNING: $args"
}

########################################################################
# @test-error msg
#
# Triggers a test-failed error with a string describing the calling
# scope and the provided message.
proc test-fail {msg} {
  #puts stderr "ERROR: \[[test-current-scope 1]]: $msg"
  #exit 1
  error "FAIL: \[[test-current-scope 1]]: $msg"
}

# Internal impl for assert-likes. Should not be called directly by
# client code.
proc test__assert {lvl script {msg ""}} {
  set src "expr \{ $script \}"
  # puts "XXXX evalling $src";
  if {![uplevel $lvl $src]} {
    if {"" eq $msg} {
      set msg $script
    }
    set caller1 [test-current-scope $lvl]
    incr lvl
    set caller2 [test-current-scope $lvl]
    error "Assertion failed in: \[$caller2 -> $caller1]]: $msg"
  }
}

########################################################################
# @assert script ?message?
#
# Kind of like a C assert: if uplevel (eval) of [expr {$script}] is
# false, a fatal error is triggered. The error message, by default,
# includes the body of the failed assertion, but if $msg is set then
# that is used instead.
proc assert {script {msg ""}} {
  test__assert 1 $script $msg
}

########################################################################
# @test-assert testId script ?msg?
#
# Works like [assert] but emits $testId to stdout first.
proc test-assert {testId script {msg ""}} {
  puts "test $testId"
  test__assert 2 $script $msg
}

########################################################################
# @test-expect testId script result
#
# Runs $script in the calling scope and compares its result to
# $result.  If they differ, it triggers an [assert].
proc test-expect {testId script result} {
  puts "test $testId"
  set x [uplevel 1 $script]
  test__assert 1 {$x eq $result} "\nEXPECTED: <<$result>>\nGOT:      <<$x>>"
}

########################################################################
# @test-catch cmd ?...args?
#
# Runs [cmd ...args], repressing any exception except to possibly log
# the failure. Returns 1 if it caught anything, 0 if it didn't.
proc test-catch {cmd args} {
  if {[catch {
    $cmd {*}$args
  } rc xopts]} {
    puts "[test-current-scope] ignoring failure of: $cmd [lindex $args 0]: $rc"
    return 1
  }
  return 0
}

array set teaish__BuildFlags {}

# @teaish-build-flag2 flag tgtVar ?dflt?
#
# Caveat #1: only valid when called in the context of teaish's default
# "make test" recipe, e.g. from teaish.test.tcl. It is not valid from
# a teaish.tcl configure script because (A) the state it relies on
# doesn't fully exist at that point and (B) that level of the API has
# more direct access to the build state. This function requires that
# an external script have populated its internal state, which is
# normally handled via teaish.tester.tcl.in.
#
# If the current build has the configure-time flag named $flag set
# then tgtVar is assigned its value and 1 is returned, else tgtVal is
# assigned $dflt and 0 is returned.
#
# Caveat #2: defines in the style of HAVE_FEATURENAME with a value of
# 0 are, by long-standing configure script conventions, treated as
# _undefined_ here.
#
proc teaish-build-flag2 {flag tgtVar {dflt ""}} {
  upvar $tgtVar tgt
  if {[info exists ::teaish__BuildFlags($flag)]} {
    set tgt $::teaish__BuildFlags($flag)
    return 1;
  } elseif {0==[array size ::teaish__BuildFlags]} {
    test-warn \
      "\[[test-current-scope]] was called from " \
      "[test-current-scope 1] without the build flags imported."
  }
  set tgt $dflt
  return 0
}

# @teaish-build-flag flag ?dflt?
#
# Convenience form of teaish-build-flag2 which returns the
# configure-time-defined value of $flag or "" if it's not defined (or
# if it's an empty string).
proc teaish-build-flag {flag {dflt ""}} {
  set tgt ""
  teaish-build-flag2 $flag tgt $dflt
  return $tgt
}
