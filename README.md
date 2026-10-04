<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# mw12F

mw12F is a circuit-modelled emulation of the classic 1982 silver bass line synth, built for Linux as VST3, CLAP, LV2 and a standalone app. The name is the original model number written in hex.

The voice is derived from the service-notes schematic rather than fitted to recordings: a nonlinear diode ladder with its 18 nF bottom capacitor solved with zero-delay feedback, the square wave produced by a two-state model of the transistor shaper (so its duty falls from about 0.58 at C1 to 0.36 at C5, as the hardware's does), and the accent network with its 1 µF capacitor that keeps charge between notes, so three accents in a row climb instead of repeating. On top of that sit the sequencer, arpeggiator, generator, distortion and effects rack you would expect from the best commercial emulation of the instrument, plus a layer of circuit mods that emulation never had.

## What is in it

**Voice.** 13 front-panel knobs, waveform switch, extended tuning, and the two couple switches (resonance drives the accent sweep shape; accent drives both filter and volume accent). Slide is a 22 ms RC lag on pitch, the same time for a semitone or two octaves. Gates follow the original CPU: half a step plus 1.35 ms, with an analog turn-on delay that depends on how long the gate was off. The output amplifier's rails are modelled, so hot accents round off the way the hardware does.

**Calibration.** A hinged cover hides 15 trimmers in physical units: cutoff trim in mV, coupling and accent capacitors in nF and µF, octave width in oct/V, square pulse-width shift, VCA clicks and noise, ladder load, loop gain, filter CV scale, gate delay, drift and CPU gate jitter. 22 factory calibration units range from a stock reference to worn and modified boards.

**Mods.** Overdrive into the filter (up to 66.6x the stock level, which quenches resonance as the circuit does), filter FM, filter tracking, amp decay and sustain, soft attack, double resonance, accent sweep speed, a muffler, extended ranges and a sub oscillator. One switch returns all of it to stock, bit for bit.

**Effects.** A parametric distortion with eight circuit families (diode pair, op-amp hard clip, asymmetric overdrive, scooped stomp box, mid-hump soft clip, gated fuzz, console input stage, wavefolder), 32 built-in voicings level-matched on the instrument's own output, an editable custom voicing and a one-knob compressor before or after the clipper. Then a 4-band EQ, a tempo-synced stereo delay with a filter inside the feedback loop, an early/late reverb sized in metres, a two-line chorus and a limiter with soft clip. All six blocks can be dragged into any order, the distortion included.

**Sequencer.** 48 pattern slots of 1 to 64 steps, each step with note, octave, accent, slide, tie, vibrato, probability, ratchet and gate. Per pattern: rate from 1/64 to 1/1 with dotted and triplet, shuffle, gate length, direction and transpose. Patterns switch at the end of the current one (or the next bar, or at once), from the editor or from MIDI notes. Playback locks to the host's song position and runs on an internal clock when the host is stopped. Live overrides force or mute notes, accents, slides and vibrato while held. A piano roll and a keyboard-style step editor both edit the same pattern, with undo.

**Arpeggiator and generator.** 48 arpeggiator patterns that index the held notes from lowest to highest. The generator fills a pattern from a note mask, root, octave mask and seed; four density knobs then reveal or hide notes, accents, slides and vibrato without re-rolling, and hand edits survive them.

**MIDI.** External play mode: overlapping notes slide, velocity above a threshold accents, pitch bend and mod wheel vibrato work as expected. The sequencer's notes go to MIDI out, delayed to line up with the audio. Every control can be MIDI-learned. Patterns export as .mid by drag or button and import back.

**Presets.** 858 factory presets in twelve types (127 scenes, 183 sounds, 310 patterns, 56 arpeggiator patterns, 90 generator templates, calibration and effect presets), all original and generated from recipes in `tools/factory/`, level-matched to -24 dBFS RMS. A browser with tag groups, search, favourites and audition with cancel. User presets live in `~/.config/mwAudio/mw12F/`.

**Editor.** Vector drawn, Silver and Night themes, zoom from 75 to 200 %.

## Building

Requirements on Ubuntu 24.04: gcc 13, CMake 3.25 or later, Ninja, and the JUCE Linux dependencies (ALSA, JACK, X11, Xrandr, Xinerama, Xcursor, FreeType, fontconfig, GL). JUCE 8.0.15, clap-juce-extensions and Catch2 are fetched at pinned versions; `scripts/fetch-deps.sh` checks them out once under `.deps/` so build trees share them.

```bash
scripts/fetch-deps.sh                       # optional, saves re-downloading
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
scripts/install-user.sh build               # ~/.vst3, ~/.clap, ~/.lv2, ~/.local/bin/mw12F
```

Add `-DMW_LTO=ON` for a release build (slower to link). `-DMW_BUILD_PLUGIN=OFF` builds only the JUCE-free core, its tests and the command-line tools.

## Testing

```bash
cmake -S . -B build-core -G Ninja -DMW_BUILD_PLUGIN=OFF && cmake --build build-core
ctest --test-dir build-core                 # core: voice, effects, sequencer, player, engine
ctest --test-dir build                      # plugin, editor and core, in the full tree
scripts/fetch-validators.sh                 # pluginval, clap-validator, LV2 tools, Carla (pinned by SHA-256)
scripts/validate.sh build --release         # every validator, plus the branding audit
```

The core has nine Catch2 suites, one per module, with 543 test cases. Most of them check the model against numbers from the research: filter response against the analytic transfer function, envelope time constants, the accent table for one to six consecutive accents, slide time, square duty against pitch, gate timing to the sample, block-size invariance, and zero allocations on the audio path. The plugin adds 145 cases for state, presets, MIDI learn, host transport and the editor.

`scripts/validate.sh` runs pluginval at strictness 10 on VST3 and LV2, clap-validator, `lv2_validate` against a baseline of the errors JUCE's own LV2 export produces, Carla load checks, and `scripts/branding-audit.sh`.

`tools/acid-render` renders any preset or pattern to WAV without a host, bit-identically every run:

```bash
build/tools/acid-render/acid-render --preset assets/factory/scene/first-squelch.mw12f \
    --bpm 125 --bars 8 --out squelch.wav
build/tools/acid-render/acid-render --steps "C:A C C+:S C D#:A . G-:S C" \
    --param cutoff=30 --param resonance=85 --bpm 126 --bars 4 --out line.wav
```

## Layout

| Path | What lives there |
|---|---|
| `libs/acid-core/` | the JUCE-free core: `dsp` (voice), `fx`, `seq` (patterns, generator, codecs, player), `engine` (wiring, state codec) |
| `plugin/src/` | the processor, model, preset manager and the Controller API the editor uses |
| `plugin/ui/` | the editor and its component kit; `plugin/ui-gallery/` renders it to PNG |
| `tests/` | one Catch2 suite per module, plus `tests/plugin/` |
| `tools/` | `acid-render`, the factory library builder and QA script, analysis scripts |
| `assets/` | embedded fonts (OFL) and the factory library |
| `docs/SPEC.md` | the build contract and the decisions made along the way |
| `docs/research/` | seven research documents behind every number in the model |
| `docs/parity.md` | the feature checklist, item by item, with evidence |
| `docs/host-matrix.md` | what was tested in which host, and how |

## Limits

Nobody has listened to it against real hardware yet. Every judgement of the sound so far is a measurement against the research documents, which rest on the schematic, published analyses and a handful of measurements of real units. Eight quantities the documents could not settle are trimmers with documented defaults (SPEC section 7); the most audible are where full resonance starts to self-oscillate, the accent volume boost and the bass corner at the VCA. A recording of a stock unit playing a few test patterns would settle them.

Tested hosts are REAPER 7.79 (VST3, CLAP and LV2, rendered headless) and Carla's discovery tools. Bitwig, Ardour and Renoise have not been tried. `docs/host-matrix.md` has the details.

Not in v1: song mode, a modulation matrix, importing other products' pattern files, audio input to the filter. Ten checklist items below MUST are partial or missing, listed in `docs/parity.md`.

## Licence

AGPL-3.0-or-later. JUCE 8 is used under its AGPLv3 option. Third-party components: the VST3 SDK, CLAP and clap-juce-extensions (MIT), LV2 (ISC), Barlow and Share Tech Mono fonts (SIL OFL 1.1, licences in `assets/fonts/`).
