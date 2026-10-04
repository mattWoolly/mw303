# 07 — Engineering a stable, Linux-native instrument plugin with JUCE 8

Status: research deliverable, 2026-10-02. Audience: the architect and the DSP, sequencer and GUI implementers.
Scope: formats, host transport, real-time safety, state and presets, GUI on Linux, validation and CI, build and distribution, and this author's house conventions. It does not cover DSP modelling or sequencer musical behaviour, which other docs in this folder own.

## How to read this doc

Confidence tags: **[primary]** = source code, spec, vendor doc, or a measurement I ran on this machine. **[secondary]** = reputable analysis or a maintainer's forum post. **[lore]** = forum consensus or an unconfirmed single report.

**[measured]** marks a result I produced on this machine (Ubuntu 24.04, gcc 13.3, cmake 3.28, kernel 7.0.0-34, X11 at `:0`) between 21:10 and 22:40 on 2026-10-02. Measured results count as [primary].

To check claims, I built a throwaway JUCE instrument ("spike") in the scratchpad:

- `IS_SYNTH`, MIDI in and MIDI out, VST3 + CLAP + LV2 + Standalone.
- An APVTS holding a float, a choice, a bool and an int parameter.
- An internal 16th-note sequencer that writes notes to MIDI out.
- A resizable editor with a 30 Hz LED strip.

I built it twice:

- JUCE 8.0.12 (`~/projects/mw140/libs/JUCE`) + clap-juce-extensions `e1f6789`.
- JUCE 8.0.15 + clap-juce-extensions `55525c9` (upstream main, 2026-09-12).

I ran every validator below against both builds, and against the CLAP, VST3 and LV2 binaries of the sibling projects. The spike source is at `/tmp/claude-1000/-home-mwoolly-projects-mw303/dad63690-756f-4b60-b4c2-2627c3ad5533/scratchpad/spike/` (`CMakeLists.txt`, `src/Processor.cpp`). It is reference material only. Nothing from it was written into the project.

I could not reach these sources:

- `lv2lint` is not packaged for Ubuntu 24.04 (`apt-cache show lv2lint` → "No packages found"). I did not build it from source.
- Bitwig, Ardour, Qtractor and Carla-as-a-host are not installed. Only REAPER 7.79 and Renoise 3.5.4 are. Host-behaviour claims about the missing hosts therefore rely on reading host and wrapper source, or on forum reports, and are tagged that way.

---

## 0. Decisions this doc forces (summary)

1. **Pin JUCE 8.0.15 and clap-juce-extensions `55525c9`.** The current scaffold pins JUCE 8.0.13 + CJE `51a9359`. Both combinations work; the newer pair builds clean with gcc 13 and passes every validator [measured]. Do not move to JUCE 9 in v1 (§8.6).
2. **Call `updateHostDisplay (ChangeDetails().withProgramChanged (true))` at the end of `setStateInformation`.** Without it, clap-validator 0.4.1 fails three state tests on *every* JUCE + CJE plugin, including all of this author's existing plugins. With it, the spike passes 33/33 [measured] (§5.3).
3. **Never trust the playhead to be present each block.** JUCE's LV2 wrapper clears the playhead at the start of every `run()` and only fills it when the host sent a `time:Position` atom *in that cycle*. Ardour sends one only when something changes [primary, source read]. So the sequencer needs a `PlayheadTracker` that extrapolates position (§3).
4. **Leave `CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES` at 0.** If the wrapper splits blocks, every sub-block sees the playhead of the *parent* block's start, and a sequencer reading ppq would double-fire or drift [primary, source read] (§2.3).
5. **Lock-free hand-off of patterns uses a triple buffer, not a seqlock and not a mutex.** The model is owned by the message thread. A `std::mutex` that only non-real-time threads take guards it, because hosts call `getStateInformation` from background threads (§4.3, §5.2).
6. **The UI needs a user zoom setting.** JUCE forces the display scale to 1.0 for plugins on Linux and relies entirely on the host to send a scale [primary, source read] (§6.1).
7. **MIDI-file drag-out is worth building but needs a fallback.** It works through XDND, but host acceptance on Linux is uneven. Ship an "Export .mid" button next to the drag handle (§6.7).

---

## 1. House conventions (from sibling projects)

### 1.1 What each sibling does

| Item | mw140 | ssl4k-2 (mw4000) | mwStime | mw303VinylSim | Source |
|---|---|---|---|---|---|
| JUCE | git submodule `libs/JUCE`, tag **8.0.12** (`29396c2`, 2025-12-16) | git submodule, **7.0.12** | FetchContent, tag **8.0.13**, `GIT_SHALLOW` | git submodule (version not checked) | `.gitmodules`, `cmake/FetchJUCE.cmake` [primary] |
| clap-juce-extensions | submodule `e1f6789` (0.26.0-106, 2026-03-09); CLAP 1.2.7 | submodule, branch main | FetchContent **commit `51a9359`** (upstream main 2026-06-01; the 0.26.0 *tag* does not build against JUCE 8) | none | `cmake/FetchClapExtensions.cmake` comment [primary] |
| Company / manufacturer code | `mwAudio` / `mwAu` | `mwAudio` / `mwAu` | `mattWoolly` / `MwSt` (outlier) | `mw303` / `Mw3x` (outlier) | CMakeLists [primary] |
| Bundle / CLAP id | `com.mwaudio.mw140` | `com.mwaudio.mw4000` | `com.mattwoolly.mwstime` | `com.mw303.VinylSim` | CMakeLists [primary] |
| C++ standard | 20 | 20 | 20 | 17 | CMakeLists |
| Flags | `juce_recommended_{config,lto,warning}_flags`, `JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0` | same | same + `-ffp-contract=off` on the DSP core, never `-ffast-math` | same | CMakeLists |
| Tests | Catch2 v3.5.2, many executables, `--wrap=malloc` alloc spy | Catch2 v3 | Catch2 **v3.7.1**, JUCE-free core + one JUCE console-app test target, `catch_discover_tests` | Catch2 v3.5.2 | tests/CMakeLists.txt |
| Validators | pluginval strict 5 on PRs, strict 10 at release | pluginval in CI with `continue-on-error` (QA flagged it as a gap) | ctest label `validators`, exit 77 = skip; pluginval v1.0.4 strict 10 `--repeat 3 --randomise`; clap-validator 0.3.2; LV2 = lv2lint + lv2_validate + Carla | `scripts/run_pluginval.sh`, strict 5 | `tests/plugin/run_*.sh` [primary] |
| Presets dir | `~/.config/mwAudio/<product>/Presets/` (style guide) | same | — | — | `ssl4k-2/docs/MW_AUDIO_STYLE_GUIDE.md` [primary] |
| Packaging | `.deb` + AppImage (appimagetool 1.9.1, SHA pinned) | `.deb` + `.rpm` (`/usr/lib/vst3`, `/usr/lib/clap`) | — | — | `scripts/packaging/linux/` [primary] |

The house style guide (`ssl4k-2/docs/MW_AUDIO_STYLE_GUIDE.md`) is the closest thing to a standard [primary]:

- `COMPANY_NAME "mwAudio"`, `PLUGIN_MANUFACTURER_CODE "mwAu"` (shared by every plugin), `BUNDLE_ID` and `CLAP_ID` both `com.mwaudio.<productName>`.
- Plugin code is `mw` + 2 characters. The current scaffold uses `m12F`, which breaks that pattern. It is still unique among the line's codes `mw14 mw40 mw16 mwVn MwS1 Vs03`; uniqueness is what matters, because the VST3 class ID derives from the manufacturer and plugin codes (spike CID `ABCDEF01 9182FAEB` + `6D774175` "mwAu" + `6D77536B` "mwSk") [measured].
- Class names `<productName>Processor` / `Editor` / `LookAndFeel`. Parameter IDs `snake_case` in one `ParameterIDs.h`, with `ParameterID{id, 1}` version hints.
- **No original-manufacturer brand names in code, comments or docs.** The guide's examples are SSL and Boss. By the same rule this product must not say "Roland" or "TB-303" in code, parameter names, presets or UI. Use "classic bass line synth". mw140 enforces this with `scripts/branding-audit.sh` + `forbidden_tokens.txt`, a word-boundary grep over tracked files and `strings` over built binaries [primary].
- Licence: the guide says "GPL v3 (JUCE GPL tier)". **That is out of date for JUCE 8**, whose open-source option is **AGPLv3** (`libs/JUCE/LICENSE.md`) [primary]. mwStime already uses `AGPL-3.0-or-later` SPDX headers.

### 1.2 Lessons the siblings already paid for (do not relearn these)

1. **`prepareToPlay` can run concurrently with `processBlock`.** mwStime backlog 059: a pluginval strictness-10 segfault ("Parameter thread safety", seed `0x26d1e76`, AU only) traced to `FxEngine::prepare()` writing non-atomic members that the audio thread was reading. TSan confirmed it. The code had a comment claiming "the audio thread is stopped" during prepare, and that was false. The fix: route prepare through the same publish/adopt handoff as runtime reconfiguration, and output silence while not fully prepared [primary, `mwStime/plan/backlog/059-fix-fxengine-prepare-race.md`].
2. **The pluginval "Plugin state restoration" flake with automatable `AudioParameterBool`.** `getValue()` returns the raw, un-snapped value pluginval poked in. mwStime disabled that one sub-test through `--disabled-tests <ABSOLUTE path>` (a relative path is parsed as a comma list, not a file) and tracks it as backlog 048c [primary]. The spike's bool did *not* trip this at strictness 10 [measured], so treat it as plugin-specific. Prefer `AudioParameterChoice` with 2 entries for automatable toggles if it recurs.
3. **`juce::HeapBlock` calls `std::malloc` directly, so an `operator new` spy misses `AudioBuffer::setSize`.** mw140 links its realtime test with `-Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc` and has `operator new` call `__real_malloc` to avoid double counting [primary, `mw140/tests/CMakeLists.txt`, `tests/realtime/AllocatorSpyTest.cpp`]. ASan did not flag the per-block allocation it found, because an allocation is a real-time violation, not a memory-safety bug [primary, `mw140/docs/MW140-040_threading_audit.md` §4].
4. **`getStateInformation` / `setStateInformation` get `JUCE_ASSERT_MESSAGE_THREAD`** in mw140. The audit notes that a hit in "Reaper's background project-save path" must be investigated, not silenced [primary]. pluginval itself runs a **"Background thread state"** test [measured: it appears in every run]. So state code must be safe off the message thread (§5.2). The assertion approach is too strict for this product.
5. **Pre-encode heavy state blobs off the message thread** so `getStateInformation` only copies memory. mwStime does this for its FLAC blob because Logic autosaves aggressively [primary, host-matrix.md]. Our state is small (patterns), so this does not apply directly. The principle stands: `getStateInformation` must be O(size) and allocation-bounded.
6. **Validator scripts self-skip with exit 77** (`SKIP_RETURN_CODE 77`). They pin URL + SHA-256, fail hard on a checksum mismatch, wrap in `xvfb-run -a` when `$DISPLAY` is unset, and carry the ctest label `validators` [primary, mwStime `tests/plugin/`].
   - The mwStime Linux SHA-256 values were never filled in (they still read `UNVERIFIED_SET_ON_LINUX`). The real digests are in §7.1.
   - mwStime `BUILDING.md` says `lilv-utils` provides `lv2_validate`. **It is `lv2-dev`** [measured: `dpkg -c`].
   - mwStime's LV2 bundle **fails `lv2_validate`** with 34 errors, all generated by JUCE (§7.4). As written, that script would go red as soon as someone installed `lv2-dev`.
