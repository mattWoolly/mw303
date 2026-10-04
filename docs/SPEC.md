# mw12F build spec

mw12F is a Linux-native emulation of a classic 1982 bass line synth, built as VST3, CLAP, LV2 and a standalone app. The name is the model number in hex. The feature target is the control surface of the best commercial emulation of that instrument, plus the circuit mods that emulation leaves out.

This file is the contract between the people and agents building it. The research behind every number is in `docs/research/` (seven documents, 585 KB). Read the ones your module depends on in full before writing code. Where this spec and a research document disagree, this spec wins; where the spec is silent, the research document's "Implications for the build" section applies.

## 1. Rules that apply to everyone

1. **No original-manufacturer brand names** in code, comments, parameter names, preset names, UI text or user docs. That covers the original instrument, its maker, the commercial plugin used as the parity target, and the pedal brands the distortion voicings are modelled on. Say "the original unit", "the stock circuit", "the service notes". Component designators from the schematic (C13, TM3, Q8) are fine. `docs/research/` is exempt because it has to cite its sources.
2. **No em dashes** in anything you write: code comments, docs, reports. Use commas, colons, periods, parentheses.
3. **Real-time rules** for anything reachable from an audio callback: no allocation, no locks, no system calls, no exceptions thrown, no unbounded loops. Size every buffer in `prepare()`. Prove it with `acid::test::ScopedAllocGuard` (see `tests/support/AllocGuard.h`) in your tests.
4. **Derive every coefficient from the sample rate and component values at prepare time.** Nothing may be tuned for one rate. Tests run at 44.1, 48, 96 and 192 kHz.
5. **Determinism.** Same inputs, same output, on every run. Randomness comes from `acid::Rng` with explicit seeds. Rendering in blocks of 1, 64 or 4096 samples must give the same audio (bit-identical where the algorithm allows, otherwise within 1e-6).
6. **Style.** C++20, namespace `acid` (`acid::dsp`, `acid::fx`, `acid::seq`). Match `libs/acid-core/include/acid/common/*.h`: PascalCase types, camelCase functions and variables, a space before call parentheses, `noexcept` on audio-path functions, doc comments that say what a caller needs to know. Double precision for filter and envelope state; float is fine for audio buffers at the module boundary.
7. **Zero warnings** under the flags in `libs/acid-core/CMakeLists.txt`. No `-ffast-math`.
8. **Licence hygiene.** The model is derived from circuit equations and public analysis. Do not copy code from GPL projects or from repositories without a licence. Reference clones under the scratchpad `refs/` directory are for reading only. MIT sources may be used as test references with attribution.
9. **Stay in your files.** Each workstream owns the paths listed for it in section 8. Do not edit another workstream's files or the contract headers. If a contract header is wrong or insufficient, work around it locally and say so in your report.
10. **Report honestly.** Your final report lists what is done, what is not, which tests exist and whether they pass (with the command), and every place you deviated from this spec or the research. A known gap reported is fine. A gap discovered later is not.

## 2. What ships in v1

Everything tagged MUST in the parity checklist (`docs/research/01-phoscyon2-feature-inventory.md`, "Parity checklist"), most of the SHOULD items, and a Mods layer the parity target lacks.

