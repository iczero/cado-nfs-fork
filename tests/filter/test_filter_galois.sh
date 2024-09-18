#!/usr/bin/env bash

# This test is here to check that the filter_galois binary rewrite relations
# correctly.

set -e

build_tree="$PROJECT_BINARY_DIR"
REFERENCE_SHA1="8070f6b82b084ff3a2260fd0b9cd2141fe5ed0f5"

while [ $# -gt 0 ] ; do
    if [ "$1" = "-b" ] ; then
        shift
        build_tree="$1"
        shift
    elif [ "$1" = "-S" ] ; then
        shift
        REFERENCE_SHA1="$1"
        shift
    else
        echo "bad arg: $1" >&2
        exit 1
    fi
done

FG="$build_tree/filter/filter_galois"
SOURCE_TEST_DIR="`dirname "$0"`"

: ${WORKDIR?missing}

poly="${SOURCE_TEST_DIR}/test_filter_galois.d20.poly"
renumber="${SOURCE_TEST_DIR}/test_filter_galois.d20.renumber"
rels="${SOURCE_TEST_DIR}/test_filter_galois.d20.rels"
outrels="${WORKDIR}/test_filter_galois.d20.rels"
args="-poly ${poly} -nrels 35 -renumber ${renumber} -galois _y -dl\
       -outdir ${WORKDIR} ${rels}"

SHA1BIN=sha1sum
if ! type -p "$SHA1BIN" > /dev/null ; then SHA1BIN=sha1 ; fi
if ! type -p "$SHA1BIN" > /dev/null ; then SHA1BIN=shasum ; fi
if ! type -p "$SHA1BIN" > /dev/null ; then
    echo "Could not find a SHA-1 checksumming binary !" >&2
    exit 1
fi

if ! "${FG}" ${args}; then
  echo "$0: filter_galois binary failed. Files remain in ${WORKDIR}"
  exit 1
fi

SHA1=$(cat ${outrels} | grep -v "^#" | sort -n | ${SHA1BIN})
SHA1="${SHA1%% *}"

echo ""
echo "Got SHA1 of ${SHA1}"
echo "expected ${REFERENCE_SHA1}"
if [ "${SHA1}" != "${REFERENCE_SHA1}" ] ; then
  if [ "$CADO_DEBUG" ] ; then
      REFMSG=". Files remain in ${WORKDIR}"
  else
      REFMSG=". Set CADO_DEBUG=1 to examine log output"
  fi
  echo "Got SHA1(sort(${outrels}))=${SHA1} but expected ${REFERENCE_SHA1}${REFMSG}"
  cat ${outrels}
  exit 1
fi