7. **pluginval loads LV2 since v1.0.4** (CHANGELIST "Added LV2 support [#25]") [primary]. mwStime's ADR-002 and scripts say "pluginval CANNOT load LV2", which is outdated. I validated the spike's `.lv2` bundle with pluginval at strictness 10 [measured].
8. **Never launch a GUI DAW from an automated runner.** mwStime's REAPER smoke test only probes for the binary [primary, `run_reaper_smoke.sh`]. §7.5 has a headless REAPER approach to try instead.
9. **Editor cost shows up in pluginval.** mwStime's VST3 timed out in pluginval's **"Editor Automation"** test (no output for 120 s) at strictness 10 under `xvfb-run` [measured; exit 1 after 2 m 12 s]. The minimal spike passed the same test in about 1 s. An editor that repaints heavily on every parameter change fails CI under the software renderer. See §6.3.
10. **Oversized blocks.** mw140 had a heap overrun when the host passed a block larger than the `prepareToPlay` hint into fixed oversampling buffers. The fix was chunk-splitting [primary, `mw140/tests/CMakeLists.txt` MW140-070b]. Hosts *do* exceed the hint (the JUCE LV2 wrapper asserts on it; it does not prevent it).

---

## 2. Formats on Linux for an instrument with a built-in sequencer

### 2.1 `juce_add_plugin` settings

This is the recommended block. It mirrors and corrects the current scaffold `plugin/CMakeLists.txt`.

```cmake
juce_add_plugin(mwacid_plugin
    PRODUCT_NAME                "${MW_PRODUCT_NAME}"      # e.g. mw12F
    VERSION                     ${PROJECT_VERSION}
    COMPANY_NAME                "mwAudio"
    COMPANY_WEBSITE             "https://github.com/mattWoolly/${MW_PRODUCT_NAME}"  # else LV2 doap has empty <> homepage/mbox
    BUNDLE_ID                   "com.mwaudio.${MW_PRODUCT_ID}"
    PLUGIN_MANUFACTURER_CODE    mwAu                       # NEVER change after release
    PLUGIN_CODE                 ${MW_PLUGIN_CODE}          # NEVER change after release (VST3 CID)
    DESCRIPTION                 "Classic bass line synthesizer"   # no brand names
    FORMATS                     VST3 LV2 Standalone        # CLAP added below
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE                       # live play, transpose-by-key, MIDI-triggered patterns
    NEEDS_MIDI_OUTPUT           TRUE                       # sequencer -> host MIDI out
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE                      # §6.5
    VST3_CATEGORIES             Instrument Synth
    LV2URI                      "https://github.com/mattWoolly/${MW_PRODUCT_NAME}"  # NEVER change after release
    LV2_SHARED_LIBRARY_NAME     ${MW_PRODUCT_NAME}
    COPY_PLUGIN_AFTER_BUILD     FALSE)                     # install via a script/target (§8.4)

target_compile_definitions(mwacid_plugin PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0       # forces async file choosers/alerts — the only safe kind in plugins
    JUCE_JACK=1                        # Standalone: JACK backend is off by default (§2.4)
    JUCE_JACK_CLIENT_NAME="${MW_PRODUCT_NAME}")
    # Do NOT define JUCE_DISPLAY_SPLASH_SCREEN — JUCE 8 ignores it and emits a #pragma warning [measured].

clap_juce_extensions_plugin(TARGET mwacid_plugin
    CLAP_ID        "com.mwaudio.${MW_PRODUCT_ID}"
    CLAP_FEATURES  instrument synthesizer stereo          # "mono"/"stereo" describe the AUDIO config; FX rack is stereo
    CLAP_SUPPORT_URL "https://github.com/mattWoolly/${MW_PRODUCT_NAME}/issues"
    CLAP_MANUAL_URL  "https://github.com/mattWoolly/${MW_PRODUCT_NAME}")
    # Leave CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES at its default 0 (§2.3).
    # Leave CLAP_USE_JUCE_PARAMETER_RANGES at its default OFF unless a host-side need appears.
```

Sources: `JUCEUtils.cmake` properties [primary]; CJE README "Arguments to CLAP CMake functions" [primary]; CLAP feature strings in `clap/plugin-features.h` (`instrument`, `synthesizer`, `mono`, `stereo`; there is no "bass" feature) [primary]. CLAP features in the scaffold are `instrument synthesizer mono`. Keep `mono` only if the output bus really is mono.

Identity facts [measured on the spike]:

- **VST3**: `moduleinfo.json` is generated at build time (`VST3_AUTO_MANIFEST` defaults to TRUE) and shows `"SDKVersion": "VST 3.8.0"` and `Sub Categories: Instrument, Synth`.
- **LV2**: the class is `lv2:InstrumentPlugin`; the output atom port declares `midi:MidiEvent`.
- Carla's scanner reports `midi.ins 1 / midi.outs 1` for both LV2 and VST3.

Two build-time consequences:

- **The build runs the plugin.** `juce_lv2_helper` `dlopen`s the built `.so` and calls its recall entry point to write the TTL [primary, `juce_LV2ManifestHelper.cpp`]. The VST3 manifest helper also loads the module. So the processor constructor must be fast, need no display, and touch no audio device or user files.
- Factory presets that JUCE exposes as programs get **baked into `manifest.ttl` at build time** as `pset:Preset` entries (§5.4).

### 2.2 Per-format behaviour that matters for this product

| Concern | VST3 (JUCE 8) | CLAP (via CJE) | LV2 (JUCE 8) | Standalone |
|---|---|---|---|---|
| Parameter identity | 32-bit ID = hash of the JUCE string ID (`convertJuceParameterId`, Studio-One-compatible 31-bit mask by default) [primary, `juce_audio_plugin_client_VST3.cpp` L741] | same JUCE hash ("just like the VST3 wrapper") [primary, CJE README] | URI `<LV2URI>:<paramID>` as `lv2:Parameter` with `patch:writable` — **not** control ports [primary, generated `dsp.ttl`] | n/a |
| → rule | **The string `paramID` is the cross-format contract. Never rename one; add new IDs instead.** Never toggle `JUCE_FORCE_USE_LEGACY_PARAM_IDS` or `JUCE_USE_STUDIO_ONE_COMPATIBLE_PARAMETERS` after release | | | |
| Automation granularity | **Last point per block only.** JUCE reads `getPoint (numPoints-1)` [primary, VST3 wrapper ~L3641] | Block-start only while `CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES=0`; sample-accurate if >0, at the cost of the §2.3 playhead bug | Applied as `patch:Set` events in order, before `processBlock` (whole block) [primary, LV2 `run()`] | n/a |
| → rule | Plan for **block-rate parameter updates + smoothing** in DSP. Sample accuracy comes from the internal sequencer, not from automation | | | |
| MIDI CC in | Only through JUCE's emulation: **16 ch × 130 hidden "MIDI CC" params**. Carla counted **2085 parameters** on a 4-parameter spike [measured]. Sample-accurate (every point is turned into a MidiBuffer event) [primary] | Raw MIDI events | Raw MIDI atoms | ALSA sequencer |
| MIDI out | Event output bus [measured] | Note port "out", dialect MIDI only. CJE forwards **only 3-byte messages**, so SysEx is dropped [primary, CJE L1810–1825] | `midi:MidiEvent` on the `out` atom port [measured] | `AudioProcessorPlayer::setMidiOutput (default MIDI output device)` [primary] |
| Transport | `ProcessContext` every block | `clap_event_transport*` per `process()` call; may be null [primary; clap-validator `transport-null` passes] | **Only when the host sent `time:Position` this cycle** (§3.1) | **None.** `getPlayHead()` is nullptr except on iOS IAA [primary, `juce_StandaloneFilterWindow.h` L263–273] |
| State | `getStateInformation` blob | same blob, streamed with no modification [primary, CJE `stateSave`/`stateLoad`] | `state:interface` blob; also `Program` index presets | settings file `~/.config/<Name>.settings` [primary] |
| Presets / programs | JUCE programs → VST3 program list + program-change parameter | No program concept in CJE. CJE turns a `programChanged` notification into `params.rescan(VALUES)` [primary L643–653] | Programs baked into `manifest.ttl` as `pset:Preset` | n/a |
| UI embedding | X11 parent window; scale via `IPlugViewContentScaleSupport` | X11 (`CLAP_WINDOW_API_X11` only), `gui.set_scale` → `setScaleFactor` [primary CJE L2258] | `ui:X11UI`, requires **`instance-access`** (UI and DSP in one process) and `ui:parent`; supports the `ui:scaleFactor` option [measured `ui.ttl`] | top-level window |
| Latency | `setLatencySamples` → restart | latency change → `host.request_restart` [primary CJE L630] | `lv2:latency` control-out port [measured] | n/a |

Formats this author's hosts can actually load:

- **REAPER on Linux**: VST3, CLAP, LV2.
- **Bitwig on Linux**: VST3 and CLAP, **no LV2** [lore, widely stated, not verified here].
- **Ardour on Linux**: LV2 and VST3; CLAP from Ardour 8.x onward [lore].
- **Renoise 3.5**: VST3 (CLAP support not verified).

So LV2 is mainly for Ardour, Carla and Qtractor users.

### 2.3 Format gotchas that bite a sequencer instrument

1. **Do not enable CJE block splitting.** With `CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES > 0`, CJE calls `processBlock` several times per host `process()` with offset buffers. But `getPosition()` translates the same `process->transport` struct each time, so `ppqPosition` is the *parent* block's start for every sub-block [primary, CJE `process()` L1597–1810 and `getPosition()` L845–885]. A sequencer that computes `ppq + i*ppqPerSample` would replay each sub-block's step boundaries from the wrong origin. If splitting is ever needed, the processor must count samples since the last `process()` itself, and it cannot know where that boundary falls. So keep it 0.
2. **The MIDI in/out buffer is shared.** `processBlock (AudioBuffer&, MidiBuffer& midi)` gets one buffer that is input on entry and output on exit. If `NEEDS_MIDI_OUTPUT` is TRUE and you do not clear it, **every input note is echoed to MIDI out** [primary, JUCE contract; the spike calls `midi.clear()`]. Required pattern: copy the input events you need into a fixed-capacity array, `midi.clear()`, then add the sequencer's output events.
3. **2085 VST3 parameters.** JUCE's `JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS` defaults to 1 [primary, VST3 wrapper L105]. Those CC parameters show up in some hosts' parameter lists [measured in Carla; lore for REAPER]. Disabling it removes MIDI CC input in VST3 entirely, because VST3 has no CC events. Phoscyon 2 offers MIDI-learn on CCs (`01-phoscyon2-feature-inventory.md` §1, right-click Learn/Clear). **Keep it at 1.** Document the clutter.
4. **The `Program` property in LV2 presets fails RDF validation.** JUCE writes `state:state [ <URI:Program> "N"^^xsd:int ]` without declaring that property. `lv2_validate` reports "Use of undefined property" and "has no label" for every program [measured]. It is harmless at runtime.
5. **The LV2 wrapper takes `processor->getCallbackLock()`** (a `CriticalSection`) around every `processBlock` [primary, LV2 `run()`]. It is uncontended unless something calls `suspendProcessing()`. **Never call `suspendProcessing()` during normal operation**, for example around preset loads; use the lock-free path in §4.3.
6. **AU is irrelevant on Linux**, and the scaffold correctly lists no AU. The mwStime "aumf" category trap does not apply.
7. **CLAP `state.load` always returns true** in CJE, because `setStateInformation` is `void`. clap-validator 0.4.1 warns on "state-invalid-random: the plugin loaded random bytes successfully" [measured, every JUCE plugin]. That is acceptable. Our `setStateInformation` must still reject garbage safely (§5.1).

