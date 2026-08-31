#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
source_root="$repo_root/Direct3D_12(Feature_Level_12_2)"
build_root=${ADX12_FRONTEND_BUILD_DIR:-"${TMPDIR:-/tmp}/adx12-frontend-build"}

rm -rf "$build_root"
cmake -S "$source_root" -B "$build_root" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DADX12_BUILD_TESTS=ON
cmake --build "$build_root" --parallel
ctest --test-dir "$build_root" --output-on-failure

printf 'ADX12 frontend smoke suite passed\n'
