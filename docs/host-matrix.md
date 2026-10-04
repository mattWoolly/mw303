# mw12F host matrix

Recorded 2026-10-03 against the Release artefacts in `build-plugin/plugin/mwacid_plugin_artefacts/Release`, unchanged for the whole run:

| Artefact | md5 |
|---|---|
| `CLAP/mw12F.clap` | `5e727f20f20cc7486c4fbc3be9f0eaa4` |
| `VST3/mw12F.vst3/Contents/x86_64-linux/mw12F.so` | `55cd255b105a5fcea9a02ac6382e9a79` |
| `LV2/mw12F.lv2/libmw12F.so` | `75712509ef3424bce0e81900519aa8cb` |

Hosts on this machine: REAPER 7.79 (linux-x86_64; loads VST3, CLAP and LV2), Carla 2.5.8 discovery (from `tools/validators/lv2root`; LV2 and VST3, no CLAP), pluginval 1.0.4 (VST3, LV2), clap-validator 0.4.1 (CLAP). Renoise 3.5.4 is installed but has no command-line render, so it was not tested. Ardour, Bitwig, Qtractor and jalv are not installed.

## How the REAPER runs were made

Everything ran headless: `env -u DISPLAY xvfb-run -a /opt/REAPER/reaper -cfgfile <scratch>/cfg/reaper.ini -nosplash -newinst -ignoreerrors`, with `HOME` and `XDG_CONFIG_HOME` pointed at scratch so neither REAPER nor the plugin touched the user's files. The resource directory's `reaper.ini` sets `vstpath`, `clap_path_linux-x86_64` and `lv2path_linux` to the three artefact directories and nothing else. Audio was either no device (`linux_audio_mode=5`) or the Dummy Audio device (`linux_audio_mode=2`); results differ between the two, so each row says which.

A ReaScript (`build.lua`) builds each project: one track with the instrument, a second track fed by a MIDI-only send from the first and holding a JSFX that writes every incoming MIDI event into its audio output (note-on on the left channel as pitch/128 + velocity/16384, note-off on the right). Renders are 48 kHz 32-bit float stems of both tracks, made with `-renderproject`. The JSFX stem turns the instrument's MIDI out into sample-exact data.

