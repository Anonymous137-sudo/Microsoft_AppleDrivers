#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
commit=$(awk -F '|' '$1 == "dxmt" { print $2; exit }' "$repo_root/dependencies/upstreams.lock.tsv")
short=$(printf '%.8s' "$commit")
deps=${ADX12_DEPENDENCY_ROOT:-"$repo_root/.adx12-deps"}
patchset=$(sed -n 's/^patchset_sha256=//p' \
    "$repo_root/source/dxmt-adx12/ADX12_SOURCE_PROVENANCE")
short_patchset=$(printf '%.8s' "$patchset")
source=${ADX12_DXMT_SOURCE:-"$repo_root/source/dxmt-adx12"}
runtime=${ADX12_RUNTIME_ROOT:-"$deps/runtime-$short-adx12-$short_patchset"}
host=${ADX12_CROSSOVER_ROOT:-"$deps/crossover-hosts/CrossOver-26.3-adx12-managed"}
probe="$runtime/x86_64-windows/adx12-resource-heap.exe"
log=$(mktemp -t adx12-resource-heap)
trap 'rm -f "$log"' EXIT HUP INT TERM
"$script_dir/build-adx12-resource-heap-probe.sh" "$source" "$probe"
ADX12_CROSSOVER_ROOT="$host" ADX12_RUNTIME_ROOT="$runtime" \
    "$script_dir/adx12-crossover-launch.sh" run --bottle ADX12-Test -- "$probe" \
    2>&1 | tr -d '\r' | tee "$log"
for result in single_alignment single_size pair_alignment pair_size \
    reject_zero_width reject_buffer_height reject_buffer_format \
    reject_buffer_layout create_buffer_heap create_placed_buffer \
    placed_buffer_alias_handoff \
    reject_unaligned_offset reject_placed_overflow \
    reject_buffer_on_texture_heap texture_allocation_alignment \
    texture_allocation_size create_placed_texture reject_unaligned_texture \
    create_aliased_texture create_offset_texture \
    reject_placed_texture_overflow placed_texture_retains_heap \
    reject_texture_on_buffer_heap \
    resource_heap_semantics; do
    grep -Fqx "$result=passed" "$log"
done