### 2.4 Standalone on Linux

- **JACK is off by default** (`JUCE_JACK 0` in `juce_audio_devices.h`) [primary]. Set `JUCE_JACK=1`. JUCE `dlopen`s `libjack.so` at runtime (`juce_loadJackFunction` → `dlsym`) [primary], so the binary has no link-time JACK dependency; JACK headers are needed at build time (`libjack-jackd2-dev` is installed). With PipeWire, both the JACK and ALSA backends work. `pipewire-jack` is installed [measured, dpkg].
- **MIDI uses the ALSA sequencer only.** JUCE's JACK backend has no JACK-MIDI ports: `grep -c jack_midi juce_JackAudio.cpp` → 0 [measured]. Under PipeWire, ALSA-seq clients appear in the graph through the MIDI bridge. Under plain JACK2 without `a2jmidid`, MIDI will not reach JACK-MIDI apps [secondary, standard Linux audio knowledge].
- **No transport.** `getPlayHead()` is nullptr, so an internal clock is mandatory (§3.4). Optional extras:
  - JACK transport follow: call `jack_transport_query` through our own `dlopen`; not provided by JUCE.
  - MIDI clock (0xF8) input, which arrives in the MidiBuffer through ALSA seq [primary for arrival; behaviour to test].
- MIDI out goes to `deviceManager.getDefaultMidiOutput()`, which the user picks in the audio settings dialog [primary].
- The settings file is `~/.config/<PluginName>.settings` (`options.folderName = "~/.config"` on Linux) [primary, `juce_audio_plugin_client_Standalone.cpp` L77].
- Runtime dependencies of the Standalone add `libasound.so.2` (linked). The plugin binaries do not link ALSA [measured, `ldd`].

---

## 3. Host transport

### 3.1 What each wrapper actually delivers (source-verified)

**VST3 (JUCE).** A `PositionInfo` built from `ProcessContext` every block. Fields are `Optional` and depend on the host's `ProcessContext::state` flags. Position is "at the start of the current processing block" [primary, JUCE `AudioPlayHead` docs; `docs.juce.com/master/classAudioPlayHead.html`].

**CLAP (CJE)** [primary, CJE L845–885]:

- `getPosition()` returns nullopt if `process->transport == nullptr`.
- `bpm` is set only if `CLAP_TRANSPORT_HAS_TEMPO`.
- `ppqPosition`, `ppqPositionOfLastBarStart`, `barCount` and loop points are set only if `CLAP_TRANSPORT_HAS_BEATS_TIMELINE`.
- `timeInSamples` is *derived from seconds × sample rate*. Upstream `55525c9` changed this from truncation to rounding. **Never use `timeInSamples` for sample-exact sync. Use ppq.**
- Loop points are reported even when the loop is inactive. Check `getIsLooping()`.

**LV2 (JUCE)** [primary, `juce_audio_plugin_client_LV2.cpp` L301–360 and `run()`]:

- `run()` calls `playHead.invalidate()` first, then parses only the `time:Position` objects that arrive *in this cycle's* input atom sequence.
- The event's frame offset is **ignored**: a mid-cycle tempo change is reported as if it happened at block start.
- If no `time:Position` arrived this cycle, `getPosition()` returns **nullopt**.
- `isPlaying = (speed != 0)`; ppq = `time:beat`. A JUCE comment notes that Carla sends integral beats.

**Host side for LV2:**

