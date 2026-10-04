# mw12F factory library

859 files: 858 presets in twelve type folders, plus `default.mw12f`, the session state a fresh instance loads. Every one of them is generated. Nothing in this folder is edited by hand; the recipes live in `tools/factory/` and `factory-build` writes the files through the core's StateCodec writer, so the bytes are canonical and a second build changes nothing.

| Type | Files | What is in it |
|---|---|---|
| `scene` | 127 | Chicago 12, Anthem 12, Rolling 12, Melodic 12, Minimal 12, Hard 12, Trance 10, Dub 10, Breaks 10, Experimental 10, Polymeter 8, Teaching 6, Showcase 1 |
| `synth` | 183 | Stock 33, Drive 18, Squelch 15, Deep Bass 14, Scream 14, Liquid Line 12, Sustain 12, Pluck 10, Space 10, Whistle 10, Overdrive 8, Filter FM 8, Sub 8, Tracking 6, Utility 5 |
| `pattern` | 310 | 127 main lines, 127 variations, 56 generated lines |
| `arp` | 56 | Up 20, Up Down 8, Octave Spread 8, Pedal 7, Random Walk 5, Chord 4, Down 4 |
| `template` | 90 | one per built-in generator template (76), plus 14 acid templates that also set the density knobs |
| `calibration` | 22 | stock reference, unit-to-unit spread, worn units, extended trims |
| `distortion` | 18 | 16 built-in voicings with their own front panel, 2 custom voicings |
| `delay` | 11 | |
| `limiter` | 11 | |
| `eq`, `reverb`, `chorus` | 10 each | |

`default.mw12f` and `scene/first-squelch.mw12f` hold the same scene. The first is written as a `state` document, because it is what a new instance restores and not a preset; a preset browser lists scenes, so the scene appears once, as First Squelch from `scene/`. The scene: saw, a squelchy line on A1 in A, diode drive with a low lift (tagged Medium), a light dotted echo and a tight room. Its bank A is a tour: the opening line, six variations of it, then one line each from the Anthem, Chicago, Hard (twelve-step triplet), Polymeter (13 steps) and Melodic (32 steps) scenes, all in the same key.

## Levels

Levels are measured on both channels (the RMS of L and R together) with true peaks (4x oversampled), and set through `master_volume` only, so the sound is exactly what the recipe says. The target is -24 dBFS RMS, with no true peak above -1 dBTP unless the preset's own limiter is on.

A synth preset is not measured on one line. Accents move the level a long way (the stock accent adds about 11 dB of amplitude at full Accent), so a preset matched on a line with three accents can sit 5 dB hot on a line with an accent on every step. Every synth preset plays three lines for four bars: the first-load line (16 steps, three accents, three slides, a tie, an octave jump, a rest) at 125 BPM, an all-accent anthem line at 130 BPM, and a sparse line (five notes, one accent, two ties) at 120 BPM. Each line has its own target, because a denser line reads louder and a sparser one quieter for the same note level: -24.0, -22.0 and -28.0 dBFS, which are the library's median offsets between the lines, fixed in `qa.py`. The level trim is the one that minimises the squared error over the three lines, held back where the worst line's true peak would pass -1 dBTP. A preset passes when its mean error is within 1.5 dB and each line is within 3 dB of its own target.

The current library: on the reference line synth presets sit at -26.1 to -21.9 dBFS, on the anthem line at -23.8 to -19.1 and on the sparse line at -30.5 to -26.9. Matched on the reference line alone, the anthem line had run -25.0 to -17.8 and four presets peaked above 0 dBFS on it. Every preset's mean error is between -1.4 and 0.0 dB (17 are held under the target by their true peak), every line of every preset is within 2.9 dB of its target, 172 to 176 of the 183 are within 1.5 dB on each line, and no preset peaks above -1.25 dBTP on any line.

Every scene plays its own A1 at the tempo it was written for, and its A1 sits at -25.2 to -24.0 dBFS. Every other slot stays within 3 dB of its A1, except Breakdown, Half Time and Build slots, which thin the line out on purpose and may drop to 10 dB under it. The other slots sit 2.4 dB under to 2.9 dB over their A1; the thinning slots 4.6 dB under to 1.4 dB over.

