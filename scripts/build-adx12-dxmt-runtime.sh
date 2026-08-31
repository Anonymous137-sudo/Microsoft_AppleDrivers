#!/bin/sh
set -eu

usage()
{
    cat <<'EOF'
usage: build-adx12-dxmt-runtime.sh [baseline-runtime] [output-runtime]

Builds the patched D3D12/DXGI pair and the WineMetal PE/Unix bridge from the
same pinned source. All intermediate paths can be
overridden with ADX12_DEPENDENCY_ROOT, ADX12_DXMT_SOURCE,
ADX12_DXMT_BUILD_DIR, ADX12_WINE_SDK_ARCHIVE, and ADX12_WINE_SDK_ROOT.
EOF
}

[ "$#" -le 2 ] || { usage >&2; exit 64; }

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
lock_file="$repo_root/dependencies/upstreams.lock.tsv"
patch_root="$repo_root/dependencies/patches/dxmt"
dependency_root=${ADX12_DEPENDENCY_ROOT:-"$repo_root/.adx12-deps"}

locked_commit()
{
    awk -F '|' -v wanted="$1" '
        $0 !~ /^#/ && $1 == wanted { print $2; exit }
    ' "$lock_file"
}

require_file()
{
    [ -f "$1" ] || {
        printf 'ADX12 DXMT build: required file is missing: %s\n' "$1" >&2
        exit 1
    }
}

require_tool()
{
    command -v "$1" >/dev/null 2>&1 || {
        printf 'ADX12 DXMT build: required tool is unavailable: %s\n' "$1" >&2
        exit 1
    }
}

hash_file()
{
    shasum -a 256 "$1" | awk '{print $1}'
}

dxmt_commit=$(locked_commit dxmt)
[ -n "$dxmt_commit" ] || {
    printf 'ADX12 DXMT build: the DXMT source pin is missing\n' >&2
    exit 1
}
patch_list=$(find "$patch_root" -type f -name '*.patch' -print | LC_ALL=C sort)
[ -n "$patch_list" ] || {
    printf 'ADX12 DXMT build: the downstream patch queue is empty\n' >&2
    exit 1
}
patchset_sha256=$(
    while IFS= read -r queued_patch; do
        shasum -a 256 "$queued_patch" | awk '{print $1}'
    done <<EOF
$patch_list
EOF
)
patchset_sha256=$(printf '%s\n' "$patchset_sha256" |
    shasum -a 256 | awk '{print $1}')
short_commit=$(printf '%.8s' "$dxmt_commit")
short_patchset=$(printf '%.8s' "$patchset_sha256")
baseline_runtime=${1:-"$dependency_root/runtime-$short_commit"}
runtime_root=${2:-"$dependency_root/runtime-$short_commit-adx12-$short_patchset"}
source_root=${ADX12_DXMT_SOURCE:-"$repo_root/source/dxmt-adx12"}
build_root=${ADX12_DXMT_BUILD_DIR:-"$dependency_root/build-dxmt-$short_commit-adx12-$short_patchset"}
builtin_build_root=${ADX12_DXMT_BUILTIN_BUILD_DIR:-"$build_root-winemetal-builtin"}
wine_sdk_archive=${ADX12_WINE_SDK_ARCHIVE:-"$dependency_root/toolchains/wine-dxmt.tar.gz"}
wine_sdk_root=${ADX12_WINE_SDK_ROOT:-"$dependency_root/toolchains/wine-dxmt"}
llvm_archive="$dependency_root/toolchains/clang+llvm-15.0.7-x86_64-apple-darwin21.0.tar.xz"
bundled_llvm_path="$dependency_root/toolchains/clang+llvm-15.0.7-x86_64-apple-darwin21.0"
native_llvm_path=${ADX12_NATIVE_LLVM_PATH:-$bundled_llvm_path}
wine_sdk_url=https://github.com/3Shain/wine/releases/download/v8.16-3shain/wine.tar.gz
wine_sdk_sha256=289c7f19e270a3d3d0a6fdb07691b176c70a0795f6811e5255cba82425de4f10
llvm_url=https://github.com/llvm/llvm-project/releases/download/llvmorg-15.0.7/clang%2Bllvm-15.0.7-x86_64-apple-darwin21.0.tar.xz
llvm_sha256=d16b6d536364c5bec6583d12dd7e6cf841b9f508c4430d9ee886726bd9983f1c

