#!/bin/sh
set -eu

usage()
{
    echo 'usage: populate-adx12-compiler-cache.sh input.dxil root-descriptor-count cache-root' >&2
}

[ "$#" -eq 3 ] || { usage; exit 64; }
input=$1
descriptor_count=$2
cache_root=$3
root_constants=${ADX12_ROOT_CONSTANTS:-}
if [ -n "$root_constants" ]; then
    printf '%s\n' "$root_constants" | awk -F, '
        {
            for (i = 1; i <= NF; i++) {
                if ($i !~ /^[0-9]+:[0-9]+$/) exit 1
                split($i, pair, ":")
                if (pair[2] < 1 || pair[2] > 64 || seen[pair[1]]++) exit 1
            }
        }
    ' >/dev/null || {
        echo 'ADX12_ROOT_CONSTANTS must contain unique binding:dword-count entries (1-64 DWORDs)' >&2
        exit 64
    }
    root_constants=$(printf '%s\n' "$root_constants" | tr ',' '\n' |
        LC_ALL=C sort -t: -k1,1n | paste -sd, -)
fi
root_constant_dwords()
{
    printf '%s\n' "$root_constants" | awk -F, -v binding="$1" '
        {
            for (i = 1; i <= NF; i++) {
                split($i, pair, ":")
                if (pair[1] == binding) { print pair[2]; exit }
            }
        }
    '
}
case "$descriptor_count" in
    ''|*[!0-9]*) echo 'root-descriptor-count must be an integer from 1 through 16' >&2; exit 64 ;;
esac
[ "$descriptor_count" -ge 1 ] && [ "$descriptor_count" -le 16 ] || {
    echo 'root-descriptor-count must be an integer from 1 through 16' >&2
    exit 64
}
[ -f "$input" ] || { echo "DXIL input is missing: $input" >&2; exit 1; }

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
lock="$repo_root/dependencies/upstreams.lock.tsv"
deps=${ADX12_DEPENDENCY_ROOT:-"$repo_root/.adx12-deps"}
locked()
{
    awk -F '|' -v name="$1" '$0 !~ /^#/ && $1 == name { print $2; exit }' "$lock"
}
short() { printf '%.8s' "$1"; }
hash() { shasum -a 256 "$1" | awk '{print $1}'; }

dxmt_commit=$(locked dxmt)
dxil_spirv_commit=$(locked dxil-spirv)
mesa_commit=$(locked mesa)
dxil_spirv=${ADX12_DXIL_SPIRV:-"$deps/build-dxil-spirv-$(short "$dxil_spirv_commit")/dxil-spirv"}
kosmicomp=${ADX12_KOSMICOMP:-"$deps/build-mesa-$(short "$mesa_commit")/src/kosmickrisp/kosmicomp"}
for tool in "$dxil_spirv" "$kosmicomp"; do
    [ -x "$tool" ] || { echo "compiler-cache tool is unavailable: $tool" >&2; exit 1; }
done
for tool in spirv-val spirv-dis spirv-as xcrun shasum; do
    command -v "$tool" >/dev/null 2>&1 || { echo "compiler-cache tool is unavailable: $tool" >&2; exit 1; }
done

work=$(mktemp -d -t adx12-compiler-cache)
trap 'rm -rf "$work"' EXIT HUP INT TERM
spirv_raw="$work/shader.raw.comp.spv"
spirv="$work/shader.comp.spv"
assembly="$work/shader.spvasm"
msl="$work/shader.metal"
air="$work/shader.air"
metallib="$work/shader.metallib"
reflection="$work/descriptors.txt"

"$dxil_spirv" "$input" --output "$spirv_raw" --entry main \
    --ssbo-uav --ssbo-srv --no-bda --validate
"$script_dir/remap-adx12-spirv-bindings.sh" "$spirv_raw" "$spirv"
spirv-val --target-env vulkan1.3 --scalar-block-layout "$spirv"
spirv-dis "$spirv" -o "$assembly"
reflected_count=$(awk '/OpDecorate .* DescriptorSet 0/ { print $2 }' "$assembly" |
    sort -u | sed '/^$/d' | wc -l | tr -d ' ')
