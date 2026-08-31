#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
lock_file="$repo_root/dependencies/upstreams.lock.tsv"
shader_dir="$repo_root/Direct3D_12(Feature_Level_12_2)/tests/shaders"
shader_name=phase2_buffer.comp.hlsl

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

require_file()
{
    [ -f "$1" ] || {
        printf 'phase2: required file is missing: %s\n' "$1" >&2
        exit 1
    }
}

require_executable()
{
    [ -x "$1" ] || {
        printf 'phase2: required executable is missing: %s\n' "$1" >&2
        exit 1
    }
}

hash_file()
{
    shasum -a 256 "$1" | awk '{print $1}'
}

verify_source()
{
    component=$1
    source_root=$2
    pin=$3
    [ -d "$source_root/.git" ] || {
        printf 'phase2: %s source is not materialized: %s\n' \
            "$component" "$source_root" >&2
        exit 1
    }
    git -C "$source_root" merge-base --is-ancestor "$pin" HEAD || {
        printf 'phase2: %s source does not retain pin %s\n' \
            "$component" "$pin" >&2
        exit 1
    }
}

dxmt_commit=$(locked_commit dxmt)
dxc_commit=$(locked_commit directx-shader-compiler)
dxil_spirv_commit=$(locked_commit dxil-spirv)
mesa_commit=$(locked_commit mesa)
[ -n "$dxmt_commit" ] && [ -n "$dxc_commit" ] &&
    [ -n "$dxil_spirv_commit" ] && [ -n "$mesa_commit" ] || {
    printf 'phase2: the shader pipeline lock is incomplete\n' >&2
    exit 1
}

dependency_root=${ADX12_DEPENDENCY_ROOT:-"$repo_root/.adx12-deps"}
dxmt_source=${ADX12_DXMT_SOURCE:-"$dependency_root/dxmt-$(short_commit "$dxmt_commit")"}
dxc_source=${ADX12_DXC_SOURCE:-"$dependency_root/dxc-$(short_commit "$dxc_commit")"}
dxil_spirv_source=${ADX12_DXIL_SPIRV_SOURCE:-"$dependency_root/dxil-spirv-$(short_commit "$dxil_spirv_commit")"}
mesa_source=${ADX12_MESA_SOURCE:-"$dependency_root/mesa-$(short_commit "$mesa_commit")"}
dxc=${ADX12_DXC:-"$dependency_root/build-dxc-$(short_commit "$dxc_commit")/bin/dxc"}
dxil_spirv=${ADX12_DXIL_SPIRV:-"$dependency_root/build-dxil-spirv-$(short_commit "$dxil_spirv_commit")/dxil-spirv"}
kosmicomp=${ADX12_KOSMICOMP:-"$dependency_root/build-mesa-$(short_commit "$mesa_commit")/src/kosmickrisp/kosmicomp"}
runtime_root=${ADX12_RUNTIME_ROOT:-"$dependency_root/runtime-$(short_commit "$dxmt_commit")"}
host_root=${ADX12_CROSSOVER_ROOT:-"$dependency_root/crossover-hosts/CrossOver-26.3-adx12-managed"}
bottle=${ADX12_PHASE2_BOTTLE:-ADX12-Test}
work_root=${ADX12_PHASE2_WORK_ROOT:-"$dependency_root/phase2-shader-smoke"}

verify_source dxmt "$dxmt_source" "$dxmt_commit"
verify_source directx-shader-compiler "$dxc_source" "$dxc_commit"
verify_source dxil-spirv "$dxil_spirv_source" "$dxil_spirv_commit"
verify_source mesa "$mesa_source" "$mesa_commit"
require_executable "$dxc"
require_executable "$dxil_spirv"
require_executable "$kosmicomp"
require_executable "$host_root/bin/wine"
require_file "$shader_dir/$shader_name"
require_file "$runtime_root/x86_64-windows/winemetal.dll"
require_file "$runtime_root/x86_64-unix/winemetal.so"

for tool in spirv-val spirv-dis spirv-as xcrun shasum strings; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'phase2: required host tool is unavailable: %s\n' "$tool" >&2
        exit 1
    }