Plugin state was set two ways. First, by moving parameters through ReaScript on a live instance (audio running) and letting REAPER save the project. Second, by writing the plugin's own XML state into REAPER's chunk encoding for each format (CLAP: base64 of the state; VST3: REAPER's 44-byte header plus JUCE's component state; LV2: JUCE's base64 text in `|` / `c|` lines). The injector reproduces REAPER's own files byte for byte before it is used. The modified state changes six parameters (cutoff 60, resonance 20, decay 70, delay off, reverb off, pattern slot 3) and replaces pattern A3 with a 12-step chromatic ladder from C with an accent every third step, so every note in the MIDI out says which step played.

Reference audio comes from `build-plugin/tools/acid-render/acid-render` with the same state, tempo, quality tier and 512-sample blocks. A REAPER render "matches" when it equals the reference sample for sample after removing the plugin latency REAPER compensates (45 samples at Normal, 47 at High).

Scripts, projects, renders and logs: `/tmp/claude-1000/-home-mwoolly-projects-mw303/dad63690-756f-4b60-b4c2-2627c3ad5533/scratchpad/reaper-qa/`.

## Matrix

| # | Host | Format | Test | Result | Evidence |
|---|---|---|---|---|---|
| 1 | REAPER 7.79 | VST3 | Scan and load | PASS | `VST3i: mw12F (mwAudio)`, 0 in / 2 out, 2229 parameters (JUCE's MIDI CC parameters included) |
| 2 | REAPER 7.79 | CLAP | Scan and load | PASS | `CLAPi: mw12F (mwAudio)`, 147 parameters |
| 3 | REAPER 7.79 | LV2 | Scan and load | PASS | `LV2i: mw12F (mwAudio) (2 out)`, 149 parameters (147 plus freewheel and enabled) |
| 4 | REAPER 7.79 | VST3 | Factory default plays from its own sequencer with the host transport rolling (`seq_run` 0) | PASS | 4 bars at 125 BPM: peak -12.16 dBFS, RMS -24.03 dBFS, no NaN or Inf |
| 5 | REAPER 7.79 | CLAP | Same | PASS (sound) | peak -12.11 dBFS, RMS -24.23 dBFS; see row 10 for the dropped notes |
| 6 | REAPER 7.79 | LV2 | Same | PASS (sound) | peak -12.16 dBFS, RMS -24.09 dBFS |
| 7 | REAPER 7.79 | VST3 | Offline render equals a second offline render, bit for bit | PASS | sample data identical in every pair: factory default, modified state, host-modified state, tempo step, tempo ramp; 5 of 5 renders identical with no device, 5 of 5 with the Dummy device (the bext timestamp chunk differs, the data does not) |
| 8 | REAPER 7.79 | VST3 | Offline render equals the engine reference | PASS | factory default and modified state: equal to `acid-render --quality high` with 47 samples removed, max error 0.0 |
| 9 | REAPER 7.79 | CLAP | Offline render equals a second offline render | FAIL | factory default, no device: r1 and r2 differ from frame 10195 (3451 frames differ), the third step's note (A2, frame 11473 in VST3) is missing from MIDI out; modified state: r1 loses notes E2 and F2 (VST3 frames 22993 and 28753). Equal realtime and offline tiers: 4 of 5 identical, the fifth has a 15-block silent gap from frame 14291 and loses the note at frame 17235. Dummy device on, equal tiers: 3 distinct outputs in 5 renders, MIDI identical |
| 10 | REAPER 7.79 | CLAP | Offline render equals the engine reference | FAIL with default settings, PASS with equal tiers | default (offline High, realtime Normal): starts at latency 45, ends at 47, best-lag error 0.31; with `offline_quality="normal"`: equal to `acid-render --quality normal` minus 45 samples, max error 0.0 |
| 11 | REAPER 7.79 | LV2 | Offline render equals a second offline render | FAIL | factory default, no device: r1 and r2 differ from frame 1491; modified state: the pattern restarts at frame 12288 (C2 retriggered). Equal tiers, no device: 5 of 5 identical. Equal tiers, Dummy device on: 5 different outputs in 5 renders, first note at frames 2182, 1828, 3165 (the pattern starts mid-way) |
| 12 | REAPER 7.79 | LV2 | Offline render equals the engine reference | FAIL with default settings, PASS with equal tiers and no device | equal tiers: max error 0.0 against `acid-render --quality normal` minus 45 samples |
| 13 | REAPER 7.79 | all | State saved in the project restores patterns and parameters | PASS | injected state renders the 12-step ladder from slot A3 (MIDI out 36, 37, 38, ... with velocity 96 on every third step) in all three formats; ReaScript reads back cutoff 60, resonance 20, decay 70; VST3 audio equals the reference exactly |
| 14 | REAPER 7.79 | all | Load, save, load, save keeps the state | PASS | first re-save fills the generator defaults into the slot A3 line (canonical form); second re-save is byte-identical; all three formats produce the same XML (md5 `804eec31f4cbed70815d5ad561260e96`) |
| 15 | REAPER 7.79 | VST3 | State from a modified instance (parameters moved by the host, project saved by REAPER) | PASS | saved XML: cutoff `60.000004`, resonance 20, decay 70, pattern_slot 3; render equals the reference within 4.5e-8 |
| 16 | REAPER 7.79 | CLAP, LV2 | Same, with the audio device running | PASS (state), FAIL (render) | saved XML identical to VST3's; renders non-reproducible as in rows 9 and 11 |
| 17 | REAPER 7.79 | CLAP, LV2 | Same, with no audio device | Host quirk | CLAP: values set through ReaScript are never applied (read back 18 after a 2 s wait) until audio runs. LV2: REAPER shows natural value 0 for every untouched parameter (cutoff 0 instead of 18) until the plugin has run once |
| 18 | REAPER 7.79 | VST3, CLAP | Tempo change 125 to 150 BPM at bar 3 moves the sequencer | PASS | all 64 steps land within 1 sample of REAPER's own beat-to-time map (constant -45 sample offset, the compensated latency) |
| 19 | REAPER 7.79 | LV2 | Same | PASS | all 64 steps exactly on the grid (no device) |
| 20 | REAPER 7.79 | VST3, CLAP | Linear tempo ramp 125 to 170 BPM over 4 bars | PASS | 96 steps, offsets -45 or -44 samples throughout |
| 21 | REAPER 7.79 | LV2 | Same | FAIL | the sequencer falls behind during the ramp (2, 3, 6, ... 68 samples) and stays 68 to 69 samples late after it; it never re-locks |
| 22 | REAPER 7.79 | VST3, CLAP | Playback starting mid-bar (render from beat 5.5) starts at the right step | PASS | first note A#2 (46), the ladder's step 11 |
| 23 | REAPER 7.79 | LV2 | Same | FAIL | first note C2 (36): the pattern starts at step 1 wherever playback starts |
| 24 | REAPER 7.79 | all | MIDI out routed to a second track | PASS | MIDI-only send into the encoder JSFX: VST3 events match the engine's MIDI file 112 of 112 (kind, note, velocity) within 30 samples; slides overlap by 1 sample |
| 25 | REAPER 7.79 | VST3, CLAP | MIDI out aligned with the instrument's own audio | FAIL (minor) | MIDI out arrives 45 or 47 samples (the reported latency) before the compensated audio: note-ons at 5713, 11473, ... against the grid's 5760, 11520 |
| 26 | REAPER 7.79 | LV2 | Same | PASS | MIDI out on the grid (5760, 11520, ...) |
| 27 | REAPER 7.79 | VST3 | Cutoff automation lane | PASS | envelope created at the current value 0.6; render equals the reference within 4.5e-8 |
| 28 | REAPER 7.79 | CLAP | Cutoff automation lane | PASS (lane), FAIL (render) | lane created at 0.6; 4 renders give 3 different outputs, MIDI identical, RMS within 0.01 dB |
| 29 | REAPER 7.79 | LV2 | Cutoff automation lane | Host limitation | `GetFXEnvelope` returns nil for LV2 patch parameters |
| 30 | REAPER 7.79 | all | Realtime playback, offline equals realtime | Not tested | the Dummy device advanced 1024 frames in 9 s under xvfb; JACK or PulseAudio would have joined the user's live audio graph |
| 31 | Carla 2.5.8 discovery | LV2 | Discovery | PASS | `category::synth`, `audio.outs::2`, `midi.ins::1`, `midi.outs::1`, `parameters.ins::1` (control ports only) |
| 32 | Carla 2.5.8 discovery | VST3 | Discovery | PASS | `category::synth`, `audio.outs::2`, `midi.ins::1`, `midi.outs::1`, `parameters.ins::2226` |
| 33 | Carla 2.5.8 discovery | CLAP | Discovery | Not supported | `getPluginTypeFromString("clap") - invalid string type` |
| 34 | pluginval 1.0.4 | VST3 | Strictness 10, in process, xvfb | FAIL (intermittent) | Single pass SUCCESS in 17 s on the matrix build. Release verification build: crashed (SIGSEGV) in Editor Automation in both `validate.sh --release` runs (25 s), in 1 of 3 single passes (seeds 21 to 23) and 0 of 6 churn runs (seeds 11 to 16); seed 15 instead failed Plugin state restoration ("Waveform not restored", expected 0, got 0.19). The fault is in the host's `XEmbedComponent::Pimpl::handleX11Event` callAsync lambda, which captures `this` unguarded, so a ConfigureNotify from the editor window that arrives after the editor is closed runs on a deleted component |
| 35 | pluginval 1.0.4 | LV2 | Strictness 10, in process, xvfb | PASS single pass, FAIL with churn | Single pass, `--repeat 3` alone and `--randomise` alone: SUCCESS. `--repeat 3 --randomise` (validate.sh `--release`): Plugin state restoration fails in every run (278 to 367 "not restored" lines, nearly every parameter, all three repeats; 40 s) |
| 36 | clap-validator 0.4.1 | CLAP | Full suite | PASS | 44 run, 33 passed, 0 failed, 1 warning, 10 skipped |
| 37 | Renoise 3.5.4 | VST3 | Any | Not tested | GUI only, no command-line render |

## Release verification, 2026-10-03

The REAPER rows above were not re-run. The validator rows (31 to 36) were re-run against fresh Release artefacts in `build-plugin`, built after two source changes made during verification (a `using` declaration in `PluginProcessor.h` and the Console loop solver's bracket test in `DistortionClippers.h`):

| Artefact | md5 |
|---|---|
| `CLAP/mw12F.clap` | `ce5c7f044eb94d2d220f453c74e4a89c` |
| `VST3/mw12F.vst3/Contents/x86_64-linux/mw12F.so` | `6f983666ef7dc34d6f84b88891f176b8` |
| `LV2/mw12F.lv2/libmw12F.so` | `22505c89abf42d4169298df400d9d510` |

`scripts/validate.sh build-plugin --release`, two runs (57 s and 70 s wall; second run per step: pluginval VST3 25.3 s, pluginval LV2 39.6 s, clap-validator 0.4 s, lv2_validate 0.1 s, Carla LV2 and VST3 0.2 s together, branding audit 3.9 s). Both runs: pluginval VST3 FAIL (row 34), pluginval LV2 FAIL (row 35), clap-validator PASS (44 run, 33 passed, 0 failed, 1 warning, 10 skipped, as row 36), lv2_validate PASS against the baseline, Carla LV2 and VST3 PASS (same ports and parameter counts as rows 31 and 32), branding audit PASS.

## What the failures come down to

Rows 9 to 12 and 16: CLAP's render extension and LV2's freewheel port tell the processor it is offline only after it has been prepared. The processor then re-prepares from its 30 Hz message timer while the host keeps rendering, which silences blocks, restarts the sequencer and moves the latency from 45 to 47 samples in the middle of the file. Setting `offline_quality` equal to `realtime_quality` makes both formats bit-exact against the engine (rows 10 and 12), which isolates the cause. `tests/plugin/review_qa_HostRenderTests.cpp` reproduces it without a host.

Row 9's remaining 1-in-5 failure with equal tiers: the instance reports latency 0 until its first prepare and 45 after it, so clap-juce-extensions asks REAPER for a restart, and REAPER sometimes serves it during the render.

Rows 9, 11 and 28 with the Dummy device on: the voice runs while REAPER idles before a render, and nothing resets it when the render starts, so each render begins from a different oscillator state. VST3 is re-prepared by REAPER before its render and stays reproducible, though even it differs from the no-device render by up to 4.7e-6 over the first 7721 frames.

Rows 21 and 23: JUCE 8.0.15's LV2 wrapper never passes `time:beat` on as a ppq position (it queries the atom and drops it). The plugin sees "playing" and a tempo but no position, so it counts samples from wherever playback started and never relocks.

## Re-test after the host-integration fixes, 2026-10-03

Same harness, run from a copy (`scratchpad/reaper-fix/`, its own `-cfgfile` and `HOME`), against these Release artefacts:

| Artefact | md5 |
|---|---|
| `CLAP/mw12F.clap` | `025a30428cd11b5b269df492c9faf512` |
| `VST3/mw12F.vst3/Contents/x86_64-linux/mw12F.so` | `57d892389b4243dcdc3aa873060c22cf` |
| `LV2/mw12F.lv2/libmw12F.so` | `582df8b107d0e77aa6f2c2d1830bda7a` |

What changed in the processor: the quality tier is read in `prepareToPlay` only (an offline flag or a settings change that arrives later waits for the next prepare, and the processor asks the host for one only while stopped and not rendering); the engine is reset, not re-prepared, at the block where the offline flag turns on; the constructor reports the latency the first prepare gives, and under CLAP the latency never drops below the 44.1 kHz figure (68 samples at Normal, the audio is delayed by the difference); a missing song position is rebuilt from the frame, tempo and bar the LV2 wrapper does pass; VST3 and CLAP delay their MIDI out by the reported latency; host-set values are stored at six significant digits.

| # | Host | Format | Test | Result | Evidence |
|---|---|---|---|---|---|
| R1 | REAPER 7.79 | CLAP | Factory default, default tiers, no device: render equals itself | PASS | 5 of 5 renders identical (audio and MIDI); replaces rows 9 and 10 |
| R2 | REAPER 7.79 | LV2 | Same | PASS | 5 of 5 identical, and identical to CLAP (both render at the realtime tier the instance was prepared with); replaces rows 11 and 12 |
| R3 | REAPER 7.79 | VST3 | Same | PASS | 5 of 5 identical (REAPER prepares VST3 offline, so it renders at the offline tier) |
| R4 | REAPER 7.79 | all | Modified state (12-step ladder, six parameters) | PASS | 2 of 2 identical per format; CLAP and LV2 identical to each other; MIDI identical in all three |
| R5 | REAPER 7.79 | all | Dummy device running, factory default | PASS | 3 of 3 identical per format (CLAP and LV2 identical to each other); replaces rows 16 and 28. They still differ from the no-device render (the engine reset at the start of a render is not a full return to the prepared state: core finding 46) |
| R6 | REAPER 7.79 | LV2 | Render from beat 5.5 starts at the right step | PASS | first note A#2 (46), as VST3 and CLAP; replaces row 23 |
| R7 | REAPER 7.79 | LV2 | Linear tempo ramp 125 to 170 BPM over 4 bars | PASS | 96 of 96 steps within 1 sample of VST3; replaces row 21 |
| R8 | REAPER 7.79 | LV2 | Tempo step 125 to 150 at bar 3 | PASS | 64 of 64 steps on the same samples as CLAP |
| R9 | REAPER 7.79 | VST3, CLAP | MIDI out aligned with the compensated audio | PASS | note-ons at 0, 5760, 11520, 17280 in both (the grid); replaces row 25 |
| R10 | REAPER 7.79 | VST3, LV2 | Host-set cutoff 0.6, resonance 0.2, decay 0.7, project saved by REAPER | PASS | saved XML reads `v="60"`, `v="20"`, `v="70"` (was `60.000004`); replaces row 15. CLAP still shows row 17's host quirk (values set through ReaScript without audio running are not applied) |

Validators against the same artefacts, `scripts/validate.sh build-plugin --release`, three runs: pluginval LV2 PASS in all three, clap-validator, lv2_validate, Carla and the branding audit PASS in all three, pluginval VST3 PASS in one and FAIL in two (a SIGSEGV, below).

- pluginval VST3, Plugin state restoration: fixed in the plugin. A host-set raw value could outlive a write the host had been told about, so after a restore `getValue` reported an old poke instead of what the host held (seeds 0xf and 0x11). 12 of 12 randomised `--repeat 3` runs pass it now (seeds 0xf, 0x11, 0x21 to 0x29, 0x2008542); `tests/plugin/HostIntegrationTests.cpp` models the VST3 wrapper's value cache.
- pluginval LV2, Plugin state restoration in a random order: not a plugin defect. pluginval reads a value, pokes another, restores the state and reads again without running the plugin, and JUCE's LV2 host only learns parameter values from the plugin's `run()` output. A stock JUCE 8.0.15 plugin with four APVTS parameters and nothing else (`scratchpad/spike`) fails the same check on the same seeds (0x373650a, 0x1). `validate.sh --release` now runs LV2 twice: the fixed order with `--repeat 3` (which includes the check, and passes) and the randomised churn with that one test listed in `tests/plugin/pluginval-lv2-disabled-tests.txt`.
- pluginval VST3, SIGSEGV: about 1 in 18 randomised runs, always during or right after Editor Automation. The fault is in pluginval's own `juce::XEmbedComponent::Pimpl::handleX11Event` lambda (`MessageManager::callAsync ([this] { ... })` on a ConfigureNotify, no frame from the plugin), which runs after the host has deleted the editor's embedding component. The plugin makes it likely: the editor changes its window height whenever `play_mode` enters or leaves External, and Editor Automation sets random parameter values, so the editor window is resized many times a second while pluginval opens and closes it. The editor should not resize its window in response to host automation of `play_mode` (or should defer and coalesce the resize). Until it does, the release gate fails intermittently on VST3.

LV2 hosts have no way for a plugin to ask for a new prepare, so a quality-tier change in an LV2 host applies the next time the host activates the plugin. VST3 and CLAP hosts are asked for a restart while stopped; the Standalone re-prepares itself while stopped.

## Final release verification, 2026-10-03

Run after all stage-5 fixes, against Release artefacts from a `build-plugin` deleted and configured again (md5 checked before and after the runs):

| Artefact | md5 |
|---|---|
| `CLAP/mw12F.clap` | `aa034b99b42b724425f28f3054cf46a5` |
| `VST3/mw12F.vst3/Contents/x86_64-linux/mw12F.so` | `06a6a88038baf08290b697fc2774e5a7` |
| `LV2/mw12F.lv2/libmw12F.so` | `f0611fa1642539238552b104852ef178` |

The same harness ran from its own copy (`scratchpad/reaper-final/`, its own `-cfgfile` and `HOME`, every project path pointed at the copy) using `final.sh`, which renders the same projects as the re-test above. Engine references were rendered again with the current `acid-render`, because the stage-5 core changes moved the engine's output (by up to 7.2e-6 against the QA-time reference).

| # | Format | Test | Result | Evidence |
|---|---|---|---|---|
| F1 | all | Factory default, default settings, no device: render equals itself | PASS | 5 of 5 identical in each format (audio and MIDI). CLAP and LV2 identical to each other (data hash `71fb2c1bedee267b`); VST3 `13240da2b2a49dd5` (rendered at the offline tier). Confirms R1 to R3 |
| F2 | all | Modified state (12-step ladder, six parameters) | PASS | 2 of 2 identical per format, CLAP and LV2 identical to each other, MIDI identical. Confirms R4 |
| F3 | VST3 | Dummy device running | PASS | 3 of 3 identical and now identical to the no-device render as well (it differed by up to 4.7e-6 before core finding 46 was fixed) |
| F4 | CLAP, LV2 | Dummy device running | PARTIAL | LV2 3 of 3 identical to each other, CLAP 2 of 3 identical to those. The third CLAP render equals the no-device render instead. The two outcomes differ from frame 216 (the first sound) by at most 5.2e-7 (-126 dBFS), then by 3e-8 or less. So R5's "3 of 3 per format" does not hold for CLAP every time: the reset at the start of an offline render does not return CLAP and LV2 exactly to a fresh instance's state. The core test (tests/engine/OfflineResetTests.cpp) passes, so the difference is in the processor layer. Not root-caused |
| F5 | all | Render from beat 5.5 starts at the right step | PASS | first note A#2 (46) in all three, 32 notes, same samples as VST3. Confirms R6 and row 22 |
| F6 | all | Linear tempo ramp 125 to 170 BPM | PASS | 96 of 96 notes; CLAP on the same samples as VST3, LV2 within 1 sample. Confirms R7 |
| F7 | CLAP, LV2 | Tempo step 125 to 150 at bar 3 | PASS | 64 of 64 notes on the same samples. Confirms R8 |
| F8 | VST3, CLAP | MIDI out aligned with the compensated audio | PASS | note-ons at 0, 5760, 11520, 17280 in both. Confirms R9 |
| F9 | all | Host-set cutoff 0.6, resonance 0.2, decay 0.7, pattern 3, set on a live instance with the Dummy device, 2 s wait, saved by REAPER | PASS | saved XML reads `v="60"`, `v="20"`, `v="70"` in all three formats, and `pattern_slot` 3 in VST3 and CLAP. CLAP now passes too once audio runs (row 17's quirk is about values set without audio running). `final.sh`'s own build step saves without waiting; for LV2 that saved the defaults (18, 84, 46), because JUCE's LV2 wrapper only learns a value in `run()`. That is harness timing, not a plugin fault |
| F10 | all | Offline render equals the engine reference | FAIL (minor) | not bit-exact any more. CLAP and LV2 against `acid-render --quality normal` minus 45 samples: max error 1.3e-7, 5.2e-7 in the first 3000 frames of the no-device render; VST3 against `--quality high` minus 47: max error 4.5e-8. About 84 % of frames differ, all at float-rounding level (-125 dB relative). The engine is bit-identical with itself at block sizes 64 to 2048, so the difference comes from how parameter values reach the engine in the plugin, not from block size. Rows 8, 10 and 12 recorded 0.0. Not root-caused |

Validators, `scripts/validate.sh build-plugin --release`, three runs (124 s, 109 s, 128 s wall): pluginval LV2 (fixed order with `--repeat 3`, then the randomised pass with one test disabled), clap-validator (44 run, 33 passed, 0 failed, 1 warning, 10 skipped), lv2_validate, both Carla loads and the branding audit pass in all three. pluginval VST3 crashes in runs 1 and 2 (seeds 0x475952f and 0x79a8631, both in Editor Automation) and passes in run 3 (seed 0x51e5e1e).

The VST3 crash, measured on the shipping binary: 10 SIGSEGVs in 44 randomised `--repeat 3` runs, mostly in or right after Editor Automation. The same seed does not reproduce it. Under gdb the faulting frame is `juce::XEmbedComponent::Pimpl::handleX11Event(...)::{lambda()#1}`, in pluginval's binary, called from the message loop, with no plugin frame. To find what triggers it, a throwaway build of the same source with the editor's play-mode window resize disabled (`plate.onDesignSizeChanged` made a no-op) was run 20 times alongside 20 runs of the shipping binary, interleaved under the same load: 0 crashes against 4. A stock JUCE 8.0.15 plugin with a fixed-size editor crashed 0 times in 16. So the release gate failed because the editor resized its window whenever automation moved `play_mode` into or out of External.

Fixed in the editor (SPEC section 9): the window size never follows a parameter. External play mode now swaps the sequencer screen, in place and at the same size, for an external-MIDI panel (a monitor of gate, accent, envelope, C13 charge and MIDI input overload from `EngineStatus`, and the accent threshold, bend range, note priority and MIDI thru settings); the collapse and every design-size switch are gone. On that build: pluginval VST3, strictness 10, `--repeat 3 --randomise`, 15 sequential runs with random seeds (45 passes of Editor Automation): 15 passed, 0 crashes. Then `scripts/validate.sh build-plugin --release` three times in a row: every check passed in all three (125 s wall each; VST3 seeds 0x57d2088, 0x41b1e36, 0x189c34b; LV2 randomised seeds 0x6aa516, 0x70a5dcc, 0x300ef2).

One more intermittent pluginval VST3 failure in those 44 runs: Plugin state restoration, "Waveform not restored on setStateInformation", expected 0, got 0.984 (seed 0x7038271). Three re-runs with that seed passed, so it depends on timing. The plugin-side state-restoration fix above covers the deterministic seeds; this one is still open.
