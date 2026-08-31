#!/bin/sh
set -eu

usage()
{
    printf 'usage: package-adx12-frontend.sh runtime-root dxmt-source output-directory\n' >&2
}

[ "$#" -eq 3 ] || { usage; exit 64; }
runtime_root=$1
dxmt_source=$2
output_root=$3
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
baseline_file="$runtime_root/baseline.env"
patch_root="$repo_root/dependencies/patches/dxmt"

[ -f "$baseline_file" ] || {
    printf 'Missing ADX12 runtime provenance: %s\n' "$baseline_file" >&2
    exit 1
}
[ -f "$runtime_root/SHA256SUMS" ] &&
    (cd "$runtime_root" && shasum -a 256 -c SHA256SUMS >/dev/null) || {
    printf 'ADX12 runtime checksum verification failed.\n' >&2
    exit 1
}
[ -f "$dxmt_source/LICENSE" ] && [ -f "$dxmt_source/COPYING.LIB" ] || {
    printf 'DXMT source and license files are required.\n' >&2
    exit 1
}
[ ! -e "$output_root" ] || {
    printf 'Refusing to replace an existing package directory: %s\n' \
        "$output_root" >&2
    exit 1
}

source_commit=$(sed -n 's/^ADX12_BASELINE_COMMIT=//p' "$baseline_file")
runtime_abi=$(sed -n 's/^ADX12_RUNTIME_ABI=//p' "$baseline_file")
runtime_kind=$(sed -n 's/^ADX12_RUNTIME_KIND=//p' "$baseline_file")
patchset_sha256=$(sed -n 's/^ADX12_DOWNSTREAM_PATCHSET_SHA256=//p' \
    "$baseline_file")
patch_list=$(find "$patch_root" -type f -name '*.patch' -print | LC_ALL=C sort)
[ -n "$patch_list" ] || {
    printf 'The DXMT downstream patch queue is empty.\n' >&2
    exit 1
}
current_patchset=$(
    while IFS= read -r queued_patch; do
        shasum -a 256 "$queued_patch" | awk '{print $1}'
    done <<EOF
$patch_list
EOF
)
current_patchset=$(printf '%s\n' "$current_patchset" |
    shasum -a 256 | awk '{print $1}')
[ -n "$patchset_sha256" ] && [ "$patchset_sha256" = "$current_patchset" ] || {
    printf 'Runtime and repository DXMT patchsets differ.\n' >&2
    exit 1
}
[ "$runtime_abi" = 2 ] &&
    grep -qx 'ADX12_D3D12_DLL_CLASS=native' "$baseline_file" &&
    grep -qx 'ADX12_DXGI_DLL_CLASS=native' "$baseline_file" &&
    grep -qx 'ADX12_WINEMETAL_DLL_CLASS=builtin' "$baseline_file" || {
    printf 'ADX12 runtime has an incompatible module-loader contract.\n' >&2
    exit 1
}
[ -n "$source_commit" ] &&
    git -C "$dxmt_source" merge-base --is-ancestor "$source_commit" HEAD || {
    printf 'DXMT source does not retain the staged upstream revision.\n' >&2
    exit 1
}
grep -q 'ADX12GetCompilerABI' "$dxmt_source/src/d3d12/d3d12.def" || {
    printf 'DXMT source does not contain the ADX12 compiler seam.\n' >&2
    exit 1
}
git -C "$dxmt_source" diff --quiet &&
    git -C "$dxmt_source" diff --cached --quiet || {
    printf 'DXMT source contains unrecorded edits.\n' >&2
    exit 1
}
while IFS= read -r queued_patch; do
    patch_subject=$(sed -n 's/^Subject: \[PATCH\] //p' "$queued_patch" | head -n 1)
    [ -n "$patch_subject" ] &&
        git -C "$dxmt_source" log --format=%s "$source_commit..HEAD" |
            grep -Fqx "$patch_subject" || {
        printf 'DXMT source lacks downstream patch: %s\n' "$queued_patch" >&2
        exit 1
    }
done <<EOF
$patch_list
EOF

