#!/bin/sh
set -eu

usage()
{
    cat <<'EOF'
usage:
  adx12-crossover-launch.sh inspect [--bottle name]
  adx12-crossover-launch.sh run --bottle name -- command [arguments...]

Environment:
  ADX12_CROSSOVER_ROOT  CrossOver support directory
  ADX12_RUNTIME_ROOT    root containing x86_64-windows and x86_64-unix
EOF
}

[ "$#" -ge 1 ] || { usage >&2; exit 64; }
operation=$1
shift

crossover_root=${ADX12_CROSSOVER_ROOT:-/Applications/CrossOver.app/Contents/SharedSupport/CrossOver}
crossover_wine="$crossover_root/bin/wine"
bottle=${CX_BOTTLE:-}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --bottle)
            [ "$#" -ge 2 ] || { usage >&2; exit 64; }
            bottle=$2
            shift 2
            ;;
        --)
            shift
            break
            ;;
        *)
            break
            ;;
    esac
done

[ -x "$crossover_wine" ] || {
    printf 'CrossOver launcher not found: %s\n' "$crossover_wine" >&2
    exit 1
}

if [ "$operation" = inspect ]; then
    "$crossover_wine" --version
    printf 'crossover_root=available\n'
    printf 'bottle=%s\n' "${bottle:-not-selected}"
    printf 'renderer_policy=adx12-only\n'
    printf 'dll_overrides=d3d12,dxgi=n;winemetal=b\n'
    exit 0
fi

[ "$operation" = run ] || { usage >&2; exit 64; }
[ -n "$bottle" ] || {
    printf 'A CrossOver bottle is required.\n' >&2
    exit 64
}
[ "$#" -gt 0 ] || { usage >&2; exit 64; }
host_marker="$crossover_root/adx12-host.env"
[ -f "$host_marker" ] || {
    printf 'ADX12 requires a prepared CrossOver host overlay: %s\n' \
        "$crossover_root" >&2
    exit 1
}
grep -qx 'ADX12_HOST_POLICY=environment-only' "$host_marker" || {
    printf 'CrossOver host overlay has an invalid ADX12 policy marker.\n' >&2
    exit 1
}
grep -qx 'ADX12_HOST_OVERLAY_ABI=3' "$host_marker" || {
    printf 'CrossOver host overlay must be regenerated for ADX12 host ABI 3.\n' >&2
    exit 1
}
[ -n "${ADX12_RUNTIME_ROOT:-}" ] || {
    printf 'ADX12_RUNTIME_ROOT must identify the ADX12 runtime directory.\n' >&2
    exit 1
}
runtime_windows="$ADX12_RUNTIME_ROOT/x86_64-windows"
runtime_unix="$ADX12_RUNTIME_ROOT/x86_64-unix"
[ -f "$runtime_windows/d3d12.dll" ] || {
    printf 'Missing ADX12 DLL: %s/d3d12.dll\n' "$runtime_windows" >&2
    exit 1
}
[ -f "$runtime_windows/dxgi.dll" ] || {
    printf 'Missing ADX12 DLL: %s/dxgi.dll\n' "$runtime_windows" >&2
    exit 1
}
[ -f "$runtime_windows/winemetal.dll" ] || {
    printf 'Missing ADX12 DLL: %s/winemetal.dll\n' "$runtime_windows" >&2
    exit 1
}
[ -f "$runtime_unix/winemetal.so" ] || {
    printf 'Missing ADX12 Unix library: %s/winemetal.so\n' "$runtime_unix" >&2
    exit 1
}
baseline_file="$ADX12_RUNTIME_ROOT/baseline.env"
[ -f "$baseline_file" ] || {
    printf 'ADX12 runtime provenance is missing: %s\n' "$baseline_file" >&2
    exit 1
}
grep -qx 'ADX12_RUNTIME_ABI=2' "$baseline_file" &&
    grep -qx 'ADX12_D3D12_DLL_CLASS=native' "$baseline_file" &&
    grep -qx 'ADX12_DXGI_DLL_CLASS=native' "$baseline_file" &&
    grep -qx 'ADX12_WINEMETAL_DLL_CLASS=builtin' "$baseline_file" || {
    printf 'ADX12 runtime has an incompatible module-loader contract.\n' >&2
    exit 1
}
runtime_commit=$(sed -n 's/^ADX12_BASELINE_COMMIT=//p' "$baseline_file")
host_commit=$(sed -n 's/^ADX12_DXMT_COMMIT=//p' "$host_marker")
[ -n "$runtime_commit" ] && [ "$runtime_commit" = "$host_commit" ] || {
    printf 'ADX12 runtime and CrossOver host commits do not match.\n' >&2
    exit 1
}
verify_component()
{
    runtime_component=$1
    host_component=$2
    marker_key=$3
    label=$4
    [ -f "$host_component" ] || {
        printf 'Prepared CrossOver host is missing ADX12 %s.\n' "$label" >&2
        exit 1
    }
    runtime_hash=$(shasum -a 256 "$runtime_component" | awk '{print $1}')
    host_hash=$(shasum -a 256 "$host_component" | awk '{print $1}')
    marker_hash=$(sed -n "s/^$marker_key=//p" "$host_marker")
    [ -n "$marker_hash" ] &&
        [ "$runtime_hash" = "$host_hash" ] &&
        [ "$runtime_hash" = "$marker_hash" ] || {
        printf 'Prepared CrossOver host %s does not match ADX12.\n' \
            "$label" >&2
        exit 1
    }
}