- **Voice:** circuit-derived oscillator (saw, and a square from the Q8 shaper model), nonlinear 4-pole diode ladder solved with zero-delay feedback, main envelope, accent network with C13 accumulation, slide, VCA with gate shaping, coupling filters. 13 front-panel knobs, waveform switch, two couple switches, extended tuning.
- **Calibration panel:** 15 trimmers in physical units, with calibration presets.
- **Mods:** filter overdrive, filter FM, filter tracking, amp decay and sustain, soft attack, high resonance, accent sweep speed, muffler, extended ranges, sub oscillator. One switch returns everything to stock.
- **Effects:** a parametric distortion (8 clipper families, at least 24 voicings, user-editable voicing, one-knob dynamics pre or post), 4-band EQ, tempo-synced stereo delay with an in-loop filter, early/late reverb, two-line chorus, limiter with soft clip. All six blocks reorderable.
- **Sequencer:** 48 pattern slots of 1 to 64 steps; note, tie, rest, octave, accent, slide, vibrato, probability and ratchet per step; per-pattern rate, shuffle, gate length and direction; queued switching; host-locked playback with an internal clock when the host is stopped; Live and Host Note trigger modes; live overrides; live transpose.
- **Arpeggiator:** 48 template patterns that index the held notes, with the same per-step flags.
- **Generator:** note and octave masks with root, attribute toggles, seed, and four density knobs that work without re-rolling.
- **MIDI:** external play mode (overlap slides, velocity accents, pitch bend, mod wheel vibrato), MIDI out from the sequencer and arpeggiator, MIDI learn, MIDI file export by drag and by button, MIDI file import.
- **Presets:** scene, synthesis, calibration, distortion voicing, per-effect, sequencer pattern, arpeggiator pattern and generator template types; a browser with tags, search and favourites; original factory content.
- **GUI:** vector drawn, scalable 75 to 200 %, light and dark themes, piano-roll and keyboard-style step editors.
- **Quality tiers:** Draft, Normal, High, Ultra, chosen separately for real-time and offline rendering.

Deferred, with the data model left room for them: song mode, a modulation matrix, importing other products' pattern files, audio input to the filter.

## 3. Layout and ownership

```
libs/acid-core/include/acid/   public headers, one directory per module
libs/acid-core/src/<module>/   sources (globbed, no CMake edits needed)
tests/<module>/                Catch2 tests (globbed), one executable per module
tests/support/                 AllocGuard, Wav I/O
tools/                         offline render tool and analysis scripts
plugin/                        the only JUCE code: processor, state, presets, editor
assets/                        fonts, factory content
docs/research/                 research (read-only)
```

Modules and their CMake targets: `acid_dsp`, `acid_fx`, `acid_seq`, `acid_engine` (links the other three), `acid_common` (header-only).

Contract headers, owned by the architect and read-only for everyone else:

- `acid/engine/ParamTable.h`: every host parameter, with id, range, default and unit.
- `acid/engine/Settings.h`: saved settings that are not host parameters, including the effects order.
- `acid/seq/Pattern.h`: steps, patterns, the bank, generator parameters.
- `acid/seq/PlayerTypes.h`: transport, MIDI message, voice event, mode enums.
- `acid/common/*.h`: math helpers, `Rng`, `SpscQueue`.

## 4. Signal flow

```
MIDI in + host transport
        |
   seq::Player  (sequencer | arpeggiator | external-note handler)
        |  VoiceEvents, sample-accurate            -> MIDI out
   dsp::Voice   (mono; oscillator, filter, VCA run oversampled, then decimate)
        |  mono
   fx::FxChain  (six blocks in Settings::fxOrder; stereo from the first stereo effect on)
        |
   master volume, meters -> stereo out
```

The engine splits each host block at voice-event offsets, so the voice only ever sees events at the start of a `process()` call.

## 5. Threads

- **Audio thread:** `Engine::process()` and everything under it. Reads a `ParamValues` snapshot and `Settings` once per block.
- **Message thread:** owns the editable model (pattern bank, settings, presets). Publishes whole `PatternBank` and `Settings` snapshots to the audio thread through a wait-free triple buffer. Never mutates anything the audio thread reads in place.
- **Audio to UI:** playing step, playing and queued slot, run state, meter levels and accent-capacitor charge go through atomics or an `SpscQueue`, polled by one UI timer at 30 Hz.
- **Any non-audio thread** may call state save and load (hosts do this from background threads). The model is guarded by a mutex that the audio thread never takes.
- `prepare()` can race with `process()` in real hosts. The plugin must output silence until prepared and must not let prepare write state the audio thread is reading.

## 6. Module requirements

Signatures below are the minimum each module must expose. Owners may add to them.

### 6.1 `acid::dsp` (voice)