for component in \
    x86_64-windows/d3d12.dll \
    x86_64-windows/dxgi.dll \
    x86_64-windows/winemetal.dll \
    x86_64-unix/winemetal.so; do
    [ -f "$runtime_root/$component" ] || {
        printf 'Missing ADX12 runtime component: %s\n' "$component" >&2
        exit 1
    }
done

mkdir -p \
    "$output_root/x86_64-windows" \
    "$output_root/x86_64-unix" \
    "$output_root/licenses"
for component in d3d12.dll dxgi.dll winemetal.dll; do
    install -m 0755 \
        "$runtime_root/x86_64-windows/$component" \
        "$output_root/x86_64-windows/$component"
done
install -m 0755 \
    "$runtime_root/x86_64-unix/winemetal.so" \
    "$output_root/x86_64-unix/winemetal.so"
install -m 0644 "$dxmt_source/LICENSE" "$output_root/licenses/DXMT-LICENSE"
install -m 0644 "$dxmt_source/COPYING.LIB" \
    "$output_root/licenses/LGPL-2.1-or-later.txt"

"$script_dir/build-windows-loader-probe.sh" \
    "$dxmt_source" \
    "$output_root/x86_64-windows/adx12-frontend-probe.exe" >/dev/null

cat > "$output_root/manifest.env" <<EOF
ADX12_FRONTEND_PACKAGE_ABI=2
ADX12_SOURCE_PROJECT=DXMT
ADX12_SOURCE_COMMIT=$source_commit
ADX12_DOWNSTREAM_PATCHSET_SHA256=$patchset_sha256
ADX12_SOURCE_ARCH=x86_64
ADX12_RUNTIME_KIND=$runtime_kind
ADX12_D3D12_DLL_CLASS=native
ADX12_DXGI_DLL_CLASS=native
ADX12_WINEMETAL_DLL_CLASS=builtin
ADX12_RENDERER_POLICY=adx12-only
EOF

cat > "$output_root/README.txt" <<'EOF'
ADX12 frontend runtime

This machine-neutral package contains the hash-pinned and downstream-patched
DXMT D3D12/DXGI runtime plus the pinned WineMetal PE/Unix bridge. Wine or
CrossOver supplies only the Windows process and loader environment. WineD3D,
DXVK, vkd3d, and CrossOver D3DMetal are not permitted execution fallbacks.

The loader must select d3d12.dll and dxgi.dll as native PEs and winemetal.dll
as a Wine builtin so its Unix-call bridge reaches winemetal.so. Source pin,
downstream patchset, relative module paths, and SHA-256 identities establish
ADX12 ownership.
EOF

(
    cd "$output_root"
    shasum -a 256 \
        x86_64-windows/d3d12.dll \
        x86_64-windows/dxgi.dll \
        x86_64-windows/winemetal.dll \
        x86_64-windows/adx12-frontend-probe.exe \
        x86_64-unix/winemetal.so \
        licenses/DXMT-LICENSE \
        licenses/LGPL-2.1-or-later.txt \
        manifest.env \
        README.txt > SHA256SUMS
)

if grep -R -E '/Users/|/home/|[A-Za-z]:\\Users\\' "$output_root" \
    --exclude='*.dll' --exclude='*.exe' --exclude='*.so' >/dev/null 2>&1; then
    printf 'Package text contains a host-specific user path.\n' >&2
    exit 1
fi
for component in \
    "$output_root/x86_64-windows/d3d12.dll" \
    "$output_root/x86_64-windows/dxgi.dll" \
    "$output_root/x86_64-windows/winemetal.dll" \
    "$output_root/x86_64-windows/adx12-frontend-probe.exe" \
    "$output_root/x86_64-unix/winemetal.so"; do
    if strings "$component" |
       grep -E '/Users/|/home/|[A-Za-z]:\\Users\\' >/dev/null 2>&1; then
        printf 'Package binary contains absolute build-host metadata: %s\n' \
            "$component" >&2
        exit 1
    fi
done

printf 'packaged ADX12 frontend runtime\n'
printf 'source_commit=%s\n' "$source_commit"
printf 'output=%s\n' "$output_root"
