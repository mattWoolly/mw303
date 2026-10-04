# mw12F feature parity

QA parity pass, 2026-10-03. Every row below was checked against the product as it stands: the code in `libs/acid-core` and `plugin/`, the running editor (rendered headless with the gallery tool, `xvfb-run -a build-plugin/plugin/ui-gallery/mwacid_ui_gallery_artefacts/Release/mwacid_ui_gallery <dir>`), offline renders through `acid-render`, the factory files, and the test suites built fresh in `build-qa-parity`. Nothing is taken from earlier reports.

"The reference plugin" is the commercial emulation the feature inventory (docs/research, document 01) describes. Its 132-item parity checklist (134 rows: 78a and 78b are extra) is section A. Section B is what docs/SPEC.md section 2 promises for v1, section C the circuit mods, section D the claims in `assets/factory/README.md`.

Status: **done** (present and working), **partial** (present but short of the item), **missing**. Tags are the checklist's: MUST, SHOULD, NICE; "(+)" marks an improvement over the reference plugin.

QA found four gaps that have tests in `tests/plugin/review_qa_parity_editor.cpp` (named "qa parity: ..."). Since stage 5 that file is built and run by ctest (`mwacid_qa_editor_parity_tests` from plugin/ui-gallery, and `mwacid_plugin_review_qa_editor_tests`), and all four pass.

## Summary

Section A, after the stage-5 fixes (final verification, 2026-10-03):

| | done | partial | missing |
|---|---|---|---|
| MUST (88 rows) | 88 | 0 | 0 |
| SHOULD (41 rows) | 31 | 8 | 2 |
| NICE (5 rows) | 2 | 0 | 3 |

At QA time it was MUST 84/2/2 and SHOULD 29/8/4. Stage 5 closed MUST 60, 80, 81 and 112, SHOULD 42 and 68, and SPEC section 2's B.9 (per-step ratchet). Item 123 (75 % minimum zoom against the checklist's 60 %) follows SPEC section 2, which sets 75 %, so it is counted done with the deviation recorded. Item 131 is done as a capability (all three formats build, load and render, and the third release validation run passed everything), but the release gate it relies on is still open: pluginval VST3 crashed in 10 of 44 randomised runs (row 131 has the evidence). Still open below MUST: SHOULD 61 and 64 (missing), 72, 88, 96, 113 to 116 and 118 (partial); NICE 77, 78 and 83 are deferred by SPEC section 2.

Checks run in the parity build: `ctest` in build-qa-parity, 523 of 523; `scripts/validate.sh build-qa-parity` (single pluginval pass, no churn): all PASS.

