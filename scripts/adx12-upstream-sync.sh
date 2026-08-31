#!/bin/sh
set -eu

usage()
{
    cat <<'EOF'
usage:
  adx12-upstream-sync.sh check [all|component]
  adx12-upstream-sync.sh prepare [all|component]
  adx12-upstream-sync.sh verify [all|component] [lock-file]
  adx12-upstream-sync.sh materialize component destination [lock-file]

Environment:
  ADX12_UPSTREAM_MANIFEST   alternate upstream manifest
  ADX12_UPSTREAM_LOCK       alternate current lock
  ADX12_SYNC_OUTPUT_DIR     report/candidate output directory
  ADX12_SYNC_KEEP_WORK      retain temporary checkouts when set to 1
EOF
}

if [ "$#" -lt 1 ]; then
    usage >&2
    exit 64
fi

command_name=$1
target=${2:-all}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
manifest=${ADX12_UPSTREAM_MANIFEST:-"$repo_root/dependencies/upstreams.tsv"}
default_lock=${ADX12_UPSTREAM_LOCK:-"$repo_root/dependencies/upstreams.lock.tsv"}
output_root=${ADX12_SYNC_OUTPUT_DIR:-"$repo_root/artifacts/upstream-sync"}
patch_root="$repo_root/dependencies/patches"
overlay_root="$repo_root/dependencies/overlays"

die()
{
    printf 'adx12-upstream-sync: %s\n' "$*" >&2
    exit 1
}

require_file()
{
    [ -f "$1" ] || die "required file is missing: $1"
}

require_file "$manifest"
require_file "$default_lock"

component_exists()
{
    awk -F '|' -v wanted="$1" '
        $0 !~ /^#/ && NF >= 8 && $1 == wanted { found = 1 }
        END { exit found ? 0 : 1 }
    ' "$manifest"
}

manifest_field()
{
    awk -F '|' -v wanted="$1" -v field="$2" '
        $0 !~ /^#/ && NF >= 8 && $1 == wanted { print $field; exit }
    ' "$manifest"
}

locked_commit()
{
    awk -F '|' -v wanted="$1" '
        $0 !~ /^#/ && NF >= 2 && $1 == wanted { print $2; exit }
    ' "$2"
}

selected_components()
{
    if [ "$target" = all ]; then
        awk -F '|' '$0 !~ /^#/ && NF >= 8 { print $1 }' "$manifest"
    else
        component_exists "$target" || die "unknown component: $target"
        printf '%s\n' "$target"
    fi
}

remote_head()
{
    remote_url=$1
    remote_branch=$2
    git ls-remote --heads "$remote_url" "refs/heads/$remote_branch" |
        awk 'NR == 1 { print $1 }'
}

replace_lock_commit()
{
    replace_lock_file=$1
    replace_name=$2
    replace_commit=$3
    replace_tmp="$replace_lock_file.tmp"
    awk -F '|' -v OFS='|' -v wanted="$replace_name" -v commit="$replace_commit" '
        $0 ~ /^#/ { print; next }
        $1 == wanted { $2 = commit; found = 1 }
        { print }
        END { if (!found) exit 1 }
    ' "$replace_lock_file" > "$replace_tmp" || {
        rm -f "$replace_tmp"
        die "cannot update candidate lock for $replace_name"
    }
    mv "$replace_tmp" "$replace_lock_file"
}

check_required_paths()
{
    required_checkout=$1
    required_csv=$2
    old_ifs=$IFS
    IFS=,
    set -- $required_csv
    IFS=$old_ifs
    for required_path in "$@"; do
        [ -e "$required_checkout/$required_path" ] ||
            die "required upstream path disappeared: $required_path"
    done
}

apply_downstream_delta()
{
    delta_name=$1
    delta_checkout=$2
    delta_patch_dir="$patch_root/$delta_name"
    delta_overlay_dir="$overlay_root/$delta_name"

    git -C "$delta_checkout" config user.name 'ADX12 upstream sync'
    git -C "$delta_checkout" config user.email 'adx12-sync@invalid.example'

    if [ -d "$delta_patch_dir" ]; then
        patch_list=$(find "$delta_patch_dir" -type f -name '*.patch' -print |
            LC_ALL=C sort)
        if [ -n "$patch_list" ]; then
            while IFS= read -r patch_file; do
                if ! git -C "$delta_checkout" am --3way "$patch_file"; then
                    git -C "$delta_checkout" am --abort >/dev/null 2>&1 || true
                    die "$delta_name downstream patch no longer applies: $patch_file"
                fi
            done <<EOF
$patch_list
EOF
        fi
    fi

    if [ -d "$delta_overlay_dir" ] &&
       find "$delta_overlay_dir" -type f -print -quit | grep -q .; then
        (cd "$delta_overlay_dir" && tar -cf - .) |
            (cd "$delta_checkout" && tar -xf -)
        git -C "$delta_checkout" add -A
        if ! git -C "$delta_checkout" diff --cached --quiet; then
            git -C "$delta_checkout" commit --quiet \
                -m "adx12: apply $delta_name source overlay"
        fi
    fi
}

