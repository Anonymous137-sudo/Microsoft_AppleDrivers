#!/bin/sh
set -eu

[ "$#" -eq 2 ] || { echo 'usage: build-adx12-resource-heap-probe.sh dxmt-source output.exe' >&2; exit 64; }
dxmt_source=$1
output=$2
compiler=${ADX12_MINGW_CXX:-x86_64-w64-mingw32-g++}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
mkdir -p "$(dirname -- "$output")"
"$compiler" -std=c++17 -O2 -Wall -Wextra -Werror -static \
    -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp \
    -I "$dxmt_source/include/native/directx" \
    "$repo_root/Direct3D_12(Feature_Level_12_2)/tests/windows/resource_heap_semantics.cpp" \
    -o "$output" -lole32 -luuid