Sources: research documents 02, 03 and 04, all of them, in that order of authority for facts (04 for algorithms).

```cpp
struct VoiceParams;   // every Synth, Calibration and Mods row of ParamTable.h, natural units, plus oversampling-independent extras
class Voice
{
public:
    void prepare (double sampleRate, int maxBlockSize, int oversampling);  // oversampling: 1, 2, 4 or 8
    void reset() noexcept;                             // panic: every state to rest, including the accent capacitor
    void setParams (const VoiceParams&) noexcept;      // once per block; smoothing is internal
    void noteOn (int midiNote, bool accent, bool legato, bool vibrato) noexcept;
    void noteOff() noexcept;
    void setPitchBend (double semitones) noexcept;
    void setVibratoWheel (double depth01) noexcept;
    void process (float* out, int numSamples) noexcept;   // mono, base rate
    int latencySamples() const noexcept;
    VoiceMeters meters() const noexcept;               // accent charge, envelope level, gate state, for the UI
};
VoiceParams voiceParamsFrom (const ParamValues&) noexcept;   // lives in acid_dsp; includes ParamTable.h
```

Required behaviour, each with an automated test (the acceptance target tables in documents 02 and 03 are the test list):

- Filter: the nonlinear TPT diode ladder of document 04 section 1.7, with the 18 nF bottom capacitor, Q21 load and coupling state, and the feedback network inside the solve. Frequency input is the resonant frequency. Linearised response must match the analytic transfer function; sub-audio loop gain must stay below 0.9 at every legal trim setting (target F11b).
- Oscillator: generated at the oversampled rate with polyBLEP; square from the two-state Q8 model, so duty falls with pitch (about 0.57 at C1 to 0.35 at C5 at the default trim). No phase reset at note-on.
- Envelopes, accent, slide: exact exponential updates from the component values in document 03. Accent forces the short decay through the accent latch (held across ties and rests). The accent sweep is the two-node network with the diode and the 3.3 V rest level; resonance position is the tap; C13 charge persists across notes. Slide is a 22 ms one-pole on pitch in octaves, engaged for the whole destination note.
- Gate shaping: turn-on delay that depends on the preceding off time, fast rise, 8 ms hold and 8 ms fall. `cal_vca_clicks` off replaces this with a click-free ramp.
- Knob laws live here. `cutoff`, `decay`, `envmod`, `slide_time` and the rest map percent to physics so that the documented stock ranges sit inside the knob travel. `slide_time` 50 % is the stock 22 ms. `acc_decay` 0 % is the stock accent decay. With `sweep_couple` on, resonance sets the accent tap as in the circuit; off, `sweep_amt` sets it. With `accvol_couple` on, `accent` drives both filter and amplitude accent; off, `acc_vol` drives amplitude.
- Vibrato: the trill-with-slide trick from the parity target (document 01 section 1.3). Pitch target alternates around the note at `vib_speed`, spread `vib_depth`, smoothed by the slide lag. Active on notes flagged vibrato and scaled by the mod wheel in external mode.
- Mods are inert when `mod_on` is 0: the voice must be sample-identical to stock whatever the mod knobs say.
- `cal_c21` is the coupling capacitor in nF against the 222 k source resistance (15.9 nF gives about 45 Hz).
- Output level: a plain note at default settings peaks near -12 dBFS.
- CPU: at 48 kHz with 4x oversampling the voice must run faster than 15x real time on one core of this machine. Report the measured figure.

Also deliver `tools/analysis/` Python scripts (numpy, scipy, matplotlib are installed) that plot filter responses, envelope and accent curves, and waveforms from rendered data, and write PNGs you have looked at.

### 6.2 `acid::fx` (effects)

Sources: document 01 section 3 for the control surface, document 05 section B.3 for the clipper circuits.