checkout_component()
{
    checkout_name=$1
    checkout_pin=$2
    checkout_destination=$3
    checkout_url=$(manifest_field "$checkout_name" 2)
    checkout_branch=$(manifest_field "$checkout_name" 3)
    checkout_required=$(manifest_field "$checkout_name" 6)
    checkout_submodules=$(manifest_field "$checkout_name" 8)

    [ -n "$checkout_pin" ] || die "missing lock commit for $checkout_name"
    [ ! -e "$checkout_destination" ] ||
        die "materialization destination already exists: $checkout_destination"

    git clone --quiet --filter=blob:none --no-checkout \
        --branch "$checkout_branch" "$checkout_url" "$checkout_destination"
    git -C "$checkout_destination" cat-file -e "$checkout_pin^{commit}" ||
        die "$checkout_name pin is not available from its declared branch"
    git -C "$checkout_destination" checkout --quiet --detach "$checkout_pin"
    if [ "$checkout_submodules" = recursive ]; then
        git -C "$checkout_destination" submodule sync --quiet --recursive
        git -C "$checkout_destination" submodule update --quiet --init --recursive
    elif [ "$checkout_submodules" != none ]; then
        die "$checkout_name has invalid submodule policy: $checkout_submodules"
    fi
    check_required_paths "$checkout_destination" "$checkout_required"
    apply_downstream_delta "$checkout_name" "$checkout_destination"
    git -C "$checkout_destination" merge-base --is-ancestor \
        "$checkout_pin" HEAD || die "$checkout_name downstream delta lost its pin"
    git -C "$checkout_destination" diff --check "$checkout_pin..HEAD"
}

