#!/usr/bin/env bash
# Branding audit: word-boundary, case-insensitive search for forbidden brand tokens in the
# project's source, assets and user-facing docs, and optionally in the built binaries.
#
#   scripts/branding-audit.sh              # sources only
#   scripts/branding-audit.sh --binaries   # sources plus `strings` of built plugin binaries
#                                          # (in $MW_BUILD_DIR, default build/)
#
# Binaries are checked only for tokens of five characters or more (short ones match
# random bytes). Exempt: docs/research/ (it cites its sources), build trees, fetched dependencies,
# validators, and the token list itself. Exit 0 when clean, 1 on any hit.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scan_binaries=0
[[ "${1:-}" == "--binaries" ]] && scan_binaries=1

mapfile -t tokens < <(grep -v '^\s*#' "$root/scripts/forbidden_tokens.txt" | sed 's/\s*$//' | grep -v '^$')
pattern="\\b($(printf '%s\n' "${tokens[@]}" | sed 's/[.[\*^$()+?{}|]/\\&/g' | paste -sd '|'))\\b"

hits=0
while IFS= read -r -d '' file; do
    if out=$(grep -nIiP "$pattern" "$file" 2>/dev/null); then
        while IFS= read -r line; do echo "${file#"$root"/}:$line"; done <<< "$out"
        hits=$((hits + $(wc -l <<< "$out")))
    fi
done < <(find "$root" \
    \( -path "$root/build*" -o -path "$root/.deps" -o -path "$root/docs/research" \
       -o -path "$root/tools/validators" -o -path "$root/.git" -o -path "$root/.cache" \) -prune \
    -o -type f ! -name forbidden_tokens.txt ! -name '*.png' ! -name '*.ttf' ! -name '*.otf' \
       ! -name '*.wav' ! -name '*.mid' -print0)

if (( scan_binaries )); then
    # Short tokens match random byte runs in machine code, so binaries are checked only for
    # tokens of five characters or more.
    long_pattern="\\b($(printf '%s\n' "${tokens[@]}" | awk 'length >= 5' | sed 's/[.[\*^$()+?{}|]/\\&/g' | paste -sd '|'))\\b"
    artefacts="${MW_BUILD_DIR:-$root/build}/plugin/mwacid_plugin_artefacts"
    while IFS= read -r -d '' bin; do
        if out=$(strings -n 4 "$bin" | grep -iP "$long_pattern" | sort -u); then
            while IFS= read -r line; do echo "${bin#"$root"/}: $line"; done <<< "$out"
            hits=$((hits + $(wc -l <<< "$out")))
        fi
    done < <(find "$artefacts" -type f \( -name '*.so' -o -name '*.clap' -o -perm -u+x \) -print0 2>/dev/null)
fi

if (( hits > 0 )); then
    echo "branding audit: $hits hit(s)" >&2
    exit 1
fi
echo "branding audit: clean"
