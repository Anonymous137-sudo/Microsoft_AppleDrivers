#!/bin/sh
set -eu

usage()
{
    printf 'usage: stage-dxmt-baseline.sh dxmt-install runtime-root commit\n' >&2
}

[ "$#" -eq 3 ] || { usage; exit 64; }
install_root=$1
runtime_root=$2
source_commit=$3
windows_source="$install_root/x86_64-windows"
unix_source="$install_root/x86_64-unix"
windows_destination="$runtime_root/x86_64-windows"
unix_destination="$runtime_root/x86_64-unix"

case "$source_commit" in
    *[!0-9a-f]*|'')
        printf 'Invalid source commit: %s\n' "$source_commit" >&2
        exit 1
        ;;
esac

for source_file in \
    "$windows_source/d3d12.dll" \
    "$windows_source/dxgi.dll" \
    "$windows_source/winemetal.dll" \
    "$unix_source/winemetal.so"; do
    [ -f "$source_file" ] || {
        printf 'Required DXMT baseline file is missing: %s\n' "$source_file" >&2
        exit 1
    }
done

mkdir -p "$windows_destination" "$unix_destination"
install -m 0755 "$windows_source/d3d12.dll" "$windows_destination/d3d12.dll"
install -m 0755 "$windows_source/dxgi.dll" "$windows_destination/dxgi.dll"
install -m 0755 "$windows_source/winemetal.dll" "$windows_destination/winemetal.dll"
install -m 0755 "$unix_source/winemetal.so" "$unix_destination/winemetal.so"

cat > "$runtime_root/baseline.env" <<EOF
ADX12_BASELINE_KIND=unmodified-dxmt
ADX12_BASELINE_COMMIT=$source_commit
ADX12_BASELINE_ARCH=x86_64
EOF

(
    cd "$runtime_root"
    shasum -a 256 \
        x86_64-windows/d3d12.dll \
        x86_64-windows/dxgi.dll \
        x86_64-windows/winemetal.dll \
        x86_64-unix/winemetal.so > SHA256SUMS
)

printf 'staged DXMT baseline %s in %s\n' "$source_commit" "$runtime_root"
