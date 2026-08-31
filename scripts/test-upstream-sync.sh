#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sync_script="$script_dir/adx12-upstream-sync.sh"
fixture_root=$(mktemp -d "${TMPDIR:-/tmp}/adx12-sync-test.XXXXXX")
trap 'rm -rf "$fixture_root"' EXIT HUP INT TERM

upstream="$fixture_root/upstream"
fixture_repo="$fixture_root/repo"
manifest="$fixture_root/upstreams.tsv"
lock="$fixture_root/upstreams.lock.tsv"
output="$fixture_root/output"
patch_dir="$fixture_repo/dependencies/patches/fixture"

git init --quiet -b main "$upstream"
git -C "$upstream" config user.name fixture
git -C "$upstream" config user.email fixture@invalid.example
printf 'base\n' > "$upstream/required.txt"
git -C "$upstream" add required.txt
git -C "$upstream" commit --quiet -m base
old_commit=$(git -C "$upstream" rev-parse HEAD)

mkdir -p "$fixture_repo/scripts" "$patch_dir"
cp "$sync_script" "$fixture_repo/scripts/adx12-upstream-sync.sh"
printf '# name|url|branch|license|integration|required_paths|role|submodules\n' > "$manifest"
printf 'fixture|%s|main|MIT|patch-queue|required.txt|fixture|none\n' "$upstream" >> "$manifest"
printf '# name|commit\nfixture|%s\n' "$old_commit" > "$lock"

patch_work="$fixture_root/patch-work"
git clone --quiet "$upstream" "$patch_work"
git -C "$patch_work" config user.name fixture
git -C "$patch_work" config user.email fixture@invalid.example
printf 'downstream\n' > "$patch_work/downstream.txt"
git -C "$patch_work" add downstream.txt
git -C "$patch_work" commit --quiet -m downstream
git -C "$patch_work" format-patch --quiet -1 --output-directory "$patch_dir"

printf 'upstream\n' >> "$upstream/required.txt"
git -C "$upstream" commit --quiet -am upstream
new_commit=$(git -C "$upstream" rev-parse HEAD)

ADX12_UPSTREAM_MANIFEST="$manifest" \
ADX12_UPSTREAM_LOCK="$lock" \
ADX12_SYNC_OUTPUT_DIR="$output" \
    "$fixture_repo/scripts/adx12-upstream-sync.sh" prepare fixture >/dev/null

candidate="$output/candidate-upstreams.lock.tsv"
grep -q "fixture|$new_commit" "$candidate"

materialized="$fixture_root/materialized"
ADX12_UPSTREAM_MANIFEST="$manifest" \
ADX12_UPSTREAM_LOCK="$lock" \
ADX12_SYNC_OUTPUT_DIR="$output" \
    "$fixture_repo/scripts/adx12-upstream-sync.sh" \
    materialize fixture "$materialized" "$candidate" >/dev/null

expected=$(printf 'base\nupstream\n')
actual=$(cat "$materialized/required.txt")
[ "$actual" = "$expected" ]
[ "$(cat "$materialized/downstream.txt")" = downstream ]
git -C "$materialized" merge-base --is-ancestor "$new_commit" HEAD

printf 'upstream sync fixture passed\n'
