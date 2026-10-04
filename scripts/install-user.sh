#!/usr/bin/env bash
# Install a build into the user's plugin folders: ~/.vst3, ~/.clap, ~/.lv2, and the
# standalone app into ~/.local/bin. Replaces any previous mw12F install there.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build}"
A="$build/plugin/mwacid_plugin_artefacts/Release"
[[ -d "$A" ]] || { echo "no Release artefacts under $build" >&2; exit 1; }

mkdir -p ~/.vst3 ~/.clap ~/.lv2 ~/.local/bin
rm -rf ~/.vst3/mw12F.vst3 ~/.lv2/mw12F.lv2 ~/.clap/mw12F.clap
cp -r "$A/VST3/mw12F.vst3" ~/.vst3/
cp -r "$A/LV2/mw12F.lv2" ~/.lv2/
cp "$A/CLAP/mw12F.clap" ~/.clap/
install -m 755 "$A/Standalone/mw12F" ~/.local/bin/mw12F
echo "installed: ~/.vst3/mw12F.vst3 ~/.clap/mw12F.clap ~/.lv2/mw12F.lv2 ~/.local/bin/mw12F"
