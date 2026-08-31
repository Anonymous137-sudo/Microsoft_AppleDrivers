#!/bin/sh
set -eu

usage()
{
    printf 'usage: test-crossover-frontend.sh host-overlay runtime-root dxmt-source bottle\n' >&2
}

[ "$#" -eq 4 ] || { usage; exit 64; }
host_overlay=$(CDPATH= cd -- "$1" && pwd)
runtime_root=$(CDPATH= cd -- "$2" && pwd)
dxmt_source=$(CDPATH= cd -- "$3" && pwd)
bottle=$4
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
probe="$runtime_root/x86_64-windows/frontend-probe.exe"
output=$(mktemp -t adx12-frontend-output)
trace=$(mktemp -t adx12-frontend-trace)
trap 'rm -f "$output" "$trace"' EXIT HUP INT TERM

show_diagnostics()
{
    printf '%s\n' '--- ADX12 frontend output ---' >&2
    sed -n '1,160p' "$output" >&2
    printf '%s\n' '--- ADX12 CrossOver diagnostics ---' >&2
    tail -n 240 "$trace" >&2
}

run_probe()
{
    : >"$output"
    : >"$trace"
    ADX12_CROSSOVER_ROOT="$host_overlay" \
    ADX12_RUNTIME_ROOT="$runtime_root" \
    "$script_dir/adx12-crossover-launch.sh" run --bottle "$bottle" -- \
        "$probe" >"$output" 2>"$trace"
}

"$script_dir/build-windows-loader-probe.sh" "$dxmt_source" "$probe" >/dev/null

status=0
run_probe || status=$?

# A newly prepared bottle can lose its first process while CrossOver finishes
# startup bookkeeping. Retry only that empty-output condition; semantic or
# provenance failures from the probe are never retried or hidden.
if [ "$status" -ne 0 ] && [ ! -s "$output" ]; then
    sleep 1
    status=0
    run_probe || status=$?
fi
if [ "$status" -ne 0 ]; then
    show_diagnostics
    printf 'ADX12 frontend probe failed with status %d.\n' "$status" >&2
    exit "$status"
fi

for expected in \
    'module_provenance=adx12-runtime' \
    'adx12_compiler_abi=2' \
    'create_factory=0x00000000' \
    'query_factory6=0x00000000' \
    'factory_identity=stable' \
    'enum_adapter=0x00000000' \
    'query_adapter4=0x00000000' \
    'adapter_parent=0x00000000' \
    'create_device=0x00000000' \
    'query_device1=0x00000000' \
    'feature_levels=0x00000000' \
    'create_queue=0x00000000' \
    'queue_get_device=0x00000000'; do
    grep -q "$expected" "$output" || {
        printf 'Missing frontend result: %s\n' "$expected" >&2
        show_diagnostics
        exit 1
    }
done

grep -Eq 'maximum_feature_level=0x(b000|b100|c000|c100|c200)' "$output" || {
    printf 'The frontend did not report a recognized D3D feature level.\n' >&2
    show_diagnostics
    exit 1
}

if grep -q '^forbidden_renderer=' "$output"; then
    printf 'The frontend probe found a forbidden alternative renderer.\n' >&2
    show_diagnostics
    exit 1
fi

printf 'ADX12 CrossOver frontend smoke passed\n'
grep 'maximum_feature_level=' "$output"
printf 'renderer_policy=adx12-only\n'
