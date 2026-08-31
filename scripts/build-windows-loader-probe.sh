#!/bin/sh
set -eu

usage()
{
    printf 'usage: build-windows-loader-probe.sh dxmt-source output.exe\n' >&2
}

[ "$#" -eq 2 ] || { usage; exit 64; }
dxmt_source=$1
output=$2
compiler=${ADX12_MINGW_CXX:-x86_64-w64-mingw32-g++}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
source_file="$repo_root/Direct3D_12(Feature_Level_12_2)/tests/windows/frontend_loader_probe.cpp"
directx_include="$dxmt_source/include/native/directx"

[ -f "$directx_include/d3d12.h" ] || {
    printf 'DXMT DirectX headers are missing: %s\n' "$directx_include" >&2
    exit 1
}
command -v "$compiler" >/dev/null 2>&1 || {
    printf 'MinGW C++ compiler is unavailable: %s\n' "$compiler" >&2
    exit 1
}

mkdir -p "$(dirname -- "$output")"
"$compiler" \
    -std=c++17 \
    -O2 \
    -Wall \
    -Wextra \
    -Werror \
    -static \
    -static-libgcc \
    -static-libstdc++ \
    -Wl,--no-insert-timestamp \
    -I "$directx_include" \
    "$source_file" \
    -o "$output" \
    -lole32 \
    -luuid

file "$output"