Release verification, 2026-10-03, on fresh `build-all` and `build-plugin` trees (Release): the nine core test executables run 521 test cases, 519 pass; the two failures are QA review cases still open (`review_qa: switching the distortion tilt mid-note does not step the output` and `review_qa: voicings stay level-matched to Custom on the real voice`, Tin Whistle 11.5 dB under Custom). The six plugin test executables pass all 85 cases. `scripts/validate.sh build-plugin --release` FAILS: pluginval VST3 crashes (SIGSEGV in the host's XEmbedComponent during Editor Automation, also seen in 1 of 3 single passes) and pluginval LV2 fails Plugin state restoration in every `--repeat 3 --randomise` run (it passes single, repeat-only and randomise-only runs); clap-validator, lv2_validate, both Carla loads and the branding audit PASS. `tools/factory/qa.py`: 1,726 jobs, 0 failures.

Final verification after the stage-5 fixes, 2026-10-03, on `build-all` (`-DMW_BUILD_PLUGIN=OFF`) and `build-plugin` (Release) deleted and configured again:
- Build: zero compiler or linker warnings in either tree (not even in dependencies).
- Core: the nine core test executables run 543 test cases and all pass, in both trees, including every review_qa case in tests/dist and tests/engine. One finding fixed during verification: the MIDI import report gave `notesRead` 0 when the lossless sidecar was used (row 82); a new case in tests/seq pins it.
- Plugin: the eleven plugin test executables run 141 cases and all pass, every review_qa case included (HostRender, Robustness, Ux, EditorRobustness, parity editor).
- Timing: `acid_fx_tests` "review: no single block blows the real-time budget" failed once with 9 suites running in parallel on a machine at load 17 from other work (175.6 us against a 166.7 us limit). Twelve runs on their own measured 42 to 67 us.
- `scripts/validate.sh build-plugin --release`, three runs, 124 s, 109 s and 128 s wall. Runs 1 and 2 FAIL on pluginval VST3 (SIGSEGV in Editor Automation); run 3 passes everything. pluginval LV2 (fixed order and churn), clap-validator, lv2_validate, both Carla loads and the branding audit pass in all three.
- `tools/factory/qa.py`: 0 failures (row D.14). `scripts/branding-audit.sh --binaries`: clean.

## A. Parity checklist

### Synthesis

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| 1 | Wave switch Saw/Square | MUST | done | `wave` Choice Saw/Square (ParamTable.h); WAVE switch in the OSCILLATOR section of the rendered editor |
| 2 | Tuning ±12 st continuous, centre A=440 | MUST | done | `tune` -12..12 st. Render of an A at octave 0: 110.00 Hz at 0, 220.00 at +12, 54.98 at -12 (expected 110, 220, 55) |
| 3 | Ext tuning: coarse ±12 stepper + fine ±100 ct | SHOULD | done | `tune_ext`, `tune_coarse` Int ±12, `tune_fine` ±100 ct; EXT lamp swaps the knob for TRANS readout and FINE knob (editor-fx-full render) |
| 4 | Cutoff 0..100 % | MUST | done | `cutoff` 0..100 %, knob in FILTER |
| 5 | Resonance 0..100 % | MUST | done | `resonance`, knob in FILTER |
| 6 | Env Mod 0..100 % | MUST | done | `envmod`, knob in FILTER |
| 7 | Decay (non-accented only) | MUST | done | `decay`; accent forces the short decay through the latch (Voice, tests/dsp/TestEnvelope.cpp) |
| 8 | Accent (filter accent level) | MUST | done | `accent`, ACCENT knob |
| 9 | Slide Time | MUST | done | `slide_time` (50 % = stock 22 ms), SLIDE TIME knob |
| 10 | Sweep Amount | MUST | done | `sweep_amt`, SWEEP knob |
| 11 | Resonance/Sweep couple, default on | MUST | done | `sweep_couple` default 1; couple LED drawn on RESONANCE and SWEEP |
| 12 | Env Attack | MUST | done | `env_attack`, ATTACK knob in FILTER |
| 13 | Accent Decay | MUST | done | `acc_decay`, DECAY knob in ACCENT |
| 14 | Accent Volume | MUST | done | `acc_vol`, VOLUME knob in ACCENT |
| 15 | Accent/Volume couple, default on | MUST | done | `accvol_couple` default 1; LEDs on ACCENT and VOLUME |
| 16 | Vibrato Speed | MUST | done | `vib_speed`, VIB SPEED knob; trill-with-slide in Voice |
| 17 | Vibrato Depth, smoothed by slide | MUST | done | `vib_depth`, VIB DEPTH knob |
| 18 | Coupling LEDs over both knobs; inactive knob disabled | SHOULD | done | Render: VOLUME and SWEEP captions dimmed while coupled; gallery checks "couple LED toggles the parameter", "sweep amount knob enabled when uncoupled" |

### Calibration

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| 19 | Hidden calibration panel under a cover, 9 trims | MUST | done | CoverPanel "CALIBRATION"; open render shows 15 trimmers (the 9 plus 6) |
| 20 | TM3 cutoff offset ±500 mV | MUST | done | `cal_tm3` -500..500 mV |
| 21 | C21/C22 coupling capacitor 7.9 nF..31.8 µF | MUST | done | `cal_c21` 7.9..31800 nF, log |
| 22 | Accent filter gain 0..4000 mV | MUST | done | `cal_accenv` 0..4000, default 1951 |
| 23 | Accent amp gain 0..3.5 V | MUST | done | `cal_accamp` 0..3.5, default 1.1 |
| 24 | C13 at least 0.46..1.64 µF | MUST | done | `cal_c13` 0.33..4.7 µF |
| 25 | TM5 octave width 0.9..1.1 | MUST | done | `cal_tm5` |
| 26 | Square PW shift ±100 % | MUST | done | `cal_pw` |
| 27 | VCA clicks on/off | MUST | done | `cal_vca_clicks`, VCA CLICKS lamp |
| 28 | VCA noise 0..100 %, gated by the VCA | MUST | done | Render, one note then 15 rests: noise 100 % adds a signal 21 dB under the note while it sounds and nothing in the rests (-184 dB difference) |
| 29 | Calibration presets, own browser, at least 9 units | SHOULD | done | 22 factory units; PRESET box on the calibration panel opens the Calibration type |
| 30 | Cover animation with an off switch | NICE | done | Settings, GUI, "Animate calibration cover" (global) |

### Distortion and effects

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| 31 | Distortion: active, model, drive, colour ±100 | MUST | done | `dist_on`, model box, `dist_drive`, `dist_color` -100..100 |
| 32 | One-knob dynamics, pre/post switch | MUST | done | `dist_dynamics`, `dist_dyn_post`; DYNAMICS knob and PRE/POST switch |
| 33 | At least 3 clipper families with voicing parameters | MUST | done | 8 families (`dist_clipper`); voicing rows pre-HP, pre-EQ, tilt, bias, knee, drive range, colour filter, post-HP, trim |
| 34 | At least 19 factory voicings | MUST | done | 32 built-in voicings plus Custom (DistortionVoicings.cpp), all in the model box |
| 35 | User-editable, savable custom voicing | SHOULD (+) | done | FULL view of the DIST tab edits every voicing row; Distortion preset type saves it |
| 36 | Extra clipper families (hard-clip stomp, mid-hump overdrive, fuzz) | SHOULD (+) | done | Stomp, Mid Hump, Fuzz, Console, Fold families |
| 37 | Five inserts with on LEDs | MUST | done | EQ, DELAY, REVERB, CHORUS, LIMIT tabs, each tab LED is the on switch |
| 38 | Insert chain reorderable by dragging tabs | MUST | done | TabBar drag; gallery "tab reorder writes the effects order" |
| 39 | Distortion reorderable too | SHOULD (+) | done | DIST is a draggable tab in the same rack; `fxOrder` is a permutation of all six |
| 40 | EQ: 4 bands, 20 Hz..16 kHz, 0.5..4 oct, ±24 dB | MUST | done | `eq_*` rows match |
| 41 | Delay: sync, L/R n/d 1..32 with Full/Dotted/Triplet, free 0.01..1 s, feedback, in-loop Off/LP/BP/HP 10 Hz..16 kHz with reso, spread ±100, mix | MUST | done | `delay_*` rows match |
| 42 | Delay buffer-overrun protection and warning | SHOULD | done | Protection: Delay.cpp clamps to kMaxDelaySeconds and sets `timeClamped()`; Engine publishes `EngineStatus::delayClamped`. Warning (stage-5 fix): the editor reads `delayClamped`, turns the DELAY tab amber and shows "Delay time clamped: the synced time is longer than the delay line" (mw12FEditor.cpp, `setTabWarning`) |
| 43 | Reverb: pre-delay 0..1000 ms, early 1.1..20.5 m, late 5.1..51.1 m, diffusion and brightness per section, feedback, early/late, tail mod, mix | MUST | done | `rev_*` rows match |
| 44 | Chorus: offset, fatness, wet HPF, volume, depth, rate, spatial, mix | MUST | done | `cho_*` rows match |
| 45 | Limiter: knee, ratio 2..40, attack 0.1..100 ms, release 25..500 ms, threshold 0..-48 dB, make-up 0..24 dB, soft clip | MUST | done | `lim_*` rows match |
| 46 | Per-insert presets and save; basic/full view with the reference basic sets | SHOULD | done | Rack PRESET box opens that effect's type; BASIC/FULL selector; basic sets in FxPanels.cpp match (EQ 4 gains; delay feedback, cutoff, type, spread, mix; reverb pre-delay, early/late, feedback, mix; chorus rate, depth, spatial, mix; limiter attack, release, threshold, gain, soft clip) |
| 47 | Master: volume -inf..+12 dB, bypass all FX, stereo meter | MUST | done | `master_volume` -60..12, -60 is exact silence (Engine.cpp kSilenceDb); `fx_bypass`; stereo meter in MASTER |
| 48 | Pitch-bend range 0..12, default 2 | SHOULD | done | Settings `bendRange`; Settings, MIDI, "Pitch bend range" |

### Sequencer

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| 49 | Patterns 1..64 steps | MUST | done | `kMaxSteps` 64; LENGTH 1..64 |
| 50 | Per-step note or rest, octave -1..+3, slide, accent, vibrato; octave and flags kept on rests | MUST | done | Pattern.h Step; piano roll lanes; keyboard view |
| 51 | Rate 1/64..1/1 × Full/Dotted/Triplet | MUST | done | RATE box and FULL/DOT/TRIP selector |
| 52 | Shuffle 0..100 % | MUST | done | SHUFFLE readout |
| 53 | Step (gate) length 0..100 % | MUST | done | GATE readout, 0 shows STOCK (authentic gate), 1..100 % |
| 54 | 4 banks × 12 = 48 slots per scene | MUST | done | PatternBank 48 seq + 48 arp; selector A..D × 1..12 |
| 55 | Selector with playing (red) and queued (blue) LEDs | MUST | done | PatternSelector; render shows red PLAY lamp and NEXT readout |
| 56 | Switching quantised to the pattern end | MUST | done | LAUNCH box: Pattern end, Next bar, At once |
| 57 | Live trigger mode and Start/Stop | MUST | done | Trigger box "Live"; RUN button (`seq_run`) |
| 58 | Host Note mode: 48 notes, on plays, off stops | MUST | done | `TriggerMode::HostNote`; Player.cpp lines near 1000..1044 |
| 59 | Configurable Host Note base | SHOULD | done | Settings, Sequencer, "Host note base" (C-1..C5 in octaves) |
| 60 | MIDI-CC pattern control: learnable pattern 1..12, bank A..D, start/stop | MUST | done | Stage-5 fix: the selector's bank and pattern halves are bound to `pattern_bank` and `pattern_slot` and offer right-click Learn (SequencerPanel.cpp, PatternSelector.h); RUN was already learnable. Test "qa parity: pattern bank and pattern slot can be MIDI-learned from the editor" passes (final verification, `mwacid_qa_editor_parity_tests`, 4 of 4) |
| 61 | MIDI-learnable edit controls (step, transpose, shift, copy, note toggles...) | SHOULD | missing | Edit operations are plain buttons with no parameter behind them; no learn path |
| 62 | Host sync with continuous alignment | MUST | done | `Settings::hostLink`; "Host link" menu item; PlayheadTracker tests |
| 63 | Continuous alignment by default; internal clock when stopped | SHOULD (+) | done | `hostLink = true` default; internal clock with Run on (tests/player/TransportTests.cpp) |
| 64 | Pattern Follow toggle | SHOULD | missing | No setting. The edited slot is always `pattern_bank`/`pattern_slot`, which is also what plays, so a pattern can only be edited by queueing it |
| 65 | Piano roll: octave lane, 12-row note lane, attributes, drag drawing, play cursor, past 16 steps | MUST | done | PianoRoll.h; render shows lanes, red cursor column, PAGE strip 1-16..49-64 |
| 66 | Keyboard view: step LEDs, parts 1..4, step ◀▶, mini keyboard, octave LEDs, flag buttons | MUST | done | editor-keyboard render |
| 67 | Auto Increment | SHOULD | done | AUTO INC lamp in keyboard view |
| 68 | Note Preview | SHOULD | done | Stage-5 fix: ControllerPatternModel forwards `previewNote` to `Controller::previewNote`, which sounds the note through the processor. Test "qa parity: keyboard-view note preview sounds the entered note" passes |
| 69 | Shift ◀▶ with wrap, all attributes | MUST | done | SHIFT buttons, `Controller::shiftPattern` |
| 70 | Transpose ±1 with wrap | MUST | done | TRANSPOSE buttons, `transposePattern` |
| 71 | Whole-pattern copy, paste, clear | MUST | done | COPY, PASTE, CLEAR (system clipboard, lossless codec text) |
| 72 | Step-range copy/duplicate, undo/redo | SHOULD (+) | partial | Undo/redo yes (buttons and menu). DUP copies only the first half into the second; no arbitrary step-range copy in the editor although PatternOps has one |
| 73 | Live Override Note/Slide/Accent/Vibrato × Set/Clr, momentary, automatable | MUST | done | 8 `ovr_*` parameters; LIVE OVERRIDE buttons; gallery "mouse down on ACC SET forces the accent". Stage-5 fix: they stay visible under the GENERATE tab (editor-generator render, final verification) |
| 74 | Slide glides to the next note without retrigger | MUST | done | Player slide semantics, tests/player |
| 75 | Stock-strict slide-into-rest option | SHOULD (+) | done | Settings, "Slide into rest": Release, Hold, Sustain |
| 76 | Directions forward, reverse, ping-pong, random | SHOULD (+) | done | DIRECTION box |
| 77 | Pattern chaining / song mode | NICE (+) | missing | Deferred by SPEC section 2 |
| 78 | Per-step parameter locks / modulation lane | NICE (+) | missing | Deferred by SPEC section 2 (modulation matrix) |
| 78a | Explicit tie | SHOULD (+) | done | `StepState::Tie`; TIE button; drag-right ties in the roll |
| 78b | Stock-strict timing (gate ≈ 58 %, slide through the target note) | SHOULD (+) | done | Gate 0 = authentic gate (half a step plus 1.35 ms); slide held through the destination note |

### Export and import

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| 79 | Drag pattern as a MIDI clip with accent/normal velocities (96/32) | MUST | done | DRAG MIDI handle and EXPORT button; velocities from Settings (Accent/Normal velocity out); gallery "drag handle writes a MIDI file" |
| 80 | Drag a pattern in native format to disk or another instance | MUST | done | Stage-5 fix: dragging a pattern key, or Shift-dragging the MIDI handle, writes a .mw12f and starts an external file drag (`performExternalDragDropOfFiles` in PatternSelector.cpp and MidiDragHandle.cpp) |
| 81 | Drag-and-drop load of one or many patterns onto slots | MUST | done | Stage-5 fix: PatternSelector is a FileDragAndDropTarget; .mw12f and .mid files fill consecutive slots, one undo step per file. Test "qa parity: pattern files can be dropped onto the pattern selector" passes |
| 82 | Import MIDI files into patterns | SHOULD (+) | done | IMPORT button, `Controller::importMidiFile`, report notice. Final verification fixed the report for files this product exported: the lossless-sidecar path returned before `notesRead` was set, so the core report said 0 notes (MidiFile.cpp; pinned by "an import that uses the sidecar still reports the notes in the file" in tests/seq and review_qa_UxTests.cpp:89) |
| 83 | Import other products' pattern files | NICE | missing | Deferred by SPEC section 2 |

### Arpeggiator

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| 84 | ARP mode with index 1..10, octave, flags, same rate/length/shuffle/gate | MUST | done | editor-arpeggiator render: ten index rows, same tools column |
| 85 | Up to 10 held notes low to high, missing index → 1, cursor persists | MUST | done | Arpeggiator.cpp; tests/player/ArpTests.cpp |
| 86 | Separate 48 arp slots and arp presets | MUST | done | `PatternBank::arp`; ARP PATTERN preset type, 56 factory |
| 87 | Inversion edit | SHOULD | done | TRANSPOSE becomes INVERSION in ARP mode (`invertArpPattern`) |
| 88 | Randomize and MIDI-export arp output | SHOULD (+) | partial | Export yes (C minor triad by default). GENERATE is disabled for arp patterns (`SequencerPanel::setArpMode`) |

### Randomizer

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| 89 | Randomizer tab beside editing | MUST | done | EDIT/GENERATE tabs; roll stays editable |
| 90 | 12-class mask, octave mask, attribute toggles | MUST | done | editor-generator render |
| 91 | Draw | MUST | done | DRAW button; gallery "draw generated a line" |
| 92 | Four live density knobs, disabled when the part is off | MUST | done | NOTE, SLIDE, ACC, VIB; VIB dimmed with VIBRATO off; gallery "amount change keeps the seed" |
| 93 | Template transpose | MUST | done | ROOT readout rotates the mask; template arrows |
| 94 | At least 50 templates | MUST | done | 76 built-in (Scales.cpp) and 90 template presets |
| 95 | Root selector and scale dropdown | SHOULD (+) | done | ROOT and TEMPLATE box |
| 96 | Visible seed, re-roll, history, per-step lock | SHOULD (+) | partial | Stage-5 fix: the seed shows all 8 hex digits and a click types a seed; DRAW uses 32-bit seeds, so a shown seed redraws the same line. Still no history and no lock editing although `Step::locks` exists |
| 97 | Randomizer in keyboard view | NICE (+) | done | Tools column is independent of ROLL/KEYS |

### Play modes and MIDI

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| 98 | EXT / SEQ / ARP selector | MUST | done | MODE selector in the top bar |
| 99 | Sequencer hidden in EXT | SHOULD | done | editor-external render; gallery "sequencer hidden in external mode" |
| 100 | EXT: mono, overlap = slide, velocity > threshold = accent (default 64) | MUST | done | Player.cpp `velocity > accentThreshold`; tests/player/ExternalTests.cpp |
| 101 | Pitch bend in EXT | MUST | done | `bendRange`; ExternalTests |
| 102 | Mod wheel → vibrato depth in EXT | MUST | done | CC1 → vibrato wheel; ExternalTests |
| 103 | MIDI out of seq and arp notes, velocities 96/32 | MUST | done | `Player::mirrorNoteOn`; Settings velocities |
| 104 | MIDI out CC1 for vibrato, CC64 same-pitch slides, overlaps | SHOULD | done | CC1 per vibrato change; CC64 when a same-pitch slide changes velocity; overlap with a one-sample-late note-off |
| 105 | MIDI out on/off | SHOULD | done | Settings, MIDI, "MIDI out" |
| 106 | MIDI thru in EXT | SHOULD | done | "MIDI thru (external mode)" |
| 107 | MIDI Learn/Clear on every control by right-click | MUST | done | ParamHooks on every knob, switch, selector, LED, dropdown; gallery "learn hook arms the parameter". Pattern selection: learnable too (row 60). Stage-5 fix: an armed learn shows a ring and a banner and cancels by Escape, a banner click, a click elsewhere or the menu |
| 108 | Save/load CC map files | SHOULD | done | Settings, MIDI, Save/Load MIDI map |

### Presets

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| 109 | Scene presets (sound, play mode, 48+48 patterns, template) | MUST | done | `scene` type; generator fields live in each pattern |
| 110 | Synthesis presets (sound with calibration, distortion, FX, order) | MUST | done | `synth` type (SPEC 6.7) |
| 111 | Sequencer- and arp-pattern presets | MUST | done | `pattern`, `arp` types; pattern name box browses them |
| 112 | Browser: factory/user, tag groups with Any/All cascade, search, OK/Cancel audition; groups Play Mode, Waveform, Distortion | MUST | done | Stage-5 fix: tags are grouped (Play Mode, Style, Family, Waveform, Distortion, Character, Mods, plus User), each group has its own ANY/ALL switch and a preset must pass every group, so "any waveform AND Strong distortion" works. Distortion None is shown as Off. Remaining wart: Deep and Liquid appear in both Family and Character but match as plain strings, so either chip selects both meanings (row D.6) (PresetBrowser.cpp; editor-preset-browser renders in both themes, final verification) |
| 113 | Prev/next arrows; Init / Reload / Save | SHOULD | partial | Arrows and SAVE yes, Init in the settings menu. No Reload |
| 114 | Favourites (global) and pins (per instance) | SHOULD | partial | Favourites yes (favourites.txt). No pins |
| 115 | Batch tag editing, author/description, rename/delete, user tags and groups | SHOULD | partial | Author, description, rename, delete, free-text tags yes. No batch or tri-state editing, no user groups |
| 116 | Drag-export and drag-import of preset files | SHOULD | partial | Stage-5 fix: .mw12f files dropped on the browser are imported. Export is still the EXPORT... button only; no drag out of the browser |
| 117 | At least 800 factory presets and patterns | MUST | done | 858 presets plus the default scene; `factory-build --verify`: 859 files, 0 failures (final verification, `tools/factory/qa.py`) |
| 118 | Default State save / restore factory | SHOULD | partial | Stage-5: `Controller::saveDefaultState`, `resetDefaultState` and `hasUserDefaultState` exist and a new instance and Init scene start from the saved state. No editor control calls them yet (nothing in `plugin/ui` references them), so a user cannot reach it |
| 119 | Double-click resets to the last-loaded preset value | SHOULD | done | `mw12FEditor::applyDefaults`; gallery checks |

### GUI

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| 120 | Right-click parameter menu with value and learn | MUST | done | ParamMenu (name, value, typed entry, reset, learn, clear) |
| 121 | Ctrl fine-drag | MUST | done | `DragSlider::isFine` (Ctrl, Shift or Cmd) |
| 122 | Typed value entry | SHOULD (+) | done | ParamMenu `enterValue`, gallery kit-text-entry render |
| 123 | GUI sizes 60..200 % | MUST | done (deviation) | 75..200 %, the range SPEC section 2 sets; zoom box 75/100/.../200 and free host resize in between. 60 % is not available |
| 124 | Free or vector scaling | SHOULD (+) | done | Vector drawing under one transform; renders at 75, 100, 150, 200 % |
| 125 | HiDPI | MUST | done | Static tiles rendered at `getPhysicalPixelScaleFactor()` (Faceplate.cpp) |
| 126 | Light and dark themes | SHOULD | done | Silver and Night, both rendered |
| 127 | Value tooltips | SHOULD | done | In-editor ValueTooltip on drag |

### Audio and platform

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| 128 | Draft/Normal/High/Ultra, separate real-time and offline | MUST | done | Settings, Quality, Real-time and Offline render |
| 129 | Stereo out, 44.1 kHz and up | MUST | done | Stereo bus; tests at 44.1/48/96/192 kHz |
| 130 | Automation of all sound, FX and override parameters | MUST | done | Every ParamTable row is a host parameter with `withAutomatable (true)` |
| 131 | Linux-native VST3, CLAP, LV2 | MUST | done (release gate open) | All three formats build, load and render in REAPER; offline renders are bit-identical run to run in all three (docs/host-matrix.md, final verification). Final `validate.sh --release`: LV2, CLAP, lv2_validate and Carla pass in 3 of 3 runs. pluginval VST3 fails 2 of 3 runs: a SIGSEGV in pluginval's own `XEmbedComponent::Pimpl::handleX11Event` callAsync lambda (gdb backtrace, no plugin frame), 10 crashes in 44 randomised `--repeat 3` runs on the shipping binary (validate.sh included). One further run in those 44 failed Plugin state restoration ("Waveform not restored", expected 0, got 0.984, seed 0x7038271); the same seed passed 3 of 3 times afterwards, so it is timing-dependent and still open. The editor triggers it: it resizes its window every time `play_mode` enters or leaves External, and Editor Automation sets random values. A throwaway build with that resize disabled crashed 0 times in 20 runs against 4 in 20 for the shipping binary under the same load, and a stock JUCE plugin with a fixed-size editor 0 in 16. Reason this is still open: the fix (coalesce or defer the play-mode resize, or stop collapsing the window on host automation) is an editor design change |
| 132 | Standalone with internal clock | SHOULD (+) | done | Standalone target; Settings, Tempo (20..300 BPM) |

## B. SPEC section 2 (v1 scope)

| # | Item | Tag | Status | Evidence |
|---|---|---|---|---|
| B.1 | Voice: circuit oscillator, ZDF diode ladder, envelopes, accent with C13, slide, gated VCA, coupling filters; 13 knobs, wave, two couples, extended tuning | MUST | done | Voice, DiodeLadder, Oscillator; test suites pass |
| B.2 | Calibration: 15 trimmers in physical units, calibration presets | MUST | done | 15 Calibration rows, 22 presets |
| B.3 | Mods with one switch back to stock | MUST | done | Section C |
| B.4 | Distortion: 8 families, at least 24 voicings, user voicing, dynamics pre/post | MUST | done | 8 families, 32 voicings plus Custom |
| B.5 | EQ, synced stereo delay with in-loop filter, early/late reverb, two-line chorus, limiter with soft clip; all six blocks reorderable | MUST | done | Rows 37..45, 38, 39 |
| B.6 | 48 slots of 1..64 steps | MUST | done | Rows 49, 54 |
| B.7 | Per step: note, tie, rest, octave, accent, slide, vibrato, probability | MUST | done | Piano roll lanes incl. PROB %; keyboard view TIE/REST |
| B.8 | Per pattern: rate, shuffle, gate length, direction | MUST | done | Tools column |
| B.9 | Per step: ratchet | MUST | done | Stage-5 fix: a RATCHET lane in the piano roll and RATCHET and GATE buttons in the keyboard view set `Step::ratchet` 1..4 and `Step::gate` (PianoRoll.cpp, StepKeyboard.cpp; RATCHET lane visible in the editor-sequencer renders). Test "qa parity: the step editors expose a ratchet control" passes |
| B.10 | Queued switching, host-locked playback, internal clock, Live and Host Note, overrides, live transpose | MUST | done | Rows 56..58, 62, 63, 73; TRANSPOSE readout (`live_transpose` ±24) |
| B.11 | Arpeggiator: 48 templates indexing held notes, same per-step flags | MUST | done | Rows 84..86 |
| B.12 | Generator: masks with root, attribute toggles, seed, four density knobs without re-rolling | MUST | done | Rows 90..95; seed visible and typeable (row 96). Stage-5 fix: a density move no longer reverts hand edits on a generated pattern (`seq::keepEdits` on every Controller edit path; review_qa_UxTests.cpp passes) |
| B.13 | MIDI: external mode, MIDI out from seq and arp, learn, MIDI file export by drag and button, import | MUST | done | Rows 79, 82, 100..107 |
| B.14 | Preset types scene, synth, calibration, voicing, per effect, pattern, arp, template; browser with tags, search, favourites; original factory content | MUST | done | 12 types in the browser; see row 112 for tag groups |
| B.15 | GUI vector, 75..200 %, light and dark, piano roll and keyboard editors | MUST | done | Rows 65, 66, 123..126 |
| B.16 | Quality tiers separate for real-time and offline | MUST | done | Row 128 |
| B.17 | Deferred: song mode, modulation matrix, other products' patterns, audio into the filter | n/a | not shipped (by design) | Not present; the data model leaves room |

## C. Circuit mods (SPEC section 2, research document 05)

Each mod rendered through `acid-render` on a 16-step line (accents, slides, a tie, rests), distortion off, and compared sample by sample with the stock render.

| # | Mod | Tag | Status | Evidence |
|---|---|---|---|---|
| C.1 | One switch returns everything to stock (`mod_on`) | MUST | done | `mod_on=0` with every mod knob at its extreme: bit-identical to stock. `mod_on=1` with neutral mod values: bit-identical too |
| C.2 | Filter overdrive | MUST | done | `mod_overdrive=100`: +19.6 dB RMS at QA time. Final verification, same 16-step line through `acid-render`: peak +5.71 dBFS (was +8.5) with the stage-5 output ceiling, RMS -4.4 dBFS against -23.8 for stock |
| C.3 | Filter FM | MUST | done | differs from stock by -26.7 dB RMS |
| C.4 | Filter tracking | MUST | done | -28.7 dB difference |
| C.5 | Amp decay and sustain | MUST | done | Amp decay 7 ms: level -9 dB; sustain 100 %: -46 dB difference |
| C.6 | Soft attack | MUST | done | 30 ms: -39.9 dB difference |
| C.7 | High resonance | MUST | done | -26.8 dB difference |
| C.8 | Accent sweep speed Normal/Fast/Slow | MUST | done | Fast -39.1 dB, Slow -47.0 dB difference |
| C.9 | Muffler Off/Soft/Hard | MUST | done | -46.0 and -43.5 dB difference |
| C.10 | Extended ranges | MUST | done | -39.0 dB difference |
| C.11 | Sub oscillator, -1/-2 octave | MUST | done | -29.9 dB difference |
| C.12 | Accent sweep Off (research A.1.6, three-position switch) | not promised | missing | `mod_sweep_speed` is Normal, Fast, Slow only |
| C.13 | Separate normal decay 30 ms..3 s in mod mode | not promised | partial | Decay keeps the stock law; extended ranges widen cutoff and Env Mod only |
| C.14 | Filter FM source select, audio into the filter, filter-out bus | not promised | missing | Deferred by SPEC (audio input) |
| C.15 | Output safety soft limiter in mod mode (research A.1.13) | not promised | done | Stage-5 fix, in every mode, not only mods: the voice's output stage soft-clips (linear to 0 dBFS, rounding off towards +6) and the engine ends in a safety clip that cannot be turned off (linear to +6 dBFS, never above +12, non-finite samples become 0; Engine.h). Final verification: Overdrive 100 peaks at +5.71 dBFS; Overdrive 100 with resonance, Env Mod and accent at 100 and master at +12 dB peaks at +11.99 dBFS |

## D. Factory README claims

| # | Claim | Status | Evidence |
|---|---|---|---|
| D.1 | 858 presets in twelve folders plus `default.mw12f` | true | Counted: arp 56, calibration 22, chorus 10, delay 11, distortion 18, eq 10, limiter 11, pattern 310, reverb 10, scene 127, synth 183, template 90 |
| D.2 | Every file generated, canonical, a second build changes nothing | true | `factory-build --out` into scratch, `diff -rq` against `assets/factory`: only README.md differs |
| D.3 | `--verify`: zero adjustments, zero ignored, names checked | true | "verified 859 file(s), 0 failure(s)" |
| D.4 | `default.mw12f` holds the same scene as `scene/first-squelch.mw12f` | true | Stage-5 fix for the duplicate in the scene list: `default.mw12f` is now written as a `type="state"` document, so the browser lists First Squelch once. Final verification: the two files differ only in that attribute (`sed 's/type="state"/type="scene"/'` makes them identical); bank A has 12 slots |
| D.5 | Synth: 112 None, 25 Light, 31 Medium, 15 Strong; 66 square; 50 with Mods | true | Parsed from the files |
| D.6 | Synth families Stock 33 ... Utility 5 | true, with a caveat | factory-build's manifest matches. Counting the tags gives Deep 25 and Liquid 13, because Deep and Liquid are also Character tags. The stage-5 browser lists Deep and Liquid under both Family and Character, but matches tags as plain strings (`hasTag`), so either chip selects both meanings |
| D.7 | Scenes: 122 with six slots, two five, two four, default twelve; four arp patterns each; `internal_bpm` on all | true | Parsed |
| D.8 | Melodic scenes Amp Sustain 25..50, three run 64 steps | true | Parsed: sustain 25..50; Hill Road, Inkwell, Tide Chart are 64 steps |
| D.9 | Hard scenes Env Mod 28..40 | true | Parsed: 28..40 |
| D.10 | Templates: 76 built-in plus 14 that also set densities | true | 90 files; the generator library has 76 entries |
| D.11 | Arp shapes Up 20, Up Down 8, Octave Spread 8, Pedal 7, Random Walk 5, Chord 4, Down 4 | true | Parsed |
| D.12 | Distortion: 16 built-in voicings plus 2 custom | true | 18 files |
| D.13 | Branding and em dash clean | true | `scripts/branding-audit.sh`: clean; no U+2014 in any factory file |
| D.14 | Levels, loudness, mono loss, preset distance | true | Final verification, `tools/factory/qa.py` with build-factory rebuilt from the current source: 1,714 jobs, 0 failures, 143 s; "verified 859 file(s), 0 failure(s)"; `factory-build` wrote and pruned nothing; `assets/factory` (md5 of every file) and `levels.tsv` byte-identical before and after. Synth presets on the reference line -26.1..-21.9 dBFS RMS, true peak max -1.26 dBTP; scenes -25.2..-24.0 dBFS |