```cpp
struct FxParams;      // every Distortion, EQ, Delay, Reverb, Chorus, Limiter and Master row, plus fxOrder, tempo
class FxChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void setDistortionOversampling (int factor) noexcept;   // 1, 2, 4, 8; switching must not allocate
    void setParams (const FxParams&) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;  // in place; the engine copies mono to both
    int latencySamples() const noexcept;
    FxMeters meters() const noexcept;                  // output peak L/R, limiter gain reduction
};
FxParams fxParamsFrom (const ParamValues&, const Settings&, double bpm) noexcept;

struct DistortionVoicing { const char* name; /* one value per Distortion Voicing parameter */ };
std::span<const DistortionVoicing> distortionVoicings() noexcept;   // index 0 is "Custom"
```

- Distortion: one engine. Signal path: optional clean low band split (`dist_low_keep`), pre high-pass, pre EQ, drive (interpolated between `dist_pre_min` and `dist_pre_max` by `dist_drive`), clipper, tilt, colour filter (swept by `dist_color`, neutral at 0), post high-pass, trim. Eight clipper families named in `ParamTable.h`: symmetric diode pair, a slew-limited op-amp hard clipper, an asymmetric soft overdrive, a hard-clip stomp box with a mid scoop, a mid-hump soft clipper, a gated transistor fuzz, a console input stage, a wavefolder. Oversampled 1x to 8x with band-limited resampling. At least 24 voicings with names of our own.
- Dynamics: one-knob compressor, placed before or after the clipper by `dist_dyn_post`.
- EQ: four peaking bands, width in octaves.
- Delay: stereo, independent L and R times as fractions (n/d of a whole note, with dotted and triplet), free time when unsynced, feedback filter Off/LP/BP/HP inside the loop, stereo spread, click-free time changes, buffer sized for 32/1 at 40 BPM capped at a sane maximum with graceful clamping.
- Reverb: separate early and late sections sized in metres, diffusion and brightness per section, late feedback, tail modulation, pre-delay.
- Chorus: two modulated lines (second line level is Fatness), wet high-pass, spatial phase offset.
- Limiter: knee, ratio 2 to 40, attack, release, threshold, make-up, optional soft clip.
- Each block bypasses click-free and costs nothing when off. `fx_bypass` skips all six. Master volume and meters are applied by the engine, not here.
- Any order of the six blocks must work. Mono-in blocks placed after a stereo block must process both channels.

### 6.3 `acid::seq` model (patterns, edits, generator, codecs)

Source: document 06 sections 1, 5, 7, 8.

- `PatternOps`: shift left/right with wrap over `length`, transpose with wrap inside the octave range, reverse, clear, copy a step range, duplicate the first half into the second, set length, arpeggiator inversion, and the lossless conversion to and from the pitch-list/time-list view.
- `Generator`: `generate (Pattern&, std::uint64_t seed)` fills per-step candidates and magnitudes for every unlocked lane; `applyDensities (Pattern&)` re-derives step states and flags from the stored magnitudes and the four amounts in `GenParams`, with no randomness. Document 06 section 5.8 is the algorithm. Changing a density must only add or remove events, never move the ones already showing.
- `PatternCodec`: a lossless, versioned text encoding of one `Pattern` (every field), used inside preset and state files; unknown trailing fields are ignored on read.
- `MidiFile`: write a pattern (or a recorded arpeggiator take) as a type-0 Standard MIDI File with configurable accent and normal velocities, slides as overlaps, ties as merged notes; read a monophonic MIDI clip back into a pattern using the heuristics in document 06 section 7.1.
- `Scales`: the generator template library (at least 50 masks: scales, chords, intervals) with names of our own.

### 6.4 `acid::seq` player (playback)

Source: document 06 sections 2, 4, 6, 9; document 07 section 3; document 03 section 8 for gate timing.

```cpp
struct PlayerControls;   // play mode, run, bank and slot, live transpose, the eight overrides
struct PlayerConfig;     // the sequencer-related fields of Settings
class Player
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process (const TransportInfo&, std::span<const MidiMsg> midiIn, const PatternBank&,
                  const PlayerControls&, const PlayerConfig&, int numSamples, PlayerOutput& out) noexcept;
};
struct PlayerOutput;     // fixed-capacity arrays of VoiceEvent and MidiMsg (MIDI out), plus status for the UI
```