for tool in \
    awk codesign curl file git install_name_tool meson ninja otool shasum strings tar \
    x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ \
    x86_64-w64-mingw32-objdump x86_64-w64-mingw32-strip; do
    require_tool "$tool"
done
require_file "$lock_file"
require_file "$baseline_runtime/baseline.env"
require_file "$baseline_runtime/SHA256SUMS"

baseline_commit=$(sed -n 's/^ADX12_BASELINE_COMMIT=//p' \
    "$baseline_runtime/baseline.env")
[ "$baseline_commit" = "$dxmt_commit" ] || {
    printf 'ADX12 DXMT build: baseline and source pins differ\n' >&2
    exit 1
}
(cd "$baseline_runtime" && shasum -a 256 -c SHA256SUMS >/dev/null) || {
    printf 'ADX12 DXMT build: baseline runtime checksum verification failed\n' >&2
    exit 1
}
if [ -e "$source_root/.git" ]; then
    git -C "$source_root" merge-base --is-ancestor "$dxmt_commit" HEAD || {
        printf 'ADX12 DXMT build: source checkout lost the pinned ancestor\n' >&2
        exit 1
    }
    git -C "$source_root" diff --quiet &&
        git -C "$source_root" diff --cached --quiet || {
        printf 'ADX12 DXMT build: source checkout contains unrecorded edits\n' >&2
        exit 1
    }
    git -C "$source_root" diff --check "$dxmt_commit..HEAD"
    while IFS= read -r queued_patch; do
        patch_subject=$(sed -n 's/^Subject: \[PATCH\] //p' "$queued_patch" |
            head -n 1)
        [ -n "$patch_subject" ] &&
            git -C "$source_root" log --format=%s "$dxmt_commit..HEAD" |
                grep -Fqx "$patch_subject" || {
            printf 'ADX12 DXMT build: source checkout lacks patch: %s\n' \
                "$queued_patch" >&2
            exit 1
        }
    done <<EOF
$patch_list
EOF
    source_date_epoch=$(git -C "$source_root" show -s --format=%ct "$dxmt_commit")
else
    "$script_dir/verify-visible-dxmt-source.sh" "$source_root" >/dev/null
    provenance="$source_root/ADX12_SOURCE_PROVENANCE"
    source_date_epoch=$(sed -n 's/^source_date_epoch=//p' "$provenance")
    [ -n "$source_date_epoch" ] || {
        printf 'ADX12 DXMT build: visible source epoch is missing\n' >&2
        exit 1
    }
fi
grep -q 'ADX12GetCompilerABI' "$source_root/src/d3d12/d3d12.def" || {
    printf 'ADX12 DXMT build: the downstream compiler ABI patch is absent\n' >&2
    exit 1
}

mkdir -p "$(dirname -- "$wine_sdk_archive")"
if [ ! -f "$wine_sdk_archive" ]; then
    curl --fail --location --output "$wine_sdk_archive" "$wine_sdk_url"
fi
[ "$(hash_file "$wine_sdk_archive")" = "$wine_sdk_sha256" ] || {
    printf 'ADX12 DXMT build: Wine SDK archive hash mismatch\n' >&2
    exit 1
}
if [ ! -e "$wine_sdk_root" ]; then
    mkdir -p "$wine_sdk_root"
    tar -xzf "$wine_sdk_archive" -C "$wine_sdk_root"
