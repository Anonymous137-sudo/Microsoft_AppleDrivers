#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
source_root=${1:-"$repo_root/source/dxmt-adx12"}
provenance="$source_root/ADX12_SOURCE_PROVENANCE"
checksums="$source_root/ADX12_SOURCE_SHA256SUMS"
lock_file="$repo_root/dependencies/upstreams.lock.tsv"
patch_root="$repo_root/dependencies/patches/dxmt"

fail()
{
    printf 'ADX12 visible source: %s\n' "$1" >&2
    exit 1
}

field()
{
    sed -n "s/^$1=//p" "$provenance"
}

[ -d "$source_root" ] || fail "source tree is missing: $source_root"
[ -f "$provenance" ] || fail "provenance record is missing"
[ -f "$checksums" ] || fail "source checksum manifest is missing"
[ "$(field format)" = adx12-visible-source-v1 ] ||
    fail "unsupported provenance format"

locked_commit=$(awk -F '|' '$1 == "dxmt" { print $2; exit }' "$lock_file")
[ -n "$locked_commit" ] || fail "DXMT lock is missing"
[ "$(field upstream_commit)" = "$locked_commit" ] ||
    fail "visible source and accepted DXMT pin differ"

patch_list=$(find "$patch_root" -type f -name '*.patch' -print | LC_ALL=C sort)
[ -n "$patch_list" ] || fail "downstream patch queue is empty"
patchset_sha256=$(
    while IFS= read -r patch; do
        shasum -a 256 "$patch" | awk '{print $1}'
    done <<EOF
$patch_list
EOF
)
patchset_sha256=$(printf '%s\n' "$patchset_sha256" |
    shasum -a 256 | awk '{print $1}')
[ "$patchset_sha256" = "$(field patchset_sha256)" ] ||
    fail "visible source and downstream patch queue differ"
[ "$(wc -l < "$checksums" | tr -d ' ')" = 378 ] ||
    fail "source checksum manifest has an unexpected entry count"
(cd "$source_root" && shasum -a 256 -c ADX12_SOURCE_SHA256SUMS >/dev/null) ||
    fail "visible source differs from its complete export manifest"

for required in \
    COPYING.LIB LICENSE meson.build \
    src/d3d12/d3d12.cpp \
    src/dxgi/dxgi.cpp \
    src/dxmt/dxmt_command.cpp \
    src/winemetal/main.c \
    include/native/directx \
    external/nvapi; do
    [ -e "$source_root/$required" ] ||
        fail "required source path is missing: $required"
done

grep -q 'ADX12GetCompilerABI' "$source_root/src/d3d12/d3d12.def" ||
    fail "ADX12 compiler ABI source is absent"
grep -q 'd08488fcc82eef313b0464db37d2955709691e94' \
    "$source_root/ADX12_NESTED_DEPENDENCIES" ||
    fail "NVAPI gitlink identity is absent"
grep -q '9df86f2341616ef1888ae59919feaa6d4fad693d' \
    "$source_root/ADX12_NESTED_DEPENDENCIES" ||
    fail "DirectX header gitlink identity is absent"

if LC_ALL=C grep -rIlE \
    '(/Users/[^/[:space:]]+|/home/[^/[:space:]]+|[A-Za-z]:\\\\Users\\\\[^\\\\[:space:]]+)' \
    "$source_root" --exclude=ADX12_SOURCE_PROVENANCE |
    grep -q .; then
    fail "host-specific user path found in visible source"
fi

printf 'ADX12 visible source: PASS\n'
printf '  upstream:   %s\n' "$(field upstream_commit)"
printf '  downstream: %s\n' "$(field downstream_commit)"
printf '  patchset:   %s\n' "$(field patchset_sha256)"
