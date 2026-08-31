#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
lock_file="$repo_root/dependencies/upstreams.lock.tsv"
dependency_root=${ADX12_DEPENDENCY_ROOT:-"$repo_root/.adx12-deps"}
jobs=${ADX12_BUILD_JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || printf 4)}

locked_commit()
{
    awk -F '|' -v wanted="$1" '
        $0 !~ /^#/ && $1 == wanted { print $2; exit }
    ' "$lock_file"
}

short_commit()
{
    printf '%.8s' "$1"
}

materialize()
{
    component=$1
    destination=$2
    pin=$3
    if [ ! -d "$destination/.git" ]; then
        "$script_dir/adx12-upstream-sync.sh" \
            materialize "$component" "$destination" "$lock_file"
    fi
    git -C "$destination" merge-base --is-ancestor "$pin" HEAD || {
        printf 'shader-toolchain: %s does not retain pin %s\n' \
            "$component" "$pin" >&2
        exit 1
    }
    submodule_policy=$(awk -F '|' -v wanted="$component" '
        $0 !~ /^#/ && $1 == wanted { print $8; exit }
    ' "$repo_root/dependencies/upstreams.tsv")
    if [ "$submodule_policy" = recursive ]; then
        git -C "$destination" submodule sync --quiet --recursive
        git -C "$destination" submodule update --quiet --init --recursive
    elif [ "$submodule_policy" != none ]; then
        printf 'shader-toolchain: invalid submodule policy for %s: %s\n' \
            "$component" "$submodule_policy" >&2
        exit 1
    fi
}

for tool in git cmake meson ninja pkg-config; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'shader-toolchain: required build tool is missing: %s\n' \
            "$tool" >&2
        exit 1
    }
done

dxmt_commit=$(locked_commit dxmt)
dxc_commit=$(locked_commit directx-shader-compiler)
dxil_spirv_commit=$(locked_commit dxil-spirv)
mesa_commit=$(locked_commit mesa)

dxmt_source="$dependency_root/dxmt-$(short_commit "$dxmt_commit")"
dxc_source="$dependency_root/dxc-$(short_commit "$dxc_commit")"
dxil_spirv_source="$dependency_root/dxil-spirv-$(short_commit "$dxil_spirv_commit")"
mesa_source="$dependency_root/mesa-$(short_commit "$mesa_commit")"
dxc_build="$dependency_root/build-dxc-$(short_commit "$dxc_commit")"
dxil_spirv_build="$dependency_root/build-dxil-spirv-$(short_commit "$dxil_spirv_commit")"
mesa_build="$dependency_root/build-mesa-$(short_commit "$mesa_commit")"
mesa_prefix="$dependency_root/prefix-mesa-$(short_commit "$mesa_commit")"

mkdir -p "$dependency_root"
printf '%s\n' '[1/6] Materialize hash-pinned sources and downstream patches'
materialize dxmt "$dxmt_source" "$dxmt_commit"
materialize directx-shader-compiler "$dxc_source" "$dxc_commit"
materialize dxil-spirv "$dxil_spirv_source" "$dxil_spirv_commit"
materialize mesa "$mesa_source" "$mesa_commit"

printf '%s\n' '[2/6] Configure DXC'
if [ ! -f "$dxc_build/build.ninja" ]; then
    cmake -S "$dxc_source" -B "$dxc_build" -G Ninja \
        -C "$dxc_source/cmake/caches/PredefinedParams.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DHLSL_INCLUDE_TESTS=OFF
fi
printf '%s\n' '[3/6] Build DXC'
cmake --build "$dxc_build" --target dxc --parallel "$jobs"

printf '%s\n' '[4/6] Configure and build dxil-spirv'
if [ ! -f "$dxil_spirv_build/CMakeCache.txt" ]; then
    cmake -S "$dxil_spirv_source" -B "$dxil_spirv_build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DDXIL_SPIRV_CLI=ON
fi
cmake --build "$dxil_spirv_build" --parallel "$jobs"

llvm_config=${ADX12_LLVM_CONFIG:-}
if [ -z "$llvm_config" ] && command -v llvm-config >/dev/null 2>&1; then
    llvm_config=$(command -v llvm-config)
fi
if [ -z "$llvm_config" ] && command -v brew >/dev/null 2>&1; then
    brew_llvm=$(brew --prefix llvm 2>/dev/null || true)
    if [ -x "$brew_llvm/bin/llvm-config" ]; then
        llvm_config="$brew_llvm/bin/llvm-config"
    fi
fi
[ -x "$llvm_config" ] || {
    printf '%s\n' \
        'shader-toolchain: LLVM with llvm-config is required for Mesa VTN/NIR' >&2
    exit 1
}

pkg_paths=${PKG_CONFIG_PATH:-}
if command -v brew >/dev/null 2>&1; then
    brew_prefix=$(brew --prefix 2>/dev/null || true)
    if [ -n "$brew_prefix" ]; then
        pkg_paths="$brew_prefix/lib/pkgconfig:$brew_prefix/share/pkgconfig${pkg_paths:+:$pkg_paths}"
    fi
fi

printf '%s\n' '[5/6] Configure Mesa VTN/NIR and KosmicKrisp'
if [ ! -f "$mesa_build/build.ninja" ]; then
    LLVM_CONFIG="$llvm_config" PKG_CONFIG_PATH="$pkg_paths" \
        meson setup "$mesa_build" "$mesa_source" \
            --buildtype=release \
            --prefix="$mesa_prefix" \
            -Dplatforms=macos \
            -Dvulkan-drivers=kosmickrisp \
            -Dgallium-drivers= \
            -Dopengl=false \
            -Dzstd=disabled \
            -Dprefer_static=true \
            -Dbuild-tests=false \
            -Dllvm=enabled
fi

printf '%s\n' '[6/6] Build the patched KosmicKrisp compiler'
LLVM_CONFIG="$llvm_config" PKG_CONFIG_PATH="$pkg_paths" \
    ninja -C "$mesa_build" src/kosmickrisp/kosmicomp

printf 'ADX12_DXMT_SOURCE=%s\n' "$dxmt_source"
printf 'ADX12_DXC=%s\n' "$dxc_build/bin/dxc"
printf 'ADX12_DXIL_SPIRV=%s\n' "$dxil_spirv_build/dxil-spirv"
printf 'ADX12_KOSMICOMP=%s\n' "$mesa_build/src/kosmickrisp/kosmicomp"