[ "$reflected_count" -eq "$descriptor_count" ] || {
    echo "DXIL reflection has $reflected_count set-0 resources, expected $descriptor_count" >&2
    exit 1
}
parameter=0
for resource_id in $(awk '$1 == "OpDecorate" && $3 == "DescriptorSet" && $4 == 0 { print $2 }' "$assembly"); do
    binding=$(awk -v resource_id="$resource_id" \
        '$1 == "OpDecorate" && $2 == resource_id && $3 == "Binding" { print $4; exit }' \
        "$assembly")
    [ -n "$binding" ] || {
        echo "DXIL reflection resource $resource_id has no binding" >&2
        exit 1
    }
    storage_class=$(awk -v resource_id="$resource_id" \
        '$1 == resource_id && $2 == "=" && $3 == "OpVariable" { print $NF; exit }' \
        "$assembly")
    if [ "$storage_class" = Uniform ]; then
        dwords=$(root_constant_dwords "$binding")
        if [ -n "$dwords" ]; then
            echo "descriptor_$parameter=set0,binding$binding,uniform-buffer,read-only,root-constants,dwords$dwords,parameter$parameter" >>"$reflection"
        else
            echo "descriptor_$parameter=set0,binding$binding,uniform-buffer,read-only,root-cbv,parameter$parameter" >>"$reflection"
        fi
    elif grep -Eq "OpDecorate[[:space:]]+$resource_id[[:space:]]+NonWritable" "$assembly"; then
        echo "descriptor_$parameter=set0,binding$binding,storage-buffer,read-only,root-srv,parameter$parameter" >>"$reflection"
    elif grep -Eq "OpDecorate[[:space:]]+$resource_id[[:space:]]+NonReadable" "$assembly"; then
        echo "descriptor_$parameter=set0,binding$binding,storage-buffer,write-only,root-uav,parameter$parameter" >>"$reflection"
    else
        echo "DXIL reflection resource $resource_id must be read-only or write-only" >&2
        exit 1
    fi
    parameter=$((parameter + 1))
done
[ "$parameter" -eq "$descriptor_count" ] || {
    echo "DXIL reflection produced $parameter descriptors, expected $descriptor_count" >&2
    exit 1
}
declared_root_constants=$(printf '%s' "$root_constants" | awk -F, 'NF && $0 != "" { print NF }')
reflected_root_constants=$(grep -c ',root-constants,' "$reflection" || true)
[ "${declared_root_constants:-0}" -eq "$reflected_root_constants" ] || {
    echo 'ADX12_ROOT_CONSTANTS names a binding that is not a reflected uniform buffer' >&2
    exit 1
}
threadgroup=$(awk '/OpExecutionMode .* LocalSize / { print $(NF-2) "," $(NF-1) "," $NF; exit }' "$assembly")
[ -n "$threadgroup" ] || { echo 'DXIL reflection has no fixed compute local size' >&2; exit 1; }

"$kosmicomp" --adx12-compute-ssbo-count "$descriptor_count" "$spirv" >"$msl"
xcrun -sdk macosx metal -c "$msl" -o "$air"
xcrun -sdk macosx metallib "$air" -o "$metallib"

dxil_hash=$(hash "$input")
spirv_hash=$(hash "$spirv")
msl_hash=$(hash "$msl")
reflection_hash=$(hash "$reflection")
cache_key=$(printf '%s\n' adx12-compiler-cache-v1 "$dxmt_commit" \
    "$dxil_spirv_commit" "$mesa_commit" "$dxil_hash" "$spirv_hash" \
    "$msl_hash" "$descriptor_count" "$threadgroup" "$reflection_hash" "$root_constants" | shasum -a 256 | awk '{print $1}')
entry="$cache_root/$cache_key"
if [ -f "$entry/manifest.txt" ]; then
    printf 'ADX12_COMPILER_MANIFEST=%s\n' "$entry/manifest.txt"
    exit 0
fi

stage="$work/publish"
mkdir -p "$stage"
install -m 0644 "$msl" "$stage/shader.metal"
{
    echo 'schema=adx12-compiler-cache-v1'
    echo 'compiler_abi=2'
    echo "cache_key=$cache_key"
    echo "dxmt_commit=$dxmt_commit"
    echo "dxil_spirv_commit=$dxil_spirv_commit"
    echo "mesa_commit=$mesa_commit"
    echo "dxil_sha256=$dxil_hash"
    echo "spirv_sha256=$spirv_hash"
    echo "msl_sha256=$msl_hash"
    echo 'stage=compute'
    echo 'entry_point=main_entrypoint'
    echo "threadgroup_size=$threadgroup"
    echo "descriptor_count=$descriptor_count"
    cat "$reflection"
    echo 'msl_file=shader.metal'
    echo 'kk_root_set0_offset=928'
    echo 'kk_root_size=2240'
} >"$stage/manifest.txt"

mkdir -p "$cache_root"
mv "$stage" "$entry"
printf 'cache_key=%s\n' "$cache_key"
printf 'ADX12_COMPILER_MANIFEST=%s\n' "$entry/manifest.txt"