done

mkdir -p "$work_root"
dxil="$work_root/phase2_buffer.dxil"
dxil_second="$work_root/phase2_buffer.second.dxil"
spirv="$work_root/phase2_buffer.comp.spv"
spirv_second="$work_root/phase2_buffer.second.comp.spv"
spirv_text="$work_root/phase2_buffer.spvasm"
msl="$work_root/phase2_buffer.metal"
msl_second="$work_root/phase2_buffer.second.metal"
air="$work_root/phase2_buffer.air"
metallib="$work_root/phase2_buffer.metallib"
winemetal_probe="$runtime_root/x86_64-windows/phase2-winemetal-compute.exe"
d3d12_probe="$runtime_root/x86_64-windows/phase2-d3d12-compute.exe"
execution_log="$work_root/execution.log"
execution_raw_log="$work_root/execution.raw.log"
d3d12_execution_log="$work_root/d3d12-execution.log"
d3d12_execution_raw_log="$work_root/d3d12-execution.raw.log"
d3d12_rejection_log="$work_root/d3d12-rejection.log"
d3d12_rejection_raw_log="$work_root/d3d12-rejection.raw.log"
manifest="$work_root/phase2-manifest.txt"
rejection_manifest="$work_root/phase2-rejection-manifest.txt"
type_rejection_manifest="$work_root/phase2-type-rejection-manifest.txt"
type_rejection_log="$work_root/d3d12-type-rejection.log"
type_rejection_raw_log="$work_root/d3d12-type-rejection.raw.log"

compile_dxil()
{
    destination=$1
    (cd "$shader_dir" && "$dxc" \
        -T cs_6_0 \
        -E main \
        -Ges \
        -Qstrip_debug \
        -Qstrip_reflect \
        -Fo "$destination" \
        "$shader_name")
}

compile_spirv()
{
    source_dxil=$1
    destination=$2
    raw_destination="$destination.raw"
    "$dxil_spirv" "$source_dxil" \
        --output "$raw_destination" \
        --entry main \
        --ssbo-uav \
        --ssbo-srv \
        --no-bda \
        --validate
    "$script_dir/remap-adx12-spirv-bindings.sh" "$raw_destination" "$destination"
}

printf '%s\n' '[1/9] DXC: HLSL -> DXIL (two deterministic runs)'
compile_dxil "$dxil"
compile_dxil "$dxil_second"
cmp "$dxil" "$dxil_second" || {
    printf 'phase2: DXC output is not deterministic\n' >&2
    exit 1
}

printf '%s\n' '[2/9] dxil-spirv: DXIL -> SPIR-V (two deterministic runs)'
compile_spirv "$dxil" "$spirv"
compile_spirv "$dxil_second" "$spirv_second"
cmp "$spirv" "$spirv_second" || {
    printf 'phase2: dxil-spirv output is not deterministic\n' >&2
    exit 1
}
spirv-val --target-env vulkan1.3 --scalar-block-layout "$spirv"
spirv-dis "$spirv" -o "$spirv_text"

resource_ids=$(awk '/OpDecorate .* DescriptorSet 0/ { print $2 }' "$spirv_text" | sort -u)
[ "$(printf '%s\n' "$resource_ids" | sed '/^$/d' | wc -l | tr -d ' ')" -eq 4 ] &&
    printf '%s\n' "$resource_ids" | while IFS= read -r resource_id; do
        grep -Eq "OpDecorate[[:space:]]+$resource_id[[:space:]]+Binding[[:space:]]+[0123]" "$spirv_text"
    done &&
    grep -Eq 'OpExecutionMode .* LocalSize 4 1 1' "$spirv_text" &&
    grep -Eq 'OpDecorate .* Binding 2' "$spirv_text" &&
    [ "$(grep -Ec 'OpDecorate .* NonWritable' "$spirv_text")" -eq 1 ] &&
    [ "$(grep -Ec 'OpDecorate .* NonReadable' "$spirv_text")" -eq 2 ] &&
    grep -Eq 'OpVariable .* Uniform$' "$spirv_text" || {
    printf 'phase2: SPIR-V reflection does not match one SRV, two UAVs, and one CBV\n' >&2
    exit 1
}

