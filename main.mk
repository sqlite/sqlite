###############################################################################
# This is the main makefile for sqlite. It expects to be included from
# a higher-level makefile which configures any dynamic state needed by
# this one.
#
# The following variables must be defined before this script is
# invoked:
#
#XX# FIXME: the list of vars from the historical main.mk is dated and
#XX# needs to be updated for autosetup.
#
# TOP              The toplevel directory of the source tree.  This is the
#                  directory that contains this "Makefile.in" and the
#                  "configure.in" script.
#
# BCC              C Compiler and options for use in building executables that
#                  will run on the platform that is doing the build.
#
# TCC              C Compiler and options for use in building executables that
#                  will run on the target platform.  This is usually the same
#                  as BCC, unless you are cross-compiling.
#
# AR               Tools used to build a static library.
#
# BEXE             File extension for executables on the build platform. ".exe"
#                  for Windows and "" everywhere else.
#
# TEXE             File extension for executables on the target platform. ".exe"
#                  for Windows and "" everywhere else.
#
# ... and many, many more ...
#
# Once the variables above are defined, the rest of this make script
# will build the SQLite library and testing tools.
################################################################################