- `PlayheadTracker`: treats a missing transport as "no news" and extrapolates; detects relocation by discontinuity; falls back to a sample-counting internal clock when the host is stopped and Run is on, and in the standalone. The nine tests in document 07 section 3.3 are required.
- Step timing is computed from PPQ in closed form, so playback is sample-accurate, block-size invariant, and stays locked to the host position (`Settings::hostLink`).
- Authentic gate: half a step plus 1.35 ms (`Pattern::gateLength == 0`), otherwise a percentage of the step; a non-legato gate never reaches the next onset. Shuffle delays every second step: at 100 % the pair ratio is 75:25. Triplet and dotted divisions ignore shuffle.
- Slide uses source semantics. The next Note gets `legato = true` and its own accent flag. Slides wrap across the loop and across queued pattern changes. The three `SlideIntoRest` policies are implemented. Ties hold the gate with no new event. NoteOff precedes NoteOn at equal offsets.
- Probability and Random direction use a hash of (pattern seed, absolute step index), so offline and real-time renders match and block size does not matter.
- Queued pattern switching per `LaunchQuant`. Host Note mode: 48 notes from `hostNoteBase` select and start patterns; note-off stops.
- Live overrides: Set forces the attribute on every step while held, Clr removes it; Note Set makes every step sound, Note Clr mutes. Non-destructive.
- Arpeggiator: held notes sorted low to high, up to 10; a step's `arpIndex` picks one, a missing index falls back to index 1, `octave` offsets it; the cursor runs while any key is held and resets when all are released; accent, slide and vibrato come from the step.
- External mode: monophonic with `NotePriority`; a note-on while another is held is a legato NoteOn; releasing back to a held note slides back; velocity above `accentThreshold` accents; pitch bend scaled by `bendRange`; CC1 to `VibratoDepth`; CC64 holds the gate; all-notes-off and reset handled.
- MIDI out mirrors what the voice plays: configured velocities, overlapping notes for slides, CC1 for vibrato steps. Hanging notes are impossible by construction: every stop path sends the note-off.
- The 28 edge cases in document 06 section 9.5 each get a test.

### 6.5 `acid::engine`

The interface is the contract header `acid/engine/Engine.h`. The engine owns the triple buffers, maps `ParamValues` and `Settings` to the module parameter structs, drives `Player`, `Voice` and `FxChain`, applies quality tiers and master volume, splits blocks at voice-event offsets and at the player's overflow-free block size, zeroes the vibrato wheel outside external mode, and publishes UI state. It ships with `tools/acid-render`, a command-line renderer used for golden tests, factory-content authoring and listening: it reads a `.mw12f` file of type `scene`, `synth` or `pattern` (or several, applied in order), renders a given number of bars at a given tempo and sample rate to a WAV, and can also take a pattern in the compact step notation on the command line.

### 6.6 Plugin and editor

Built after the engine. Class names follow the house style: `mw12FProcessor`, `mw12FEditor`, `mw12FLookAndFeel`. Document 07 is the rule book: playhead handling, MIDI buffer staging, state envelope and migrations, the CLAP rescan call after state load, no modal loops, no keyboard-focus grabs, embedded fonts, user zoom, validators.

### 6.7 State and preset files

One XML envelope serves the plugin state, every preset type and the factory content. Text values use the C locale (a period as decimal separator), parameters are written by string id in table order, and output is byte-deterministic (same state, same bytes).