verify_component \
    "$runtime_windows/d3d12.dll" \
    "$crossover_root/lib/wine/x86_64-windows/d3d12.dll" \
    ADX12_D3D12_PE_SHA256 d3d12.dll
verify_component \
    "$runtime_windows/dxgi.dll" \
    "$crossover_root/lib/wine/x86_64-windows/dxgi.dll" \
    ADX12_DXGI_PE_SHA256 dxgi.dll
verify_component \
    "$runtime_windows/winemetal.dll" \
    "$crossover_root/lib/wine/x86_64-windows/winemetal.dll" \
    ADX12_WINEMETAL_PE_SHA256 winemetal.dll
verify_component \
    "$runtime_unix/winemetal.so" \
    "$crossover_root/lib/wine/x86_64-unix/winemetal.so" \
    ADX12_WINEMETAL_UNIX_SHA256 winemetal.so

export ADX12_RENDERER_POLICY=adx12-only
unset VKD3D_CONFIG
unset VKD3D_SHADER_DEBUG
unset DXVK_CONFIG_FILE

case "$runtime_windows" in
    /*) ;;
    *)
        printf 'ADX12_RUNTIME_ROOT must resolve to an absolute path.\n' >&2
        exit 1
        ;;
esac

# ADX12's matched D3D12 and DXGI modules are native PEs. WineMetal remains a
# Wine builtin so its PE thunk retains the matching Unix Metal boundary.
runtime_windows_win=$(
    "$crossover_wine" --bottle "$bottle" -- \
        winepath.exe -w "$runtime_windows" | tr -d '\r' | tail -n 1
)
[ -n "$runtime_windows_win" ] || {
    printf 'CrossOver could not map the ADX12 runtime path.\n' >&2
    exit 1
}
runtime_dll_path="$runtime_windows:$crossover_root/lib/wine/x86_64-windows:$crossover_root/lib/wine/x86_64-unix"
runtime_environment="ADX12_RENDERER_POLICY=adx12-only ADX12_EXPECTED_MODULE_ROOT='$runtime_windows_win' WINEDLLPATH='$runtime_dll_path' WINEPATH='$runtime_windows_win'"

append_runtime_variable()
{
    append_name=$1
    append_value=$2
    case "$append_value" in
        *"'"*|*'
'*)
            printf 'ADX12 runtime variable %s contains unsupported characters.\n' \
                "$append_name" >&2
            exit 1
            ;;
    esac
    runtime_environment="$runtime_environment $append_name='$append_value'"
}

for runtime_name in \
    ADX12_COMPILER_MANIFEST \
    DXMT_LOG_LEVEL \
    DXMT_LOG_PATH \
    WINEDEBUG; do
    eval "runtime_value=\${$runtime_name:-}"
    [ -z "$runtime_value" ] || \
        append_runtime_variable "$runtime_name" "$runtime_value"
done

exec "$crossover_wine" \
    --bottle "$bottle" \
    --dll 'd3d12,dxgi=n,winemetal=b' \
    --env "$runtime_environment" \
    -- "$@"