printf '%s\n' '[3/9] Mesa VTN/NIR + KosmicKrisp: SPIR-V -> MSL'
"$kosmicomp" --adx12-compute-ssbo-count 4 "$spirv" > "$msl"
"$kosmicomp" --adx12-compute-ssbo-count 4 "$spirv_second" > "$msl_second"
cmp "$msl" "$msl_second" || {
    printf 'phase2: Mesa/KosmicKrisp MSL is not deterministic\n' >&2
    exit 1
}

printf '%s\n' '[4/9] Apple Metal offline validation'
xcrun -sdk macosx metal -c "$msl" -o "$air"
xcrun -sdk macosx metallib "$air" -o "$metallib"

printf '%s\n' '[5/9] Build WineMetal and application-facing D3D12 probes'
"$script_dir/build-phase2-winemetal-compute.sh" "$dxmt_source" "$winemetal_probe" >/dev/null
"$script_dir/build-phase2-d3d12-compute.sh" "$dxmt_source" "$d3d12_probe" >/dev/null

source_hash=$(hash_file "$shader_dir/$shader_name")
dxil_hash=$(hash_file "$dxil")
spirv_hash=$(hash_file "$spirv")
msl_hash=$(hash_file "$msl")
cache_material=$(printf '%s\n' \
    'adx12-phase2-cache-v1' \
    "$dxmt_commit" \
    "$dxc_commit" \
    "$dxil_spirv_commit" \
    "$mesa_commit" \
    "$source_hash" \
    "$dxil_hash" \
    "$spirv_hash" \
    "$msl_hash" \
    'cs_6_0:main:set0:srv-binding0,uav-binding0-1,cbv-binding0:local-size-4-1-1')
cache_key=$(printf '%s' "$cache_material" | shasum -a 256 | awk '{print $1}')

cat > "$manifest" <<EOF
schema=adx12-compiler-cache-v1
compiler_abi=2
cache_key=$cache_key
dxmt_commit=$dxmt_commit
dxc_commit=$dxc_commit
dxil_spirv_commit=$dxil_spirv_commit
mesa_commit=$mesa_commit
source_sha256=$source_hash
dxil_sha256=$dxil_hash
spirv_sha256=$spirv_hash
msl_sha256=$msl_hash
stage=compute
entry_point=main_entrypoint
threadgroup_size=4,1,1
descriptor=set0,srv-binding0,storage-buffer,read-only;set0,uav-binding1-2,storage-buffer,write-only;set0,cbv-binding3,uniform-buffer,read-only
descriptor_count=4
descriptor_0=set0,binding0,storage-buffer,read-only,root-srv,parameter0
descriptor_1=set0,binding1,storage-buffer,write-only,root-uav,parameter1
descriptor_2=set0,binding2,storage-buffer,write-only,root-uav,parameter2
descriptor_3=set0,binding3,uniform-buffer,read-only,root-constants,dwords4,parameter3
msl_file=$(basename "$msl")
kk_root_set0_offset=928
kk_root_size=2240
expected_readback=12,15,18,21;19,25,31,37
d3d12_api_route=compute-pso,root-srv,root-uav,root-constants,uav-barrier,transition-barrier,dispatch,copy,queue-signal,fence,readback
d3d12_compiler_route=mesa-vtn-nir-kosmickrisp-msl
kk_d3d12_compiler_integration=content-addressed-bundle-admitted
EOF

cache_output=$(
    ADX12_DXIL_SPIRV="$dxil_spirv" ADX12_KOSMICOMP="$kosmicomp" \
        ADX12_ROOT_CONSTANTS=3:4 \
        "$script_dir/populate-adx12-compiler-cache.sh" \
        "$dxil" 4 "$work_root/compiler-cache"
)
generated_manifest=$(printf '%s\n' "$cache_output" |
    sed -n 's/^ADX12_COMPILER_MANIFEST=//p' | tail -n 1)
