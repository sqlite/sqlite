#!/usr/bin/env bash
#
# Creates the zip bundle for the sqlite3 wasm builds.
# $1 is a build name, defaulting to sqlite-wasm.

function die(){
    local rc=$1
    shift
    echo "Error: $@" 1>&2
    exit $rc
}

buildName=${1-sqlite-wasm}

make=
for i in gmake make; do
    make=$(which $i 2>/dev/null)
    [[ x != x$make ]] && break
done
[[ x = x$make ]] && die 127 "Cannot find make"

dirTop=../..

echo "Creating the SQLite wasm dist bundle..."

#
# Generates files which, when built, will also build all of the pieces
# neaded for the dist bundle.
#
tgtFiles=(
    tester1.html
    tester1-worker.html
    tester1-esm.html
    tester1.js tester1.mjs
    demo-worker1-promiser.html
    demo-worker1-promiser.js
    demo-worker1-promiser-esm.html
    demo-worker1-promiser.mjs
)

if false; then
    optFlag=-O0
else
    optFlag=-Oz
    $make clean
fi
$make -j4 \
      t-version-info t-stripccomments \
      ${tgtFiles[@]} \
      "emcc_opt=${optFlag}" || die $?

dirTmp=d.dist
rm -fr $dirTmp
mkdir -p $dirTmp/jswasm || die $?
mkdir -p $dirTmp/common || die $?

# Files for the top-most dir:
fTop=(
    demo-worker1.html
    demo-worker1.js
    demo-jsstorage.html
    demo-jsstorage.js
    demo-123.html
    demo-123-worker.html demo-123.js
)

# Files for the jswasm subdir sans jswasm prefix:
#
# fJ1 = JS files to stripccomments -k on
# fJ2 = JS files to stripccomments -k -k on
fJ1=(
    sqlite3-opfs-async-proxy.js
    sqlite3-worker1.js
    sqlite3-worker1.mjs
    sqlite3-worker1-bundler-friendly.mjs
    sqlite3-worker1-promiser.js
    sqlite3-worker1-promiser.mjs
    sqlite3-worker1-promiser-bundler-friendly.mjs
)
fJ2=(
    sqlite3.js
    sqlite3.mjs
)

function fcp() {
    cp -p $@ || die $?
}

function scc(){
    ${dirTop}/tool/stripccomments $@ || die $?
}

jw=jswasm
fcp ${tgtFiles[@]} $dirTmp/.
fcp README-dist.txt $dirTmp/README.txt
fcp index-dist.html $dirTmp/index.html
fcp common/*.css common/SqliteTestUtil.js $dirTmp/common/.
fcp $jw/sqlite3.wasm $dirTmp/$jw/.

for i in ${fTop[@]}; do
    fcp $i $dirTmp/.
done

for i in ${fJ1[@]}; do
    scc -k < $jw/$i > $dirTmp/$jw/$i || die $?
done

for i in ${fJ2[@]}; do
    scc -k -k < $jw/$i > $dirTmp/$jw/$i || die $?
done

#
# Done copying files. Now zip it up...
#

svi=${dirTop}/version-info
vnum=$($svi --download-version)
vdir=${buildName}-${vnum}
fzip=${vdir}.zip
rm -fr ${vdir} ${fzip}
mv $dirTmp $vdir || die $?
zip -rq $fzip $(find $vdir -type f | sort) || die $?
ls -la $fzip
unzip -lv $fzip || die $?