prepare_component()
{
    prepare_name=$1
    prepare_lock=$2
    prepare_candidate_lock=$3
    prepare_url=$(manifest_field "$prepare_name" 2)
    prepare_branch=$(manifest_field "$prepare_name" 3)
    prepare_license=$(manifest_field "$prepare_name" 4)
    prepare_mode=$(manifest_field "$prepare_name" 5)
    prepare_submodules=$(manifest_field "$prepare_name" 8)
    prepare_old=$(locked_commit "$prepare_name" "$prepare_lock")
    prepare_latest=$(remote_head "$prepare_url" "$prepare_branch")
    prepare_report_dir="$output_root/$prepare_name"
    prepare_report="$prepare_report_dir/report.md"
    prepare_work="$work_root/$prepare_name"

    [ -n "$prepare_old" ] || die "$prepare_name has no lock entry"
    [ -n "$prepare_latest" ] || die "$prepare_name remote branch is unavailable"
    mkdir -p "$prepare_report_dir"
    rm -f "$prepare_report"

    checkout_component "$prepare_name" "$prepare_latest" "$prepare_work"

    if ! git -C "$prepare_work" merge-base --is-ancestor \
        "$prepare_old" "$prepare_latest"; then
        die "$prepare_name upstream rewrote history after the locked commit"
    fi

    prepare_candidate=$(git -C "$prepare_work" rev-parse HEAD)
    replace_lock_commit "$prepare_candidate_lock" "$prepare_name" "$prepare_latest"

    cat > "$prepare_report" <<EOF
# ADX12 upstream candidate: $prepare_name

| Field | Value |
| --- | --- |
| Upstream | \`$prepare_url\` |
| Branch | \`$prepare_branch\` |
| License | \`$prepare_license\` |
| Integration | \`$prepare_mode\` |
| Submodules | \`$prepare_submodules\` |
| Previous pin | \`$prepare_old\` |
| Candidate pin | \`$prepare_latest\` |
| Materialized head after downstream delta | \`$prepare_candidate\` |
| Previous pin retained as upstream ancestor | yes |
| Candidate pin retained as downstream ancestor | yes |
| Required source paths | present |
| Patch/overlay whitespace validation | passed |

The live lock and any developer checkout were not modified. Review the
candidate lock and run the component build/regression suite before promotion.
EOF
}

run_check()
{
    mkdir -p "$output_root"
    aggregate="$output_root/check-report.md"
    cat > "$aggregate" <<'EOF'
# ADX12 upstream status

| Component | Locked | Remote | Status |
| --- | --- | --- | --- |
EOF
    for check_name in $(selected_components); do
        check_url=$(manifest_field "$check_name" 2)
        check_branch=$(manifest_field "$check_name" 3)
        check_locked=$(locked_commit "$check_name" "$default_lock")
        check_remote=$(remote_head "$check_url" "$check_branch")
        [ -n "$check_locked" ] || die "$check_name has no lock entry"
        [ -n "$check_remote" ] || die "$check_name remote branch is unavailable"
        if [ "$check_locked" = "$check_remote" ]; then
            check_status=current
        else
            check_status=update-available
        fi
        printf '| `%s` | `%s` | `%s` | %s |\n' \
            "$check_name" "$check_locked" "$check_remote" "$check_status" \
            >> "$aggregate"
    done
    cat "$aggregate"
}

run_prepare()
{
    mkdir -p "$output_root"
    candidate_lock="$output_root/candidate-upstreams.lock.tsv"
    prepare_summary="$output_root/prepare-report.md"
    cp "$default_lock" "$candidate_lock"
    for prepare_name in $(selected_components); do
        prepare_component "$prepare_name" "$default_lock" "$candidate_lock"
    done
    cat > "$prepare_summary" <<'EOF'
# ADX12 upstream synchronization candidate

| Component | Previous pin | Candidate pin | Status |
| --- | --- | --- | --- |
EOF
    for prepare_name in $(selected_components); do
        prepare_previous=$(locked_commit "$prepare_name" "$default_lock")
        prepare_next=$(locked_commit "$prepare_name" "$candidate_lock")
        if [ "$prepare_previous" = "$prepare_next" ]; then
            prepare_status=current
        else
            prepare_status=update-prepared
        fi
        printf '| `%s` | `%s` | `%s` | %s |\n' \
            "$prepare_name" "$prepare_previous" "$prepare_next" \
            "$prepare_status" >> "$prepare_summary"
    done
    cat >> "$prepare_summary" <<'EOF'

Every selected candidate retained its previous pin as an upstream ancestor,
retained its new pin below the downstream patch/overlay head, preserved the
declared source paths, and passed downstream whitespace validation. Component
reports are stored in the sibling directories.
EOF
    printf '\nCandidate lock: %s\n' "$candidate_lock"
    if cmp -s "$default_lock" "$candidate_lock"; then
        printf 'Result: all selected upstreams are current.\n'
    else
        printf 'Result: reviewable upstream updates were prepared.\n'
    fi
}

run_verify()
{
    verify_lock=${3:-$default_lock}
    require_file "$verify_lock"
    for verify_name in $(selected_components); do
        verify_pin=$(locked_commit "$verify_name" "$verify_lock")
        verify_destination="$work_root/verify-$verify_name"
        checkout_component "$verify_name" "$verify_pin" "$verify_destination"
        printf 'verified %s at %s\n' "$verify_name" "$verify_pin"
    done
}

run_materialize()
{
    [ "$#" -ge 3 ] && [ "$#" -le 4 ] || {
        usage >&2
        exit 64
    }
    materialize_name=$2
    materialize_destination=$3
    materialize_lock=${4:-$default_lock}
    component_exists "$materialize_name" ||
        die "unknown component: $materialize_name"
    require_file "$materialize_lock"
    materialize_pin=$(locked_commit "$materialize_name" "$materialize_lock")
    checkout_component "$materialize_name" "$materialize_pin" \
        "$materialize_destination"
    printf 'materialized %s at %s in %s\n' \
        "$materialize_name" "$materialize_pin" "$materialize_destination"
}

work_root=$(mktemp -d "${TMPDIR:-/tmp}/adx12-upstream-sync.XXXXXX")
cleanup()
{
    if [ "${ADX12_SYNC_KEEP_WORK:-0}" = 1 ]; then
        printf 'retained sync work directory: %s\n' "$work_root" >&2
    else
        rm -rf "$work_root"
    fi
}
trap cleanup EXIT HUP INT TERM

case "$command_name" in
    check)
        [ "$#" -le 2 ] || { usage >&2; exit 64; }
        run_check
        ;;
    prepare)
        [ "$#" -le 2 ] || { usage >&2; exit 64; }
        run_prepare
        ;;
    verify)
        [ "$#" -le 3 ] || { usage >&2; exit 64; }
        run_verify "$@"
        ;;
    materialize)
        run_materialize "$@"
        ;;
    *)
        usage >&2
        exit 64
        ;;
esac