[ -f "$generated_manifest" ] || {
    printf 'phase2: reusable compiler-cache population produced no manifest\n' >&2
    exit 1
}
generated_msl=$(dirname -- "$generated_manifest")/shader.metal
cmp "$msl" "$generated_msl" || {
    printf 'phase2: reusable compiler-cache output is not deterministic\n' >&2
    exit 1
}
if ADX12_DXIL_SPIRV="$dxil_spirv" ADX12_KOSMICOMP="$kosmicomp" \
    ADX12_ROOT_CONSTANTS=3:4,3:8 \
    "$script_dir/populate-adx12-compiler-cache.sh" \
    "$dxil" 4 "$work_root/compiler-cache-invalid-duplicate" >/dev/null 2>&1; then
    printf 'phase2: compiler cache accepted duplicate root-constant bindings\n' >&2
    exit 1
fi
if ADX12_DXIL_SPIRV="$dxil_spirv" ADX12_KOSMICOMP="$kosmicomp" \
    ADX12_ROOT_CONSTANTS=3:4,15:2 \
    "$script_dir/populate-adx12-compiler-cache.sh" \
    "$dxil" 4 "$work_root/compiler-cache-invalid-unused" >/dev/null 2>&1; then
    printf 'phase2: compiler cache accepted an unused root-constant binding\n' >&2
    exit 1
fi
manifest=$generated_manifest
cache_key=$(sed -n 's/^cache_key=//p' "$manifest")

sed 's/^msl_sha256=.*/msl_sha256=0000000000000000000000000000000000000000000000000000000000000000/' \
    "$manifest" >"$rejection_manifest"
sed 's/root-constants,dwords4/root-cbv/' \
    "$manifest" >"$type_rejection_manifest"

for neutral_artifact in "$dxil" "$spirv" "$msl" "$manifest" "$rejection_manifest" "$type_rejection_manifest"; do
    if strings "$neutral_artifact" | grep -F "$repo_root" >/dev/null 2>&1; then
        printf 'phase2: absolute repository path leaked into %s\n' \
            "$(basename "$neutral_artifact")" >&2
        exit 1
    fi
done

printf '%s\n' '[6/9] Content-addressed cache and reflection manifest'
printf 'cache_key=%s\n' "$cache_key"

printf '%s\n' '[7/9] Mesa/KosmicKrisp WineMetal compute submission and fence wait'
msl_windows=$(
    "$host_root/bin/wine" --bottle "$bottle" -- \
        winepath.exe -w "$msl" | tr -d '\r' | tail -n 1
)
[ -n "$msl_windows" ] || {
    printf 'phase2: CrossOver could not map the generated MSL path\n' >&2
    exit 1
}
if ! ADX12_CROSSOVER_ROOT="$host_root" \
     ADX12_RUNTIME_ROOT="$runtime_root" \
     "$script_dir/adx12-crossover-launch.sh" run --bottle "$bottle" -- \
        "$winemetal_probe" "$msl_windows" > "$execution_raw_log" 2>&1; then
    cat "$execution_raw_log" >&2
    printf 'phase2: WineMetal compute execution failed\n' >&2
    exit 1
fi
tr -d '\r' < "$execution_raw_log" > "$execution_log"
cat "$execution_log"
grep -qx 'command_buffer_status=4' "$execution_log"
grep -qx 'readback=12,15,18,21' "$execution_log"
grep -qx 'readback_b=19,25,31,37' "$execution_log"
grep -qx 'phase2_compute=passed' "$execution_log"

