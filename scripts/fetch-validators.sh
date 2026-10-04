#!/usr/bin/env bash
# Fetch the plugin validators into tools/validators/ (not tracked). Downloads are pinned by
# SHA-256; a mismatch is fatal. LV2 RDF tools and Carla's discovery tool are unpacked from
# Ubuntu packages without root (apt-get download + dpkg -x).
set -euo pipefail
dest="$(cd "$(dirname "$0")/.." && pwd)/tools/validators"
mkdir -p "$dest"
cd "$dest"

fetch() {   # url sha256 output
    curl -fsSL -o "$3" "$1"
    echo "$2  $3" | sha256sum -c --quiet - || { echo "checksum mismatch for $3" >&2; rm -f "$3"; exit 1; }
}

if [[ ! -x pluginval ]]; then
    fetch https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Linux.zip \
          c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352 pluginval.zip
    unzip -oq pluginval.zip && rm pluginval.zip && chmod +x pluginval
fi

if [[ ! -x clap-validator ]]; then
    # The release zip wraps a tarball that holds the binary.
    fetch https://github.com/free-audio/clap-validator/releases/download/0.4.1/clap-validator-0.4.1-127-g152b982-ubuntu-22.04.zip \
          49edadcfb407ea0dd946ce418300e853fbd2660fa4b0d00e4f19ff8eef24ad90 clap-validator.zip
    unzip -oq clap-validator.zip && rm clap-validator.zip
    tar -xzf clap-validator-*.tar.gz && rm clap-validator-*.tar.gz
    chmod +x clap-validator
fi

if [[ ! -x lv2_validate_local ]] && command -v apt-get >/dev/null; then
    tmp="$(mktemp -d)"
    (cd "$tmp" && apt-get download lv2-dev sordi carla carla-data >/dev/null 2>&1) || true
    mkdir -p lv2root
    for deb in "$tmp"/*.deb; do [[ -e "$deb" ]] && dpkg -x "$deb" lv2root; done
    rm -rf "$tmp"
    if [[ -x lv2root/usr/bin/lv2_validate ]]; then
        sed "s#LV2DIR=\"/usr/lib/lv2\"#LV2DIR=\"$dest/lv2root/usr/lib/lv2\"#; s#^sord_validate#$dest/lv2root/usr/bin/sord_validate#" \
            lv2root/usr/bin/lv2_validate > lv2_validate_local
        chmod +x lv2_validate_local
    fi
fi

./pluginval --version
./clap-validator --version
[[ -x lv2_validate_local ]] && echo "lv2_validate: ok" || echo "lv2_validate: unavailable"
[[ -x lv2root/usr/lib/carla/carla-discovery-native ]] && echo "carla-discovery: ok" || echo "carla-discovery: unavailable"
