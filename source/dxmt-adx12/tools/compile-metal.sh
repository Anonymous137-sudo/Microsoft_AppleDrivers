#!/bin/sh
set -eu

[ "$#" -ge 2 ] || {
    printf 'usage: compile-metal.sh input.metal output.air [metal-options...]\n' >&2
    exit 64
}

input=$1
output=$2
shift 2
input_dir=$(CDPATH= cd -- "$(dirname -- "$input")" && pwd)

# Metal records an absolute source name in AIR when given a filesystem input.
# Stdin preserves identical source bytes without embedding the build host path.
xcrun -sdk macosx metal -x metal -I "$input_dir" -c - -o "$output" "$@" < "$input"
