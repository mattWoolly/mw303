# 06: Sequencer, arpeggiator and pattern randomizer

Research doc for the architect and for the sequencer, generator and GUI implementers. Scope: the original TB-303 sequencer's exact semantics, what the reference clones (D16 Phoscyon 2, AudioRealism ABL3, Roland TB-03/TB-3/Roland Cloud TB-303, Behringer TD-3, x0xb0x, Open303, TB-3PO, Endless Acid Banger) do on top of it, and a spec we can code directly. Sections 1 to 7 cover the evidence. Section 8 has the data model and Section 9 the playback state machine. Open questions and build requirements come last.

Confidence tags: **[primary]** means a schematic, service note, vendor manual or source code. **[secondary]** means reputable analysis or reverse-engineering code that hasn't been checked on hardware. **[lore]** means forum consensus.

## 0. Sources and licenses

Local copies live under `/tmp/claude-1000/-home-mwoolly-projects-mw303/dad63690-756f-4b60-b4c2-2627c3ad5533/scratchpad/refs/` (shortened below to `refs/`).

| Id | Source | Where | License / status |
|---|---|---|---|
| SN | Roland TB-303 Service Notes, 1st ed., 19 Feb 1982 (spec page, OCR) | `refs/web/sm/sn2.txt` (scans via firstpr.com.au) | Roland document, reference only |
| RW-SLIDE | Robin Whittle, "TB-303 Slide" (1999, rev. 2012) | https://www.firstpr.com.au/rwi/dfish/303-slide.html (`refs/web/303-slide.html`) | Reference |
| DF | Devil Fish manual (Whittle) | `refs/web/dfman.txt` | Reference |
| TL | tinyloops, "Programming the TB-303: Pattern Write" | https://tinyloops.com/tb303/pattern_write.html | Reference [secondary] |
| TB03 | Roland TB-03 Owner's Manual | https://static.roland.com/assets/media/pdf/TB-03_eng03_W.pdf (`refs/manuals/t3.txt`) | Vendor manual |
| TB3 | Roland TB-3 Owner's Manual | https://static.roland.com/assets/media/pdf/TB-3_OM.pdf (`refs/manuals/tb3.txt`) | Vendor manual |
| RC303 | Roland Cloud "TB-303 Software Bass Line" manual | https://rolandcloud.com/getmedia/b3e2da28-1951-467e-bd79-7aae34481570/TB-303-Manual-E.pdf (`refs/manuals/c.txt`) | Vendor manual |
| P2 | D16 Phoscyon 2 User Manual (dated 21 Nov 2022) | https://d16.pl/pub/manuals/Phoscyon%202-manual-gb.pdf (`refs/manuals/p2.txt`) | Vendor manual |
| P1 | D16 Phoscyon (v1) manual | https://d16.pl/pub/manuals/Phoscyon-manual-gb.pdf (`refs/manuals/p1.txt`) | Vendor manual |
| ABL3 | AudioRealism ABL3 VST/AU Operation Manual | https://www.audiorealism.se/uploads/4/5/5/4/45545237/abl3vstauoperationmanual.pdf (`refs/manuals/abl3.txt`) | Vendor manual |
| X0X | x0xb0x firmware (Limor Fried, 2005) | github.com/a1k0n/x0xb0x `firmware/main.c`, `synth.c`, `midi.c`, `pattern.h` (`refs/x0xb0x`) | **MIT** |
| SOKK | SokkOS x0xb0x firmware | github.com/ssidorenko/sokkos `main.c`, `pattern_play.c` (`refs/sokkos`) | MIT header (x0xb0x-derived) |
| O303 | Open303 (Robin Schmidt) | github.com/RobinSchmidt/Open303 `Source/DSPCode/rosic_AcidSequencer.*`, `rosic_AcidPattern.*`, `rosic_Open303.*` | **MIT** (`License.txt`). The seed report says GPL. That is wrong. |
| EAB | Endless Acid Banger (David Whiting / Vitling) | github.com/vitling/acid-banger `src/pattern.ts`, `src/app.ts`, `src/audio.ts` | **CC BY 4.0**: reuse needs credit |
| 3PO | TB-3PO (Logarhythm; djphazer mods), Phazerville O_C firmware | github.com/djphazer/O_C-Phazerville `software/src/applets/TB3PO.h`, `docs/_applets/TB-3PO.md` | File header **MIT**. The repo as a whole also contains GPLv3 parts. |
| TD3E | behringer-td3-editor (Kevin Meinert / subatomicglue) | github.com/subatomicglue/behringer-td3-editor `app/td3.js` | MIT-style |
| ST2M | synthtribe2midi | github.com/james-see/synthtribe2midi `docs/TD3_SEQ_FORMAT.md`, `pkg/converter/devices/td3.go` | MIT |
| AINJ | Acid-Injector (echolevel) | github.com/echolevel/Acid-Injector `script.js`, `index.html` | No license file, so read-only |
| CRAVE | CraveSeq | github.com/claziss/CraveSeq `src/parser.c` | GPL-3.0, read-only for us |
| SEED | Earlier project's research and code | `.../scratchpad/ref-mwAudio303/docs/research/*`, `libs/sequencer-core/` | Untrusted. Corrections are noted inline. |

