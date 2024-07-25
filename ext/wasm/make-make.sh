#!/usr/bin/bash
########################################################################
# Emits the makefile code for .cache.make. Any makefile bits which are
# potentially expensive to calculate on every build but rarely change
# should be emitted from here.
#
# Arguments:
#
#  $1 = path to sqlite3.c. Default: ../../sqlite3.c
#
########################################################################

function die(){
    local rc=$1
    shift
    echo "\$(error $0 failed: $@)"
    # ^^^ Ensure that if this output is being redirected, the
    # resulting makefile will fail loudly instead of just being
    # truncated.
    echo "Error: $@" 1>&2
    exit $rc
}

SQLITE3_C="${1-../../sqlite3.c}"

echo "# GENERATED makefile code. DO NOT EDIT."

if grep sqlite3_activate_see "$SQLITE3_C" &>/dev/null; then
  echo 'SQLITE_C_IS_SEE := 1'
  echo '$(info This is an SEE build)'
else
  echo 'SQLITE_C_IS_SEE := 0'
fi

########################################################################
# Locate the emcc (Emscripten) binary...
EMCC_BIN=$(which emcc)
if [[ x = "x${EMCC_BIN}" ]]; then
  if [[ x != "x${EMSDK_HOME}" ]]; then
    EMCC_BIN="${EMSDK_HOME}/upstream/emscripten/emcc"
  fi
fi
if [[ x = "x${EMCC_BIN}" ]]; then
  die 1 "Cannot find emcc binary in PATH or EMSDK_HOME."
fi
[[ -x "${EMCC_BIN}" ]] || die 1 "emcc is not executable"
echo "emcc.bin := ${EMCC_BIN}"
echo "emcc.version :=" $("${EMCC_BIN}" --version | sed -n 1p \
                           | sed -e 's/^.* \([3-9][^ ]*\) .*$/\1/;')
echo '$(info using emcc version [$(emcc.version)])'

#########################################################################
# wasm-strip binary...
WASM_STRIP_BIN=$(which wasm-strip 2>/dev/null)
echo "wasm-strip ?= ${WASM_STRIP_BIN}"
if [[ x = "x${WASM_STRIP_BIN}" ]]; then
cat <<EOF
maybe-wasm-strip = echo "not wasm-stripping"
ifeq (,\$(filter clean,\$(MAKECMDGOALS)))'
  \$(info WARNING: *******************************************************************)
  \$(info WARNING: builds using -O2/-O3/-Os/-Oz will minify WASM-exported names,)
  \$(info WARNING: breaking _All The Things_. The workaround for that is to build)
  \$(info WARNING: with -g3 (which explodes the file size) and then strip the debug)
  \$(info WARNING: info after compilation, using wasm-strip, to shrink the wasm file.)
  \$(info WARNING: wasm-strip was not found in the PATH so we cannot strip those.)
  \$(info WARNING: If this build uses any optimization level higher than -O1 then)
  \$(info WARNING: the ***resulting JS code WILL NOT BE USABLE***.)
  \$(info WARNING: wasm-strip is part of the wabt package:)
  \$(info WARNING:    https://github.com/WebAssembly/wabt)
  \$(info WARNING: on Ubuntu-like systems it can be installed with:)
  \$(info WARNING:    sudo apt install wabt)
  \$(info WARNING: *******************************************************************)
endif
EOF
else
  echo 'maybe-wasm-strip = $(wasm-strip)'
fi


dir_dout=jswasm
dir_tmp=bld
for d in $dir_dout $dir_tmp; do
  [ -d $d ] || mkdir -p $d
done
