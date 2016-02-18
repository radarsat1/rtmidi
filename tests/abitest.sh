#!/bin/bash

CUR=$(git name-rev --name-only HEAD)
REV=$(git name-rev --name-only "$1")
OPT=$2

function bye {
    echo git checkout $OPT $CUR
    git checkout $OPT $CUR
    exit $1
}

function die {
    echo
    echo === There was an error.
    if [ x"$1" != x ]; then echo === "$1"; fi
    echo
    bye 1
}

echo -n Comparing $CUR and $REV ''
if [ x"$OPT" != x ]; then echo using checkout options \"$OPT\"; fi
echo

if [ x"$CUR" == x"$REV" ]; then
    echo Error: Versions are the same.
    die
fi

echo 
echo === Directory will be CLEANED \(non-tracked files deleted\)
echo -n Continue '(y/n)? '
read ans
if [ x"${ans}" != xy ]; then exit 1; fi

echo
echo === Checking out and building version $REV

echo git checkout $OPT $REV
git checkout $OPT $REV || die

echo git clean -xfd .
git clean -xfd .

function build {
if [ -e autogen.sh ]; then
    echo ./autogen.sh --prefix=$PWD/abitest-$1
    ./autogen.sh --prefix=$PWD/abitest-$1 || die

    echo make
    make || die

    echo make install
    make install || die
else
    # echo
    # echo Error: No ./autogen.sh found.
    # echo This script only works for automake versions of the project.
    # die
    echo autoconf || die
    autoconf

    if ! [ -d abitest-$1 ]; then mkdir -v abitest-$1 || die; fi
    if ! [ -d abitest-$1/lib ]; then mkdir -v abitest-$1/lib || die; fi

    echo ./configure --prefix="$PWD"/abitest-$1
    ./configure --prefix="$PWD"/abitest-$1 || die

    echo make
    make || die

    for i in *.so.*.*.*; do
        echo cp -v "$i" "$PWD"/abitest-$1/lib/
        cp -v "$i" "$PWD"/abitest-$1/lib/ || die
    done
fi
}

build rev

REVLIB=$(ls abitest-rev/lib/*.so.*.*.* | tail -n1)
echo
echo Found "$REVLIB"

echo
echo === Checking out and building version $CUR

echo git checkout $OPT $CUR
git checkout $OPT $CUR || die

echo git clean -xfd -e abitest-rev .
git clean -xfd -e abitest-rev .

build cur

CURLIB=$(ls abitest-cur/lib/*.so.*.*.* | tail -n1)
echo
echo Found "$CURLIB"

function runtest {
echo
echo === Checking "$1" linkage

echo ldd "$1" \| grep librtmidi.so
ldd "$1" | grep librtmidi.so || die

echo
echo === Running "$1" normally

echo "$@"
$@ || die "Test \'$@\' failed running normally!"

echo
echo === Running "$1" with $REV

echo cp "$CURLIB" /tmp/
cp "$CURLIB" /tmp/ || die

echo cp "$REVLIB" "$CURLIB"
cp "$REVLIB" "$CURLIB" || die

echo "$@"
$@ || die "Test \'$@\' failed with old version, problem with ABI compatibility."

echo
echo === Test \'$@\' passed!  ABI compatiblity is good.

echo mv /tmp/$(basename "$CURLIB") "$CURLIB"
mv /tmp/$(basename "$CURLIB") "$CURLIB" || die
}

runtest tests/.libs/midiprobe
runtest tests/.libs/sysextest 10

bye
