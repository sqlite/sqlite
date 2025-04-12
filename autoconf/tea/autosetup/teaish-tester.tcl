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
# @section TEA-ish Testing APIs

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

proc test-msg {args} {
  puts "{*}$args"
}

########################################################################
# @test-error msg
#
# Emits an error message to stderr and exits with non-0.
proc test-fail {msg} {
  #puts stderr "ERROR: \[[test-current-scope 1]]: $msg"
  #exit 1
  error "ERROR: \[[test-current-scope 1]]: $msg"
}

########################################################################
# @assert script ?message?
#
# Kind of like a C assert: if uplevel (eval) of [expr {$script}] is
# false, a fatal error is triggered. The error message, by default,
# includes the body of the failed assertion, but if $msg is set then
# that is used instead.
proc assert {script {msg ""}} {
  set x "expr \{ $script \}"
  if {![uplevel 1 $x]} {
    if {"" eq $msg} {
      set msg $script
    }
    test-fail "Assertion failed in \[[test-current-scope 1]]: $msg"
  }
}

########################################################################
# @test-expect testId script result
#
# Runs $script in the calling scope and compares its result to
# $result.  If they differ, it triggers an [assert].
proc test-expect {testId script result} {
  puts "test $testId"
  set x [uplevel 1 $script]
  assert {$x eq $result} "\nEXPECTED: <<$result>>\nGOT:      <<$x>>"
}

########################################################################
# @test-assert testId script ?msg?
#
# Works like [assert] but emits $testId to stdout first.
proc test-assert {testId script {msg ""}} {
  puts "test $testId"
  assert $script $msg
}

########################################################################
# @test-catch cmd ?...args?
#
# Runs [cmd ...args], repressing any exception except to possibly log
# the failure.
proc test-catch {cmd args} {
  if {[catch {
    $cmd {*}$args
  } rc xopts]} {
    puts "[test-current-scope] ignoring failure of: $cmd [lindex $args 0]"
    #how to extract just the message text from $xopts?
  }
}
