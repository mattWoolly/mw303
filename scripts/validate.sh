#!/usr/bin/env bash
# Run every plugin validator against a build. Exit 0 only when all available checks pass.
#
#   scripts/validate.sh [build-dir] [--release]
#
# --release adds pluginval's --repeat 3 --randomise churn. Tools come from
# scripts/fetch-validators.sh; a missing tool is reported as SKIP, not as a pass.
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build}"
[[ "${2:-}" == "--release" || "${1:-}" == "--release" ]] && churn="--repeat 3 --randomise" || churn=""
[[ "$build" == "--release" ]] && build="$root/build"

V="$root/tools/validators"
A="$build/plugin/mwacid_plugin_artefacts/Release"
[[ -d "$A" ]] || A="$(find "$build/plugin/mwacid_plugin_artefacts" -mindepth 1 -maxdepth 1 -type d | head -1)"
logs="$build/validation-logs"; mkdir -p "$logs"

status=0
result() { printf '%-28s %s\n' "$1" "$2"; [[ "$2" == FAIL* ]] && status=1; return 0; }
headless() { env -u DISPLAY xvfb-run -a -s "-screen 0 1920x1080x24" "$@"; }

pluginval_run() {   # <log name> <artefact> [extra args...]
    local log="$1" artefact="$2"; shift 2
    headless "$V/pluginval" --strictness-level 10 --validate-in-process --timeout-ms 300000 "$@" \
        --validate "$artefact" > "$logs/$log.log" 2>&1
}

if [[ -x "$V/pluginval" ]]; then
    if pluginval_run pluginval-VST3 "$A/VST3/mw12F.vst3" $churn; then
        result "pluginval VST3" PASS
    else
        result "pluginval VST3" "FAIL (see $logs/pluginval-VST3.log)"
    fi

    # LV2: pluginval's "Plugin state restoration" pokes a parameter, restores a state and reads
    # the value back without processing in between. JUCE's LV2 host only learns parameter values
    # from the plugin's run() output (patch:Set), so in a random test order that check fails for
    # any JUCE LV2 plugin (a stock four-parameter APVTS plugin fails it on the same seeds;
    # docs/host-matrix.md). It still runs in the fixed-order pass, where it passes; only the
    # randomised churn pass leaves it out. pluginval needs an absolute path for the list.
    lv2_status=PASS
    pluginval_run pluginval-LV2 "$A/LV2/mw12F.lv2" ${churn:+--repeat 3} || lv2_status="FAIL (see $logs/pluginval-LV2.log)"
    if [[ -n "$churn" && "$lv2_status" == PASS ]]; then
        pluginval_run pluginval-LV2-churn "$A/LV2/mw12F.lv2" $churn \
            --disabled-tests "$root/tests/plugin/pluginval-lv2-disabled-tests.txt" \
            || lv2_status="FAIL (see $logs/pluginval-LV2-churn.log)"
    fi
    result "pluginval LV2" "$lv2_status"
else
    result pluginval SKIP
fi

if [[ -x "$V/clap-validator" ]]; then
    rm -rf "${XDG_RUNTIME_DIR:-/run/user/$UID}/clap-validator"
    if headless "$V/clap-validator" validate --only-failed "$A/CLAP/mw12F.clap" > "$logs/clap-validator.log" 2>&1; then
        result clap-validator PASS
    else
        result clap-validator "FAIL (see $logs/clap-validator.log)"
    fi
else
    result clap-validator SKIP
fi

if [[ -x "$V/lv2_validate_local" ]]; then
    # JUCE's LV2 export produces known RDF errors; fail only on error kinds not in the baseline.
    baseline="$root/tests/plugin/lv2_validate.baseline"
    "$V/lv2_validate_local" "$A/LV2/mw12F.lv2"/*.ttl > "$logs/lv2_validate.log" 2>&1
    grep -oE 'error: .*' "$logs/lv2_validate.log" | sed -E 's/[0-9]+//g' | sort -u > "$logs/lv2_validate.kinds"
    if [[ -f "$baseline" ]]; then
        new="$(comm -23 "$logs/lv2_validate.kinds" "$baseline")"
        [[ -z "$new" ]] && result lv2_validate PASS || { result lv2_validate "FAIL (new: $(wc -l <<< "$new") kinds)"; echo "$new"; }
    else
        result lv2_validate "SKIP (no baseline; candidate at $logs/lv2_validate.kinds)"
    fi
else
    result lv2_validate SKIP
fi

carla="$V/lv2root/usr/lib/carla/carla-discovery-native"
if [[ -x "$carla" ]]; then
    for pair in "lv2 $A/LV2/mw12F.lv2" "vst3 $A/VST3/mw12F.vst3"; do
        set -- $pair
        out="$(LD_LIBRARY_PATH="$V/lv2root/usr/lib/carla" timeout 120 "$carla" "$1" "$2" 2>&1)"
        echo "$out" > "$logs/carla-$1.log"
        if grep -q 'midi.ins::1' <<< "$out" && grep -q 'audio.outs::2' <<< "$out" && grep -q 'midi.outs::1' <<< "$out"; then
            result "carla load $1" PASS
        else
            result "carla load $1" "FAIL (see $logs/carla-$1.log)"
        fi
    done
else
    result "carla load" SKIP
fi

if MW_BUILD_DIR="$build" "$root/scripts/branding-audit.sh" --binaries > "$logs/branding.log" 2>&1; then
    result "branding audit" PASS
else
    result "branding audit" "FAIL (see $logs/branding.log)"
fi

exit $status