Where a preset could not meet the rules, the recipe was changed rather than the rule: accent volume decoupled from Accent (Shriek, Siren Song, Short Amp, Metal Tongue, Fast Sweep Snap, Vanishing Point and five scenes keep the full filter accent and lose some of the jump in level) or light diode drive in front (Accent Only, Ghost Notes). Two variation rules changed for level. A Mutation now plays within a quarter of the line's note count and gets the line's ties back (on a sustaining sound the ties are much of the level; without them Dub relatives played 4 to 5 dB quiet, and counting ties as notes made a tied Minimal line's relative 3 dB loud). In Experimental banks Mutation is the last resort: those lines are each written for one Mods behaviour (tracking, accent-only percussion, vibrato, ratchets, drones), a generated relative keeps none of it, and it played 3 to 7 dB away from the main line.

Mono: summed to mono, synth presets and scene A1s lose 0.0 to 2.9 dB (median 0.0). `qa.py` fails anything that loses more than 3 dB; Smoke Ring did, through the Wet Wash chorus and a wide dotted echo, and now runs both narrower.

### Effect presets

Each effect preset is applied over Init Line on the first-load line and compared with Init Line alone in integrated loudness (BS.1770, LUFS), so flicking through presets does not reward the louder one. Every effect preset sits within 1 LU of the sound without it: distortion, chorus and limiter presets within 0.05 LU, EQ -0.6 to +0.8, delay -0.5 to 0.0, reverb -0.2 to +0.6.

The trims are measured, through each block's own output control where it has one: `dist_volume` for distortion (the one voicing row a distortion preset may set away from its model's value; the models' own trims are fitted on the voice, so a distortion preset now moves it by -0.6 to +1.5 dB from its model), `lim_gain` for limiters, and the wet level `cho_volume` for chorus. The chorus needs the most: its mix is a linear crossfade and its two lines are delayed 5 to 25 ms, so the wet is largely decorrelated from the dry and the sum loses 0.9 dB at 10 % mix and 3.2 dB at 50 %. Only a louder wet makes that up, so the chorus presets carry wet levels of +3.0 to +12.0 dB, and at a low mix the chorus is more audible than the mix knob suggests. EQ, delay and reverb have no output control; their recipes were matched by hand (Low Trim's cut moved down to 30 Hz, Mid Scoop and Smile shallower, Telephone's mid band raised to +10 dB, Deep Space and Endless Void at a lower mix). Calibration units have no output control either and change the voice itself; they stay within 8 LU of the reference unit (-1.4 to +0.8 LU).

Synth presets and scenes that name an effect preset (`+chorus=slow-swirl`) copy it with its trim, so a sound and the effect preset it names always hold the same settings.

### Distance between presets

Two synth presets must not sound alike. Each plays the first-load line; `qa.py` takes a 40-band log-frequency spectrogram of each (level removed) and the RMS difference in dB between every pair. For scale, a 10-point resonance move reads 1.3 to 1.6 dB on this measure and a 5-point cutoff move 0.9 to 1.6 dB, and the floor is 2.0 dB. The closest pair is now Copper Wire and Thin Coupling Pluck at 2.11 dB; the median preset's nearest neighbour is 5.3 dB away. Bench Line (Init Line on the bench-ideal unit) and Out Of The Box (Init Line with four knobs moved 5 to 10 points) were merged into Init Line; Low Trim Groove now raises its cutoff knob to 48 so the low trim's darker floor is what you hear; Pip Squeak, Cellar Door, Chalk Line, Low Tide and Rumble Strip were moved away from their neighbours.

## Tempo

Scenes carry the tempo they were written for as `internal_bpm`, and the description says it too ("Chicago scene in F, written for about 122 BPM"). With a host tempo the scene follows the host; the standalone app and hosts that send no tempo play it at its own tempo, so Drone Field runs at 70 BPM and the Hard scenes at 136 to 150.

## Scenes and their banks

A scene is a sound from the synth library (sometimes with an effect or a knob changed for the scene), and bank A filled from one hand-written line in the scene's key. A1 is the main line; the other slots are variations made by rule, chosen in an order each style prefers, and kept only if they differ from everything already in the bank by more than a rotation along the grid or a transposition:

- **Push**: the line rotated along the grid, landing on a note (the one rule that is a rotation by design: it moves the line against the kick)
- **Shift**: the accents moved one note later (skipped on lines of one pitch, where it is only a rotation)
- **Up Top**: one or two off-beat notes up an octave, with a slide into each
- **Breakdown**: only the notes on the strongest beat phase and the accents, ringing into the gaps
- **Answer**: two bars, the second half of the second bar moved by the style's interval (a fourth for Chicago and dub, a minor third for rolling and hard lines, an octave lift for anthems) with its accents moved
- **Fill**: the last beat replaced by a root, its octave under a slide, a ratcheted neighbour in the same octave as the octave note, and a slide back into the loop, with accents only where the line already uses them; only on a line whose last beat already plays two notes or more (a line that leaves it empty keeps the space)
- **Glide**: two slides added to the longest unslid run
- **Half Time**, **Triplet** (the first 12 steps on eighth-note triplets), **Drift** (a different loop length), **Build** (accents on the even steps only, for anthem builds)
- **Mutation**: a generated relative that uses the line's pitch classes and the octaves it really lives in, rests where the line rests on a beat, plays within a quarter of its note count, keeps at least half its accents and slides, and gets its ties back (where the line ties, then after other notes)

Every variation also passes the generator's chaotic-leap rule against its own main line: it may not leap past the octave, or over a fifth without a slide, more often than the main line does (the step from the last note back to the first counts). Breaks and Pitch Morse lines leap past the octave by design, so their variations may too.

122 scenes have six slots, two have five (Grey Light, Quiet Machine), two have four (Drone Field, Decay Contrast), and the default has twelve. Experimental and Teaching banks may use Half Time, which suits a drone and a decay study. The arpeggiator bank of every scene holds four arp patterns so ARP mode has something to play. Scenes carry the sequencer settings (trigger mode, launch quantise, slide into rest, note priority), the effects order and the internal tempo; they leave quality tiers, MIDI routing and host following alone.

The pattern library takes every main line and one variation per scene, and only a variation that is a new line: Push and Half Time (the main line moved along the grid, or the same steps at 1/8) stay scene slots.

The styles follow the factory content brief in `docs/research/05`. Anthem lines use one or two pitch classes with accents on 70 to 100 % of the notes. Chicago lines tie 2 to 9 steps in 16 and accent at most a third of their notes; four of them swing (15 to 25 %), as do three Dub lines (15 to 25 %) and five Breaks lines. Minimal lines rest on 5 to 11 steps in 16, and the Minimal, Polymeter and Experimental styles use lengths 5, 7, 9, 11, 13 and 15. Three Minimal and two Experimental lines put a probability under 100 % on some steps, so they change from bar to bar. Hard scenes keep Env Mod low (28 to 40) and let the drive do the work. Every Melodic scene has Amp Sustain on (25 to 50) so ties hold; three Melodic lines run four bars (64 steps), the rest two. Teaching scenes isolate one mechanism each: four accents in a row on fixed knobs (the accent capacitor climbing), slides into the octave, tie against slide against plain note, overdrive before the filter against a pedal after it, decay against accent decay, and a slide into an accented note.

### What the format cannot hold

Some of the brief does not fit a scene file, which stores one pattern per slot with a single length and no automation:

- independent accent-lane and slide-lane lengths for the Polymeter studies (the polymeter here comes from odd pattern lengths instead)
- layered scenes of two or three voices with offset accent phases (one scene is one voice; MIDI out to a second instance is a session setup)
- chained pairs of 12-step triplet patterns (the Hard scenes put a triplet line in its own slot; chaining is up to the player)
- automation: the slow Chicago cutoff rides and "15-minute jam" demos, Anthem resonance and cutoff arcs, the Minimal mute-and-return moves, big-beat whisper-to-scream builds, the slow LFO on cutoff for drones
- arpeggiator playback in QA: `acid-render` plays the sequencer, so arp patterns and templates are checked as files only

## How the patterns were made and curated

Every hand-written line is checked against its style's rules when the library builds, and a line that breaks them stops the build. The rules, per 16 steps:

| Style | Pitch classes | Accent share of notes | Slides | Rests | Ties | Octave steps |
|---|---|---|---|---|---|---|
| Chicago | 3 to 7 | 0.05 to 0.35 | 0 to 3 | 0 to 3 | 2 to 9 | |
| Anthem | 1 to 2 | 0.70 to 1.00 | 0 to 7 | 0 to 1 | 0 to 2 | |
| Rolling | 1 to 4 | 0.10 to 0.45 | 0 to 4 | 0 to 2 | 0 to 1 | 2 or more |
| Melodic | 5 to 8 | 0.05 to 0.35 | 0 to 4 | 0 to 4 | 0 to 7 | |
| Minimal | 1 to 3 | 0 to 0.40 | 0 to 2 | 5 to 11 | 0 to 5 | |
| Hard | 1 to 4 | 0.10 to 0.40 | 0 to 4 | 1 to 5 | 0 to 2 | 1 or more |
| Trance | 1 to 4 | 0.10 to 0.50 | 0 to 3 | 2 to 5 | 0 to 2 | |
| Dub | 1 to 5 | 0 to 0.40 | 0 to 3 | 3 to 10 | 1 to 8 | |
| Breaks | 3 to 6 | 0.50 to 1.00 | 0 to 4 | 0 to 1 | 0 to 1 | 6 or more |
| Polymeter | 1 to 3 | 0 to 0.50 | 0 to 4 | 0 to 8 | 0 to 4 | |

Two more rules keep the hand-written lines apart. Within a style, no two lines of the same length share more than 80 % of their rhythmic skeleton (step by step: rest, tie or note, and whether the note is accented and slides). Across the library, no two lines of the same length play the same thing on more than 80 % of their steps at any rotation and transposition. The skeleton rule is what broke up the Chicago lines that shared one tie-and-rest shape, the Trance lines that rested on the same steps, the Rolling lines that were sixteen straight notes, the Anthem lines that differed by one slide, and the Breaks lines that were sixteen accented notes with nothing else.

Lengths are whole bars for the four-on-the-floor styles (8 or 16 steps, 12 for triplets) and two or four bars for melodic lines. Every shipped pattern, made by hand or not, also passes the sanity rules: at least two notes, no tie on step 1 or after a rest (it would play as a rest), no slide into a rest.

Generated lines come from the core's generator with a style's note mask, octave mask and density amounts. The generator writes no ties, so Chicago and Dub draws get them afterwards: unaccented, unslid notes after an unslid note become ties (repeated pitches first, then off-beats) until the line has one more than the style's minimum. Then every draw goes through the same style table and three more sets of rules:

- **Boring**: one pitch with fewer than two accents or no slide; fewer than three notes a bar; a hole of rests longer than the style allows.
- **Chaotic**: any leap of more than an octave between neighbouring notes, the loop seam included; more than three leaps over a fifth per 16 steps without a slide (octaves of the same note excepted); a melodic line where fewer than half the steps move by four semitones or less; more than five slides per 16; an accent share between 0.45 and 0.6 in a sparse-accent style (acid accents are either rare or nearly everywhere, rarely in between).
- **Too close**: a line that matches an accepted one, or a scene line of the same style, on more than two thirds of its steps.

The last build drew 4,015 candidates for the 56 generated library lines and turned down 3,959: 3,021 for leaps past the octave (the loop seam included), 853 for an accent share outside the style or in the dead zone, 52 for too few octave steps, 12 for rests, 12 for pitch classes, 7 for melodic lines that did not walk, one for too many slides and one for too many wide leaps without a slide. Generated lines keep their generator memory, so the density knobs still add and remove notes on them without re-rolling (tied steps stay put). The generated trance lines have their beats locked as rests in the gate lane, which is how they stay off the kick. Each has a name of its own, like the hand-written lines.

## Names

Preset, pattern and scene names are original. `scripts/branding-audit.sh` keeps equipment brands out; `tools/factory/forbidden_names.txt` lists the rest a title could point at: well-known records, artists, labels, a record shop, and product and game trademarks. `factory-build` checks every name, description and tag it writes, and every pattern name inside a scene, against that list (whole words, any case), and so does `factory-build --verify` for files already on disk. The list includes the names this library used to carry and dropped for that reason (a 1991 techno record, a big-beat single, a breakbeat classic, a house duo and its single, two more electronic tracks, a Chicago record shop, a game, a lamp, a sandwich chain and a television series).

## Tags

Tags are written comma separated, in a fixed order, from this vocabulary only (the build rejects anything else). No tag sits in two groups: the families Deep Bass and Liquid Line are separate from the characters Deep and Liquid.

| Group | Tags | Used on |
|---|---|---|
| Play Mode | Seq, Arp | scenes, arp patterns |
| Style | Showcase, Chicago, Anthem, Rolling, Melodic, Minimal, Hard, Trance, Dub, Breaks, Experimental, Polymeter, Teaching | scenes, patterns, acid templates |
| Family | Stock, Squelch, Drive, Deep Bass, Scream, Whistle, Liquid Line, Pluck, Space, Overdrive, Filter FM, Tracking, Sub, Sustain, Utility | synth presets |
| Waveform | Saw, Square | scenes, synth presets |
| Distortion | None, Light, Medium, Strong | scenes, synth presets |
| Character | Squelchy, Screaming, Deep, Whistling, Plucky, Liquid, Dark, Bright, Gritty, Rubbery, Talking, Percussive, Droning, Chaotic, Warm, Hollow, Spacey, Clean, Noisy, Vintage, Aggressive, Smooth | scenes, synth presets, calibration units |
| Mods | Mods | anything with the Mods layer switched on |
| Pattern | Main, Variation, Generated, One Note, Two Note, Melodic Line, All Accent, Sparse Accent, Slides, Octave Jumps, Ties, Rests, Shuffle, Triplet, Odd Length, Long, Half Time, Vibrato, Ratchet | patterns, arp patterns |
| Arp Shape | Up, Down, Up Down, Chord, Pedal, Random Walk, Octave Spread | arp patterns |
| Unit | Stock Unit, Unit Variation, Worn, Extended Range | calibration |
| Effect | Short, Long, Dotted, Wide, Filtered, Tempo Sync, Free Time, Room, Plate, Hall, Subtle, Heavy, Glue, Safety, Scoop, Boost, Cut, Custom Voicing, Pump, Motion, Clean Low | effect presets |
| Template | Scale, Pentatonic, Chord, Interval, Acid, Other | templates |

Distortion intensity is computed, not chosen: the drive knob's position between the voicing's preamp minimum and maximum (and 0.8 of the Mods overdrive, which saturates the filter input), with Light under 30, Medium under 55, Strong above. Pattern tags are computed from the steps. Of the 183 synth presets, 112 are tagged None, 25 Light, 31 Medium and 15 Strong; 66 are square; 50 use the Mods layer.

A preset that uses distortion carries every voicing parameter and `dist_model` set to that voicing's index, with the voicing's values except, on distortion presets, `dist_volume` (the output trim the level matching sets); the two custom distortion presets set `dist_model` to 0. `factory-build` checks this on every file.

Every template file loads something no other one does; the build stops on two identical ones. Root Fifth Bounce has the same notes and octaves as Perfect Fifth, so its file sets its own density knobs (fewer notes, more slides and accents).

## Rebuilding and checking

```
cmake -S . -B build-factory -G Ninja -DMW_BUILD_PLUGIN=OFF -DMW_BUILD_TESTS=OFF
cmake --build build-factory --target factory-build acid-render
python3 tools/factory/qa.py               # build, verify, render, measure
python3 tools/factory/qa.py --calibrate   # re-measure levels into tools/factory/levels.tsv, rebuild, check
```

`qa.py` runs `factory-build --verify` (each file parsed with zero adjustments and zero ignored items, written back to identical bytes, voicing and names checked), the branding audit, an em dash scan and the minimum counts, then renders 1,726 jobs through `acid-render`: every synth preset on the three lines, every scene slot, every pattern over the Rubber Floor sound, every calibration and effect preset over Init Line. It checks level, true peak, mono loss, silence, the effect loudness window and the distance between synth presets, compares the MIDI output of every scene's A1 and every pattern against the accents and slides its steps call for, and writes `build-factory/qa/report.txt`, per-render measurements (RMS on both channels and in mono, peak, true peak, loudness, spectral centroid, accent brightness, resonant peak prominence), each synth preset's nearest neighbour (`neighbours.tsv`) and spectrogram PNGs of one scene per style. `--calibrate` repeats measure, trim and rebuild until no trim moves by more than 0.15 dB; it divides each move by the slope it saw between rounds (a chorus wet level or a limiter's makeup gain does not move the level one for one) and stops a trim at its parameter's range.

After any change to a recipe, run `qa.py --calibrate`, then `qa.py`, and expect `0 failure(s)`.
