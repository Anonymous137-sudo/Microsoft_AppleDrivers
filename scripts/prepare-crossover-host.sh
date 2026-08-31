#!/bin/sh
set -eu

usage()
{
    printf 'usage: prepare-crossover-host.sh source-crossover-root overlay-root runtime-root\n' >&2
}

[ "$#" -eq 3 ] || { usage; exit 64; }
source_root=$1
overlay_root=$2
runtime_root=$3
baseline_file="$runtime_root/baseline.env"

[ -x "$source_root/bin/wine" ] && [ -x "$source_root/bin/cxmenu" ] || {
    printf 'Invalid CrossOver source root: %s\n' "$source_root" >&2
    exit 1
}
[ -f "$baseline_file" ] || {
    printf 'Missing ADX12 runtime provenance: %s\n' "$baseline_file" >&2
    exit 1
}
dxmt_commit=$(sed -n 's/^ADX12_BASELINE_COMMIT=//p' "$baseline_file")
runtime_abi=$(sed -n 's/^ADX12_RUNTIME_ABI=//p' "$baseline_file")
runtime_kind=$(sed -n 's/^ADX12_RUNTIME_KIND=//p' "$baseline_file")
d3d12_class=$(sed -n 's/^ADX12_D3D12_DLL_CLASS=//p' "$baseline_file")
dxgi_class=$(sed -n 's/^ADX12_DXGI_DLL_CLASS=//p' "$baseline_file")
winemetal_class=$(sed -n 's/^ADX12_WINEMETAL_DLL_CLASS=//p' "$baseline_file")
case "$dxmt_commit" in
    *[!0-9a-f]*|'')
        printf 'Invalid ADX12 baseline commit.\n' >&2
        exit 1
        ;;
esac
[ "$runtime_abi" = 2 ] &&
    [ "$d3d12_class" = native ] &&
    [ "$dxgi_class" = native ] &&
    [ "$winemetal_class" = builtin ] || {
    printf 'ADX12 runtime does not implement the native/native/builtin ABI.\n' >&2
    exit 1
}

d3d12_source="$runtime_root/x86_64-windows/d3d12.dll"
dxgi_source="$runtime_root/x86_64-windows/dxgi.dll"
winemetal_pe_source="$runtime_root/x86_64-windows/winemetal.dll"
winemetal_unix_source="$runtime_root/x86_64-unix/winemetal.so"
for source_file in \
    "$d3d12_source" \
    "$dxgi_source" \
    "$winemetal_pe_source" \
    "$winemetal_unix_source"; do
    [ -f "$source_file" ] || {
        printf 'ADX12 runtime component is missing: %s\n' "$source_file" >&2
        exit 1
    }
done

if [ ! -e "$overlay_root" ]; then
    mkdir -p "$(dirname -- "$overlay_root")"
    cp -cRp "$source_root" "$overlay_root"
elif [ ! -f "$overlay_root/adx12-host.env" ]; then
    printf 'Refusing to modify an unmarked existing host: %s\n' \
        "$overlay_root" >&2
    exit 1
fi

[ -x "$overlay_root/bin/wine" ] || {
    printf 'CrossOver host copy is incomplete: %s\n' "$overlay_root" >&2
    exit 1
}
install -m 0755 "$d3d12_source" \
    "$overlay_root/lib/wine/x86_64-windows/d3d12.dll"
install -m 0755 "$dxgi_source" \
    "$overlay_root/lib/wine/x86_64-windows/dxgi.dll"
install -m 0755 "$winemetal_pe_source" \
    "$overlay_root/lib/wine/x86_64-windows/winemetal.dll"
install -m 0755 "$winemetal_unix_source" \
    "$overlay_root/lib/wine/x86_64-unix/winemetal.so"

d3d12_hash=$(shasum -a 256 "$d3d12_source" | awk '{print $1}')
dxgi_hash=$(shasum -a 256 "$dxgi_source" | awk '{print $1}')
winemetal_pe_hash=$(shasum -a 256 "$winemetal_pe_source" | awk '{print $1}')
winemetal_unix_hash=$(shasum -a 256 "$winemetal_unix_source" | awk '{print $1}')
cat > "$overlay_root/adx12-host.env" <<EOF
ADX12_HOST_OVERLAY_ABI=3
ADX12_HOST_POLICY=environment-only
ADX12_DXMT_COMMIT=$dxmt_commit
ADX12_RUNTIME_KIND=$runtime_kind
ADX12_D3D12_DLL_CLASS=$d3d12_class
ADX12_DXGI_DLL_CLASS=$dxgi_class
ADX12_WINEMETAL_DLL_CLASS=$winemetal_class
ADX12_D3D12_PE_SHA256=$d3d12_hash
ADX12_DXGI_PE_SHA256=$dxgi_hash
ADX12_WINEMETAL_PE_SHA256=$winemetal_pe_hash
ADX12_WINEMETAL_UNIX_SHA256=$winemetal_unix_hash
EOF

printf 'prepared ADX12 CrossOver host overlay\n'
printf 'dxmt_commit=%s\n' "$dxmt_commit"
printf 'runtime_kind=%s\n' "$runtime_kind"
printf 'renderer_policy=adx12-only\n'
