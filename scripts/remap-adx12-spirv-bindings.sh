#!/bin/sh
set -eu

[ "$#" -eq 2 ] || {
    echo 'usage: remap-adx12-spirv-bindings.sh input.spv output.spv' >&2
    exit 64
}

input=$1
output=$2
work=$(mktemp -d -t adx12-spirv-remap)
trap 'rm -rf "$work"' EXIT HUP INT TERM
assembly=$work/input.spvasm
remapped=$work/remapped.spvasm

spirv-dis "$input" -o "$assembly"
awk '
    $1 == "OpDecorate" && $3 == "DescriptorSet" && $4 == 0 {
        set0[$2] = 1
    }
    $1 == "OpDecorate" && $3 == "Binding" && set0[$2] {
        $4 = next_binding++
    }
    { print }
' "$assembly" >"$remapped"
spirv-as --target-env spv1.3 "$remapped" -o "$output"
spirv-val --target-env vulkan1.3 --scalar-block-layout "$output"