**Could not reach:** the original 1982 TB-303 owner's manual as a PDF (no fetchable copy; Scribd only). I reconstructed it from SN, TB03 (Roland's own "original mode" reproduction), TL and the 1982 E&MM review (https://www.muzines.co.uk/articles/roland-tb-303-bass-line/4250). Also unreached: the Phoscyon 2 version-history document, the TB-03 `.PRM` backup format, ReBirth `.rbs`, ABL3's native pattern format and Phoscyon's native pattern format. No spec for any of these turned up.

---

## 1. Original TB-303 sequencer semantics

### 1.1 Memory organisation

- **Patterns:** 64 = 8 patterns × 2 sections (A/B) × 4 groups (I–IV). Each holds "Pitch, Value, Accent, Slide" [primary, SN].
- **Steps per pattern:** 1 to 16 in normal (1/16) mode, for "2, 3 or 4 meter". 1 to 15 in triplet mode, for "2, 3, 4 or 5 meter" [primary, SN]. Step count is set with FUNCTION + STEP pressed *n* times. Changing it clears TIMING data [secondary, TL]. TB03 adds TIME MODE + VALUE knob (1–16) as a temporary, unsaved change [primary, TB03].
- **Tracks:** 7. Each holds up to 64 measures. A track remembers pattern order, transpose and D.C. [primary, SN]. SN prints "Total 256 measures (64 x 7 TRACKS)", which is arithmetically inconsistent (7 × 64 = 448). The seed report asserted 448 and called 256 wrong, but the primary source says 256. **I believe the 64-per-track limit. The total is unresolved: either 256 of shared memory or a typo.** For software, use 64 entries per track and no global cap (see Open questions).
- **Track-to-group binding:** a track can only use patterns from one group. TB03: "in the case of tracks 1 and 2, the pattern group is 'I'" [primary, TB03]. The full mapping (3–4 → II, 5–6 → III, 7 → IV) is my inference and unverified.
- **Pitch range:** "3 octaves (PATTERN) w/ TRANSPOSE, 4 octaves (TRACK) w/ Key shift" [primary, SN]. The keyboard covers C…C′ (13 keys), and per-pitch DOWN/UP adds ±1 octave. That gives the 3-octave pattern span. TB03 extends this to ±2 octaves (lit = ±1, blinking = ±2) [primary, TB03].
- **Tempo:** ♩ = 40–300 [primary, SN]. x0xb0x uses 20–300 (`MIN_TEMPO 20`, `MAX_TEMPO 300`, X0X `main.h`) [primary].
- **Sync:** DIN SYNC input (24 ppqn clock plus start/stop). Gate out is 0 V / +12 V. CV out is +1 V to +5 V at 1 V/oct [primary, SN].

### 1.2 The pitch list and the time list (the core quirk)

A pattern is **two independent lists**, entered in two passes [primary, TB03 "Original mode"; E&MM 1982]:

1. **Pitch mode** writes an ordered **pitch list**. Each entry is {key C..C′, octave DOWN/UP/none, ACCENT, SLIDE}. Accent and slide belong to the *pitch entry*, not to a time position: "hold WRITE/NEXT + (DOWN/UP, ACCENT, SLIDE or none)", and "ACCENT and SLIDE pitch data is only reset when the PITCH is changed" [secondary, TL]. TD-3 dumps confirm this. Accent and slide arrays are packed together with the pitches, not per step [secondary, TD3E `unpackPattern`].
2. **Time mode** writes one **time event per step**: **Note** ("start sound" = play the *next* pitch), **Tie** ("continue sound" = sustain the last pitch) or **Rest** ("no sound") [primary, TB03; E&MM].

At playback the two lists are zipped together. **Only a Note event advances the pitch pointer. Ties and rests consume no pitch entry.** Pitch list C, D, E, F with time list Note, Note, Rest, Note plays C, D, –, **E**, not C, D, –, F [secondary, TL plus musicradar via search]. TD-3 dumps behave the same way: "Each rest '0' grabs the 'next' pitch, NOT the pitch from the corresponding slot" [secondary, TD3E]. AINJ states the tie case too: the sequencer "looks for the next note every time there's a non-rest step (and stops looking for notes if there's a tie)" [secondary, AINJ].

> Conflict: CRAVE's `.seq` parser advances the pitch index on rests, and TD3E's does on ties. Each is wrong in the other direction. The hardware model, the TB03 manual wording ("sustain the last pitch") and AINJ all agree with **Note-only consumption**. I believe that.

Consequences an implementer must reproduce in authentic edit mode:

- **Rests and ties carry no accent or slide.** "rest can not play an ACCENT or SLIDE", and "sustain does not make sense after a rest" [secondary, TL].
- **Editing the time list re-assigns pitches.** Turning a Rest into a Note shifts every later pitch (and its accent and slide) one step later. The last Note picks up the next, previously unused, pitch entry.
- **Pitch data is never deleted, only overwritten.** "All patterns initialize with 48 random pitches, but you can only use the first 16" [secondary, TL]. So unused pitch entries survive and come back into use when the time list gains Notes. **Re-entering pitches loses accent and slide data** [primary, TB03: "when you re-enter the pitches, the accent and slide data you entered are lost"].
- The pitch pointer restarts at entry 1 on every pattern cycle. TD-3 storage only has 16 packed entries per pattern, which is consistent with this [secondary, TD3E].
- Pattern Clear (hold [1..8] + PATTERN CLEAR) clears TIMING and resets to 16 steps, normal mode. It does not clear pitches [secondary, TL].
- Modes can only change while stopped [secondary, TL].
- TB03 and TB3 add a direct **step-recording mode**: per step, enter note, rest, accent, slide, or tie (hold the same key). On TB3, "Enters a slide (if the preceding step is the same note, a tie)" [primary, TB03, TB3]. **On the TB3 a tie is entered as a slide to the same note.**

### 1.3 Clock and gate timing (24 ppqn)

From RW-SLIDE [secondary; Whittle measured hardware]:

- One 1/16 step = **6 clock pulses (0..5)** at 24 ppqn.
- **Gate goes high at the rising edge of pulse 0 and low halfway through pulse 3, on its falling edge.** That is **3.5 pulses on, 2.5 off: gate = 3.5/6 = 0.5833 of a step = 0.14583 quarter notes.**
- At 120 BPM: clock pulse 20.83 ms, step 125 ms, gate 72.9 ms, gap 52.1 ms. At 135 BPM: step 111.1 ms, gate 64.8 ms.
- With external DIN sync, the gate-off moment is the incoming clock's falling edge, so it depends on the *master's clock duty cycle* (inference from the description above).
- The voice adds latency and tail: "start of the audible note was between 1 and 5 ms (typically 4 ms) after the start of the gate". Unaccented decay after gate-off is "about 16 ms: 8 ms of normal volume and 8 ms of linear decay" [primary-ish, DF "New Functionality"]. This belongs to the voice spec, but it means **perceived note length ≈ gate + ~12 ms**.
- Clones differ on gate length. x0xb0x: note-off at the odd half-step event, i.e. **3 clocks = 50 %** (`do_tempo`, `note_counter & 1`) [primary, X0X]. Open303: `stepLength = 0.5` default [primary, O303 `AcidPattern()`]. TB-3PO: "50% of the detected clock rate" [primary, 3PO]. ABL3: "Gate Trim … default 50% … set to match the TB-303 as closely as possible" (a VCA release time, not a gate fraction) [primary, ABL3]. Phoscyon 2: per-pattern "Step Len." [primary, P2]. **Recommendation: authentic default = 3.5 clocks (0.5833 step). The fraction is user-adjustable.**
- **No look-ahead.** The sequencer does not start a slide before the next note: "persistent rumours that the sequencer anticipates the next note … " are false [secondary, RW-SLIDE].

x0xb0x clocking for reference [primary, X0X `main.c`]: the tempo interrupt fires 48 times per quarter note. `dinsync_counter` wraps at `DINSYNC_PPQ/4 = 6`, and the clock line toggles on each interrupt, giving 24 ppqn out plus a MIDI clock byte (0xF8) on each rising edge. `note_counter` counts 0..7 per quarter: even values are note-on and odd values are note-off. "make sure that all notes actually start on the zero count so that tempo and SYNC are aligned."

### 1.4 Tie vs slide

| | Tie (time event) | Slide (flag on a pitch entry) |
|---|---|---|
| New pitch entry consumed | No | Yes (the *next* Note's entry) |
| Gate | Stays high through the tied step(s) | The slid note's gate stays high **into the next Note** (no re-strike) |
| Pitch | Unchanged | CV jumps to the next pitch at pulse 0 of the next note, then the RC slide circuit glides it (constant-time) |
| Envelopes | Not retriggered | Not retriggered (no gate edge) |
| Accent line | Continues the note's accent | Follows the *new* entry's accent flag |
| Where the flag lives | On the step (time list) | On the **source** note (the one slid *from*) |

Whittle's measured rules for slide timing [secondary, RW-SLIDE]:
1. "The slide signal will go high 1/16 note after the start of slid note." This holds even if the slid note is longer than 1/16 because of ties, but makes no audible difference.
2. "The slide signal will remain active until the end of the note which follows the slid note. This includes if it is 1, 2 or more 1/16ths notes long."
3. The pitch slides *during* the following note, from its pulse 0. The tail of the glide is inaudible after gate-off unless that note is accented.

So a slid note followed by ties and then a Note holds the gate through the ties and glides into the Note. **Slide to the same pitch sounds like a tie** ("no audible difference on the TD-3 between a tied note and a slide to the same note" [secondary, AINJ]; TB-3 enters ties that way [primary, TB3]). The only difference is that the accent line can change.

Which step carries the flag: **the source**. This is confirmed by X0X ("slide is from prev note!": `note_on(..., (prev_note >> 7) | all_slide, ...)` [primary]), O303 (`note->slide && nextNote->gate`) [primary], 3PO (`step_is_slid(step_pv)`) [primary] and P2 ("Slide – When set, the current step will slide to the next set Note"; "Slide Time … when the Slide attribute is placed on the first of them") [primary]. **Exceptions to watch for in import:** ABL3's manual says "When Slide is activated the sequencer will tie the step to the previous step via a smooth glide" [primary, ABL3], which describes destination semantics. EAB applies its `glide` flag to the note being played (destination) and never holds the gate legato [primary, EAB `audio.ts noteOn`: `setTargetAtTime(freq, t, glide ? 0.02 : 0.002)`, no note-off between notes]. A KVR comment (Muon's "Dave") says the slide comes "from the note *before*" [lore, https://www.kvraudio.com/forum/viewtopic.php?p=1282039]. That describes when the glide is *heard*, not where the flag is stored.

### 1.5 Edge cases on the original and on firmware clones

- **Slide into a Rest.** Undocumented for the 303 itself. Two clone behaviours exist:
  - **x0xb0x (hold):** the rest step's `note_on` gets `slide = prev slide`, so the gate is not re-struck but stays high, and the CV is not re-latched. At the rest's own half-step `note_off(rest's slide bit)` drops the gate. **The previous pitch sounds through the gate portion of the rest.** Comment in source: "one can slide to/from a rest and rests can have accent (tip to memology)" [primary, X0X `synth.c`]. x0x users use this as their tie [lore, search summary of x0x forums].
  - **Open303 (cancel):** `if (note->slide && nextNote->gate == true) hold else noteOffCountDown = stepLength`. A slide before a rest is ignored [primary, O303 `getSample`].
  - TB-3PO does something else again: a slid previous step forces the current step's gate on even if it is ungated, and glides to that step's stored pitch [primary, 3PO `Controller`].
  - **Recommendation:** make it an option, `SlideIntoRest = Hold | Release`, defaulting to **Hold** (x0x behaviour, the closest firmware emulation). Flag it for hardware verification.
- **Slide on the last step wraps.** The glide goes into the first Note of the next cycle, or of the next pattern in a chain. X0X keeps `prev_note` across the pattern and chain boundary [primary]. O303's `getNextScheduledNote()` reads the already-wrapped `step` [primary]. 3PO uses `step_pv` with modulo wrap [primary].
- **Accent and slide together.** The accent flag of the *slid-to* entry drives the accent line during that step, but there is no gate edge, so the MEG/VEG don't retrigger. What that sounds like is a voice question (see the DSP docs). The sequencer must deliver `{legato=true, accent=flag}`. P2 emits MIDI CC#64 to carry this case through MIDI when consecutive notes share a pitch: "CC#64 (Sustain) can be utilized – when there's slide used and accent for the consecutive note with same pitch" [primary, P2 MIDI Output].
- **Accent persists after stop.** On the Devil Fish, and inherited from the 303 CPU latch, the synth stays accented "if the internal sequencer is not running, but the last note had the accent on" [primary, DF]. This is a quirk; an option at most.
- **Tie after a rest, or a tie on step 1 with nothing sounding:** silent (acts as a rest) [secondary, TL "sustain does not make sense after a rest"; AINJ "The first step can't be a tie"].
- **Pattern switch:** queued until the end of the current pattern. X0X loads the next chain or bank only when `curr_pattern_index` wraps [primary]. P2: "The next pattern will start playing when the current pattern comes to the end of its final step. The waiting pattern will be indicated by an LED glowing blue" [primary].
- **Live transpose:** X0X pattern play takes UP/DOWN + key = −12..+12 semitones into `next_pitch_shift`, applied **at the next pattern loop** [primary, X0X `synth.c` comment and `main.c`]. TB03 and TB3: hold PITCH MODE (or KEYBOARD) + key [primary]. A chain or bank change resets pitch shift to 0 on x0xb0x [primary].

### 1.6 Triplet ("3/4") mode

- TB03: "By default, each step is a 16th note, but by holding down [FUNCTION] and pressing [ ] you can use a triplet grid (**each step is an 8th note triplet**)" [primary, TB03]. TB3: "On: 1 step = 8th note triplet" [primary, TB3].
- SN allows 1–15 steps in that mode for 2/3/4/5 meter, i.e. 6/9/12/15 steps of 3 per beat [primary]. That fits 8th-triplet steps: **12 steps = one 4/4 bar, 8 clocks per step at 24 ppqn**. tinyloops calls it "3/4, 12 steps" [secondary]. I believe this is the same feature mislabelled; a 12-step 16th pattern can already be made with the step count, so a separate mode would be pointless.
- **Gate length in triplet mode is undocumented.** It is either 3.5 clocks (if the CPU counts pulses from the step start, giving 43.75 % of an 8-clock step) or proportional (58.3 %). See Open questions.
- TD-3 and TB-03 store a per-pattern triplet flag [secondary, TD3E offset 0x6C; ST2M offset 0x84].

### 1.7 Shuffle and swing

The TB-303 has **no shuffle** (no control in SN; Whittle: the TR-808 "plays in perfect time with the clock pulses"). Who added it:
- **TB-03 / TB-3:** SHUFFLE −50…0…+50 (0 = off) [primary, TB03, TB3]. Roland's scaling isn't published.
- **Roland Cloud TB-303:** SHUFFLE knob, "amount of shuffle (bounce)" [primary, RC303].
- **Phoscyon 1 and 2:** per-pattern Shuffle 0–100 % ("When it is set to 0, shuffle is turned off and notes are spread equally") [primary, P1, P2]. Phoscyon 1's arpeggiator had its own Arp.Shuffle [primary, P1].
- **SokkOS (x0xb0x):** 6 swing levels, `swing_time` 0..5. Every other 16th's events are delayed by `swing_time` interrupt ticks (1/48 beat each), so the maximum delay is 5/12 of a 16th [primary, SOKK `do_tempo`].
- **TB-3 SCATTER** is a different, performance-only groove effect. It exchanges steps and changes playback direction or gate length. 8 types × 10 depths, held while pressed, and "will not apply to the first cycle of the loop" [primary, TB3].

### 1.8 Track (song) mode: D.C. and D.S.

From TB03 (Roland's reproduction) [primary]:
- Measures are written in order. Each is {pattern, transpose (hold PITCH MODE + key while writing)}.
- **D.C. (Da Capo)** marks the last measure: "When playback reaches the end of the measure that is specified as D.C., playback returns to the first measure of the track."
- **Segno** marks a measure. **D.S. (Dal Segno)** pressed during playback: "the playback jumps to the measure that is specified as Segno". The manual doesn't say whether the jump is immediate or at the end of the current measure. I recommend end-of-measure.
- Selecting another track during playback plays it "consecutively after the last measure (D.C. measure) of the track has played."
- BAR RESET returns to measure 1. Insert and delete measure are available.
- Chain play: select several patterns at once and they "play consecutively" [primary, TB03, TB3]. X0X chains up to `MAX_CHAIN 32` patterns [primary].

---

## 2. Gate timing in software (any host tempo, with swing)

Work in **musical time (PPQ, quarter notes)**, never in milliseconds. Convert to samples only at the end, per block.

Definitions (doubles):
- `stepQ` = step length in quarters. 1/16 = 0.25, 1/8T = 1/3, 1/16T = 1/6, 1/32 = 0.125, dotted = ×1.5. Phoscyon's per-pattern Scale goes from 1/64 to whole note, × {Full, Dotted, Triplet} [primary, P2].
- `clockQ` = 1/24 quarter (one 24 ppqn pulse).
- Authentic gate: `gateQ = 3.5 * clockQ = 0.145833` for 1/16 steps. In general `gateQ = gateFrac * stepQ`, with `gateFrac` defaulting to 3.5/6.
- Step *k* (global integer index from the anchor): `onQ(k) = anchorQ + k*stepQ + swingOffsetQ(k)`.
- Samples per quarter: `spq = sr * 60 / bpm`. Offset in block = `round((tQ - blockStartQ) * spq)`.

**Swing definition (recommended).** `swing ∈ [−0.5, +0.5]` of a step, stored as an int8 in −50..+50 to match the TB-03 and TB-3 range. It applies only when the step division is a binary 1/16 or 1/32.

```
swingOffsetQ(k) = (gridParity(k) == odd) ? swing * stepQ : 0
gridParity(k)   = floor((onQ_unswung(k) - 0) / stepQ) mod 2    // parity on the HOST grid, not pattern index
```
- Parity comes from the **host beat grid**, not the pattern step index. An odd-length pattern (13 or 15 steps) therefore doesn't flip its swing feel every cycle. The SEED engine got this wrong (`g % 2` on the global step from play start).
- UI mapping: Phoscyon-style 0–100 % → `swing = 0.5 * pct/100 * (2/3)`, so that 100 % is a triplet feel (pair ratio 2:1). TB-03's −50..+50 scaling is unknown; it is a candidate for linear `swing = v/100` (see Open questions).
- Gate on swung steps: measured from the *actual* onset. Clamp `offQ ≤ nextOnQ − minGapQ` (`minGapQ` = 1 sample) unless the transition is legato. An on-beat step followed by a late off-beat gets the longer window. The off-beat step gets `stepQ*(1−swing)` and its gate is clamped.
- Ties and slides hold until the **next actual onset**, wherever swing put it.
- Triplet, dotted and 1/64 divisions ignore swing (P2 behaviour isn't documented; this is the conservative choice).

Tempo changes: because everything is in PPQ, a tempo change re-times gates automatically. Hardware does the same, since gate = 3.5 clocks of the running clock. Hosts report tempo per block, so in-block ramps are approximated per block; that is acceptable.

How software clones do it (for comparison):
- O303: a sample countdown per step, `round(beatsToSeconds(0.25,bpm)*sr)`, with accumulated `driftError` compensation (±0.5 sample). Free-running, no host position, and the step length is fixed at 1/16 [primary, O303 `getNote`].
- 3PO: measures the incoming clock period (`ClockCycleTicks`) and sets gate-off = clock period/2 [primary].
- ABL3: "always matches the host tempo", with "Host Sync" (follows transport and "will also keep sync with the host when scrolling forward or back") or "MIDI Trigger" [primary, ABL3].
- P2: "Host Link Active" off (default) syncs the cursor "only once, when the sequencer starts to play, then the internal clock takes it over". On, it is "continuously aligned as long as the pattern plays". Relevant when the DAW loops [primary, P2].

---

## 3. Modern sequencer features: what clones have and what matters

| Feature | Who has it (evidence) | Priority for us |
|---|---|---|
| Direct per-step grid editing (note/octave/accent/slide/gate lanes) | P2 piano roll + classic "Keyboard" view; ABL3 Classic + Pattern view; RC303; TB03 step-rec [primary] | **P0** |
| Authentic pitch/time editing mode | TB03 "Original mode", the default there [primary] | **P0** (purists; cheap given §8) |
| Pattern length 1–64 | P2 (64), ABL3 (1–64), TB3 (1–32), 3PO (1–32), Cyclone TT-303 (64) [primary except TT-303, secondary] | **P0** (64) |
| Per-pattern step division (Scale) incl. dotted/triplet | P2 (1/64…1/1 × Full/Dotted/Triplet) [primary] | **P0** |
| Per-pattern shuffle | P1/P2, TB03, TB3, RC303 [primary] | **P0** |
| Per-pattern gate length ("Step Len.") | P2 [primary]; O303 `stepLength` | **P0** |
| Pattern bank + queued switching at pattern end | P2 48 seq + 48 arp; ABL3 128; X0X 16×8 [primary] | **P0** |
| Host-note pattern triggering | P2 "Host Note" (48 notes from "the C note of the second lowest octave"; note-on plays, note-off "will eventually stop"; transport Start ignored); ABL3 "Pattern" MIDI mode (one key per pattern, 128) + "MIDI Trigger" sync; RC303 variations on C4 D4 E4 F4 G4 A4 B4 [primary] | **P0** |
| Transpose (pattern edit, with wrap) | P2: transposes ±1 semitone, notes "wrapped at each end of the keyboard note range… B3 → C1"; ABL3: "out of range will cycle"; ABL3 leaves Up/Down flags unchanged [primary] | **P0** |
| Shift/rotate with wrap | P2 Shift "All attributes of the step are shifted as well"; ABL3 "works cyclically"; RC303 ROTATE [primary] | **P0** |
| Copy / Paste / Clear | P2, ABL3, RC303. ABL3 Clear sets "All steps … C with Gate On", Shift+Clear sets gates off [primary] | **P0** |
| Drag-and-drop MIDI export | P2 (MIDI clip *or* native pattern; accent/non-accent export velocities settable); RC303 (MIDI or audio); ABL3 export .mid, "padded to be a multiple of 16 steps" and "may not … sound identical" [primary] | **P0** |
| MIDI out of sequenced notes | P2 (velocities for accent/non-accent configurable, CC#01 for vibrato, CC#64 for same-pitch slide+accent, can be disabled against loopback); ABL3 (VST only, some hosts) [primary] | **P1** |
| Live Override (non-destructive force set/clear of slide/accent/notes while playing) | P2 [primary] | **P1** (cheap and performative) |
| Vibrato step attribute | P2 (alternating-notes vibrato, CC#01 out); ABL3 (Up+Down on a step) [primary] | **P1** (flag only; the voice implements it) |
| Play direction (fwd/rev/ping-pong/random) | RC303 PLAY MODE knob ("how the step sequencer plays"; option list not in the manual). SEED lists Forward/Reverse/Fwd&Rev/Invert/Random [secondary] | **P1** |
| Song/track mode (D.C./D.S., chains) | 303/TB03 tracks, X0X chains [primary] | **P2** (DAW arrangement plus host-note triggering covers most needs) |
| Per-step probability, ratchets, conditions | Not in P2, ABL3, RC303 or 3PO. Common in modern sequencers | **P2** differentiator; deterministic RNG required |
| Pattern import | ABL3: Rebirth, technoBox2, TB-03, T-8, TD-3 [primary product page]; P2: native only | **P1** (SMF, TD-3) |
| Step record from MIDI | ABL3 Step Rec: velocity ≥ 100 = accent; overlap = slide; Next = rest; hold + Next = tie [primary] | **P1** |
| Undo | ABL3 Undo for some operations; P1 Accept/Undo for randomizer [primary] | **P0** (full undo stack) |

---

## 4. Arpeggiator

### 4.1 Phoscyon 2: index-pattern arpeggiator [primary, P2 "Arpeggiator Play Mode"]

- ARP is a play mode exclusive with SEQ and EXT. It has its own 48-pattern store, and arp and sequencer patterns can't be used in place of each other. "Randomizer and Export don't work with Arpeggiator. Host Note Mode is unavailable."
- Held keys are "sorted by their pitch and indexed. The note with the lowest pitch is indexed as no. 1 … The Arpeggiator can index up to 10 notes at once."
- Each arp pattern step holds **Pitch Index + Octave + attributes (Slide, Accent, Vibrato)**. The pattern properties match a sequencer pattern: Length, Shuffle, Step Len, Scale. **Accent and slide come from the arp pattern's steps**, not from input velocity.
- Changing keys while at least one is still held keeps the cursor running. "The cursor will be reset when you take your hands off the MIDI keyboard." The next press starts from step 1.
- **Missing index** (index > number held): "the Note indexed as no.1 will be played". Example: pattern 1-2-3 with C and E held plays C, E, C.
- Edit op "Inversion" works like transpose but shifts indexes.
- v2.1.0 fixed "hanging arpeggiator notes" [secondary, SEED from the D16 changelog].

### 4.2 Phoscyon 1: classic mode arpeggiator [primary, P1]

- Arp.Mode: Off, then "Random to Up-Down" (orders the notes). Arp.Chord: 7 predefined chord types (SEED lists Major, Minor, 7th, m7, M7, m7-5, Dim) or Custom (from incoming notes). Oct.Range: −1..+2 octaves. Arp.Tempo multiplier, Arp.Repeater (each note repeated *n* times), Arp.Shuffle 0–100.
- In **Internal** mode the arp is fed by the sequencer. A step without Gate sends Note On then Note Off; a step with Gate sends Note On held until the next note or an ungated step. "Accent and Slide attributes work in the same way as when the Arpeggiator is turned off."
- In External mode, simultaneous notes are ordered as the host delivers them. "Setting such notes with a little time distance stops any issues."

### 4.3 Recommended arp spec (supersets P2, adds classic modes)

- **Input set:** up to 16 held notes, deduped. We keep two orderings: `sorted` (ascending) and `asPlayed` (arrival order).
- **Engine:** an **index-pattern** (P2 model). Steps `{state Note/Tie/Rest, index 1..16, octave −2..+2, accent, slide, vibrato, prob, ratchet}`, with Length, Scale, Shuffle and Gate like a sequencer pattern. It shares the step type with the sequencer (see §8).
- **Classic modes as pattern fillers** ("Fill from mode"), which write indexes into the arp pattern:
  - Up, Down, UpDown (ends not repeated), UpDownInc (ends repeated), DownUp, AsPlayed, Random (seeded, option for no immediate repeat), Converge, Diverge.
  - Octaves 1–4 with direction Up, Down or Alternate.
  - Repeater 1–4.
  - Because N (the number of held notes) changes at play time, a "live classic" option evaluates the mode against the current set every step instead of the pre-filled indexes.
- **Missing index policy:** `First` (P2, default) | `Wrap (idx mod N)` | `ClampHighest` | `Rest`.
- **Articulation source:** `Pattern` (P2, default) | `InputVelocity` (accent if velocity ≥ threshold) | `PatternOrVelocity`. Slide: `Pattern` | `LegatoInput` | `Both`.
- **Articulation lane independence** (beyond P2): an optional accent/slide/rest lane of its own length (1–64) that cycles independently of the index lane, so the two drift against each other (polymeter).
- **Hold/Latch:** with Latch on, releasing all keys keeps the set. The first new note-on after a full release replaces the set and resets the cursor.
- **Cursor reset:** when the held set goes from empty to non-empty, reset to step 0 and start at the **next grid step boundary**. If the key arrives within `catchWindowQ` (default 1/4 of a step, or ≤ 30 ms) after a boundary, play that step at once (late-but-tight).
- **Export:** we record the arp's output stream to a MIDI clip (the last N bars, or until stopped) for drag-out. P2 can't export arp output; this is a gap to exploit.
- **Timing:** same host-locked grid and swing as the sequencer (§2, §9).

---

## 5. Randomizers and generators

### 5.1 Endless Acid Banger (EAB) [primary, `refs/acid-banger/src/pattern.ts`; CC BY 4.0]

```
offsetChoices = [[0,0,12,24,27],[0,0,0,12,10,19,26,27],[0,1,7,10,12,13],[0],
                 [0,0,0,12],[0,0,12,14,15,19],[0,0,0,0,12,13,16,19,22,24,25],
                 [0,0,0,7,12,15,17,20,24]]
changeNotes(): root = rndInt(15) + 16          // MIDI 16..30 (E0..F#1), rndInt(n) in [0,n)
               noteSet = choose(offsetChoices).map(o => o + root)
createPattern(): density = 1.0
  for i in 0..15:
    chance = density * (i%4==0 ? 0.6 : i%3==0 ? 0.5 : i%2==0 ? 0.3 : 0.1)
    if rand() < chance: {note: choose(noteSet), accent: rand() < 0.3, glide: rand() < 0.1}
    else:               rest
```
- The resulting per-step probabilities are [.6,.1,.3,.5, .6,.1,.5,.1, .6,.5,.3,.1, .6,.1,.3,.5]. Step 6 gets 0.5 because the `i%3` test comes before `i%2`. The expected number of notes is 5.8 out of 16.
- Notes are drawn uniformly from the shape *with repetition*, so the leading 0s weight the root. 12 and 24 are octaves, 7 is a fifth, 1, 10, 13, 14 and 15 are chromatic or blue.
- Autopilot (`app.ts`): every 64 measures there is a 20 % chance of a new note set. Every 16 measures each synth has a 50 % chance of a new pattern (applied at step 0). Knobs drift via `WanderingParameter`: a random walk with step `(max−min)/400`, damping 0.98 (0.8 while recovering), pushed back inside 20–80 % of range. After a user touch it pauses for 200 ticks of 100 ms and resumes once fewer than 100 remain.
- Playback quirks: there is no legato. Every note re-triggers the envelope, and `noteOff` happens only on a rest. Glide is a 20 ms time-constant pitch slew *into* the flagged note. Accent shortens the filter decay to 1/3 and raises the VCA from 0.15 to 0.2.

### 5.2 TB-3PO [primary, `refs/O_C-Phazerville/software/src/applets/TB3PO.h`; MIT header]

- **State:** 16-bit `seed`; `density` 0..14 (shown −7..+7); `num_steps` 1–32; quantizer scale (size `scale_size`); bitfields `gates`, `slides`, `accents`, `oct_ups`, `oct_downs`; `notes[32]` (scale-degree indexes).
- **Determinism:** generation reseeds the Arduino RNG with `randomSeed(seed + regenerate_phase)` per phase, then restores `randomSeed(micros())`. Pitches and gates come from different phases, so **changing density re-derives gates but keeps pitches only as far as density leaves `pitch_change_dens` unchanged**. Regeneration triggers when density or scale size changes. With the seed unlocked, every Reset picks a new random seed.
- **Pitch** (`regenerate_pitches`):
  - `pitch_change_dens = clamp(density, 0, 8)`.
  - `available_pitches`: if `pcd > 7`, then `scale_size − 1`. If `pcd < 2`, then `pcd` itself (0 = root only, 1 = root + next degree). Otherwise `3 + Proportion(pcd−3, 4, max(4, scale_size−3))`, clamped to [1, scale_size−1].
  - Per step: `force_repeat_prob = 50 − 6*pcd` %. If `s > 0` and it hits, `notes[s] = notes[s−1]`. Otherwise `notes[s] = random(available_pitches+1)`, and an octave coin `random(200) < 80` (40 %) sets oct up (odd) or down (even).
  - Quirk: the octave bitfields shift only on non-repeat notes, so octave flags don't line up with step indexes. This is an accidental source of variety.
- **Rhythm** (`apply_density`):
  - `on_off = |density − 7|`, `densProb = 10 + 14*on_off` % (10 % at centre, 108 % at the extremes). So **both ends of the knob are dense, the centre is sparse, and the left half also narrows the pitch set**: "break down from busy to sparse and back".
  - Per step: gate = `rand < densProb`. Slide = `rand < (prevSlide ? 10 : 18)` %. Accent = `rand < (prevAccent ? 7 : 16)` %. That is **anti-clustering of consecutive slides and accents.**
- **Pitch out:** `quant_note = 64 + notes[s]`, then ± `scale_size` for the octave flags (one scale span, not 12 semitones), quantized through the selected scale. Transpose is in semitones (after quantize) or scale degrees (before quantize).
- **Playback:**
  - On each clock: if the previous step slid, glide from the current CV to the new step's pitch. Otherwise jump (or hold the pitch if "hold pitch" is on and the step is ungated).
  - Gate fires if the step is gated **or the previous step slid**. Gate CV is 3 V normal, 6 V accent. Gate-off at clock period/2 unless the current step is slid.
  - Glide: a one-pole step of `x += ((target−x) >> 18) * 3` per update, clamped to the [start, end] range. A "no slides" toggle exists.
- Density automation records the encoder value per step (`density_auto[step]`).

### 5.3 Phoscyon 2 randomizer [primary, P2 "Randomizer"]

- Works only in SEQ play mode and Piano Roll view. It is "not a special mode": you can edit the grid while the Random tab is active.
- **Template:** toggle which **Note names** (pitch classes), which **Octaves** ("defining the pitch range a generated pattern will span") and which **Attributes** (Slide, Accent, Vibrato) take part. **Draw** generates.
- **Density:** "Every time a new sequence is drawn, you can use the Amount knobs to precisely set the 'density' … individually for each of the randomized parts: Note, Slide, Accent, Vibrato." Excluded parts have their knobs disabled. The product page markets this as "real-time density control".
- Transpose Up/Down shifts the *note template* by a semitone. There is **no root selector**; root is set implicitly via the template and transpose.
- Templates can be saved as presets ("Randomizer preset", SEQ only).
- v2.0.x shipped with a deterministic RNG that repeated the same sequence; v2.1.0 (Feb 2023) fixed it as "Randomizer initialization" [secondary, SEED citing the D16 changelog].

### 5.4 Phoscyon 1 randomizer: the stored-magnitude/threshold model [primary, P1 pp. 25, 32–36]

This is what the SEED report attributed to v2. It is the **v1** mechanism and is documented exactly:
- Every step's Gate, Accent and Slide attribute has a **state** (on/off) and a **value 0–127**.
- **Threshold mode:** an attribute counts as on only if its value is **below** the Event Threshold knob for that attribute (0 = none, 127 = all).
- Randomize (Rand.Gate/Accent/Slide toggles; "Randomize notes") sets the state to on for every step and writes a random **value** from the full 0–127 range. Threshold mode is forced on, so the knobs sweep density in real time **without re-rolling**.
- **Prev.Flatten** (Ctrl+Prev): for the attributes whose Rand toggle is on, it keeps the state only where value < threshold, then sets those values to 0. It "clips" the current density into fixed states.
- **Next/Accept:** Next draws a new line; Ctrl+Next accepts it into an undo buffer, and Undo returns to the last accepted state.
- **Note pools:** a one-octave keyboard pool, plus Transpose Up/Down toggles that pick which of four octaves (2nd–5th) the pool applies to. Arp.Chord presets fill the pools, and Custom pools are remembered. The Transpose rocker moves the *pools* by a semitone; existing steps are unchanged.
- On a step without Gate the oscillator still moves to that step's note ("This only concerns the amplifier") [primary, P1].

### 5.5 ABL3 [primary, ABL3 pp. 6–12]

- **Random button:** randomizes "pitch, down, up, slide and accent" of the steps.
- **Alter:** "causes the order of the steps in the pattern to be randomized. It does not change any of the steps themselves". Shift+Alter gives "another variation of altering".
- **Randomizer view:**
  - Scale: a custom scale, plus which octaves (−2, −1, +1, +2) relative to Root may be used.
  - **Note** diversity: "at 0% pitches at the Root will be generated, and at 100% all notes from the Scale are equally likely".
  - **Gate**, **Slide**, **Accent** densities, and **Root**.
  - **Next Seed / Prev Seed.**
  - **Fix Scale:** "pitches will be selected from the scale in order instead of randomly, … useful for generating arpeggios".
  - **Apply:** live regeneration while parameters move ("parameters can be adjusted in realtime to immediately hear the changes"). Apply turns off when you leave the view so that edits elsewhere aren't overwritten.
- Wave analyzer: transcribes audio into a pattern. It can also be fed arbitrary audio as a random source.

### 5.6 Roland (RC303 Generate/Modify; TB-3 random) [primary]

- RC303 **GENERATE** uses RANDOMIZE parameters **ACCENT, SLIDE, PITCH, GATE** (degree of randomness). PITCH: "0 only the root note is used; 100 only the constituent notes are used; 1–99 adjusts the balance". SCALE SETTING: **Root note**, **Key range lower/upper**, **constituent notes** (pitch-class mask repeated in all octaves), and "Set Scale Notes from Variation".
- RC303 **MODIFY:** "Randomly modifies the currently playing pattern. Since accent/slide/octave are modified only for steps whose GATE is on, the rhythmic feel and key of the original pattern are preserved." Right-click undoes. "In some cases, the randomly applied result might not be any different than the previous pattern."
- TB-3: PTN SELECT + SCATTER generates a random pattern (save with REALTIME REC or discard). KEYBOARD + SCATTER does "Random pattern 2": "the octave shift, slide, and accent will randomly change from the current pattern … without changing the character".

### 5.7 Others

- **O303 `AcidPattern::randomize()`:** uniform `key = round(U(0,11))`, `octave = round(U(−2,2))`, accent/slide/gate = `round(U(0,1))`. Because of rounding, the end values get half weight. There is no musical shaping. `circularShift` rotates all `maxNumSteps`, **not** `numSteps`, which is a bug to avoid. `getClosestPermissibleKey` snaps to a scale mask and resolves ties downward [primary, O303].
- **x0xb0x RANDOM mode:** plays fully random notes, with slide and accent commented out (`random()` 8-bit) [primary, X0X].
- **Behringer TD-3 built-in random:** widely called "bad" [lore, SEED].

### 5.8 Recommended generator: "layered, thresholded, seeded"

Design goals: idiomatic output (EAB weights and shapes, TB-3PO anti-clustering), **real-time density without re-roll** (P1 thresholds), an explicit **root** (ABL3/RC303; the P2 gap), **reproducibility** (3PO seed, ABL3 prev/next seed), and **locks**.

**Controls**

| Control | Range | Notes |
|---|---|---|
| Seed | uint64 (UI shows 8 hex digits, digits editable as in 3PO) | New / Prev / Next, history ≥ 32 |
| Root | 0–11 + base octave | Explicit |
| Scale | 12-bit mask; presets: minor, major, dorian, phrygian, minor/major pentatonic, blues, harmonic/melodic minor, chord sets (maj, min, 7, m7, M7, m7♭5, dim), chromatic, custom | "Set from pattern" (RC303) |
| Octave range | low/high in −2..+2 (default −1..+1 = 303 span) | ABL3-style per-octave toggles |
| Note diversity | 0–100 % | 0 = root only, 100 = uniform over the scale (ABL3/RC303 semantics) |
| Degree weights | per-degree 0–2 (default flat) | Optional "tonal gravity" |
| Repeat ("Motion") | 0–100 % | 3PO `force_repeat`; prob that the candidate copies the previous step's degree |
| Pitch source | Scale / Riff bank (EAB 8 shapes + user shapes) / Fix-scale order (ABL3) | |
| Gate density | 0–100 % | Real-time (threshold) |
| Rhythm model | Metric (EAB weights) / Uniform / Euclid(rotation) / Burst | Metric and Uniform are real-time; Euclid and Burst re-derive deterministically |
| Tie amount | 0–100 % | Real-time |
| Accent density | 0–100 % | Real-time; off-beat bias; anti-cluster |
| Slide density | 0–100 % | Real-time; biased toward octave or fifth leaps; anti-cluster |
| Octave jump | 0–100 %, up/down split (default 60/40 up) | Real-time |
| Vibrato density | 0–100 % | P2 parity |
| Lane locks | pitch, octave, gate/state, accent, slide, vibrato | Plus per-step locks |
| Guards | anchor downbeat (on), min notes (default max(2, ⌈len/8⌉)), max rest run (default 4 per 16), slide-into-rest (forbid) | "Anything goes" disables them |
| Actions | Generate, Modify (RC303), Alter (ABL3), Mutate *m* %, Commit/Flatten (P1), Undo | |

**Algorithm.** All per-step draws are made for all 64 steps, independent of length. Each lane gets its own RNG stream, so changing one lane's parameter never reshuffles another lane.

```
// RNG: xoshiro256** (or PCG32) seeded by splitmix64(seed ^ SALT[lane]); lanes:
//   PITCH, REPEAT, OCT, GATE, TIE, ACC, SLIDE, VIB, SHAPE, PERMUTE
// All random values are drawn in a fixed order for i = 0..63, whether or not they are used.

generate(P):
  for i in 0..63:
    s = steps[i]
    if !locked(s, GATE):  s.magGate  = rng[GATE].u7()      // 0..127
    if !locked(s, TIE):   s.magTie   = rng[TIE].u7()
    if !locked(s, ACC):   s.magAcc   = rng[ACC].u7()
    if !locked(s, SLIDE): s.magSlide = rng[SLIDE].u7()
    if !locked(s, OCT):   s.magOct   = rng[OCT].u7();  s.octDirUp = rng[OCT].unit() < P.upBias
    if !locked(s, VIB):   s.magVib   = rng[VIB].u7()
    r = rng[REPEAT].unit(); d = sampleDegree(rng[PITCH])     // always drawn
    if !locked(s, PITCH):
      s.candDegree = (i > 0 && r < P.repeat) ? steps[i-1].candDegree : d
  if P.pitchSource == RiffBank:
    shape = P.shapes[rng[SHAPE].below(P.shapes.size)]        // or user-picked
    for i: if !locked: off = shape[rng[PITCH].below(shape.size)]   // EAB "choose"
                       s.candSemis = off % 12; s.candOctFromShape = off / 12
  materialize(P)

sampleDegree(rng):        // weights over scale members in one octave
  for each pc in scaleMask: w[pc] = lerp(pc==root ? 1 : 0, degreeWeight[pc], diversity)
  return weightedPick(w, rng.unit())

// Threshold function: monotonic in density D, weight-aware, p(0)=0, p(1)=1
p(D, w, wMean) = 1 - pow(1 - D, w / wMean)
on(mag, D, w, wMean) = mag/128.0 < p(D, w, wMean)

materialize(P):           // cheap, called on every knob move; no RNG calls
  W = rhythmWeights(P.rhythm)          // Metric: EAB table indexed i%16; Uniform: all 1
  for i in 0..len-1:
    gated[i] = rhythm in {Metric,Uniform} ? on(magGate[i], P.gate, W[i], mean(W))
                                          : euclidOrBurst(i)   // deterministic from seed
  applyGuards(gated)                    // anchor step 0; min notes: enable rest steps in
                                        //   ascending (magGate/128 - p) order; break rest runs > max
  for i: state[i] = !gated[i] ? Rest
                  : (i>0 && gated[i-1] && on(magTie[i], P.tie, Wtie[i])) ? Tie : Note
  // Tie never on step 0 or after a Rest (guaranteed by construction)
  for Note steps i: pitch[i] = root + degree/semis[i] + 12*oct[i]
     oct[i] = on(magOct[i], P.octJump, 1) ? (octDirUp ? +1 : -1) : 0   (clamped to range;
              shape octave added first in RiffBank mode)
  accent: walk Note steps in order; a = P.accent * (prevNoteAccented ? P.accRepeatFactor : 1)
          accent[i] = on(magAcc[i], a, Wacc[i], mean(Wacc))
          Wacc[i] = 1 + P.offbeatBias * ((i%4==2) ? 1.0 : (i%2==1) ? 0.5 : 0)
  slide:  for Note step i with next played step j:
          if state[j] != Note: slide=false (guard) ; else
          leap = |pitch[j]-pitch[i]|; Ws = 1 + P.leapBias * (leap>=12 ? 1.0 : leap>=7 ? 0.5 : 0)
          s = P.slide * (prevNoteSlid ? P.slideRepeatFactor : 1)
          slide[i] = on(magSlide[i], s, Ws, mean(Ws))
  defaults: accRepeatFactor = 7/16, slideRepeatFactor = 10/18   (from TB-3PO)
            Metric W = [.6,.1,.3,.5,.6,.1,.5,.1,.6,.5,.3,.1,.6,.1,.3,.5]  (EAB)
```
Properties to unit-test:
1. Same seed and parameters give bit-identical patterns on every platform. Use integer RNG and no `std::uniform_*_distribution`, since library implementations differ.
2. In Metric and Uniform modes, raising gate density never removes an existing note; the gated set grows monotonically. The same holds for accent and slide, apart from the anti-cluster coupling.
3. Changing accent density never changes pitches or gates.
4. Changing length never changes steps < min(old, new).
5. Locked steps and lanes are bit-identical after Generate.

**Other operations:**
- **Commit (Flatten):** bake the current states into a plain pattern and clear the generator link. Magnitudes are kept so the user can re-link.
- **Modify:** re-roll the accent, slide, octave and vibrato magnitudes on gated steps only (RC303).
- **Alter:** Fisher-Yates permutation of steps (ABL3). Shift+Alter permutes only pitches among Note steps, which keeps the rhythm.
- **Mutate m:** each unlocked lane value is re-drawn with probability *m*.
- **Build:** force accents on the last *k* Note steps before the loop point. The voice's accent sweep then ramps.

---

## 6. External MIDI play conventions

| Behaviour | Evidence | Our default |
|---|---|---|
| Mono, last-note priority | X0X: new note-on while one is held → slide; note-off acts only if it matches the *last* note (no return to held keys) [primary `midi.c`]. P2: "if a new note comes while playing, the previous one stops … Between overlapping notes, a Slide is executed" [primary]. TD-3: Key Priority Low/High/Last [secondary, TD3E config 0x0C] | **Last**, with Low/High selectable |
| Release to still-held note | O303 keeps a note list; on release it "initiate[s] slide back" to the most recent held note [primary `releaseNote`] | **Slide back** (option: Off) |
| Overlap → slide (legato) | X0X, O303 (`noteList.empty() ? trigger : slideToNote`), ABL3 Note mode ("Slides can be achieved by overlapping two notes"), P2 [primary]. TD-3 "Multi-Trigger Mode" bool = retrigger instead [secondary] | **Slide**; option Retrigger |
| Accent from velocity | O303 `velocity >= 100` [primary]; ABL3 "velocity 100 or greater" (p. 4) and "over 100" (p. 14), inconsistent [primary]; X0X `velocity > 100` (`ACCENT_THRESH 100`) [primary]; P1 "predefined threshold values"; P2 configurable "Accent velocity threshold" [primary]; TD-3 "Accent Velocity Threshold (0 to 127)" [secondary] | **Configurable, default ≥ 100** (inclusive) |
| Pitch bend | P2: symmetric 0–12 semitones [primary]; TD-3 0–12 [secondary]; ABL3 "does not respond to pitch bend" [primary] | ±2, range 0–12 |
| Mod wheel | P2: vibrato depth [primary] | Vibrato depth |
| MIDI transpose | TD-3 "MIDI Transpose −12..+12" [secondary] | Global transpose −24..+24 |
| Explicit accent/slide controllers | None standard in the clones above. Devil Fish has CV Accent In / Slide In (Slide In above 4 V also forces Gate, "ties any and all notes") [primary, DF]. RC303 preview keyboard has ACCENT and SLIDE buttons [primary] | **Learnable CCs: "Accent hold" and "Slide hold"** (≥ 64 = on), plus optional keyswitches. Also honour CC65 (portamento on → treat next overlap as slide; with no overlap, glide anyway), CC68 (legato), CC64 used as P2 does (same-pitch retrigger under sustain = slide carrying the new velocity's accent) |
| Out velocities | X0X sends accent 127, non-accent 100, note-off velocity 32 [primary]; P2 configurable [primary] | **Accent 127, non-accent 90.** 90 is below the threshold for both "> 100" and "≥ 100" receivers. x0x's 100 would read as an accent on ≥ 100 receivers such as O303 and ABL3 |

---

## 7. Interchange formats

### 7.1 Standard MIDI File (import and export): our convention

Export (type 0 or 1, 480 PPQ, tempo meta event, track name = pattern name):
- Note step: Note On at step onset (with swing applied, as an option). Velocity = accent ? `accVel` : `normVel` (127/90).
- Plain note end: `onset + gateQ` (authentic 0.5833 step) or the pattern gate.
- **Tie:** extend the note to the gate-off point of the last tied step. One merged MIDI note.
- **Slide to a different pitch:** the source note's Note Off goes **after** the target's Note On. Overlap of ≥ 1 tick; we use max(1 tick, 1/64 step), so DAW "legato" tools that set off == on don't destroy it.
- **Slide to the same pitch:** MIDI can't overlap one note number. Write a merged note (lossless for sound). If the target step's accent differs from the source's, use the P2 convention: emit CC64 = 127 before the retrigger, Note Off / Note On of the same pitch with the new velocity, then CC64 = 0 at the end of the slid chain.
- Rest: nothing.
- Clip length: `length * stepQ`, *not* padded to a bar. ABL3 pads to multiples of 16 steps [primary]; offer that as an option.
- **Lossless sidecar:** a sequencer-specific meta event (`FF 7F len <manufacturer-id 3 bytes = 0x00 0x7D 0x?? (non-commercial/education ID)> <JSON or CBOR of the pattern>`) or a text event `FF 01 "MW303PAT:" + base64`. On import, if present, it wins. Other software ignores it.
- Optional CC lanes: vibrato as CC#01 (P2 convention); accent and slide as learnable CCs.

Import (from any SMF):
1. Pick the track: the first one with notes, or let the user pick. Reduce to mono, preferring the latest-starting note (then the highest velocity) when several notes start in the same step.
2. Grid: `stepQ` from the user (default 1/16). Quantize onsets with a tolerance of ±25 % of a step. If more than 20 % of notes fall off the grid, warn and suggest 1/8T.
3. Accent = velocity ≥ threshold (default 100).
4. Slide on note A if the next note B starts **before** A ends, by more than 1 tick, or if CC64 is held across a same-pitch retrigger.
5. Tie: if A has no slide and A's end is ≥ `nextStepOn + 0.5*stepQ`, mark the following covered steps as Tie.
6. Length = ceil(lastEnd / stepQ), clamped to 1–64. Base note = the lowest note's C, or the user's choice.

### 7.2 Behringer TD-3

**SysEx pattern dump** [secondary, TD3E `app/td3.js`, observed from hardware/SynthTribe traffic]:
- Header `F0 00 20 32 00 01 0A`, command `0x78` (set/dump). Request is `0x77 group section`.
- Body offsets, counted from F0:
  - `0x08` group (0–3); `0x09` section (A/B, 8 patterns each); `0x0A–0B` 0.
  - `0x0C–0x2B` **16 pitches**. Every byte is split into nibbles across two MIDI bytes (`hi, lo`).
  - `0x2C–0x4B` 16 accents; `0x4C–0x6B` 16 slides; `0x6C–6D` triplet (0/1); `0x6E–6F` step count (1–16); `0x70–71` 0.
  - `0x72–0x75` **tie** bitmask; `0x76–0x79` **rest** bitmask. Each is 4 bytes with only the low nibble used, in the order `[7654][3210][FEDC][BA98]`.
- Pitches, accents and slides are **packed to the front in Note order** (the pitch-list model). Rest bit 1 = rest. Tie bit: **1 = new note, 0 = tie** (ST2M; AINJ fills with 0b1111) [secondary].
- Decoder rule: walk steps 0..15. If rest → Rest. Else if tie bit = 0 and i > 0 → Tie. Else Note, taking packed entry `k++`.
- The config dump also exposes: MIDI ch in/out, MIDI transpose (−12..+12, offset 12), pitch-bend semitones (0–12), key priority (Low 0 / High 1 / Last 2), multi-trigger, clock trigger polarity, clock trigger rate (1 PPS / 2 PPQ / 24 PPQ / 48 PPQ), clock source (Int / MIDI DIN / MIDI USB / Trigger), accent velocity threshold (0–127).

**SynthTribe `.seq` file** (146 bytes) [secondary, ST2M `docs/TD3_SEQ_FORMAT.md`]:
- Magic `23 98 54 76`; UTF-16 "TD-3"; version string; then at `0x24` 16 × 2-byte nibble-split notes, `0x44` accents (flag in the 2nd byte), `0x64` slides, `0x84` triplet, `0x86` length, `0x88` reserved, `0x8A` tie mask, `0x8E` rest mask (nibble order as above).
- **ST2M's converter does not de-pack pitches.** It treats them as 1:1 with steps, which contradicts TD3E and AINJ. Our importer must de-pack by the rule above.
- **Note numbering conflict:** ST2M says MIDI = value + 24. AINJ writes raw MIDI numbers (0x18 = "MIDI C1 (decimal 24)") and pads unused entries with `0x18`. See Open questions; settle it with a SynthTribe-exported file.

### 7.3 x0xb0x

Pattern = 16 bytes [primary, X0X `pattern.h`, `synth.h`, `main.c`]:
- Bits 0–5 = note (0 = rest, `C1 = 0x0B`, up to `0x3F`). Bit 6 = accent. **Bit 7 = slide, meaning slide from this note into the next.** `0xFF` = end of pattern, so short patterns are terminated.
- MIDI number = note + 0x19 (so C1 = 0x0B → MIDI 36).
- No tie: tie = same note + slide. Rests carry accent and slide bits.
- 16 banks × 8 locations; chains up to 32.
- Import: map the per-step model directly (it's already step-aligned, not pitch-list). A rest with the slide bit set under our Hold policy needs special care: approximate it by extending the previous note.

### 7.4 Proposed human-readable text format (tests, clipboard, docs)

```
MW303PAT v1  len=16 div=1/16 swing=0 gate=auto base=C2 name="example"
C  C+:S  D#:A  ~  .  G-:AS  C  .  C:S  C+  .  A#  ~  ~  .  C:A
```
- Token := `.` (rest) | `~` (tie) | NOTE[`:`FLAGS].
- NOTE := `[A-G][#b]?` followed by `+`/`-` per octave relative to base.
- FLAGS ⊂ {`A` accent, `S` slide, `V` vibrato, `P<0-100>` prob, `R<1-4>` ratchet, `G<0-127>` gate}.
- Exact round-trip with §8. Use it for golden tests and "copy pattern as text".

### 7.5 Not pinned down

Roland TB-03 backup `TB3_PTN*.PRM` (ABL3 imports it), ReBirth `.rbs`, technoBox2, Phoscyon native, ABL3 native. No public spec was found. Defer, or reverse-engineer from sample files.

---

## 8. Proposed data model (C++20)

One canonical representation: a **step-aligned grid** with per-step pitch. The authentic **pitch/time view is derived losslessly** plus a spare-pitch tail. That keeps modern editing simple and still gives exact 303 behaviour in authentic edit mode.

```cpp
namespace mw::seq {

inline constexpr int kMaxSteps   = 64;
inline constexpr int kMaxHeld    = 16;   // arp input
inline constexpr int kSparePitch = 48;   // 303 kept 48 pitch entries (TL)

enum class StepState : uint8_t { Rest = 0, Note = 1, Tie = 2 };

enum LaneBit : uint8_t { kLanePitch=1, kLaneOct=2, kLaneGate=4, kLaneAcc=8,
                         kLaneSlide=16, kLaneVib=32, kLaneTie=64 };

struct Step {
  StepState state   = StepState::Rest;
  int8_t   note     = 0;      // semitones relative to Pattern::baseNote; -24..+48 (Note steps)
  bool     accent   = false;  // Note steps only (ignored on Tie/Rest)
  bool     slide    = false;  // SOURCE semantics: glide from this note into the next Note
  bool     vibrato  = false;  // P2/ABL3 parity; voice decides how
  uint8_t  prob     = 100;    // 0..100 %  (ext)  failed roll => behaves as Rest
  uint8_t  ratchet  = 1;      // 1..4      (ext)  sub-hits inside the step
  uint8_t  gate     = 0;      // 0 = pattern gate, else 1..127 => gate = v/127 of step (ext)
  // generator support (persisted so density stays real-time after reload)
  uint8_t  magGate = 255, magTie = 255, magAcc = 255, magSlide = 255, magOct = 255, magVib = 255;
  int8_t   candNote = 0;      // generator candidate pitch (revealed when gate turns on)
  uint8_t  locks    = 0;      // LaneBit mask
};

enum class StepDiv : uint8_t { d1_64, d1_32, d1_16, d1_8, d1_4, d1_2, d1_1 };
enum class DivMod  : uint8_t { Full, Dotted, Triplet };
enum class Direction : uint8_t { Forward, Reverse, PingPong, PingPongRepeatEnds, Random, RandomWalk };
enum class SlideIntoRest : uint8_t { Hold, Release };

struct GenParams { /* §5.8 controls: seed, root, scaleMask, octLo/Hi, diversity, degreeWeights[12],
                      repeat, pitchSource, shapeIndex, gate, rhythm, euclidRot, tie, accent,
                      offbeatBias, accRepeatFactor, slide, leapBias, slideRepeatFactor, octJump,
                      upBias, vibrato, guards..., linked(bool) */ };

struct Pattern {
  char      name[32] = {};
  uint8_t   length   = 16;               // 1..64
  StepDiv   div      = StepDiv::d1_16;
  DivMod    divMod   = DivMod::Full;     // Triplet + d1_8 == authentic 303 triplet mode
  int8_t    swing    = 0;                // -50..+50 (% of a step; §2)
  uint8_t   gateLen  = 0;                // 0 = authentic (3.5 clocks / 6 = 0.5833); else v/127 of step
  uint8_t   baseNote = 36;               // MIDI note for note==0
  int8_t    transpose = 0;               // pattern transpose (semitones)
  Direction direction = Direction::Forward;
  std::array<Step, kMaxSteps> steps{};
  // authentic-mode fidelity: pitch entries not consumed by Note steps (303 "never deleted")
  struct PitchEntry { int8_t note; bool accent, slide; };
  std::array<PitchEntry, kSparePitch> spare{}; uint8_t spareCount = 0;
  GenParams gen{};
  uint32_t  version = 1;
};

struct ArpStepExtra { uint8_t index = 1; int8_t octave = 0; };   // index 1..16
struct ArpPattern { Pattern base; std::array<ArpStepExtra, kMaxSteps> idx{};
                    enum class Missing : uint8_t { First, Wrap, ClampHighest, Rest } missing = Missing::First;
                    enum class AccSrc  : uint8_t { Pattern, Velocity, Either } accSrc = AccSrc::Pattern; };

struct Bank      { std::array<Pattern, 64> seq; std::array<ArpPattern, 64> arp; };  // 4 groups × A/B × 8
struct TrackItem { uint8_t pattern; int8_t transpose; uint8_t repeats = 1; };
struct Track     { std::vector<TrackItem> items; int16_t segno = -1; int16_t dc = -1; };   // P2 song mode

enum class PlayMode   : uint8_t { External, Sequencer, Arpeggiator };
enum class TriggerMode: uint8_t { Transport, HostNotePattern, HostNoteTranspose };
enum class LaunchQuant: uint8_t { EndOfPattern, NextBar, NextBeat, NextStep, Immediate };
enum class HostLink   : uint8_t { Continuous, OnStartOnly };

struct SeqSettings {
  PlayMode mode = PlayMode::Sequencer; TriggerMode trig = TriggerMode::Transport;
  LaunchQuant quant = LaunchQuant::EndOfPattern; HostLink link = HostLink::Continuous;
  SlideIntoRest slideRest = SlideIntoRest::Hold;
  uint8_t trigBaseNote = 24, trigCount = 64;      // host-note pattern zone
  uint8_t transposeRefNote = 48;                   // HostNoteTranspose: this note = 0
  bool    restartOnTrigger = true;                 // false = keep phase (legato pattern change)
  uint8_t accentThreshold = 100, outAccVel = 127, outNormVel = 90;
  bool    midiOut = true;
};

// Voice-facing event (sample-accurate, ordered: NoteOff before NoteOn at same offset)
struct VoiceEvent { enum Type : uint8_t { NoteOn, NoteOff, AllOff } type;
                    int32_t offset; uint8_t note; bool accent; bool legato; bool vibrato; };

}  // namespace mw::seq
```

**Authentic view conversions** (pure functions, unit-tested):
- `toPitchTime(p)`: pitch list = Note steps' {note, accent, slide} in step order, followed by `spare[0..spareCount)`; time list = states.
- `fromPitchTime(pitchList, time)`: `k = 0`. For each step: Note → take `pitchList[k++]`; Tie or Rest → no pitch. The leftover pitches become `spare`.
- **Authentic edit semantics:** editing the time list re-zips (later pitches shift). **Grid edit semantics:** toggling a step inserts or removes its own pitch, and nobody else moves. Per-step extensions (prob, ratchet, gate) stay with *time positions* in both modes.

`finalNote = clamp(baseNote + step.note + pattern.transpose + liveTranspose + trackTranspose, 0, 127)`. Pattern-edit transpose wraps inside the 3-octave window `[baseNote−12, baseNote+23]` (P2/ABL3 behaviour); playback transpose clamps.

---

## 9. Playback state machine

### 9.1 State

```
struct PlayState {
  // clock
  double  anchorQ = 0;          // PPQ of global step 0 (song-locked default: 0.0)
  int64_t nextG = 0;            // next global step index to fire
  int64_t lastFiredG = INT64_MIN;
  double  expectedQ = NaN;      // predicted PPQ at next block start
  bool    wasPlaying = false;
  // pattern position
  const Pattern* cur; const Pattern* queued = nullptr; int64_t queuedAtG = -1;
  int     pos = 0;  int dir = +1;   // pattern step index, ping-pong direction
  int64_t patternStartG = 0;        // g at which cur started (for anchoring pos)
  // gate / articulation
  bool    gateOpen = false;
  bool    slidePending = false;     // slide flag of the sounding note, carried through Ties
  uint8_t soundingNote = 0;
  double  pendingOffQ = +INF;       // lazy gate-off decision instant
  // ratchet sub-hits for the current step
  int ratchetLeft = 0; double ratchetPeriodQ = 0; double nextRatchetQ = +INF;
  // per-g resolution cache (ring of 4): {g, stepIndex, resolvedState, rollPassed, overrides}
  Resolved cache[4];
};
```

### 9.2 Per-block driver

```
processBlock(n, hostPos):
  if (!hostPos.playing):
     if wasPlaying: emitAllGateOff(offset 0); reset slidePending, pendingOffQ, ratchets
     wasPlaying = false;  (if internal RUN button active: drive from internal clock instead)
     return
  bpm = hostPos.bpm (fallback 120); spq = sr*60/bpm
  q0 = hostPos.ppq   (fallback: timeInSamples/spq from internal counter)
  if (!wasPlaying) startAt(q0)
  else if (|q0 - expectedQ| > relocTolQ)  relocate(q0)     // relocTolQ = max(1e-6, 1 ms * bpm/60000)
  qEnd = q0 + n/spq
  if (hostPos.isLooping && loopEndQ in (q0, qEnd)): split block at loopEnd → process [q0,loopEnd),
        then relocate(loopStartQ) and process the rest with q shifted   // hosts that don't split blocks
  loop:
     tOn  = onQ(nextG)                          // anchor + g*stepQ + swing(g)
     tOff = pendingOffQ;  tR = nextRatchetQ
     t = min(tOn, tOff, tR);  if t >= qEnd: break
     off = clamp(llround((t - q0)*spq), 0, n-1)
     if t == tOff (ties: tOff before tR before tOn):  gateOffDecision(off)
     elif t == tR:                                    ratchetHit(off)
     else:  if nextG > lastFiredG: boundary(nextG, off, t); lastFiredG = nextG
            nextG++
  expectedQ = qEnd; wasPlaying = true
```

`startAt(q)` and `relocate(q)`:
- Emit AllOff/NoteOff at offset 0 if the gate is open. Clear `slidePending`, the ratchets and the cache.
- `g = floor((q - anchorQ)/stepQ)`. If `q - onQ(g) <= chaseQ` (default 1/64 quarter, i.e. you landed essentially on the boundary) then `nextG = g`, otherwise `nextG = g+1`. We never start a note mid-step, except with the optional "chase" setting.
- Recompute `pos` from `(g - patternStartG)` using the direction rules. Ping-pong and random are recomputed by replaying `g - patternStartG` steps of the deterministic direction sequence (≤ 64-step period for ping-pong; random uses the RNG `hash(seed, g)`, so it's stateless).
- `lastFiredG = nextG - 1`.
- `HostLink::OnStartOnly` (P2 default) ignores later discontinuities: it never calls relocate after start and keeps counting from its own expected q.

### 9.3 Step boundary

```
boundary(g, off, tQ):
  applyQueuedPatternIfDue(g)            // EndOfPattern: when pos would wrap; NextBar: tQ on bar line
                                        //   (host lastBarStartQ + timeSig); NextBeat; NextStep; Immediate
  r = resolve(g)                         // stepIndex via direction; prob roll = hash(seed,g) < prob;
                                         //   Live Override set/clear masks; failed roll => Rest
  s = r.step
  switch r.state:
   case Note:
     legato = gateOpen && slidePending
     if gateOpen && !legato: emit NoteOff(off)               // retrigger (tie chain / held rest ends)
     emit NoteOn(off, finalNote(s), s.accent, legato, s.vibrato)
     gateOpen = true; soundingNote = finalNote(s)
     if s.ratchet > 1 && !s.slide:      // slide on a ratcheted step applies to its last sub-hit only
        ratchetLeft = s.ratchet-1; ratchetPeriodQ = actualStepQ(g)/s.ratchet
        nextRatchetQ = tQ + ratchetPeriodQ
        pendingOffQ = tQ + gateQ(s)/s.ratchet
     else pendingOffQ = tQ + gateQ(s)
     slidePending = s.slide
   case Tie:
     if gateOpen: pendingOffQ = tQ + gateQ(s)   // slidePending unchanged (carries through ties)
     // else silent
   case Rest:
     if gateOpen:                                // only possible via SlideIntoRest::Hold
        pendingOffQ = tQ + gateQ(patternGate); slidePending = false
  advancePos()

gateQ(s) = min( (s.gate ? s.gate/127.0 : patternGateFrac) * stepQ,
                nextOnQ - onQ - minGapQ )        // never reach the next onset unless legato
```

### 9.4 Lazy gate-off decision

This runs at the instant the gate *would* close. We look at the next step only now, so edits made during the step are respected.

```
gateOffDecision(off):
  pendingOffQ = +INF
  if !gateOpen: return
  nr = resolve(nextG)                    // cached; also used by boundary(nextG)
  holdForSlide = slidePending && (nr.state == Note
                 || (nr.state == Rest && settings.slideRest == Hold)
                 || nr.state == Tie)
  holdForTie   = (nr.state == Tie)
  if holdForSlide || holdForTie: return  // gate stays open; next boundary decides
  emit NoteOff(off); gateOpen = false; slidePending = false
```

`ratchetHit(off)`: emit NoteOff(off) if the gate is open, then NoteOn(off, soundingNote, accent of step, legato=false). Then `pendingOffQ = now + gateQ/ratchet`. On the last sub-hit, `pendingOffQ = now + gateQ/ratchet` and `slidePending = step.slide` (so a slide leaves from the last hit).

### 9.5 Edge cases (each needs a unit test)

1. **Slide → Note:** gate held, legato NoteOn at the next boundary, no envelope retrigger [RW-SLIDE].
2. **Slide → Tie… → Note:** gate held through the ties, then legato into the Note [RW-SLIDE rule 2].
3. **Slide → Rest:** Hold extends the old pitch until the rest's gate-off [X0X]. Release cancels the slide and uses a normal gate-off [O303].
4. **Slide → Tie → Rest (Hold):** held through the tie, then off at the rest's gateQ.
5. **Slide on the last step:** glides into the next cycle's (or the next queued pattern's) first Note [X0X/O303/3PO].
6. **Slide to the same pitch:** legato NoteOn with the same note. The voice must accept it as a tie and may change the accent state.
7. **Tie after Rest, or Tie at g = start with gate closed:** silent.
8. **Tie chain then Note without slide:** NoteOff then NoteOn at the same offset, in that order.
9. **1-step pattern with slide:** a continuous drone (legato to itself). Without slide: retrigger every step.
10. **Probability fail** on the next Note: the slide-hold decision sees a Rest and applies the policy. Rolls are cached per g, so the decision and the boundary agree. They are deterministic from (pattern seed, g), so an offline render is identical to the realtime one.
11. **Ratchet + legato in:** the first sub-hit is legato (no retrigger) and later sub-hits retrigger.
12. **Pattern switch queued:** happens at the wrap. A pending slide carries over. `NextStep` keeps `pos` (clamped/wrapped to the new length) unless `restartOnTrigger` is set.
13. **Length shortened while playing:** if `pos ≥ newLen`, wrap to 0 at the next boundary. A pending queued pattern isn't affected.
14. **Edit of step k while it sounds:** takes effect next cycle. An edit to the *next* step before the lazy decision affects the hold decision (intended).
15. **Live transpose / host-note transpose:** applied to the **next NoteOn** (option: quantized to pattern start, as on X0X). It never retunes a sounding note mid-glide. A legato transposition key keeps the phase.
16. **Transport stop:** NoteOff at offset 0 (the voice releases normally). Clear slide. No hanging notes; this was a P2 2.1.0 fix area.
17. **Relocation or loop wrap:** NoteOff, clear slide, re-anchor. If the loop start is on a boundary, the step fires at once at sample 0 of the post-wrap segment.
18. **PPQ jitter** (host reports q slightly behind the predicted value): `lastFiredG` prevents double fire. Tolerance `relocTolQ`.
19. **Negative PPQ (pre-roll / count-in):** `floor` and positive modulo for `pos`. Steps fire normally (or option: mute before 0).
20. **Tempo change between blocks:** no state change needed (PPQ domain).
21. **Swing with odd lengths:** parity comes from the host grid (§2). Triplet and dotted divisions ignore swing.
22. **Gate ≥ step without slide:** clamped to `nextOn − 1 sample`, so the voice never sees an accidental overlap. Overlap means slide, by definition, in External mode and in MIDI export.
23. **Event ordering at one sample:** NoteOff < NoteOn. For MIDI out of a slide: target NoteOn at t, then source NoteOff at t + 1 sample (live) or + 1 tick (export).
24. **Host-note triggering (P2/ABL3):** a note-on in the trigger zone queues or launches that pattern per `LaunchQuant`, with anchor = launch g. Note-off of the last held trigger note stops at the next step boundary (P2 "will eventually stop"). A second trigger note while one is held is a legato pattern change: keep the phase if `restartOnTrigger` is false. Transport Start is ignored in this mode (P2).
25. **HostNoteTranspose (O303 style):** the sequencer runs while a key is held. The pattern is played relative to the key. O303 restarts from step 0 on every note-on (`sequencer.start()`); we offer restart or keep-phase.
26. **External mode while transport runs:** the sequencer is muted and live MIDI goes straight to the voice (P2 EXT). Sequencer and live notes never merge unless an explicit "Live layer" option is on.
27. **Arp key-up mid-gate:** if the set becomes empty the cursor resets. The sounding note ends at its scheduled gate-off unless Latch is on.
28. **Block with no PPQ from the host:** fall back to an internal sample counter at host bpm (or 120). Anchor at plugin start.

---

## Open questions

1. **Slide followed by a rest on a real TB-303.** Two possibilities: x0x-style hold through the rest's gate time, or Open303-style cancel. *Handle:* the `SlideIntoRest` option, default Hold. Verify on a 303 (or capture the gate out of a TD-3 or TB-03) before 1.0.
2. **Gate length in triplet mode.** Either 3.5 clocks absolute or 58.3 % proportional. *Handle:* authentic mode uses 3.5 clocks (absolute). Expose `gateLen`. Measure a TB-03 or TD-3 gate out.
3. **Track memory total** (SN "Total 256 measures (64 x 7)"). *Handle:* irrelevant in software. Allow 64 entries per track, no total cap.
4. **TB-03/TB-3 shuffle scaling (−50..+50).** *Handle:* linear `swing = v/100` of a step. Calibrate by recording TB-03 MIDI clock-locked output if a unit is available.
5. **Phoscyon 2 randomizer internals** (whether v2 still uses v1's stored 0–127 magnitudes; how "Note" density works). *Handle:* not needed for parity. Our threshold model is a superset.
6. **Phoscyon 2 Host Note base note** ("C note of the second lowest octave"; MIDI 12 or 24 depending on octave naming). *Handle:* configurable `trigBaseNote`, default 24.
7. **TD-3 note numbering** (raw MIDI number vs value + 24) and whether ties consume a packed pitch entry on the TD-3. *Handle:* implement the decoder per §7.2, with a unit test from a SynthTribe-exported `.seq` and a sysex dump of a known pattern. If they disagree, the hardware dump wins.
8. **ABL3 slide semantics** (its manual says the slide flag ties "to the previous step"). *Handle:* any future ABL3/ReBirth importer must check with a known pattern. Our model is source semantics.
9. **RC303 PLAY MODE option list** (SEED claims Forward/Reverse/Fwd&Rev/Invert/Random). *Handle:* ship Forward, Reverse, PingPong, PingPongRepeatEnds, Random, RandomWalk.
10. **TB-03 `.PRM`, ReBirth, technoBox2, Phoscyon and ABL3 native formats:** unreached. *Handle:* defer; SMF + TD-3 + text format first.
11. **Linux drag-and-drop of MIDI files into hosts** (JUCE `performExternalDragDropOfFiles` on X11/XWayland in Bitwig, Reaper, Ardour). *Handle:* test early. Fallbacks: "Export .mid…", "Reveal in file manager", and a copy-path button.
12. **Accent-latch-after-stop** (DF): authentic, but surprising in a DAW. *Handle:* option, default off.

## Implications for the build

1. **Canonical model = step grid with per-step pitch** (§8). Derive the 303 pitch/time view losslessly with `spare` pitches. Ship **two edit modes**: Grid (default) and Authentic (re-zip on time edits; ties and rests consume no pitch; accent and slide live on pitch entries).
2. **Slide uses source semantics** (flag on the note slid *from*). Gate is held into the next Note. The NoteOn carries `legato=true` and the step's accent. The voice must not retrigger envelopes on legato. Slide wraps across the loop and across queued pattern changes.
3. **Authentic gate = 3.5/6 of a 1/16 step (0.14583 quarter)**, computed in PPQ, with per-pattern `gateLen` and per-step override. NoteOff precedes NoteOn at equal offsets. A non-legato gate never reaches the next onset.
4. **Timing is host-PPQ-locked and sample-accurate**, using the closed-form `onQ(g)` from an anchor. Relocate on discontinuity (`relocTolQ` ≈ 1 ms). Split at in-block loop wraps when the host reports loop points. Dedupe with `lastFiredG`. Offer `HostLink::OnStartOnly` for P2 parity.
5. **Swing:** per pattern, −50..+50 % of a step, applied by **host-grid parity**. Triplet and dotted divisions ignore it. Clamp gates to the shortened window.
6. **Step division per pattern:** 1/64…1/1 × Full/Dotted/Triplet. Authentic triplet = 1/8T (8 clocks), max 15 steps in authentic mode, 64 in grid mode.
7. **Pattern store:** 64 seq + 64 arp patterns (I–IV × A/B × 8 addressing), each up to 64 steps. Queued switching with `LaunchQuant` (default EndOfPattern). Host-note triggering zone (default C1 = 24, 64 notes) and a host-note transpose mode.
8. **Edit ops:** shift/rotate over `length` (not over the 64-step buffer, which is the O303 bug), transpose with 3-octave wrap (pattern edit) or clamp (playback), reverse, invert (mirror around root), copy/paste/clear (ABL3 Clear = C + gate on; Shift = gates off), Alter, Live Override, full undo.
9. **Generator:** implement §5.8 exactly. Integer RNG per lane from `splitmix64(seed^salt)`. Stored 0–127 magnitudes plus monotonic threshold `p = 1−(1−D)^(w/w̄)`, so density knobs are real-time. EAB metric weights and riff shapes (credit CC BY 4.0 in About/NOTICE). TB-3PO anti-cluster factors (7/16, 10/18). Explicit root, scale mask, octave range, diversity. Locks, Commit/Flatten, Generate/Modify/Alter/Mutate. Golden-file tests for determinism across compilers.
10. **Arpeggiator:** P2 index-pattern model (sorted index 1..16, missing → First), classic-mode fillers, octave range, repeater, latch, cursor reset on full release, accent/slide source options, an independent articulation lane, and **arp output recordable and exportable to MIDI** (P2 can't do this).
11. **External MIDI:** mono, last-note priority (Low/High optional), overlap = slide (Retrigger option), slide back to a still-held note, accent = velocity ≥ 100 (configurable), pitch bend 0–12 (default ±2), mod wheel → vibrato, learnable Accent-hold / Slide-hold CCs, CC65/CC68/CC64 honoured as in §6.
12. **MIDI out and export:** accent 127 / normal 90. Slide = overlap ≥ 1 tick (≥ 1 sample live). Same-pitch slide = merged note, or CC64-wrapped retrigger when the accent changes. Tie = merged note. Lossless pattern sidecar in an SMF meta event. Clip length = pattern length (pad-to-bar option). MIDI out can be disabled (loopback guard).
13. **Import:** SMF (§7.1 heuristics), TD-3 sysex and `.seq` (packed-pitch de-packing, tie bit 0 = tie), x0xb0x 16-byte patterns, and the §7.4 text format. Every importer gets fixture tests.
14. **Determinism:** probability rolls, random direction and ratchets all come from `hash(patternSeed, g)`. They are cached per g so look-ahead (the lazy gate-off decision) and the boundary agree, and offline renders match realtime.
15. **Real-time safety:** the generator's `materialize()` runs on the message thread and publishes an immutable pattern snapshot via an atomic pointer swap or a lock-free FIFO. The audio thread never allocates, never calls the RNG for generation, and only reads snapshots and the per-g hash.
16. **Song mode** (tracks with D.C./Segno/D.S., chains up to 32 patterns, per-entry transpose) is **P2**. Keep `Track` in the data model now so presets stay forward-compatible.