- **Ardour (master, `libs/ardour/lv2_plugin.cc` L3003–3011)** writes `time:Position` at cycle start **only if** start ≠ predicted start, speed changed, time-scale changed, the rounded beat differs from its own prediction, or the BPM changed. It also writes one at each explicit tempo-map point inside the cycle. Steady rolling playback therefore sends **no** position for most cycles [primary, source read; consistent with drobilla's "LV2 Plugin Transport Synchronisation", 2012, which says Ardour sends updates on change, https://drobilla.net/2012/11/17/lv2-plugin-transport-synchronisation.html].
- Jalv and Ingen send at cycle start, bounded by JACK transport [secondary, same post].

**Conclusion:** a JUCE LV2 build in Ardour sees `getPosition() == nullopt` on most rolling blocks. If the sequencer treated nullopt as "stopped", it would stutter or stop in Ardour. I did not reproduce this in a running Ardour because Ardour is not installed. Confidence in the mechanism is high; the visible behaviour still needs confirming in the host.

**Standalone.** No playhead.

### 3.2 Host quirks

| Host | Behaviour | Confidence |
|---|---|---|
| All | `getPosition()` is valid **only inside `processBlock`**. "Some hosts will have multithreading issues if not called on the audio thread" | [primary] JUCE docs |
| All | ppq is block-start. Loop wrap, relocate and tempo change can only be detected as a discontinuity at the next block start (VST3/CLAP), or as a mid-block event that JUCE re-labels as block start (LV2) | [primary] |
| REAPER | Supplies position when stopped (edit cursor). ppq can be **negative** during pre-roll/count-in. Loops: the host splits blocks at the loop point, so ppq jumps at a block start | [lore]; negative ppq is easy to defend against regardless |
| Bitwig | CLAP transport each `process`. Runs plugins out of process (per-plugin or grouped sandbox), so statics and singletons are per process | [lore] / [secondary] mwStime host-matrix |
| Ardour | LV2: position only on change (§3.1). VST3: JUCE gets a `ProcessContext` each block | [primary] source |
| Carla | LV2: integral `time:beat` | [primary] JUCE source comment |
| JACK hosts at large buffers | "ppqPosition gets jittery on JACK with buffer sizes of 1024 or greater" (HISE forum) | [lore] |
| Pro Tools (not Linux) | `ppqPositionOfLastBarStart` may be 0 | [primary] JUCE docs; irrelevant here but the reason never to require it |

I could not verify Renoise or Qtractor transport behaviour. Treat them through the generic tracker below and test by hand (§7.5).

### 3.3 Required: a `PlayheadTracker` (JUCE-free, unit-tested)

Put this in the JUCE-free sequencer core and feed it a POD snapshot from the processor. Every format edge case becomes a unit test.

```
struct HostSnapshot { bool valid; bool playing; std::optional<double> bpm, ppq; bool looping;
                      std::optional<double> loopStart, loopEnd; };

state: bool rolling=false; double ppq=0, bpm=120; uint64 blocksSinceInfo=0;

onBlock(HostSnapshot h, int numSamples, double sr):
  if (h.valid) {
      bpm = h.bpm.value_or(bpm);
      if (h.playing && h.ppq) {
          predicted = ppq;                        // where we extrapolated to
          if (!rolling || |*h.ppq - predicted| > kRelocateTol)  // kRelocateTol ≈ 1/64 beat
              emit RELOCATE(*h.ppq)               // seek sequencer; resolve gate/slide state (§3.5)
          ppq = *h.ppq;  rolling = true;
      } else if (!h.playing) { if (rolling) emit STOP; rolling = false; }
      blocksSinceInfo = 0;
  } else {                                        // nullopt: "no news", NOT "stopped"
      ++blocksSinceInfo;                           // LV2/Ardour steady state, or no playhead at all
      if (formatHasNoPlayhead) use internal clock
  }
  block covers [ppq, ppq + numSamples*bpm/(60*sr)); sequencer schedules events at sample offsets
  if (rolling) ppq += numSamples*bpm/(60*sr)
```

Unit tests to require:

1. A VST3-style feed: valid every block, steady.
2. An LV2/Ardour-style feed: valid once, then nullopt for N blocks. The output must equal case 1 to within floating-point error.
3. Relocate backwards (loop wrap).
4. Relocate forwards.
5. Tempo change.
6. Negative ppq pre-roll.
7. Stop, then start at the same ppq.
8. Block sizes 1, 7, 64, 4096, and an oversized block (§1.2 #10).
9. Block-size invariance: the same host timeline in any blocking must give a byte-identical event stream. The ref project already has a chunk-invariance test pattern (`ref-mwAudio303/docs/superpowers/specs/2026-06-12-acid303-plugin-shell-design.md` §9).

The reference project's v0 shell was "tempo-synced and transport-gated but NOT bar-phase-locked to the host PPQ grid" (same spec, §6). **That is not acceptable for v1.** Phase lock to ppq is required for any DAW use.

### 3.4 Internal clock (no host transport)

Phoscyon 2 starts playback "on host Play or on the plugin's Start/Stop button" (`01-phoscyon2-feature-inventory.md` L361). Implement two sources and switch automatically:

- **Host-sync**: the tracker is driven by host ppq.
- **Internal**: sample-counted ppq at (host bpm if known, else the internal tempo parameter); started and stopped by a `run` control.

In the Standalone, internal is the only option. In a plugin with the host stopped, pressing Start runs internal at the last known host bpm. Keep the internal clock's sample counter as `int64`, and derive ppq from it as `samples * bpm / (60*sr)` with a re-anchor on tempo change. Do not accumulate ppq by repeated float addition: that drifts about 1e-9 beat per block in double, and does not survive tempo ramps.

### 3.5 Relocation semantics (hand-off to the sequencer doc)

On RELOCATE or STOP the sequencer must:

1. Send note-off for any sounding note, on MIDI out *and* to the voice.
2. Drop any pending slide.
3. Resume at the step containing the new ppq, without retriggering a step whose start lies before the new ppq.

The step-grid algorithm itself is the sequencer doc's job; this is the format-side contract.

---

## 4. Real-time safety

### 4.1 Rules (audio thread = `processBlock` and everything it calls)

No:

- heap allocation or free, including `std::vector` growth, `std::function` capture, `juce::String` construction or concatenation, `juce::var`, `ValueTree`/`XmlElement` access, `MidiBuffer::addEvent` beyond capacity, `AudioBuffer::setSize` with a larger size, or `std::shared_ptr` *last-reference* release;
- locks, `CriticalSection`, `std::mutex`, or `MessageManagerLock`;
- syscalls or I/O, logging, `DBG`, file access;
- waiting or `sleep`;
- unbounded loops;
- exceptions;
- `updateHostDisplay`, `sendChangeMessage`, `AsyncUpdater::triggerAsyncUpdate` (the last posts a message, which allocates on Linux) [secondary, JUCE forum consensus; treat as banned].

Allowed: atomics (lock-free only, so `static_assert(std::atomic<T>::is_always_lock_free)`), the wait-free SPSC structures in §4.3, and `getRawParameterValue()` atomics. These rules are standard [secondary: Ross Bencina, "Real-time audio programming 101: time waits for nothing", 2011]. The siblings already codify them (mwStime architecture §4 "NEVER: allocation, locks, file IO, ValueTree access") [primary].

### 4.2 JUCE-specific traps on this product's path

| Trap | Why | Fix |
|---|---|---|
| `MidiBuffer::addEvent` on the host's buffer | Grows its internal `Array` when full. CJE pre-sizes to 2048 bytes [primary, CJE L916]; other wrappers vary | Cap sequencer output to ≤ 64 events per block. Stage in our own fixed array. Call `midi.ensureSize()` only in `prepareToPlay` on our own buffers |
| `AudioBuffer::setSize` / `HeapBlock` | `std::malloc` [primary, mw140 audit] | Size all scratch in `prepareToPlay` for `max(samplesPerBlock, 8192)`, and chunk-process larger host blocks (§1.2 #10) |
| `prepareToPlay` concurrent with `processBlock` | Seen in AU hosts (mwStime 059). pluginval exercises it | Build new DSP and sequencer state off-thread, publish it through §4.3, and output silence while `!prepared` |
| `releaseResources` | CJE `55525c9` now calls it in `deactivate()` [primary, commit "Call releaseResources() in deactivate() (#188)"] | Must be idempotent and must not free what a concurrent `processBlock` uses |
| `getStateInformation` from background threads | pluginval "Background thread state"; REAPER background save (mw140 audit) | §5.2 |
| `std::shared_ptr` RCU | Last-reference drop on the audio thread frees memory | Graveyard FIFO, as mwStime architecture §4 [primary] |
| `ScopedNoDenormals` scope | Covers only the scope it is in | First line of `processBlock`, and in any worker that runs DSP |
| `AudioProcessorValueTreeState` listeners | `parameterChanged` callbacks can fire **on the audio thread** (CJE calls `sendValueChangedMessageToListeners` from `process()`; CJE comment ~L1516, `paramSetValueAndNotifyIfChanged`: "We want to call the listener from the audio thread and leave it up to the pugin [sic] to be smart") [primary] | Listeners only set atomics or flags. Never touch the model or UI from a listener |

### 4.3 Hand-off patterns (UI/message ⇄ audio)

**Data inventory for this product.** Patterns: about 64 patterns × 16 steps × {note, octave, gate, accent, slide, vibrato…}. That is under 16 KB as POD. Song/chain data is similar. Voice/FX parameters are in APVTS (atomics already). Meters, current step and LED state flow audio → UI.

1. **Pattern bank → audio: lock-free triple buffer of a POD `PatternBankSnapshot`.** Wait-free for both sides, never torn, latest-wins [secondary: standard technique, e.g. Ross Bencina, "Lock-free triple buffer"]. Exact algorithm:

   ```cpp
   template <class T> class TripleBuffer {           // T trivially copyable, ~16 KB
       T buf[3];
       std::atomic<uint8_t> mid { 1 };               // bits 0-1 index, bit 2 = "fresh"
       uint8_t back = 0, front = 2;                  // owned by producer / consumer
     public:
       T& writeBuffer() { return buf[back]; }        // producer (message thread) fills this
       void publish() { back = mid.exchange (uint8_t (back | 4), std::memory_order_acq_rel) & 3; }
       const T& read() {                              // consumer (audio thread), once per block
           if (mid.load (std::memory_order_relaxed) & 4)
               front = mid.exchange (front, std::memory_order_acq_rel) & 3;
           return buf[front];
       }
   };
   static_assert (std::atomic<uint8_t>::is_always_lock_free);
   ```

   The producer copies the whole model into `writeBuffer()` and calls `publish()` after each edit gesture. At 16 KB, a copy takes microseconds. The audio thread calls `read()` once at block start and uses the reference for the whole block.

   The ref project's design ("pick a slot that is neither published nor the consumer's last-claimed") needs the producer to know what the consumer claimed. The single-atomic version above does not. Prefer this one.

2. **Audio → message events** (current step, transport state, live-recorded steps from MIDI input, "pattern ended" for chaining): an **SPSC FIFO** of small POD events (`juce::AbstractFifo` over a fixed array, or a hand-rolled ring with `std::atomic<size_t>` read/write indices). The message thread drains it on a 30–60 Hz `Timer`. If full, drop and count; never block.

3. **Message → audio commands that must not be lost** (e.g. "panic", "jump to pattern N now", "start internal clock"): SPSC command FIFO, drained at block start.

4. **Large or rare objects** (e.g. a new oversampling stage after a sample-rate change, decoded wavetables): `std::shared_ptr` publish + audio-side adopt + **graveyard FIFO** freed on the message thread. This is exactly mwStime's TSan-tested pattern (architecture §4, `Published<T>`) [primary].

5. **Seqlock: don't.** A reader on the audio thread can spin while the writer is mid-copy, so it is not wait-free. Doing it without UB also needs per-word atomic copies (`std::atomic_ref` or relaxed atomics). The triple buffer avoids both problems.

6. **APVTS parameters:** read `std::atomic<float>*` from `getRawParameterValue()` once per block (or per sub-block), as in mwStime and the ref project [primary].

### 4.4 Denormals

- `juce::ScopedNoDenormals` sets FTZ and DAZ in MXCSR for its scope on x86 [primary, JUCE source]. mw140's `DenormalCpuTest` measured a silent:active cost ratio of 0.997 with it in place [primary, mw140 audit §6].
- **But clap-validator 0.4.1 warned on mw140's CLAP: "The plugin took 3.11x longer to process denormals"** [measured]. So something in mw140's CLAP path runs outside the scoped FTZ, or uses x87/libm paths that ignore MXCSR. Make clap-validator's `process-audio-denormals` a gate, not just the ScopedNoDenormals line.
- Belt and braces in self-oscillating filter feedback and exponential envelope tails: add `1e-20f` DC alternated in sign per sample, or clamp envelope state to 0 below 1e-12. This matters if any DSP runs on a worker thread without the scope.

### 4.5 Enforcing real-time safety on Linux without clang

| Tool | Works here? | Use |
|---|---|---|
| **RealtimeSanitizer** (`-fsanitize=realtime`, `[[clang::nonblocking]]`) | **No.** Clang-only; the docs mention no GCC support [primary, clang.llvm.org/docs/RealtimeSanitizer.html]. No clang on this box | — |
| **House allocation spy** (`operator new` override + `-Wl,--wrap=malloc,free,calloc,realloc` + thread-local "audio context" sentinel) | Yes (mw140 pattern) [primary] | **Primary gate.** Render 10–60 s of sequenced patterns with automation storms, pattern swaps, preset loads and tempo changes through the *real* `Processor::processBlock`; assert 0 allocations and 0 frees. Caveat: `--wrap` only rewrites references inside objects linked into the test executable. JUCE is compiled in, so it is covered; calls made inside `libstdc++.so` are not, unless linked statically (`-static-libstdc++`) |
| **rtcheck** (Tracktion, GPL-3.0, `dlsym` interposition of malloc/free/pthread_mutex/…; `rtc::realtime_context` RAII) | Supports Linux (README checklist; CI on `ubuntu-latest`) [primary, github.com/Tracktion/rtcheck] | Optional second gate in a test executable (GPL-3.0 is compatible with an AGPL project). Note that pluginval's own `--rtcheck` is **macOS-only for now**: "Disable rtcheck on Linux for now until further testing" in pluginval `CMakeLists.txt` [primary] |
| **TSan** (gcc 13) | **Yes**, works on this kernel 7.0 without `setarch -R` [measured: caught a planted race] | `tsan` preset, as in mwStime. Races: triple buffer, FIFOs, prepare-vs-process storm, state save on a background thread vs processing vs UI edits. Disable LTO in sanitizer presets. JUCE needs a few suppressions (mw140 audit §5 deferred TSan for that reason) |
| **ASan + UBSan** (gcc 13) | Yes [measured] | Nightly preset over the whole test suite. To run an ASan-built plugin inside pluginval: `LD_PRELOAD=$(gcc -print-file-name=libasan.so) pluginval …` [secondary, standard practice; not run here] |
| **Valgrind** | Not installed (Ubuntu candidate 3.22.0). Can be extracted without root like §7.4, using `VALGRIND_LIB` | Optional; slow. ASan covers most of it |
| **perf** | Installed | CPU profiling. `perf trace -e 'syscalls:*'` on a render can show syscalls from the audio thread |
| **pluginval strict 10** | Yes [measured] | "Parameter thread safety", "Background thread state", "Fuzz parameters", odd block sizes and sample rates (44.1/48/96 kHz × 64–1024) |
| **clap-validator 0.4.1** | Yes [measured] | `process-random-block-sizes`, `process-varying-sample-rates` (it checked 8 kHz on mw4000), `process-audio-denormals`, `param-fuzz-sample-accurate`, `transport-fuzz-sample-accurate` |

---

## 5. State and presets

### 5.1 State format

Use one versioned envelope for every format; the processor serializes it.

```
<MW12F schema="3" pluginVersion="0.4.0">          // root type = product id; schema = integer, bumped on any layout change
  <PARAMS .../>                                    // apvts.copyState() — JUCE flushes param atomics to the tree here
  <SEQ activePattern="..." mode="..."> <BANK ...> <PATTERN id=.. steps="compact text"/> ... </BANK> <SONG .../> </SEQ>
  <UI zoom="1.25" view="grid"/>                    // per-instance UI prefs (also see §6.1 global default)
</MW12F>
```

- Encode with `copyXmlToBinary` / `getXmlFromBinary` (the house pattern; the spike uses it). The XML stays human-diffable for golden state tests. Encode pattern steps as a compact text field, for example Phoscyon's 3-hex-digit step code `29a` (`01-…inventory.md` L341–345), so a 64-pattern bank stays at a few KB.
- **Determinism.** clap-validator's `state-reproducibility-binary` requires save → load → save to be **byte-identical** [primary, cv 0.4.1 `state.rs`]. So:
  - no timestamps or random IDs in state;
  - write attributes in a fixed order;
  - format floats with a fixed precision. `juce::String(double)` round-trips, but be consistent.
- **Migration** is a pure function `upgrade(ValueTree, fromSchema) → ValueTree`, chained `n → n+1`, with a golden state file per historical schema under `tests/state/golden/`. mwStime has `state/Migrations.cpp`; mw140 has `PresetMigrationTest` [primary].
- **Forward compatibility** (older plugin, newer state): load known children, ignore unknown attributes and children, and **preserve unknown children verbatim** when re-saving, so a round trip through an old build does not destroy new data. Refuse (keep current state, log once) if `schema > supported + kMaxForwardSkew`.
- **Garbage input:** `setStateInformation` must survive the 3×1 MB random chunks clap-validator sends (`state-invalid-random`) and pluginval's fuzzing. Check root type and schema, and bounds-check every index (pattern id < 64, steps ≤ 16, note 0–11, octave −1…+3).
- **APVTS `replaceState` behaviour:** parameters *not present* in the incoming tree keep their current values. When migrating from an older schema that lacked a parameter, explicitly set new parameters to their **default**, not to "whatever the instance had".

### 5.2 Thread safety of save and load

| Format | Who calls state save/load, and when | Source |
|---|---|---|
| CLAP | `[main-thread]` per spec | [primary] CLAP `ext/state.h` |
| VST3 | UI thread in principle; hosts vary | [secondary] |
| LV2 | `save()` has "its own special threading class": it **may be called concurrently with an 'Audio' function** (`run()`). `restore()` is in the "Instantiation" class, so it is never concurrent with `run()` | [primary] `lv2/state/state.h` L211–256 (lv2-dev 1.18.10) |
| pluginval | "Background thread state" test calls get/set state from a non-message thread | [measured] |
| REAPER | Background project-save path | [primary] mw140 audit |

Therefore:

- The **model** (patterns, song, UI prefs) is owned by the message thread and guarded by a `std::mutex modelMutex`. Only non-real-time threads take it: the message thread on edits, any thread in get/set state.
- `getStateInformation`: lock → copy model → unlock → `apvts.copyState()` (APVTS locks internally) → build XML.
- `setStateInformation`: parse **outside** the lock → validate and migrate → lock → swap model → unlock → `publish()` a new snapshot to the audio thread (§4.3) → `apvts.replaceState()` → `updateHostDisplay (ChangeDetails().withProgramChanged (true))`. The last two run on the message thread: if called from another thread, marshal them with `MessageManager::callAsync` *only if* the format allows deferral. The CLAP path runs on the main thread anyway.
- **Do not** add `JUCE_ASSERT_MESSAGE_THREAD` to the state methods, as mw140 did. pluginval and hosts legitimately call them from other threads.

### 5.3 CLAP: `params.rescan` after state load (verified fix)

clap-validator 0.4.1 `state-reproducibility-{basic,binary,buffered}` works like this:

1. Randomize parameters through `params.flush`, then save.
2. Create a new instance and record its parameter values.
3. `state.load`, then `poll_callback` once.
4. Fail if values changed **without a `ParamsRescan(VALUES)` callback** [primary, `clap-validator/src/tests/plugin_instance/state.rs` @0.4.1 L100–250].

CJE never calls `params.rescan` from `stateLoad`. It does call `paramsRescan(CLAP_PARAM_RESCAN_VALUES)` when the processor signals `programChanged` [primary, CJE L643–653].

Results [measured]:

- Spike without the call: 3 failures ("Steps 16 vs 9, Cutoff 0.5 vs 0.513…").
- mwStime: same 3 failures, plus 4 param-fuzz tests that timed out at 45 s (plugin-specific).
- mw140: `state.load` returned false. Separately, the validator itself panicked "attempt to divide by zero" in `params.rs:82` on one mw140 parameter.
- mw4000: also failed on overwriting a sidechain input buffer.
- **Spike with `updateHostDisplay (ChangeDetails().withProgramChanged (true))` at the end of `setStateInformation`: 44 run, 33 passed, 0 failed, 1 warning, 10 skipped**, on both JUCE 8.0.12 + CJE `e1f6789` and JUCE 8.0.15 + CJE `55525c9` [measured].

In VST3 the same call leads the JUCE wrapper to tell the host that parameter values changed (`restartComponent`) [secondary, JUCE wrapper behaviour]. That is harmless and correct after a state load.

clap-validator 0.3.2, the version mwStime pins, does not test this. It reported 18/21 passed and 0 failed on the spike and on mwStime [measured]. **Gate on 0.4.1.**

### 5.4 Presets and programs per format

**Primary UX: the in-plugin preset and pattern browser.** Phoscyon has separate browsers for synthesis presets, patterns, FX presets, calibration presets and distortion models (`01-…inventory.md`). Host preset systems are inconsistent on Linux, so the in-plugin browser does the work.

**Expose factory synthesis presets as JUCE programs?**

- Pros: VST3 program list; LV2 `pset:Preset` entries generated into `manifest.ttl` [measured on mwStime: 7 presets visible in `lv2info`].
- Cons:
  - Some hosts call **`setCurrentProgram` after `setStateInformation`**, clobbering the restored state. This is documented for Cubase, and reported for Live and Bitwig [secondary, JUCE forum "Cubase calling setCurrentProgram after setStateInformation", t/21637]. Recommended defences: ignore `setCurrentProgram(i)` when `i == current`; store the current program index in state; ignore calls within about 500 ms after `setStateInformation` [secondary, same thread].
  - JUCE's VST3 program parameter has had scaling bugs [secondary, JUCE forum t/23270, t/43992].
- **Recommendation:** expose *only* a small factory "Init + N" program list (or `getNumPrograms() == 1`), apply all three defences, and keep the real library in the internal browser. pluginval's "Plugin programs" test cycles all programs [measured, "Num programs: 1" on the spike].

**Where things live:**

| What | Location | Notes |
|---|---|---|
| Factory presets and patterns | `juce_add_binary_data(... NAMESPACE mwFactory ...)`, as mw140 does (25 JSON presets, about 50 KB) [primary] | Read-only, versioned with the binary, no install step. Keep a `MW_FACTORY_FROM_DISK` authoring switch, as mw140 does |
| User presets and patterns | **House convention:** `~/.config/mwAudio/<productName>/Presets/` (+ `Patterns/`, `Songs/`) via `File::userApplicationDataDirectory` [primary, style guide] | JUCE resolves `userApplicationDataDirectory` by reading `XDG_CONFIG_HOME` from `~/.config/user-dirs.dirs`, which never holds that key, so it **ignores the `$XDG_CONFIG_HOME` environment variable** and is effectively always `~/.config` [primary, `juce_Files_linux.cpp` L88–135]. Honour `$XDG_CONFIG_HOME` ourselves if set. XDG proper would put user data in `$XDG_DATA_HOME` (`~/.local/share`); I recommend following the house convention for consistency across the line and noting the deviation |
| Global UI prefs (default zoom, view) | `~/.config/mwAudio/<productName>/settings.xml` | Written from the message thread only |
| Temp `.mid` for drag-out | `$XDG_RUNTIME_DIR` or `File::tempDirectory` (honours `$TMPDIR`) [primary] | Delete on the next drag or on editor close, **not** immediately after the drop callback (§6.7) |

---

## 6. GUI on Linux

### 6.1 Embedding, HiDPI and resizing

**JUCE ignores desktop scale in plugins on Linux.** In `XWindowSystem::findDisplays`: `scale = (scale <= 0.1 || ! JUCEApplicationBase::isStandaloneApp()) ? 1.0 : scale;` [primary, `juce_XWindowSystem_linux.cpp` ~L2656]. The Standalone *does* honour, in order: XSETTINGS `Gdk/WindowScalingFactor`, then dconf `/com/ubuntu/user-interface/scale-factor`, then gsettings `org.gnome.desktop.interface scaling-factor`, then `round(dpi/96)` [primary, L1145–1215].

In a plugin, HiDPI therefore comes only from the host:

- **VST3**: `IPlugViewContentScaleSupport::setContentScaleFactor`.
- **CLAP**: `gui.set_scale`. CJE fixes a percent-vs-factor units error when `scale > 50` [primary L2258–2273]. CJE `c1a5ad0` fixed doubled scale under JUCE 9 [primary, commit log].
- **LV2**: the `ui:scaleFactor` option [measured, `ui.ttl`].

Whether REAPER, Bitwig, Ardour or Renoise on Linux send a scale is **unverified** [lore: mixed].

**Requirement:** a user **UI zoom** control with steps 75/100/125/150/175/200 %. It is stored globally as the default and per instance in state. It is applied as `setScaleFactor`-independent: `setTransform(AffineTransform::scale(z))` on a fixed-design-size root component, or the house pattern of a fixed-aspect resizable editor.

**Resizable editor:**

- `setResizable (true, true)`, `setResizeLimits (minW, minH, maxW, maxH)`, `getConstrainer()->setFixedAspectRatio (designW / (double) designH)`. The spike used this and passed pluginval's editor tests [measured]. mwStime uses fixed aspect from 0.6× to 2.0× of a 1000×380 base (ADR-005) [primary].
- Lay out in **design units** and scale. Never compute pixel sizes from `getWidth()` ad hoc in each component.
- CJE `guiAdjustSize` is "a relatively naive and unsatisfactory initial implementation" [primary, CJE comment L2277]. So test CLAP resizing in Bitwig and REAPER by hand.
- Persist the last editor size in state (`UI zoom`).

### 6.2 Fonts

- Embed every font as BinaryData (mw140 embeds D-DIN TTFs) [primary]. Set it with `LookAndFeel::setDefaultSansSerifTypeface (Typeface::createSystemTypefaceFor (data, size))`.
- Never rely on fontconfig resolving "sans-serif" to the same font across distros. JUCE links `libfontconfig` and `libfreetype` [measured, `ldd`], so system lookups do work, but they vary.
- Use SIL-OFL fonts and ship their licence text. Suggested: a DIN-like sans for labels, and a 7-segment or LCD face if the UI has an LCD (for example DSEG7, OFL).

### 6.3 Rendering and repaint strategy (software renderer)

On Linux, JUCE draws with its software renderer into an X11 image. JUCE 8.0.13 "Improved the performance of Component painting" and JUCE 9 "Improved the performance of the software renderer" [primary, release notes]. The evidence that this matters: mwStime's editor made pluginval's "Editor Automation" test produce no output for 120 s under xvfb, while the minimal spike passed in about 1 s [measured].

Rules:

1. **One UI `Timer` at 30 Hz** (60 Hz only for the running-step LED, if needed). It polls atomics and the §4.3 FIFO, and calls `repaint (smallRect)` **only for changed elements**. The spike repaints only the LED strip when the step changes [primary, spike source].
2. **Parameter → UI changes do not repaint the whole editor.** Each knob repaints itself, through the attachment. The faceplate background, panel art and labels are drawn once into a cached `Image` per scale (`setBufferedToImage (true)` or a manual cache). mwStime's ADR-005 does this with "static layers cached per scale" [primary].
3. Use `setOpaque (true)` on every component that fills its bounds. That avoids repainting parents.
4. No per-frame `Path` rebuilding of static shapes, no blur or drop-shadow per frame, and no `Image` allocation in `paint()`.
5. A test target times `paint()` of the full editor at 1× and 2× in an offscreen `Image` (`createComponentSnapshot`, as mw140's `OffscreenRender`). Fail above a budget, for example 8 ms per full repaint at 2× on this machine. That catches regressions before pluginval does.
6. **Step-LED timing.** The audio thread runs ahead of the speaker by (block + device latency). Show the step for `now - outputLatency` if visible lag ever matters. It is a nicety, not a v1 requirement.

### 6.4 OpenGL

Avoid it in v1. The house ADR-005 rejects it ("Linux driver tax") [primary].

- Mesa vsync can block the message thread, and attach/detach in embedded windows is a known crash and flicker area [lore].
- JUCE 9 moves Linux GL to **EGL** and adds GLES [primary, JUCE 9.0.0 BREAKING_CHANGES]. So GL work done on JUCE 8 would be redone later anyway.

### 6.5 Keyboard focus

- Known in JUCE 8.0.10: when a JUCE plugin UI on Linux has focus, **space, Esc "and probably other keys" are not forwarded to the host**. Reproduced in REAPER, Bitwig, and Ardour (LV2 and VST3). No JUCE fix is documented [secondary, JUCE forum t/67197]. Related: grabbing focus on Linux "takes focus away from other applications" [secondary, t/28176], and TextEditor gains and immediately loses focus [secondary, t/66195].
- **Requirements:**
  - `EDITOR_WANTS_KEYBOARD_FOCUS FALSE`.
  - No component calls `grabKeyboardFocus()` on mouse-down except a text field the user clicked to edit (pattern or preset name). Call `giveAwayKeyboardFocus()` / `unfocusAllComponents()` on commit, Esc, or mouse-leave.
  - `setWantsKeyboardFocus (false)` on knobs and buttons.
  - Every function reachable by keyboard must also be reachable by mouse.
  - Computer-keyboard note entry (the x0x keyboard view) is Standalone-only, or opt-in through a "Keys" toggle that warns that host shortcuts will not work while it is on.

### 6.6 Popup menus, tooltips and dialogs in embedded windows

- On Linux, JUCE `PopupMenu` and `TooltipWindow` create separate override-redirect X11 windows. Users report menus behind host windows and the parent losing "active" state [secondary, JUCE forum t/8515 and related].
- **Requirement:** override `LookAndFeel::getParentComponentForMenuOptions` (or pass `PopupMenu::Options().withParentComponent (editor)`) so menus render *inside* the editor. Size long menus to fit, using columns or sub-menus. The trade-off is that menus are clipped to the editor bounds.
- Put the tooltip window as a child of the editor (`TooltipWindow tooltip { this }`).
- No `AlertWindow::show…` modal loops (`JUCE_MODAL_LOOPS_PERMITTED=0`). Use in-editor overlay panels for confirmations, as Phoscyon's EXPORT overlay does.

### 6.7 Drag-and-drop of a MIDI file out of the plugin (X11)

**Mechanism.** `DragAndDropContainer::performExternalDragDropOfFiles` → `XWindowSystem::externalDragFileInit`. It builds a `text/uri-list` of `file://` URIs and runs the XDND protocol from the editor's peer window. The supported targets are `text/uri-list`, `text/plain`, `text/plain;charset=utf-8` [primary, `juce_XWindowSystem_linux.cpp` L117–171, L2781–2797].

**Past bugs:** a hang was fixed in April 2021 [secondary, JUCE forum t/45490]. "Repeats the drag on every following click" was fixed in November 2021, commit "X11: Correctly reset drag and drop state when drag has ended" [secondary, t/48569]. JUCE 8 includes both.

**Host acceptance on Linux:**

| Host | Result | Confidence |
|---|---|---|
| Waveform | Works | [secondary] t/50410 |
| Carla | Works | [secondary] t/50410 |
| File manager | Works | [secondary] t/50410 |
| Bitwig | Works | [lore] |
| Ardour (timeline) | Works for MIDI patterns dragged from a plugin (Addictive Drums report) | [lore] |
| REAPER | Unverified | — |

The well-known failures go the *other* direction: dropping files *into* a plugin. REAPER's FX window is `XdndAware` and steals drops [secondary, t/50410]. We do not need drop-in (Phoscyon has no MIDI import).

**Requirements:**

- (a) Write the `.mid` with `juce::MidiFile` into the temp dir on drag start.
- (b) Keep the file until the next drag or editor destruction, because the host may read it after the callback.
- (c) A visible "Export .mid…" button (async `FileChooser`) as a guaranteed path.
- (d) Accented and non-accented velocities as Phoscyon has (96/32 defaults, `01-…inventory.md` §4.7).
- (e) A manual host-matrix row for each host.

### 6.8 File choosers without GTK

JUCE's Linux "native" chooser **spawns `zenity` or `kdialog` as a child process** when either is on `$PATH`. It prefers kdialog in KDE sessions or when zenity is missing. Otherwise it falls back to JUCE's own `FileBrowserComponent` dialog [primary, `juce_FileChooser_linux.cpp` L62–72, L280–289]. There is **no** link-time GTK dependency.

**Requirement:** use `FileChooser::launchAsync` only (modal loops are disabled). Prefer the in-editor browser for presets and patterns. Use the chooser only for import/export of external files. A zenity window is a separate process and can open behind the DAW, so `useOSNativeDialogBox = false` is a reasonable default inside plugins [secondary]. Test both.

---

## 7. Validation and CI on this machine

### 7.1 Tool pins (verified downloads)

| Tool | Version | URL | SHA-256 | Status |
|---|---|---|---|---|
| pluginval | v1.0.4 (2024-12-04; latest release; JUCE 8.0.3 inside) | `https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Linux.zip` | `c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352` | [measured]; binary identical to `~/projects/mw303VinylSim/tools/pluginval` |
| clap-validator | **0.4.1** (2026-07-22) | `https://github.com/free-audio/clap-validator/releases/download/0.4.1/clap-validator-0.4.1-127-g152b982-ubuntu-22.04.zip` (zip contains a `.tar.gz` that contains the binary) | `49edadcfb407ea0dd946ce418300e853fbd2660fa4b0d00e4f19ff8eef24ad90` | [measured] |
| clap-validator | 0.3.2 (2023-03-25), regression baseline only | `…/0.3.2/clap-validator-0.3.2-ubuntu-18.04.tar.gz` | `1476ed68f5657e76050e0c4f19790c02d819ecc62c35fd465059d21f05169cb1` | [measured] |
| lv2_validate / sord_validate | lv2-dev 1.18.10, sordi 0.16.16 | `apt-get download lv2-dev sordi` + `dpkg -x` (**no root**) | — | [measured] |
| carla-discovery-native | carla 2.5.8 (Ubuntu) | `apt-get download carla carla-data` + `dpkg -x`; run with `LD_LIBRARY_PATH=<root>/usr/lib/carla` | — | [measured]; **no CLAP support** in this build ("invalid string type") |
| lv2lint | — | not packaged for noble; build from source (meson + lilv-dev) if wanted | — | not run |

pluginval upstream `main` has unreleased changes: VST3 validator integrated into the binary (#162, Feb 2026), a bus-layout processing test (#169), JUCE 8.0.13, and `--rtcheck` (macOS-only) [primary, commit log + CHANGELIST "1.0.5"]. v1.0.4 runs Steinberg's VST3 validator only if `--vst3validator <path>` is given. It logged "Skipping vst3 validator as validator path hasn't been set" [measured].

### 7.2 pluginval (VST3 **and LV2**)

```bash
PV=tools/validators/pluginval
# headless: -s gives the virtual screen a real size; -a picks a free display number
env -u DISPLAY xvfb-run -a -s "-screen 0 1920x1080x24" \
  "$PV" --strictness-level 10 --validate-in-process --timeout-ms 300000 \
        --output-dir build/pluginval-logs \
        --validate build/.../VST3/mw12F.vst3
# same for LV2 — pass the bundle directory:
env -u DISPLAY xvfb-run -a "$PV" --strictness-level 10 --validate-in-process \
        --validate build/.../LV2/mw12F.lv2
# release gate adds churn:  --repeat 3 --randomise   (mwStime's invocation)
# replay a failure:         --random-seed 0x<seed from the log>
```

Measured on the spike (both JUCE builds):

- VST3 **SUCCESS in 12.9 s**; LV2 **SUCCESS in 13.0 s**.
- Test list: open cold/warm, info, programs, editor, open editor while processing, audio processing, non-releasing processing, state, state restoration, automation, editor automation, automatable parameters, parameters, background thread state, parameter thread safety, buses (4 tests), fuzz parameters.

Notes:

- `--validate-in-process` is accepted although the help text does not list it.
- All options can also come from environment variables (`STRICTNESS_LEVEL=10` etc.) [measured help text].
- `--skip-gui-tests` exists. Do **not** use it in the gate: the editor tests are exactly what catches §6.3 regressions.
- Use `--disabled-tests` only with an **absolute** path [primary, mwStime].

### 7.3 clap-validator

```bash
CV=tools/validators/clap-validator
rm -rf "${XDG_RUNTIME_DIR:-/run/user/$UID}/clap-validator"   # stale temp files make 0.4.1 panic (seen on mw4000 [measured])
env -u DISPLAY xvfb-run -a "$CV" validate --only-failed build/.../CLAP/mw12F.clap   # exit 0 = pass
"$CV" validate --json build/.../CLAP/mw12F.clap > build/clap-validator.json       # machine-readable
"$CV" list tests                                                                   # 44 tests in 0.4.1
"$CV" validate -t 'state-.*' --in-process --trace …                               # debugging one area
```

0.4.1 adds or strengthens: `scan-time` (under 100 ms), `scan-rtld-now`, `state-reproducibility-*`, `state-invalid-*`, `param-fuzz-{basic,bounds,sample-accurate,modulation}` (45 s timeout each), `process-audio-denormals`, `process-varying-sample-rates`, `transport-null`, `transport-fuzz{,-sample-accurate}` [measured, `list tests` + runs].

Expected result for our plugin: **0 failed, 1 warning (`state-invalid-random`, a CJE limitation, §2.3 #7)**. Validator bugs seen [measured]:

- divide-by-zero panic in `params.rs:82` on one mw140 parameter;
- temp-file-exists panic under `/run/user/1000/clap-validator/`.

If either appears, isolate it with `-x <test>` and record it as a validator bug, not a plugin failure.

### 7.4 LV2

1. **pluginval** on the bundle (§7.2): the strongest functional check available here.
2. **RDF validation without root:**

   ```bash
   R=tools/validators/lv2root                      # dpkg -x of lv2-dev + sordi debs
   sed "s#LV2DIR=\"/usr/lib/lv2\"#LV2DIR=\"$R/usr/lib/lv2\"#; s#^sord_validate#$R/usr/bin/sord_validate#" \
       "$R/usr/bin/lv2_validate" > tools/validators/lv2_validate_local
   tools/validators/lv2_validate_local build/.../LV2/mw12F.lv2/*.ttl     # exits 1 on any error
   ```

   **Every JUCE 8 LV2 bundle produces baseline errors** [measured; spike 9, mwStime 34]:

   - `lv2:portProperty` on `lv2:Parameter` subjects ("Subject not in domain lv2:Port", one per enum/toggle parameter);
   - `doap:maintainer` / `doap:release` "not in domain doap:Project";
   - `ui:noUserResize` / `opts:interface` "not in range";
   - the undeclared `…:Program` property (×2 per program).

   Gate on **no new error signatures**: normalise lines to `error: <message>` + predicate, diff against a committed `tests/plugin/lv2_validate.baseline`, and fail on additions.
3. **Load smoke without JACK:**

   ```bash
   LD_LIBRARY_PATH=$R/usr/lib/carla $R/usr/lib/carla/carla-discovery-native lv2 build/.../LV2/mw12F.lv2
   LD_LIBRARY_PATH=$R/usr/lib/carla $R/usr/lib/carla/carla-discovery-native vst3 build/.../VST3/mw12F.vst3
   ```

   Prints `name/maker/category/audio.outs/midi.ins/midi.outs/parameters.ins` and exits 0 [measured]. Assert `category::synth`, `midi.ins::1`, `midi.outs::1`, `audio.outs::2`.

   Carla 2.5.8 counts LV2 **`parameters.ins 1`**, meaning control *ports* only. It does not count JUCE's `patch:Parameter` parameters [measured]. Whether Carla 2.5 then exposes those parameters for automation is unverified [open question].
4. `lv2info` / `lv2ls` (lilv-utils, also via `dpkg -x`) for a readable dump, including presets. `lv2bench` **cannot** run JUCE LV2 plugins: "requires feature options#options, skipping" [measured]. `jalv` needs a running JACK/PipeWire graph, so it is a manual-only tool here.

### 7.5 Host smoke tests

**REAPER 7.79** (`/opt/REAPER/reaper`) supports `-renderproject file.rpp` ("render project and exit"), `-cfgfile file.ini` ("alternate resource directory"), `-nosplash`, `-ignoreerrors` and `-newinst` [measured, `reaper -h`, which prints and exits without a window].

Proposed headless smoke, **unverified**:

1. Create an isolated resource dir with a `reaper.ini` whose `vstpath64`/CLAP path points only at `build/…`.
2. Commit a template `.rpp` with one MIDI-triggered track holding the plugin, plugin state base64-embedded, and the render target set.
3. Run `env -u DISPLAY xvfb-run -a /opt/REAPER/reaper -cfgfile "$TMP/reaper.ini" -nosplash -newinst -ignoreerrors -renderproject smoke.rpp`.
4. Assert that the output WAV exists, is non-silent and NaN-free, and that rendering twice is bit-identical. That checks determinism with the internal sequencer under offline render.

This also tests the sequencer in offline (non-realtime) render, where hosts call `processBlock` faster than real time and `isNonRealtime()` is true.

Never run REAPER against the user's real config: always pass `-cfgfile`.

**Manual host matrix** (record date, host version, format, pass/fail, as mwStime's `host-matrix.md`). Hosts: REAPER (VST3/CLAP/LV2), Renoise (VST3), Bitwig (CLAP/VST3), Ardour (LV2/VST3), Carla (LV2/VST3). Rows:

1. load;
2. host-synced playback phase-locked over 64 bars;
3. loop wrap mid-pattern;
4. tempo ramp;
5. stop/start at the same ppq;
6. project save/reload restores patterns and parameters;
7. automation of cutoff;
8. MIDI out routed to a second instrument;
9. drag `.mid` to the timeline;
10. resize, zoom and host scale;
11. keyboard shortcuts still reach the host after clicking the UI;
12. offline render equals realtime.

### 7.6 Sanitizer and real-time builds (presets)

`CMakePresets.json` (house pattern from mwStime):

- `dev` (RelWithDebInfo, Ninja, no LTO);
- `release` (Release, `MW_LTO=ON`);
- `asan` (Debug, `-fsanitize=address,undefined -fno-omit-frame-pointer`, `MW_BUILD_PLUGIN` optional);
- `tsan` (Debug, `-fsanitize=thread`, plugin OFF, core + JUCE-shell tests only).

ctest labels: `unit`, `golden`, `rt` (alloc spy), `tsan`, `validators`, `host-smoke`. Validators self-skip with exit 77 when their tool is missing, and fail hard on a checksum mismatch [primary, house pattern].

---

## 8. Build and distribution

### 8.1 Dependencies

**Build** (all present on this machine):

- `build-essential cmake ninja-build pkg-config git curl unzip`
- `libasound2-dev libjack-jackd2-dev`
- `libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libxcomposite-dev libxrender-dev`
- `libfreetype-dev libfontconfig1-dev`
- for JUCE 9 only: `libxi-dev libegl-dev`

`libcurl4-openssl-dev` and `libwebkit2gtk-4.1-dev` are **not** needed with `JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0` (mwStime BUILDING.md) [primary].

**Runtime, plugin binaries** [measured, `ldd` on the spike CLAP with static libstdc++]:

- `libfontconfig.so.1`, `libfreetype.so.6` (+ transitive `libexpat`, `libz`, `libbz2`, `libpng16`, `libbrotli*`), `libm`, `libc`.
- **X11 libraries are not linked.** JUCE `dlopen`s them (`X11Symbols`).
- ALSA is not linked into plugins. The Standalone adds `libasound.so.2`; JACK is `dlopen`ed.

**Static libstdc++ / libgcc:** `target_link_options(<shared-code-target> PUBLIC -static-libstdc++ -static-libgcc)` removed `libstdc++.so.6` and `libgcc_s.so.1` from `ldd` [measured]. Recommended for distributed builds: it protects against hosts that load a different libstdc++ first, and against older distros. Symbol visibility is fine because JUCE builds with `-fvisibility=hidden` via `juce_recommended_config_flags`.

**glibc baseline:**

- A build on Ubuntu 24.04 requires **GLIBC_2.38**: `__isoc23_strtol/strtoul/strtoll/sscanf` and `fmod@GLIBC_2.38` [measured, `objdump -T`]. That excludes Debian 12 (2.36) and Ubuntu 22.04 (2.35).
- For distributed binaries, build inside `ubuntu:22.04` (glibc 2.35) with Docker, which is available and running here [measured]. Building on 20.04 (2.31) would need a newer compiler toolchain.
- For local use, the native 24.04 build is fine.

### 8.2 Build performance

| Configuration | Wall time | Source |
|---|---|---|
| Full spike build, LTO on (`juce_recommended_lto_flags` = `-flto`), Ninja `-j24` | 3 m 42 s | [measured] |
| CLAP-only relink after a one-file change, LTO on | 2 m 34 s | [measured] |
| Same build without LTO, `-j30` | 1 m 33 s | [measured] |

Under LTO, gcc printed **"lto-wrapper: warning: using serial compilation of 67–79 LTRANS jobs"** for each format, because Ninja provides no make jobserver [measured]. So:

- Keep LTO **off** in dev presets (the scaffold's `MW_LTO OFF` default is right).
- In release, add `-flto=auto` to parallelise LTRANS.

Release spike binaries are about 6.9 MB per format with LTO and static libstdc++ [measured]. mwStime's RelWithDebInfo binaries are 116–121 MB because of debug info [measured]. Strip for distribution (`-s` or `objcopy --strip-debug`).

### 8.3 Install targets

| Format | Per-user | System | Env override | Source |
|---|---|---|---|---|
| VST3 | `~/.vst3/<Name>.vst3/` (bundle dir; binary at `Contents/x86_64-linux/<Name>.so`) | `/usr/lib/vst3`, `/usr/local/lib/vst3` | — | [primary] JUCE `VST3PluginFormatHeadless.cpp` L165; JUCE copy dir `$HOME/.vst3` |
| CLAP | `~/.clap/<Name>.clap` (single file) | `/usr/lib/clap` | `CLAP_PATH` | [primary] `clap/entry.h` L14–27; CJE copy dir `$HOME/.clap` |
| LV2 | `~/.lv2/<Name>.lv2/` | `/usr/lib/lv2`, `/usr/local/lib/lv2` | `LV2_PATH` | [primary] JUCE copy dir `$HOME/.lv2` |
| Standalone | `~/.local/bin/<name>` + `~/.local/share/applications/<name>.desktop` | `/usr/bin` (house `.deb`) | — | [primary] house packaging |

This author's `~/.vst3`, `~/.clap` and `~/.lv2` already hold the sibling plugins [measured]. Add an `install-user` script or target that copies, rather than symlinking, the three artefacts. Bitwig and others cache scan results by path and mtime, and a symlink into `build/` that disappears on a clean build breaks hosts [lore].

House packaging: `.deb` via `dpkg-deb`, plus an AppImage for the Standalone (mw140: appimagetool 1.9.1, SHA `ed4ce84f…`) [primary].

### 8.4 Licensing

| Component | Licence | Source |
|---|---|---|
| JUCE 8 (and 9) | AGPLv3 **or** commercial. Commercial tiers on juce.com (JUCE 9 page): Starter free up to $20k revenue; Indie $40/user/month up to $300k (1-month minimum, $800 perpetual); Pro $175/user/month (12-month minimum, $3,500 perpetual) | [primary] `LICENSE.md`; juce.com/get-juce |
| → for this product | Ship under **AGPL-3.0-or-later**, the mwStime precedent: source published, SPDX headers on every file | Fixing the style guide's "GPL v3" line is a documentation fix |
| VST3 SDK (bundled in JUCE 8.0.12: "VST 3.8.0") | **MIT** since VST 3.8, 2025-10-29 | [primary] Steinberg press release; bundled `LICENSE.txt` "Copyright (c) 2025, Steinberg"; [measured] moduleinfo SDKVersion |
| CLAP SDK | MIT | [primary] `clap/LICENSE` |
| clap-juce-extensions | MIT | [primary] |
| LV2 SDK (bundled) | ISC | [primary] JUCE `LICENSE.md` |
| JUCE third-party | HarfBuzz (old MIT), SheenBidi (Apache-2.0, GPLv3-compatible), zlib/pnglib (zlib), FLAC/Ogg (BSD), jpeglib (IJG) | [primary] JUCE `LICENSE.md` |
| Catch2 v3 | BSL-1.0 (tests only) | [primary] |
| rtcheck (optional, tests only) | GPL-3.0 | [primary] |
| Open303 (reference DSP, if vendored) | MIT; keep attribution | [secondary] ref report §5.2 |
| Fonts | must be OFL or similar; ship the licence | — |
| Phoscyon factory content | D16 copyright. **Do not ship** (`01-…inventory.md` L29) | [primary] |

Also respect the **trademark rule**: no "Roland" or "TB-303" in code or binaries; see the branding audit in §1.1.

### 8.5 Compiler flags

- Use `juce_recommended_config_flags` and `juce_recommended_warning_flags` for the plugin. The spike showed one expected warning: `-Woverloaded-virtual=` for `processBlock(double)` being hidden. Silence it with `using AudioProcessor::processBlock;` [measured].
- On the JUCE-free DSP and sequencer core: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`. Never `-ffast-math`: it breaks NaN/Inf checks, denormal reasoning and golden determinism (mwStime rule). Add `-ffp-contract=off` on the core only if bit-exact golden renders are a goal (mwStime) [primary].
- `-fno-omit-frame-pointer` in RelWithDebInfo makes `perf` profiles usable.
- `CMAKE_POSITION_INDEPENDENT_CODE ON` (already in the scaffold) is needed because the static core links into shared plugin objects.

### 8.6 JUCE 9 — not in v1

JUCE 9.0.0 shipped 2026-07-21; the latest is 9.0.3 (2026-09-28) [primary, GitHub releases]. **No native CLAP is listed in its release notes**, so the CJE dependency stays either way [primary].

Linux-relevant changes:

- `libxi-dev` is now required (multi-touch through XInput2).
- OpenGL moves to EGL.
- 9.0.1 "Fixed unresponsive Linux GUIs", a regression in 9.0.0.
- 9.0.3 "Fixed multiple Linux ALSA sample rate and channel issues".
- zlib, libpng and libflac are now compiled as C (possible ODR clashes if anything else links them).
- CJE needed fixes for 9 ("JUCE 9 support", "Fix doubled editor scale … under JUCE 9").

[primary, release notes, `BREAKING_CHANGES.md` @9.0.3, CJE commit log]

The JUCE 8 line ends at **8.0.15** (2026-07-21: Windows scaling fixes, OpenGL texture handling, Oboe). The task brief and every house project use JUCE 8. **Recommendation:** v1 on 8.0.15. Schedule a JUCE 9 evaluation as a separate backlog item once 9.0.x has had a few more Linux fixes.

---

## 9. Recommended project skeleton

This builds on the scaffold already in `~/projects/mw303` (`MW_PRODUCT_NAME mw12F`, `libs/acid-core`, `plugin/`, `tests/`, `tools/validators/`, `scripts/fetch-validators.sh`) and on house conventions.

```
mw303/
├── CMakeLists.txt              # product identity in ONE place (already so); options MW_BUILD_PLUGIN, MW_BUILD_TESTS, MW_LTO
├── CMakePresets.json           # dev | release | asan | tsan  (Ninja; §7.6)
├── VERSION                     # house: version read from file, checked against tag at release
├── cmake/
│   ├── FetchJUCE.cmake         # JUCE 8.0.15  (GIT_SHALLOW)        — bump = explicit backlog task
│   ├── FetchClapExtensions.cmake  # CJE 55525c9858d4b25687be7759a5e0f70eccef218e (FetchContent does submodules; CLAP 1.2.7)
│   └── FetchCatch2.cmake       # v3.7.1
├── libs/                       # JUCE-FREE, static, PIC, -Wall -Wextra -Wconversion, no -ffast-math
│   ├── acid-core/              # voice DSP (osc, filter, envelopes, accent, slide, drive) — owned by DSP docs
│   ├── acid-seq/               # patterns, bank, song, PlayheadTracker (§3.3), step scheduler, MIDI-out event model,
│   │                           #   pattern text codec, .mid writer (or keep .mid in plugin via juce::MidiFile)
│   ├── acid-fx/                # FX rack DSP
│   └── acid-rt/                # TripleBuffer, SpscFifo, Graveyard, RtAssert/RtContext (sentinel) — header-only
├── plugin/                     # the ONLY JUCE code
│   ├── CMakeLists.txt          # juce_add_plugin (§2.1) + clap_juce_extensions_plugin + juce_add_binary_data (fonts, factory)
│   ├── src/Processor.*         # thin adapter: snapshot params + playhead → engine; MIDI in/out staging (§2.3 #2)
│   ├── src/state/              # StateCodec (envelope §5.1), Migrations, Model (mutex-guarded, §5.2), PresetStore (paths §5.4)
│   └── ui/                     # Editor (design-unit layout + zoom), LookAndFeel, components, overlays (no modal loops)
├── assets/fonts/ assets/factory/   # OFL fonts + licences; factory presets/patterns (our own content only)
├── tools/
│   ├── render/                 # offline CLI: (preset, pattern, bpm, bars, sr) → WAV; links libs/ only — golden + A/B
│   └── validators/             # fetched, not committed (exists): pluginval, clap-validator, lv2root/, carla
├── scripts/
│   ├── fetch-validators.sh     # exists; ADD sha256 checks (§7.1), lv2-dev/sordi/lilv-utils/carla dpkg -x into lv2root
│   ├── validate.sh             # runs §7.2–7.4 in order, xvfb-wrapped, exit 77 when a tool is absent
│   ├── install-user.sh         # copy VST3/CLAP/LV2 to ~/.vst3 ~/.clap ~/.lv2 (§8.3)
│   └── branding-audit.sh + forbidden_tokens.txt   # house pattern (mw140)
└── tests/
    ├── unit/ (Catch2, JUCE-free: seq, tracker, triple buffer, codec)   ├── golden/ (render tool outputs)
    ├── plugin/ (juce_add_console_app + Catch2: state round-trip, migration goldens, host-feed simulations,
    │            alloc-spy RT test with --wrap=malloc, editor paint-time budget)
    └── plugin/lv2_validate.baseline   # known JUCE-generated RDF errors (§7.4)
```

Target structure:

- `acid_core`, `acid_seq`, `acid_fx` (STATIC, JUCE-free).
- `acid_rt` (INTERFACE).
- `mwacid_plugin` (JUCE shared code + `_VST3 _LV2 _CLAP _Standalone` targets).
- `mwacid_render` (exe, JUCE-free).
- Test executables: `acid_unit_tests`, `acid_plugin_tests` (JUCE console app), `acid_rt_tests` (separate binary because of the global `operator new` and `--wrap`).

**Validator commands that work on this machine** (verified 2026-10-02 against the spike; replace paths):

```bash
A=build/dev/plugin/mwacid_plugin_artefacts/RelWithDebInfo     # JUCE artefact dir pattern: <target>_artefacts/<CONFIG>/<FORMAT>
V=tools/validators

env -u DISPLAY xvfb-run -a -s "-screen 0 1920x1080x24" $V/pluginval --strictness-level 10 \
    --validate-in-process --timeout-ms 300000 --validate "$A/VST3/mw12F.vst3"
env -u DISPLAY xvfb-run -a $V/pluginval --strictness-level 10 --validate-in-process \
    --timeout-ms 300000 --validate "$A/LV2/mw12F.lv2"
rm -rf "/run/user/$UID/clap-validator"; \
env -u DISPLAY xvfb-run -a $V/clap-validator validate --only-failed "$A/CLAP/mw12F.clap"
$V/lv2_validate_local "$A/LV2/mw12F.lv2"/*.ttl 2>&1 | scripts/lv2_baseline_diff tests/plugin/lv2_validate.baseline
LD_LIBRARY_PATH=$V/lv2root/usr/lib/carla $V/lv2root/usr/lib/carla/carla-discovery-native lv2  "$A/LV2/mw12F.lv2"
LD_LIBRARY_PATH=$V/lv2root/usr/lib/carla $V/lv2root/usr/lib/carla/carla-discovery-native vst3 "$A/VST3/mw12F.vst3"
```

Do **not** `xvfb-run` when a developer wants to *see* the editor tests. With `DISPLAY=:0` set, pluginval opens real windows on the user's X session.

---

## Open questions

1. **Ardour + JUCE LV2 transport.** Is the nullopt-between-changes behaviour (§3.1) visible as broken sync in a real Ardour 8/9 session, or does Ardour send `time:Position` more often than its source suggests? *Handle:* build the `PlayheadTracker` assuming the worst (it costs nothing), and add an Ardour row to the manual matrix.
2. **Host HiDPI on Linux.** Which of REAPER, Bitwig, Ardour and Renoise send a content scale on Linux for VST3, CLAP and LV2? *Handle:* user zoom (§6.1) makes the answer non-blocking. Record findings in the host matrix.
3. **MIDI out per host.** Whether, and how, REAPER, Bitwig, Ardour and Renoise on Linux route MIDI out from an *instrument* plugin for each format was not verified [lore only]. *Handle:* MIDI out is a toggle (default on). The product must be fully useful without it. Matrix row 8.
4. **REAPER for MIDI drag-out**, and Bitwig and Ardour confirmations. *Handle:* the Export button is the guaranteed path.
5. **REAPER headless `-renderproject` smoke** (§7.5) is designed but not executed. `.rpp` plugin-chunk syntax and isolated-config scanning are the risks. *Handle:* one engineer-day spike; keep it `host-smoke`-labelled and self-skipping.
6. **Carla 2.5 and JUCE LV2 parameters.** Discovery counted only 1 parameter (control ports). Does Carla expose JUCE's `patch:Parameter` set for automation? *Handle:* test in Carla manually. Not a blocker, since Carla can host the VST3.
7. **The mw140 denormal warning** under clap-validator (3.11×) was not root-caused. *Handle:* make `process-audio-denormals` a gate for our plugin, and if it fires, look for DSP outside the `ScopedNoDenormals` scope.
8. **Keyboard-focus forwarding bug** (JUCE forum t/67197): is there a JUCE fix in 8.0.11–8.0.15? I found no release-note entry. *Handle:* the §6.5 rules regardless.
9. **The pluginval `AudioParameterBool` restore flake** (mwStime 048c) did not reproduce on the spike at strictness 10 without `--repeat 3 --randomise`. *Handle:* run the release gate with `--repeat 3 --randomise`. If it flakes, switch automatable toggles to 2-entry `AudioParameterChoice` rather than waiving the test.
10. **JUCE 9 timing** (§8.6). Revisit after 9.0.x stabilises on Linux. CJE already supports it.

## Implications for the build

1. Pin **JUCE 8.0.15** and **clap-juce-extensions `55525c9858d4b25687be7759a5e0f70eccef218e`** (CLAP 1.2.7), both verified with gcc 13. Treat bumps as explicit backlog tasks that re-run the validators.
2. Fix the scaffold's `juce_add_plugin` per §2.1:
   - add `COMPANY_WEBSITE`, `DESCRIPTION`, `LV2_SHARED_LIBRARY_NAME`, `EDITOR_WANTS_KEYBOARD_FOCUS FALSE`;
   - add `JUCE_MODAL_LOOPS_PERMITTED=0`, `JUCE_JACK=1`;
   - drop `JUCE_DISPLAY_SPLASH_SCREEN`;
   - set CLAP features to `instrument synthesizer stereo` if the output is stereo.
   Freeze `PLUGIN_MANUFACTURER_CODE mwAu`, `PLUGIN_CODE`, `LV2URI`, `CLAP_ID` and every parameter string ID before the first release.
3. Never put "Roland" or "TB-303" in code, parameter names, presets, UI or binaries. Adopt mw140's `branding-audit.sh` and token list, scanning sources and `strings` of built binaries.
4. In `setStateInformation`, end with `updateHostDisplay (ChangeDetails().withProgramChanged (true))`. Gate CLAP on **clap-validator 0.4.1 with 0 failures** (one accepted warning: `state-invalid-random`).
5. Keep `CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES = 0`. Do not rely on sample-accurate automation in any format; smooth parameters in DSP.
6. In `processBlock`:
   - stage incoming MIDI into a fixed array, then `midi.clear()`, then emit sequencer events (≤ 64 per block);
   - start with `ScopedNoDenormals`;
   - chunk host blocks larger than the prepared size;
   - output silence while not fully prepared.
7. Implement a JUCE-free `PlayheadTracker` (§3.3) that treats a missing playhead as "no news" and extrapolates. Detect relocation by discontinuity (tolerance about 1/64 beat), handle negative ppq, and fall back to an `int64`-sample internal clock in the Standalone and when the host is stopped and the user presses Start. Require the 9 unit tests listed in §3.3, including block-size invariance and the LV2/Ardour feed simulation.
8. Pattern data flows message → audio through a wait-free **triple buffer** of a POD bank snapshot. Audio → UI events go through an SPSC FIFO drained by a single 30 Hz UI timer. Large objects use `shared_ptr` publish + graveyard. No seqlocks, no mutexes on the audio thread, no `suspendProcessing()`.
9. The model is message-thread-owned and guarded by a mutex that non-real-time threads take, so get/set state is safe from any non-audio thread. Do **not** assert message-thread in the state methods.
10. State: a versioned XML envelope (root = product id, integer `schema`), deterministic byte output, chained migrations with golden files per schema, unknown children preserved, strict bounds checks on load, and defaults applied for parameters missing from older state.
11. Make the in-plugin browser the primary preset UX. Expose at most "Init + a few" as JUCE programs. Guard `setCurrentProgram` (no-op on same index; ignore within about 500 ms of a state load; current index stored in state). Factory content is embedded as BinaryData. User content goes in `~/.config/mwAudio/<productName>/{Presets,Patterns,Songs}`, honouring `$XDG_CONFIG_HOME` ourselves.
12. GUI:
    - fixed-aspect resizable editor laid out in design units, plus a **user zoom** setting (75–200 %) persisted globally and per instance;
    - embedded OFL fonts;
    - cached static layers, `setOpaque(true)`, dirty-rect repaints only;
    - no OpenGL;
    - menus and tooltips parented to the editor;
    - no keyboard-focus grabs except explicit text edits;
    - an editor paint-time budget test.
13. MIDI drag-out uses `performExternalDragDropOfFiles` with a temp `.mid` kept alive until the next drag or editor close, plus an "Export .mid…" button (async `FileChooser`) and velocity settings for accented/non-accented notes (96/32).
14. CI on this machine (`ctest -L validators`, exit 77 = skip), all under `xvfb-run -a`:
    - pluginval v1.0.4 strictness 10 on **VST3 and LV2** (release: `--repeat 3 --randomise`);
    - clap-validator 0.4.1;
    - `lv2_validate` against a committed JUCE-error baseline;
    - `carla-discovery-native` load smoke for LV2 and VST3.
    Pin the SHA-256 values from §7.1 and get LV2 tools by `apt-get download` + `dpkg -x` (no root).
15. Real-time gate: an allocation and lock spy test binary (house `--wrap=malloc` + `operator new` pattern) rendering at least 30 s of sequenced patterns with automation storms, pattern and preset swaps, tempo changes and prepare/process races, asserting 0 allocations. Add `tsan` and `asan` presets (LTO off). Optionally add rtcheck as a second interposer.
16. Release builds: `-static-libstdc++ -static-libgcc`, `-flto=auto`, stripped. Build in an `ubuntu:22.04` container for a glibc 2.35 baseline (the native 24.04 build needs 2.38). Install-user script copies to `~/.vst3`, `~/.clap`, `~/.lv2`. `.deb` + AppImage per house packaging.
17. Licence the project AGPL-3.0-or-later with SPDX headers. Record third-party licences (VST3 SDK MIT, CLAP MIT, CJE MIT, LV2 ISC, fonts OFL, Open303 MIT if used) in a `LICENSES.md`. Ship no D16 content.
18. Keep a `host-matrix.md` with the 12 manual rows of §7.5 for REAPER, Renoise, Bitwig, Ardour and Carla. It is a soft gate during development and a hard gate at release.