fi
require_file "$wine_sdk_root/lib/wine/x86_64-windows/libwinecrt0.a"
[ -x "$wine_sdk_root/bin/winebuild" ] || {
    printf 'ADX12 DXMT build: Wine SDK extraction is incomplete\n' >&2
    exit 1
}

if [ -z "${ADX12_NATIVE_LLVM_PATH:-}" ]; then
    if [ ! -f "$llvm_archive" ]; then
        curl --fail --location --output "$llvm_archive" "$llvm_url"
    fi
    [ "$(hash_file "$llvm_archive")" = "$llvm_sha256" ] || {
        printf 'ADX12 DXMT build: LLVM 15 archive hash mismatch\n' >&2
        exit 1
    }
    if [ ! -e "$bundled_llvm_path" ]; then
        tar -xJf "$llvm_archive" -C "$dependency_root/toolchains"
    fi
fi
require_file "$native_llvm_path/include/llvm/IR/Module.h"
[ -x "$native_llvm_path/bin/llvm-config" ] || {
    printf 'ADX12 DXMT build: the pinned x86_64 LLVM 15 toolchain is incomplete\n' >&2
    exit 1
}
llvm_version=$("$native_llvm_path/bin/llvm-config" --version)
case "$llvm_version" in
    15.*) ;;
    *)
        printf 'ADX12 DXMT build: expected LLVM 15, found %s at %s\n' \
            "$llvm_version" "$native_llvm_path" >&2
        exit 1
        ;;
esac
export SOURCE_DATE_EPOCH="$source_date_epoch"
export ZERO_AR_DATE=1
export LDFLAGS="${LDFLAGS:+$LDFLAGS }-Wl,--no-insert-timestamp"

if [ ! -f "$build_root/build.ninja" ]; then
    meson setup "$build_root" "$source_root" \
        --cross-file "$source_root/build-win64.txt" \
        --buildtype release \
        -Dstrip=true \
        -Denable_d3d12=true \
        -Dwine_builtin_dll=false \
        -Dnative_llvm_path="$native_llvm_path" \
        -Dwine_install_path="$wine_sdk_root" \
        -Denable_tests=false \
        -Denable_nvapi=false \
        -Denable_nvngx=false \
        --prefix "$build_root/install"
else
    meson setup --reconfigure "$build_root" "$source_root" \
        -Dnative_llvm_path="$native_llvm_path"
fi
ninja -C "$build_root" \
    src/d3d12/d3d12.dll \
    src/dxgi/dxgi.dll

if [ ! -f "$builtin_build_root/build.ninja" ]; then
    meson setup "$builtin_build_root" "$source_root" \
        --cross-file "$source_root/build-win64.txt" \
        --buildtype release \
        -Dstrip=true \
        -Denable_d3d12=false \
        -Dwine_builtin_dll=true \
        -Dnative_llvm_path="$native_llvm_path" \
        -Dwine_install_path="$wine_sdk_root" \
        -Denable_tests=false \
        -Denable_nvapi=false \
        -Denable_nvngx=false \
        --prefix "$builtin_build_root/install"
else
    meson setup --reconfigure "$builtin_build_root" "$source_root" \
        -Dnative_llvm_path="$native_llvm_path"
fi
ninja -C "$builtin_build_root" \
    src/winemetal/winemetal.dll \
    src/winemetal/unix/winemetal.so \
    src/winemetal/winemetal.dll.postproc

d3d12="$build_root/src/d3d12/d3d12.dll"
dxgi="$build_root/src/dxgi/dxgi.dll"
winemetal_dll="$builtin_build_root/src/winemetal/winemetal.dll"
winemetal_so="$builtin_build_root/src/winemetal/unix/winemetal.so"
require_file "$d3d12"
require_file "$dxgi"
require_file "$winemetal_dll"
require_file "$winemetal_so"
for component in "$d3d12" "$dxgi"; do
    file "$component" | grep -q 'PE32+ executable (DLL)' || {
        printf 'ADX12 DXMT build: expected native PE DLL: %s\n' "$component" >&2
        exit 1
    }
    if strings "$component" |
       grep -E '/Users/|/home/|[A-Za-z]:\\Users\\' >/dev/null 2>&1; then
        printf 'ADX12 DXMT build: absolute build-host metadata leaked into %s\n' \
            "$component" >&2
        exit 1
    fi