```xml
<mw12F schema="1" type="scene" name="Rubber Floor" author="mwAudio" tags="Seq,Saw,Medium" description="...">
  <params>
    <p id="cutoff" v="35"/>            <!-- natural units, as in ParamTable.h -->
  </params>
  <settings trigger_mode="live" launch_quant="pattern_end" slide_into_rest="release" ... fx_order="0,1,2,3,4,5"/>
  <bank>
    <seq slot="0">ENCODED</seq>        <!-- PatternCodec::encodePattern; empty slots omitted -->
    <arp slot="0">ENCODED</arp>
  </bank>
  <ui zoom="1" theme="night" edit_view="roll" .../>   <!-- plugin state only -->
  <midi_map><cc number="74" param="cutoff"/></midi_map> <!-- plugin state only -->
</mw12F>
```

`type` is one of `state` (the plugin's own session state: everything), `scene` (params + settings + bank), `synth` (the Synth, Calibration, Mods, Distortion, Distortion Voicing, effect and Master groups, and the effects order), `calibration`, `distortion` (front panel plus voicing), `eq`, `delay`, `reverb`, `chorus`, `limiter` (that group only), `pattern` (one `<pattern>ENCODED</pattern>`), `arp` (same, an arpeggiator pattern), `template` (one `<template>` element holding the generator fields of `GenParams` as attributes: `note_mask`, `root`, `octave_mask`, `slide`, `accent`, `vibrato`, `amount_note`, `amount_slide`, `amount_accent`, `amount_vibrato`). A file carries only the elements its type needs. Loading applies only what the file carries.

Rules: unknown elements and attributes are ignored and preserved when the plugin re-saves its own state; missing parameters keep their current value when a preset is applied and take their default when a session state is restored; every number is range-checked against the table; `schema` increments on any incompatible change, with a migration function per step and golden files per schema.

File extension `.mw12f` for every type. User files live in `$XDG_CONFIG_HOME/mwAudio/mw12F/<Type>/` (default `~/.config/...`). Factory files live in `assets/factory/<type>/` in the source tree and are embedded in the binary.

## 7. Open physics, and how the build treats it

The research could not settle these from documents alone. Each is a trimmer with a default, so a future session with hardware recordings can settle it by measurement:

| Unknown | Default | Trimmer |
|---|---|---|
| Q21 input resistance (where full resonance oscillates) | 20 k (raised from 13.6 k so the default meets doc 02 targets F8 and F12) | `cal_rb`, 10 to 35 k |
| Maximum loop gain | 18.7 | `cal_kmax`, 17 to 20 |
| Octaves per volt at the filter summing node | 0.88 of schematic | `cal_cv_scale` |
| Effective bass corner at the VCA input (45 or 72 Hz) | 15.9 nF, about 45 Hz | `cal_c21` |
| Accent amplitude boost | 1.1 V, about +11 dB at full accent | `cal_accamp` |
| Accent decay (72 ms by parts, 85 ms by the factory spec) | 80 ms | `acc_decay` |
| VCA attack (0.5 or 3 ms) | 0.5 ms | `mod_soft_attack` |
| Slide into a rest | Release | `Settings::slideIntoRest` |

Distortion oversampling is fixed per quality tier, not per clipper family, so the latency a host compensates for never moves while audio runs. The families that alias most (Fuzz, Fold, Console) use antiderivative antialiasing; the High tier gives them 8x at 44.1 and 48 kHz.

## 8. Workstreams

| Workstream | Module | Owns | Build directory |
|---|---|---|---|
| Filter | `acid_filter` | `include/acid/dsp/DiodeLadder*`, `src/filter/`, `tests/filter/` | `build-filter` |
| Oscillator | `acid_osc` | `include/acid/dsp/{Oscillator,SquareShaper,Decimator}*`, `src/osc/`, `tests/osc/` | `build-osc` |
| Voice | `acid_dsp` | the rest of `include/acid/dsp/`, `src/dsp/`, `tests/dsp/`, `tools/analysis/` | `build-dsp` |
| Distortion | `acid_dist` | `include/acid/fx/{Distortion,Dynamics,Oversampler}*`, `src/dist/`, `tests/dist/` | `build-dist` |
| Inserts | `acid_inserts` | `include/acid/fx/{Eq,Delay,Reverb,Chorus,Limiter}*`, `src/inserts/`, `tests/inserts/` | `build-inserts` |
| Effects chain | `acid_fx` | `include/acid/fx/FxChain*`, `src/fx/`, `tests/fx/` | `build-fx` |
| Sequencer model | `acid_seq` | `include/acid/seq/{PatternOps,Generator,PatternCodec,MidiFile,Scales}*`, `src/seq/`, `tests/seq/` | `build-seq` |
| Player | `acid_player` | `include/acid/seq/{Player,PlayheadTracker,Arpeggiator,ExternalNotes,StepClock}*`, `src/player/`, `tests/player/` | `build-player` |
| UI kit | (JUCE) | `plugin/ui/`, `plugin/ui-gallery/`, `assets/fonts/` | `build-ui` |

Every workstream configures its own build directory and builds only its own targets, so parallel builds never collide and a half-written file in one module cannot break another:

```
cmake -S . -B build-<name> -G Ninja -DMW_BUILD_PLUGIN=OFF
cmake --build build-<name> --target acid_<module>_tests && build-<name>/tests/acid_<module>_tests
```

Headers for all modules live under `libs/acid-core/include/acid/`. A module may include another module's headers only where the table of CMake targets in `libs/acid-core/CMakeLists.txt` says it links it.

## 9. Decisions made during the build

Recorded so the next person does not re-litigate them. Each was measured before it was decided.

- **Ladder load default is 20 k**, not 13.6 k. At 20 k the stock calibration meets document 02 targets F8 and F12 and keeps the sub-audio loop gain under 0.9 at every legal trim (F11b). The range stays 10 to 35 k.
- **Distortion oversampling is fixed per quality tier.** Latency must not move while audio runs, so the factor never follows the clipper family. Fuzz, Fold and Console use antiderivative antialiasing instead; the High tier gives them 8x at 44.1 and 48 kHz.
- **A distortion model is a set of voicing values**, copied into the voicing parameters on the message thread. Editing any voicing parameter by hand switches the model to Custom without changing values; the custom voicing is kept in the session and comes back when Custom is chosen again. Voicing trims are fitted on the instrument's own output (BS.1770 loudness), not on synthetic signals.
- **The voice models its output amplifier's rails**: exactly linear to 0 dBFS, soft to about +6 dBFS, as the hardware clips on hot accents. After master volume the engine has a safety clip that cannot be turned off: linear to +6 dBFS, never above +12 dBFS, non-finite samples become 0.
- **Quality tier and the offline flag are read only in prepareToPlay.** A change that arrives later waits for the next prepare, which the plugin requests only while stopped. Reported latency changes only inside prepare.
- **Offline renders are reproducible.** In non-realtime mode the voice and effects reset at a transport start, at a seek and on the first offline block; patterns, parameters and the player are untouched.
- **LV2 song position is rebuilt** from frame, tempo, bar and time signature, because the JUCE 8.0.15 LV2 wrapper drops the beat position (document 07 section 3.1 is wrong on this point).
- **MIDI output is delayed by the reported latency** in VST3 and CLAP, so it lines up with the audio the host compensates.
- **Text from files is never trusted.** Names are repaired to valid UTF-8 at every entry point, and the editor converts all external text through one checked helper, because `juce::String (std::string)` in JUCE 8.0.15 overflows its buffer on invalid UTF-8.
- **The editor window never resizes in response to a parameter.** Only the user's zoom choice or a host resize changes its size; external play mode swaps the sequencer area in place. Resizing on automated parameter changes crashed pluginval's window embedding and invites trouble in real hosts.
- **`assets/factory/default.mw12f` is a `state` document**, so it never appears in the preset browser; the same scene is listed once as a scene preset.
- **Settings gained `internalBpm`** (20 to 300) for the standalone and for hosts that report no tempo.
- **pluginval's LV2 "Plugin state restoration" check is excluded from the randomised pass only.** A stock JUCE 8.0.15 plugin fails it on the same seeds, so the fault is in pluginval's LV2 hosting. The fixed-order LV2 run still includes it and passes.
