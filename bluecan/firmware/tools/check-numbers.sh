#!/usr/bin/env bash
# Find every place a canonical number is quoted, in both repositories.
#
#   ./tools/check-numbers.sh              # every figure, and where it is quoted
#   ./tools/check-numbers.sh queue disp   # only figures whose key or description matches
#   ./tools/check-numbers.sh --stale      # only the retired values
#   ./tools/check-numbers.sh --list       # the pattern list, without searching
#
# Why: the same figures used to live in four documents each, so a patch that changed
# reality needed four edits and usually got two. A whole-tree review on 2026-08-22 found
# six survivors of that -- documents contradicting each other about the same board. The
# fix is one home per number (../numbers.md) plus this, run after any patch that moves
# one: it prints every file that still quotes the old value, so the edit list is read off
# the screen instead of remembered.
#
# The patterns live in ../numbers.md, next to the values they belong to -- one home, not
# two. Lines in a fenced block shaped `key :: extended-regex :: description`. The
# separator is :: rather than | because the regexes use | for alternation.
#
# Exit status is 0 whenever the list parses; a hit is information, not a failure. A
# document may quote a retired figure on purpose, to mark it retired.
set -euo pipefail
cd "$(dirname "$0")/.."

NUMBERS="../numbers.md"
BLUECAN="${BLUECAN:-$HOME/projects/racechrono-canbus}"

[[ -f "$NUMBERS" ]] || { echo "pattern list not found at $NUMBERS" >&2; exit 2; }

STALE_ONLY=0
LIST_ONLY=0
FILTERS=()
for a in "$@"; do
    case "$a" in
        --stale) STALE_ONLY=1 ;;
        --list)  LIST_ONLY=1 ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        -*)      echo "unknown option: $a" >&2; exit 2 ;;
        *)       FILTERS+=("$a") ;;
    esac
done

# Pull `key | regex | description` lines out of the fenced block. Table rows start with
# a pipe and comments with a hash, so neither is mistaken for a pattern.
mapfile -t LINES < <(awk '
    /^```/      { fence = !fence; next }
    !fence      { next }
    /^[[:space:]]*#/ { next }
    /^[a-z][a-z0-9-]*[[:space:]]*::/ { print }
' "$NUMBERS")

(( ${#LINES[@]} )) || { echo "no patterns found in $NUMBERS" >&2; exit 2; }

# Where to search. The canonical file is excluded on purpose: it is meant to hold every
# figure, so listing it would bury the copies that matter.
ROOTS=(../..)
[[ -d "$BLUECAN" ]] && ROOTS+=("$BLUECAN")

EXCLUDES=(
    --exclude-dir=.git
    --exclude-dir=build
    --exclude-dir=node_modules
    --exclude-dir=candump-parse
--exclude-dir=css-skill      # vendored third-party analysis chain; its numbers are not ours
--exclude-dir=.venv
    --exclude=numbers.md
    --exclude=check-numbers.sh
)

trim() { local s="$1"; s="${s#"${s%%[![:space:]]*}"}"; printf '%s' "${s%"${s##*[![:space:]]}"}"; }

matches_filter() {
    (( ${#FILTERS[@]} == 0 )) && return 0
    local hay="$1"
    for f in "${FILTERS[@]}"; do
        [[ "$hay" == *"$f"* ]] && return 0
    done
    return 1
}

total_keys=0
total_hits=0

for line in "${LINES[@]}"; do
    key="$(trim "${line%%::*}")"
    rest="${line#*::}"
    rx="$(trim "${rest%::*}")"
    desc="$(trim "${rest##*::}")"
    [[ -n "$key" && -n "$rx" ]] || continue

    stale=0
    [[ "$key" == stale-* ]] && stale=1
    (( STALE_ONLY )) && (( ! stale )) && continue
    matches_filter "$key $desc" || continue

    if (( LIST_ONLY )); then
        printf '%-18s %s\n' "$key" "$rx"
        continue
    fi

    total_keys=$((total_keys + 1))
    mark=""
    (( stale )) && mark="  [retired]"
    printf '\n== %s%s -- %s\n' "$key" "$mark" "$desc"

    hits="$(grep -rInE "${EXCLUDES[@]}" -- "$rx" "${ROOTS[@]}" 2>/dev/null || true)"
    if [[ -z "$hits" ]]; then
        echo "   (not quoted anywhere)"
        continue
    fi

    n="$(printf '%s\n' "$hits" | wc -l)"
    total_hits=$((total_hits + n))
    printf '%s\n' "$hits" | sed 's|^\.\./\.\./|   motocan/|; s|^'"$BLUECAN"'/|   bluecan/|' \
        | cut -c1-140
done

(( LIST_ONLY )) && exit 0

printf '\n-- %d figures, %d quotes.\n' "$total_keys" "$total_hits"
echo "   A figure quoted in more than one or two places is a figure that will go stale;"
echo "   link to bluecan/numbers.md from the others instead."