done
file "$winemetal_dll" | grep -q 'PE32+ executable (DLL)' || {
    printf 'ADX12 DXMT build: expected Wine builtin PE DLL: %s\n' \
        "$winemetal_dll" >&2
    exit 1
}
strings "$winemetal_dll" | grep -Fqx 'Wine builtin DLL' || {
    printf 'ADX12 DXMT build: WineMetal PE missed winebuild postprocessing\n' >&2
    exit 1
}
file "$winemetal_so" | grep -q 'Mach-O 64-bit.*x86_64' || {
    printf 'ADX12 DXMT build: expected x86_64 WineMetal Unix bridge: %s\n' \
        "$winemetal_so" >&2
    exit 1
}
for component in "$winemetal_dll" "$winemetal_so"; do
    if strings "$component" |
       grep -E '/Users/|/home/|[A-Za-z]:\\Users\\' >/dev/null 2>&1; then
        printf 'ADX12 DXMT build: absolute build-host metadata leaked into %s\n' \
            "$component" >&2
        exit 1
    fi
done

# The official LLVM 15 archive carries an @rpath libc++ install name. The
# CrossOver Unix bridge must use the macOS system C++ runtime like upstream.
if otool -L "$winemetal_so" | grep -Fq '@rpath/libc++.1.dylib'; then
    install_name_tool -change @rpath/libc++.1.dylib /usr/lib/libc++.1.dylib \
        "$winemetal_so"
fi
codesign --force --sign - "$winemetal_so" >/dev/null

mkdir -p "$runtime_root/x86_64-windows" "$runtime_root/x86_64-unix"
install -m 0755 "$d3d12" "$runtime_root/x86_64-windows/d3d12.dll"
install -m 0755 "$dxgi" "$runtime_root/x86_64-windows/dxgi.dll"
SOURCE_DATE_EPOCH=0 x86_64-w64-mingw32-strip --strip-unneeded \
    "$runtime_root/x86_64-windows/d3d12.dll" \
    "$runtime_root/x86_64-windows/dxgi.dll"
install -m 0755 "$winemetal_dll" \
    "$runtime_root/x86_64-windows/winemetal.dll"
install -m 0755 "$winemetal_so" \
    "$runtime_root/x86_64-unix/winemetal.so"

cat > "$runtime_root/baseline.env" <<EOF
ADX12_RUNTIME_ABI=2
ADX12_RUNTIME_KIND=dxmt-source-built-runtime-compiler-cache-v1
ADX12_BASELINE_KIND=adx12-patched-dxmt
ADX12_BASELINE_COMMIT=$dxmt_commit
ADX12_BASELINE_ARCH=x86_64
ADX12_DOWNSTREAM_PATCHSET_SHA256=$patchset_sha256
ADX12_WINE_SDK_SHA256=$wine_sdk_sha256
ADX12_D3D12_DLL_CLASS=native
ADX12_DXGI_DLL_CLASS=native
ADX12_WINEMETAL_DLL_CLASS=builtin
ADX12_WINEMETAL_UNIX_CLASS=source-built
EOF

(
    cd "$runtime_root"
    shasum -a 256 \
        x86_64-windows/d3d12.dll \
        x86_64-windows/dxgi.dll \
        x86_64-windows/winemetal.dll \
        x86_64-unix/winemetal.so > SHA256SUMS
)

printf 'ADX12 patched DXMT runtime built\n'
printf 'source_pin=%s\n' "$dxmt_commit"
printf 'patchset_sha256=%s\n' "$patchset_sha256"
printf 'loader_policy=d3d12,dxgi=n;winemetal=b\n'
printf 'runtime=%s\n' "$runtime_root"