printf '%s\n' '[8/9] Application-facing D3D12 PSO, dispatch, fence, and readback'
dxil_windows=$(
    "$host_root/bin/wine" --bottle "$bottle" -- \
        winepath.exe -w "$dxil" | tr -d '\r' | tail -n 1
)
[ -n "$dxil_windows" ] || {
    printf 'phase2: CrossOver could not map the generated DXIL path\n' >&2
    exit 1
}
manifest_windows=$(
    "$host_root/bin/wine" --bottle "$bottle" -- \
        winepath.exe -w "$manifest" | tr -d '\r' | tail -n 1
)
rejection_manifest_windows=$(
    "$host_root/bin/wine" --bottle "$bottle" -- \
        winepath.exe -w "$rejection_manifest" | tr -d '\r' | tail -n 1
)
type_rejection_manifest_windows=$(
    "$host_root/bin/wine" --bottle "$bottle" -- \
        winepath.exe -w "$type_rejection_manifest" | tr -d '\r' | tail -n 1
)
[ -n "$manifest_windows" ] && [ -n "$rejection_manifest_windows" ] &&
    [ -n "$type_rejection_manifest_windows" ] || {
    printf 'phase2: CrossOver could not map the compiler manifests\n' >&2
    exit 1
}
if ADX12_CROSSOVER_ROOT="$host_root" \
     ADX12_RUNTIME_ROOT="$runtime_root" \
     ADX12_COMPILER_MANIFEST="$rejection_manifest_windows" \
     "$script_dir/adx12-crossover-launch.sh" run --bottle "$bottle" -- \
        "$d3d12_probe" "$dxil_windows" > "$d3d12_rejection_raw_log" 2>&1; then
    cat "$d3d12_rejection_raw_log" >&2
    printf 'phase2: D3D12 accepted a mismatched KK compute bundle\n' >&2
    exit 1
fi
tr -d '\r' < "$d3d12_rejection_raw_log" > "$d3d12_rejection_log"
grep -qx 'adx12_compiler_abi=2' "$d3d12_rejection_log"
grep -qx 'ADX12_COMPILER_CACHE=requested' "$d3d12_rejection_log"
grep -qx 'create_compute_pipeline=0x80070057' "$d3d12_rejection_log"

if ADX12_CROSSOVER_ROOT="$host_root" \
     ADX12_RUNTIME_ROOT="$runtime_root" \
     ADX12_COMPILER_MANIFEST="$type_rejection_manifest_windows" \
     "$script_dir/adx12-crossover-launch.sh" run --bottle "$bottle" -- \
        "$d3d12_probe" "$dxil_windows" > "$type_rejection_raw_log" 2>&1; then
    cat "$type_rejection_raw_log" >&2
    printf 'phase2: D3D12 accepted root-CBV metadata for root constants\n' >&2
    exit 1
fi
tr -d '\r' < "$type_rejection_raw_log" > "$type_rejection_log"
grep -qx 'adx12_compiler_abi=2' "$type_rejection_log"
grep -qx 'ADX12_COMPILER_CACHE=requested' "$type_rejection_log"
grep -qx 'create_compute_pipeline=0x80070057' "$type_rejection_log"

if ! ADX12_CROSSOVER_ROOT="$host_root" \
     ADX12_RUNTIME_ROOT="$runtime_root" \
     ADX12_COMPILER_MANIFEST="$manifest_windows" \
     DXMT_LOG_LEVEL=info \
     DXMT_LOG_PATH=none \
     "$script_dir/adx12-crossover-launch.sh" run --bottle "$bottle" -- \
        "$d3d12_probe" "$dxil_windows" > "$d3d12_execution_raw_log" 2>&1; then
    cat "$d3d12_execution_raw_log" >&2
    printf 'phase2: application-facing D3D12 compute execution failed\n' >&2
    exit 1
fi
tr -d '\r' < "$d3d12_execution_raw_log" > "$d3d12_execution_log"
cat "$d3d12_execution_log"
grep -qx 'adx12_compiler_abi=2' "$d3d12_execution_log"
grep -qx 'ADX12_COMPILER_CACHE=requested' "$d3d12_execution_log"
grep -qx 'ADX12_COMPILER_CACHE=admitted' "$d3d12_execution_log"
grep -qx 'create_compute_pipeline=0x00000000' "$d3d12_execution_log"
grep -qx 'fence_value=1' "$d3d12_execution_log"
grep -qx 'readback=12,15,18,21' "$d3d12_execution_log"
grep -qx 'readback_b=19,25,31,37' "$d3d12_execution_log"
grep -qx 'd3d12_compute=passed' "$d3d12_execution_log"

printf '%s\n' '[9/9] ADX12 Phase 2 compute routes passed'
printf 'manifest=%s\n' "$manifest"
