#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
lock_file="$repo_root/dependencies/upstreams.lock.tsv"
dxmt_commit=$(awk -F '|' '$1 == "dxmt" { print $2; exit }' "$lock_file")
short_commit=$(printf '%.8s' "$dxmt_commit")
dependency_root=${ADX12_DEPENDENCY_ROOT:-"$repo_root/.adx12-deps"}
patchset=$(sed -n 's/^patchset_sha256=//p' \
    "$repo_root/source/dxmt-adx12/ADX12_SOURCE_PROVENANCE")
short_patchset=$(printf '%.8s' "$patchset")
dxmt_source=${ADX12_DXMT_SOURCE:-"$repo_root/source/dxmt-adx12"}
runtime_root=${ADX12_RUNTIME_ROOT:-"$dependency_root/runtime-$short_commit-adx12-$short_patchset"}
host_root=${ADX12_CROSSOVER_ROOT:-"$dependency_root/crossover-hosts/CrossOver-26.3-adx12-managed"}
bottle=${ADX12_ROOT_SIGNATURE_BOTTLE:-ADX12-Test}
probe="$runtime_root/x86_64-windows/adx12-root-signature.exe"
raw_log=$(mktemp -t adx12-root-signature-raw)
log=$(mktemp -t adx12-root-signature)
trap 'rm -f "$raw_log" "$log"' EXIT HUP INT TERM

"$script_dir/build-adx12-root-signature-probe.sh" \
    "$dxmt_source" "$probe" >/dev/null

if ! ADX12_CROSSOVER_ROOT="$host_root" \
     ADX12_RUNTIME_ROOT="$runtime_root" \
     "$script_dir/adx12-crossover-launch.sh" run --bottle "$bottle" -- \
        "$probe" >"$raw_log" 2>&1; then
    tr -d '\r' <"$raw_log" >&2
    exit 1
fi
tr -d '\r' <"$raw_log" >"$log"
cat "$log"

for expected in \
    'adx12_compiler_abi=2' \
    'serialize_empty=0x00000000' \
    'deserialize_empty=0x00000000' \
    'versioned_roundtrip=passed' \
    'reject_mixed_table=0x80070057' \
    'admit_64_dwords=0x00000000' \
    'reject_65_dwords=0x80070057' \
    'root_signature_semantics=passed'; do
    grep -Fqx "$expected" "$log" || {
        printf 'Missing root-signature result: %s\n' "$expected" >&2
        exit 1
    }
done

printf 'ADX12 root-signature semantic regression passed\n'
