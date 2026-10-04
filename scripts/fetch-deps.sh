#!/usr/bin/env bash
# Optional: check the pinned dependencies out once under .deps/ so every build tree
# shares them. Without this, CMake fetches them into each build directory.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$root/.deps" && cd "$root/.deps"

[[ -d JUCE ]] || git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git
if [[ ! -d clap-juce-extensions ]]; then
    git clone https://github.com/free-audio/clap-juce-extensions.git
    git -C clap-juce-extensions checkout 55525c9858d4b25687be7759a5e0f70eccef218e
    git -C clap-juce-extensions submodule update --init --recursive
fi
[[ -d Catch2 ]] || git clone --depth 1 --branch v3.7.1 https://github.com/catchorg/Catch2.git
