# 05: Devil Fish and other TB-303 mods, and acid bassline technique

Research doc for the mw303 build (C++20 / JUCE 8; VST3, CLAP, LV2, standalone). Topic: (A) what the Devil Fish and other well-known TB-303 modifications add, with modeling notes, and (B) what acid producers actually do with the instrument, turned into features and a factory-content brief.

Confidence tags: **[primary]** means a schematic, service/vendor manual, the modder's own documentation, a measurement, or source code. **[secondary]** means a reputable review or analysis. **[lore]** means forum consensus or a crowd transcription that nobody has verified.

Sources I read in full or grepped, with local copies in the session scratchpad (`.../scratchpad/df/`):

- Robin Whittle, *User Manual for the Devil Fish mods to the TB-303*, 9 Feb 2019, covering mod versions 2.0 to 5.1. https://www.firstpr.com.au/rwi/dfish/Devil-Fish-Manual.pdf (local copy `df/manual.txt`). Cited below as **DFM**.
- Whittle, *TB-303's unique characteristics* (1999). https://www.firstpr.com.au/rwi/dfish/303-unique.html. Cited as **WU**.
- Whittle, *TB-303 Slide* (1999). https://www.firstpr.com.au/rwi/dfish/303-slide.html. Cited as **WS**.
- Whittle, *TB-303 mods and maintenance* (2013). https://www.firstpr.com.au/rwi/dfish/303-mods/. Cited as **WM**.
- Whittle, Devil Fish main page (feature list, notes on Behringer, Quicksilver). https://www.firstpr.com.au/rwi/dfish/. Cited as **DFP**.
- Devil Fish MIDI In and Out manual. https://www.firstpr.com.au/rwi/dfish/DF-MIDI-In-Out-Manual.pdf. Cited as **DFMIDI**.
- Ulf Kaiser, Keys Magazine, Dec 2010, English translation hosted by Whittle. https://www.firstpr.com.au/rwi/dfish/Devil-Fish-article-Keys-Magazine-Dec-2010-English-tr.pdf. Cited as **KEYS**.
- D16 Group, *Phoscyon 2 User Manual*, 21 Nov 2022. https://d16.pl/pub/manuals/Phoscyon%202-manual-gb.pdf. Cited as **P2M**.
- subatomicglue *x0xb0x mod guide*, compiled from the x0xd0x wiki. https://www.subatomicglue.com/x0xl0g/mod%20guide/mod%20guide.html. Cited as **X0X**.
- JC-303 source (GPL-3.0; the embedded Open303 engine is MIT), shallow-cloned to `.../scratchpad/refs/jc303`. https://github.com/midilab/jc303
- ElectroSmash circuit analyses of the Pro Co RAT, Boss DS-1 and Tube Screamer, fetched through the Wayback Machine because the live site would not resolve.
- Interviews, vendor tutorials and reviews, each cited inline.

---

## Part A: Devil Fish and other TB-303 modifications

### A.0 Why the Devil Fish matters for this build

The Devil Fish (DF) is the reference "extended 303". Whittle built the first in 1993, settled the v2 design in 1996, and says DF sound has been unchanged since v2.1C (1999) [primary, DFM p.2]. The Behringer TD-3-MO copies it. Whittle's own description of that product: "Apart from the sub-oscillator, this is a direct unauthorised copy of the Devil Fish circuitry" [primary, DFP]. Arturia Acid V, JC-303 and Phoscyon 2 each lift parts of it. Whittle frames it as a superset of the 303: with the right knob positions it "can still sound exactly like the TB-303" [primary, DFM p.6]. That is the right framing for our plugin too. Ship stock 303 behaviour by default, put a mod layer on top, and provide a single "stock" switch that snaps every mod control to its 303-equivalent value.

Stock 303 vs Devil Fish dimensionality, in Whittle's words: the 303 has "five pots ... a five dimensional space"; the DF expands four of those ranges and adds "six new pots (not counting the Slide Time pot) and three new switches", giving "a 14 dimensional space" plus the external audio inputs [primary, DFM p.9].

### A.1 Devil Fish feature-by-feature

Unless noted otherwise, every item below comes from DFM [primary]. Where I derive a number from component values, I say so.

#### A.1.1 Overdrive (oscillator level into the filter)

- **Behaviour.** The Overdrive pot "controls the level of Oscillator signal fed to the filter ... between none, through normal to heavy overdrive (66.6 times normal) which causes the filter to operate under duress" [primary, DFM p.4]. Stock level is at panel position "2" [primary, DFM p.8, "Limiting the Devil Fish to TB-303 sounds"]. A Behringer review confirms that turning it to zero closes off the sound completely and that it "reintroduces the main signal" at the 2 mark [secondary, SOS TD-3-MO review, https://www.soundonsound.com/reviews/behringer-td-3-mo].
- **Circuit.** On the stock board, R62 (220 kΩ) together with C17 sets the VCO-to-VCF level. 220 k / 3.3 k = 66.67, which is where the "66.6×" figure comes from [primary-derived, X0X "Devilfish Filter Overdrive"; the 220 k value of R62 comes from the same page]. The DIY approximation puts a 250 k pot in series with 3.3 k in place of R62. That version cannot reach zero. The "exact" DF version references the pot to the 5.333 V bias, and an x0x builder notes that because the VCO output is unbuffered this loads the square waveshaper and audibly changes the square [lore, X0X quoting forum user bcbox].
- **What happens sonically.** The ladder soft-clips: "a soft-clipping which is negligible in the original but is clearly noticeable when using the modification's Overdrive control ... the nonlinear behaviour of the diodes are audible" [primary-adjacent, KEYS p.2]. Crucially, **drive into the filter quenches resonance**: "The resonance of the filter is quenched when the filter is driven by a heavy input signal: the filter hits its limits at the high and low extremes of the waveform and cannot resonate at these limits" [primary, DFM p.18]. Pre-filter overdrive therefore gives a fat, compressed, less whistly tone. Post-filter distortion (pedals) keeps the resonant peak and then folds it. These are two different sounds, and the plugin needs both (see B.3).
- **Model.** Input gain `g_in ∈ [0, 66.7]` on the oscillator path into the ladder, mapped so that panel 2/10 = 1.0, with the ladder's per-stage nonlinearity (tanh or diode-pair) active at all times. At stock levels it is close to linear, so cost is unchanged. This only works if the filter model is nonlinear inside the feedback loop. A linear ladder followed by a waveshaper will not quench resonance correctly. Oversample this path at least 2×, and 4× at high drive (see the DSP research doc for the oversampling strategy).
- **Clones.** TD-3-MO: yes. JC-303: approximate, via its "filterDrive" 0 to 9 dB into the ladder's input stage, plus a post-filter GuitarML neural "overdrive" [primary, `jc303/src/JC303.cpp`]. Phoscyon 2: **no** pre-filter overdrive. Its Distortion is a post-synth FX module [primary, P2M pp.10–11]. ABL3: has "drive" and "distortion" circuits [secondary, MixDown/Synthtopia news], but I could not confirm where the drive sits in the chain.

#### A.1.2 Filter FM (audio-rate cutoff modulation from the VCA output)

- **Behaviour.** "The Filter FM pot frequency-modulates the filter frequency with the audio output of the VCA ... This gives edginess and complexity dependent on the signal level coming out of the VCA, so it is stronger on accented notes. At higher settings, the results approach chaos" [primary, DFM p.5–6]. The feed is AC-coupled. The source is the VCA output *after* the Muffler, so the Muffler reduces FM depth [primary, DFM p.19]. Overdrive raises VCA output and therefore FM depth, so the two compound [primary, DFM p.18].
- **Linear, not exponential.** Whittle lists the inputs to the filter frequency summing point and labels Filter FM, Filter Tracking and the external audio FM jack as "(Linear)". Only the DC Filter CV input is exponential, at about 1 V/oct [primary, DFM p.12]. So FM adds Hz, not octaves. The DSP consequence is that the cutoff control current cannot go negative: FM swings clamp at "filter closed" on negative peaks. That one-sided clamping is part of the grit.
- **Chaotic regimes.** With high Overdrive and high FM, Whittle got a fixed pattern to resonate "at about the frequency of the Oscillator, but at other times at half the rate (an octave below), or less ... the state ... varies from bar to bar, but remains stuck in one state or the other for the duration of each note" [primary, DFM p.18–19]. That is period-doubling in a feedback loop. The model needs **per-sample cutoff updates** with no block-rate smoothing on this path, or the effect cannot appear.
- **Self-patch variant.** Partially inserting a plug into the old headphone jack connects Filter Out (pre-VCA) straight to FM In. That version is uncontrolled, "potentially more intense", "continual ... not dependent on notes", and "the polarity ... is the opposite of that of the Filter FM pot" [primary, DFM p.18]. Also documented: main output polarity is opposite to Filter Out [primary, DFM p.24].
- **x0x variant: "Filter FM from the VCO".** A DIY mod takes the raw saw or square (switchable) through a 50 k log pot and 100 k into the filter summing node. This is audio-rate FM from the oscillator itself rather than from the VCA, and it is not gated by the envelope. The author says it produces "everything from an evil zipper to slowly dripping acid" [lore, X0X "Filter Modulation (VCO)"]. The Acidlab mod's "x-mod (cross modulation of oscillator)" is probably the same idea [lore, MATRIXSYNTH listing summary: https://www.matrixsynth.com/2009/01/roland-tb-303-bassline-with-acidlab-mod.html].
- **Model.** `fc(t) = max(f_floor, f_exp(t) + k_FM · HP5Hz(src(t)))`, where `src` is selectable (VCA-post-muffler as on the DF, pre-VCA filter out with inverted polarity, or the VCO saw/square as on the x0x), `HP5Hz` is a one-pole high-pass at about 5 Hz (Whittle: "only the audio frequency component (say 5 Hz and above)") [primary, DFM p.3], and `f_floor` is about 0 to 10 Hz. Because the loop includes the filter's own output, run it inside the oversampled block, using the previous sample's output as the modulator (a one-sample delay at 4× is inaudible).
- **Clones.** TD-3-MO: yes. Reviewers describe little effect "from zero to three o'clock and then it tripped into a crunchy bit of chaos" [secondary, SOS TD-3-MO]. That fits a linear FM amount hitting the clamp. Phoscyon 2: **no**. JC-303: no, though it has an LFO with selectable destinations. Acid V: it has modulators, but I could not confirm VCA-to-cutoff audio FM.

#### A.1.3 Filter Tracking (keyboard to cutoff, linear in Hz)

- **Behaviour.** Range is "from 0, through normal tracking to over-tracking". The zero-change pivot is "approximately C (2 volts) or D (2 2/12 volts)", meaning the bottom of the normal octave. Notes below the pivot *lower* the cutoff. Maximum is "about 2.7 kHz per volt", i.e. per octave of CV. The mapping is **linear volts to Hz**. Whittle's worked example: with cutoff 1 kHz at 2 V, tracking gives 1.5 kHz at 3 V, 2 kHz at 4 V and 2.5 kHz at 5 V. He "tried an exponential ... approach and decided the linear approach was much more interesting" [primary, DFM pp.5, 13].
- **Pivot drift.** "The higher the filter frequency as a result of these inputs, the higher the 'zero point' note" [primary, DFM p.5].
- **The trick it enables.** "One important use of Filter Tracking is for some low notes to cause the filter frequency to go below the first harmonic of the oscillator ... This cuts the sound off entirely, or almost entirely, for these notes" [primary, DFM p.5]. That turns pitch into a gate. Notes in the down-octave can vanish, so a pattern's low notes become rests or ghost notes as tracking goes up.
- **Model.** `fc = max(f_floor, f_other + T · 2700 Hz/V · (V_cv − V_ref))`, with `V_cv = 2 + (midi − 60)/12` (DF MIDI maps MIDI 60 to 2.0 V [primary, DFMIDI p.13]), `T ∈ [0, ~1.2]` for over-tracking, and `V_ref` a hidden trim around 2.0–2.17 V. Optionally let `V_ref` rise with `f_other` to mimic the pivot drift. Use the slid CV (post-slide), because Whittle says the slide circuit feeds "the Oscillator (and Filter Tracking Pot and CV Out)" [primary, DFM p.16]. Slides therefore drag the cutoff along.
- **Clones.** TD-3-MO: yes. Reviewers report it works "inversely with the Envelope knob" [secondary, SOS]. Phoscyon 2: **no**. Acid V: has "Pitch Tracking", but the CDM review describes it as setting "accuracy", so it may be oscillator tracking rather than filter tracking [secondary, CDM https://cdm.link/arturia-goes-303-acid-v-bassline/]. Unverified. JC-303: no.

#### A.1.4 Soft Attack (VCA attack on non-accented notes)

- **Behaviour.** Varies "the attack time of non-accented notes between 0.3 ms and 30 ms. In the TB-303 there was a (typical) 4 ms delay and then a 3 ms attack time" [primary, DFM p.4]. Stock equivalent is panel "2" [primary, DFM p.8]. Accented notes are unaffected because their loudness comes through the accent path (A.1.6). Soft attack therefore *increases* the click and punch contrast between accented and plain notes. Combining a fully clockwise Soft Attack with a fully anticlockwise VEG Decay silences non-accented notes, which Whittle lists as a "reason for no audible output" [primary, DFM p.25]. Musically that is an "accent-only" mode.
- **Gate latency.** Stock note start is "between 1 and 5 ms (typically 4 ms) after the start of the gate". The DF reduces this to under 0.5 ms [primary, DFM p.6].
- **Model.** A one-pole (or linear) attack on the VEG for non-accented notes only, 0.3–30 ms on an exponential taper. Add a hidden "vintage gate latency" trim of 0–5 ms, default 0 in mod mode and 4 ms in authentic mode if the DSP lead wants it.
- **Clones.** TD-3-MO: yes. Phoscyon 2's "Env. Attack" is the *filter* envelope's attack, not the VCA's [primary, P2M p.7]. That is a different control, so we should offer both. JC-303's "softAttack" maps to Open303 `setNormalAttack` over 0.3 to 3000 ms [primary, `JC303.cpp`]. That range exceeds the DF's.

#### A.1.5 Decay controls: Normal Decay, Accent Decay, and the VEG

- **Stock 303** (Whittle's description): the MEG (Main Envelope Generator, which drives the filter) decays over a variable "200 ms and 2 seconds", set by the Decay pot. On accented notes the decay is "fixed to 200 ms" [primary, DFM p.4]. Elsewhere Whittle says the accented time "is the same as when the Decay pot is fully anti-clockwise" [primary, WU]. The VEG (Volume EG) "was fixed at ~3 to 4 seconds" [primary, DFM p.4], and "drives the VCA and does nothing else" [primary, WU].
  - Conflict: Open303 uses an amp decay of 1230 ms (`setAmpDecay(1230.0)` in `rosic_Open303.cpp` via JC-303) [primary-code]. Whittle says 3–4 s. Because notes end at gate-off (A.1.5 note-end behaviour), the long decay rarely completes, which is why both values can sound plausible. **I believe Whittle** for the hardware, since he measured real units, and expect Open303's figure to be a fitted emulation constant. Expose it as a trim.
- **Devil Fish.** Two new pots, **Normal Decay** and **Accent Decay**, each 30 ms to 3 s, control the MEG. The old **Decay** pot is rewired to the **VEG**. Its "first half ... varies the decay between 16 ms and 3 seconds. The second half retains the long decay but makes the final volume vary between silence and full volume, so indefinitely long notes can be produced" [primary, DFM p.4]. In other words, the second half of the knob is a sustain level.
- **Stock-equivalent positions** [primary, DFM p.8]: Decay (VEG) "Fix at 12 o'clock"; Normal Decay "No lower than 2"; Accent Decay "Fix at about 2.7".
- **Note end.** Stock: "about 16 ms – 8 ms of normal volume and 8 ms of linear decay". DF: "a more natural logarithmic decay which starts immediately after the gate signal ends" [primary, DFM p.6].
- **Effect of separate accent decay.** It makes the length of the "wow" and "wapp" controllable: "Now that the time of the MEG can be manually controlled for accented notes, the 'wow' or 'wapp' time can be made shorter or longer" [primary, DFM p.4]. With Accent Sweep disabled, accents are still audible "due to the different time of the Env Mod filter sweep if the Accent Decay time is different from the Normal Decay time" [primary, DFM p.4].
- **Model.** MEG: one-pole exponential decay, with time switched per note (normal or accent) as on the stock unit. In mod mode both times are free (30–3000 ms). VEG: exponential decay 16 ms–3 s on the first half of the knob, then a long decay plus a sustain floor 0→1 on the second half. A one-knob two-segment map like this is faithful. A cleaner alternative is separate "Amp Decay" and "Amp Sustain" parameters with a "DF knob" macro on top.
- **Clones.** TD-3-MO: separate amp and filter decay, normal and accent decay [secondary, SOS; Thomann listing]. Phoscyon 2: Decay (non-accent) plus "Acc. Decay", but **no VEG decay or sustain** [primary, P2M pp.6–7]. JC-303: normal decay, accent decay and amp decay, but its UI parameter "normalDecay" actually calls `setAmpDecay(30..3000)` [primary, `JC303.cpp` lines ~641–650]. That is either a labelling mix-up or a deliberate copy of the DF's repurposed Decay knob. **Do not copy that ambiguity.** Name the controls *Amp Decay*, *Filter Decay*, *Accent Decay*.

#### A.1.6 Accent: the stock mechanism, and the DF's Accent Sweep switch and Sweep Speed

**Stock mechanism** [primary, WU]. On accented notes the MEG output goes "through a switch (which is only on during accented notes) and then through the Accent pot", and then:

1. It adds to the VCA control current via **47 kΩ + 0.033 µF** "to soften the attack a little". τ = 1.55 ms (derived). This "is the primary reason why accented notes are louder."
2. It drives the **Accent Sweep Circuit**: "a diode and a 47k resistor in series driving the ACW end of a 100k pot, and a 1uF capacitor (to ground) hanging off the CW end. The wiper of the pot goes via a 100k mixing resistor to one of the two summing nodes for filter frequency." **"This pot is the second section of the Resonance pot."**
   - Resonance anticlockwise: the filter gets "largely a direct copy of the output of the MEG ... 100/147 of the MEG voltage minus the capacitor voltage".
   - Resonance clockwise: the pulse is "smoothed by coming from the 1uF cap as it is charged via the diode and 147k". The result is the smooth "Wow" curve, which "is not the angular response you would get with a ADSR".
   - "Since the capacitor has not discharged fully from the one before, the second and subsequent response curves go higher." That is the climbing scream.

**Derived time constants** (my arithmetic from Whittle's component values; treat as a starting point and tune by measurement):

- Charge (resonance fully CW, diode conducting): Thevenin of 147 k into the 100 k wiper load gives τ_charge ≈ (147k ∥ 100k)·1 µF ≈ **59.5 ms**, settling toward ≈ 0.405 × V_accent-MEG.
- Discharge (diode off): only through the 100 k mixing resistor, so τ_discharge ≈ **100 ms**.
- At 16th notes, 130 BPM (115 ms per step), roughly 32% of the cap voltage survives to the next accent, before counting the charge added during the next pulse (exp(−115/100)). That is why runs of 3–5 accents climb audibly and then plateau.
- KEYS says the accent filter boost is "a 200 ms steep lasting increase" [primary-adjacent, KEYS p.2]. That figure is plausibly the fixed accented MEG time, not the cap τ.

**DF modifications to accent:**

- **Accent Sweep / Hi-Resonance three-position switch** [primary, DFM p.18 table]:

  | Pos | Accent Sweep circuit | Resonance range | Use |
  |---|---|---|---|
  | 2 | Enabled | Normal | "Acidee sounds with the Resonance pot fully clockwise" |
  | 1 | Enabled | High | "Whistly filter sweep sounds" |
  | 0 | Disabled | High | "Self resonant filter with no sweep on accented notes" |

  Disabling the sweep "stops the filter and VCA being affected by the MEG's output via the Accent pot" [primary, DFM p.4]. So position 0 removes **both** the cutoff boost and the VCA boost of accent.
- **Sweep Speed switch (Fast / Normal / Slow)**, which only acts on accented notes [primary, DFM pp.4–5]:
  - *Fast:* "The first accent causes a strong positive output, but subsequent accents produce a smaller output." This is the inverse of the stock behaviour. A high-pass or differentiating behaviour (a series cap) would produce it.
  - *Normal:* identical to the stock 303 (C13 accumulation).
  - *Slow:* "much longer time-constant. The output takes longer to rise, but it can rise twice as high. It also takes longer to cool down ... perceived when the filter frequency is settling down in the half second or so of non-accented notes which follow."
  - Whittle's audition tip: "Turn the filter into self resonance and turn down the Env Mod pot to hear the differences."
  - **Component values for Fast and Slow are not published.** See Open questions.
- **Manual Accent pushbutton**: accent "at any time" [primary, DFM pp.4, 14]. It is a performance control.
- **Accent latch quirk**: with the sequencer stopped, if "the last note had the accent on", accent stays active for externally played notes [primary, DFM p.14]. This is a bug. Do not reproduce it except as an optional curiosity.
- **Sound** (SOS on the TD-3-MO clone): sweep speeds range "from spicy 'wup' to 'wap' to a throaty 'whoa'", and "mangled into a satisfyingly sadistic scream" when cumulative [secondary].

**Model.**

```
accentCV = accentFlag ? MEG(t) * accentKnob : 0
// VCA path:     vcaBoost  = onePoleLP(accentCV, 1.55 ms)
// Sweep path (Normal): diode-charged RC:
//   if (accentCV - vC) > 0:  vC += (accentCV*k1 - vC) * dt/τc   (τc ≈ 60 ms)
//   else:                    vC -= vC * dt/τd                   (τd ≈ 100 ms)
//   sweepOut = lerp(directPulse(accentCV, vC), vC, resoGang)   // resonance pot 2nd gang
// Fast:  sweepOut = HP(accentCV) (series-cap behaviour, first hit strongest)
// Slow:  τc, τd ×~3–5 and gain ×~2 (tune by ear; values unpublished)
// Off:   sweepOut = 0 and vcaBoost = 0
```

Keep the resonance coupling as a **switchable "couple" mode**. Phoscyon 2 does exactly this: its "Resonance / Sweep Couple" switch either ties the smooth/spiky crossfade to Resonance or hands it to a separate "Sweep Amt." knob. It also splits filter-accent depth from VCA-accent depth ("EnvAcc / AccVolume Couple", "Acc. Vol.") [primary, P2M pp.6–7]. Phoscyon's hidden "C13" trim is the sweep cap time constant [primary, P2M p.9]. **Parity requires: Sweep Amt, Acc Vol, both couple switches, a C13 trim. Beyond-parity: DF Sweep Speed (Fast/Normal/Slow) and Sweep Off.**

#### A.1.7 Slide Time

- **Stock.** "The usual rather fast slide of a TB-303 is caused by the 100K resistance of the DAC feeding a 0.22uF capacitor" [primary, DFM p.16]. τ = 22 ms. Whittle quotes "60 ms", which is the *audible* time to arrival ≈ 2.7 τ.
- **DF.** A slide pot of 0–500 k in series gives 60–360 ms from the internal sequencer: 603.3 k × 0.22 µF = 132.7 ms τ, and × 2.7 ≈ 358 ms, which checks out. From external CV the total is 3.3 k to 503.3 k, i.e. 2–300 ms [primary, DFM pp.4, 16; the arithmetic is mine and matches Whittle's figures, which supports the 2.7τ = "audible time" reading]. Stock-equivalent: Slide pot at 0 on the internal sequencer, at 5 on external CV [primary, DFM p.8].
- **Constant time.** It is a plain RC with no feedback, so slide time is the same for a semitone and an octave [primary, DFM p.16 describes the RC; constant-time is the consequence].
- **Gate behaviour** [primary, WS]. Each 16th step is 6 clocks. A normal gate is high for 3.5 clocks and low for 2.5. On a slid note the gate stays high into the next note. "The slide signal will go high 1/16 note after the start of slid note" and "will remain active until the end of the note which follows the slid note." Whittle also notes that the tail of the slide is often inaudible because the gate closes, *unless the note is accented*, in which case the MEG also drives the VCA and the slide tail stays audible.
- **Model.** `τ_slide = (R_src + 3.3k + R_pot) × 0.22 µF`, with `R_src` = 100 k (internal sequencer) or ≈0 (host MIDI). Expose a **Slide Time** knob calibrated in audible ms (stock 60 ms), with τ = time/2.7. The slew operates on the CV that feeds both the VCO and Filter Tracking.
- **Clones.** Everyone has it. Phoscyon 2 "Slide Time" [primary, P2M p.7]. JC-303 2–360 ms [primary-code]. TD-3-MO "extended six-fold" [secondary, Thomann].

#### A.1.8 Extended Cutoff and Env Mod ranges; low-end response

- **Cutoff.** The range is raised and lowered. "The maximum resonant frequency of the filter, with no Env Mod or Filter Tracking, is approximately 5 kHz. This is an octave above the typical maximum for the TB-303" [primary, DFM p.6; calibrated from v2.1C onward, p.32]. Hard ceiling about 20 kHz "to reduce the likelihood of oscillations at extreme frequencies which may cause undesirable outcomes with digital audio ADCs, loudspeaker systems and canines" [primary, DFM p.6]. DIY version: turn TM3 clockwise to raise the top end; short R47 (10 k) to lower the bottom end [primary, WM §4.1]. x0x builders prefer 4.7 k in parallel with R47 (≈3.2 k) over a hard short [lore, X0X]. Stock-equivalent cutoff span: "Between about 10.30 and 3 o'clock" [primary, DFM p.8]. Cross-reference: Open303's measured stock knob range is 313.8 Hz to 2394.4 Hz nominal (`c0`, `c1` in `calculateEnvModScalerAndOffset`) [primary-code]. That is consistent with "5 kHz is an octave above".
- **Env Mod.** "Range has been tripled and made to include no Envelope Modulation" [primary, DFM p.6]. Stock "was not zero" at minimum [primary, WU]. DIY version: 100 k in parallel with R63 (220 k) gives 68.75 k, i.e. 3.2×; short R61 (10 k) to allow zero [primary, WM §4.1]. Stock-equivalent span: "Between 12 and 4 o'clock" [primary, DFM p.8].
- **Stock Env Mod also moves the floor.** Open303's measured mapping applies `envScaler*(env − envOffset)` with `envOffset ≈ 0.29–0.34`. So in emulation, raising Env Mod deepens the upward sweep *and* lowers the resting cutoff [primary-code, `rosic_Open303.cpp` lines 347–369]. KEYS describes a related stock circuit: Roland's design "opens the filter wider with increased modulation values while allowing simultaneous faster closure" [primary-adjacent, KEYS p.2]. A clone with a plain additive Env Mod will feel wrong in the knob's middle range.
- **Bass.** "In the TB-303, 32 Hz was down by 5 dB. Now it is down by 1 dB" [primary, DFM p.6]. Fix: C20 and C21 changed from 0.01 µF to 0.1 µF [primary, WM §4.2]. x0x builders call it "absolutely essential" and switch it, because it "gets too boomy" for lead use [lore, X0X]. To restore stock: "roll off the 30 to 60 Hz band by a few dB" [primary, DFM p.8].
- **Model.** Make cutoff and env-mod ranges a **"Range: 303 / Extended" switch** rather than separate knobs. Bass is a **Bass switch (303 / DF)** that changes the high-pass corner of the input coupling network. The DSP doc should model C20/C21 as an RC high-pass; going 10× in C moves the corner down by about 3.3 octaves.

#### A.1.9 High resonance and self-oscillation

- **DF.** The switch "can be switched to double the usual feedback so as to allow the filter to self-oscillate at mid and high frequencies", engaging "when the Resonance pot is past about 2 o'clock" [primary, DFM pp.5, 18]. With Overdrive at zero, the filter can sing alone and the VCO can be faded in under it [primary, DFM p.5].
- **Low-frequency limit is a feature.** "The filter will only self oscillate at mid and high frequencies. Surgical intervention, aimed at improving its gain at lower frequencies, made it sound more like a normal filter, so I abandoned this line of investigation" [primary, DFM p.24]. Do **not** make the filter self-oscillate evenly across the range. The high-pass in the resonance feedback path (Open303 `feedbackHighpass = 150 Hz` stock; JC-303 lets it range 350→100 Hz) must stay.
- **Step response.** Large steps in cutoff (from external CV or tracking) land "perhaps 99.8%" instantly, then drift for about 500 ms because of capacitors coupled to the summing point. "Only noticeable with the filter self-resonating" [primary, DFM p.12]. This is optional realism, low priority.
- **x0x "Resonance Boost".** Lower R97 from 10 k to about 5–7.5 k (optionally also R108, R112, R109) [lore, X0X]. The **"resonance accent increase"** mod shorts R46 [lore, X0X]. I could not find R46's role on the schematic in my sources. It may scale the accent-sweep injection; verify against the service manual (see the circuit research doc).
- **Model.** Resonance gain multiplier ×1 / ×2 (the "Hi-Res" switch). Self-oscillation should emerge from the model's loop gain against the feedback HPF, not from an added sine.
- **Clones.** TD-3-MO and DF: yes. Phoscyon 2: no Hi-Res switch, but its TM3 trim moves the base cutoff [primary, P2M p.9]. Acid V: "extended cutoff range" [secondary, CDM]. JC-303: feedback HPF trim.

#### A.1.10 Muffler (post-VCA soft clipper)

- **Behaviour.** Three-position switch. It "affects the output of the VCA and provides two types of muted clipping ... only roughly comparable to fuzz clipping. It softens the loudest extremes of the sound and introduces a moderate level square wave clipping buzz, whilst allowing the bass to pass largely unaffected. The Muffler affects sounds which are louder than usual (those resulting from high levels of Accent or Overdrive or high level external audio input signals). It has little or no effect when the signal level at the VCA output is low" [primary, DFM p.6]. It sits before the Filter FM tap, so it softens FM as well [primary, DFM p.19]. A user reports that "resonance and muffler settings can get relatively thin sounds ... to fit more of the Devil Fish 'juice' into the mix" [primary-user-quote, DFP]. SOS on the TD-3-MO: it "take[s] the edge off" overdriven accents but you have to push it [secondary].
- **Circuit.** Not published by Whittle. The x0x "muffler-like" mod (diodes to ground after C38, selectable germanium/silicon/LED, one or two diodes) is explicitly "a suggestion, not an authentic devilfish mod" [lore, X0X].
- **Model** (inferred). A level-dependent clipper whose threshold sits above the typical non-accented peak. Mode 1 is soft (high knee). Mode 2 is harder and more asymmetric, giving the "square wave clipping buzz". To honour "bass passes largely unaffected", either clip a high-passed band (split at about 120–200 Hz and sum the clean low band back) or use a diode clipper whose threshold rises with low-frequency content. Tune by ear against TD-3-MO recordings. **Placement:** post-VCA, pre-FM-tap, pre-output.

#### A.1.11 External audio into the filter, and Filter Out

- "The Mix In socket is changed to External Audio input to the Filter ... fed directly into the filter, along with the oscillator signal which passes through the Overdrive pot" [primary, DFM p.3]. Line level is fine, and "you may want to drive something harder" [primary, DFM p.11]. There is an optional switch: on, off, or momentary spring-return [primary, DFM p.23].
- **Filter Out** is a pre-VCA line output, "not affected by the volume control ... does not depend on notes being played" [primary, DFM p.3]. Intended for external FX loops: "the output of which might be fed back into the Audio Filter FM input or the Mix In" [primary, DFM p.3].
- **Plugin mapping.** A **sidechain audio input** routed to the filter input (the VCA is still gated by notes) and/or to the FM input. Add an optional **"filter out" auxiliary output bus** (pre-VCA). CLAP, VST3 and LV2 all support these. Also include an internal **FX return into the filter or FM**: Whittle's own patch feeds delay or EQ output back into the filter [primary, DFM p.30]. Linx's patch ran a Moog MF-104 delay into the LPF and then back into the DF filter input [primary-user-quote, DFP]. A "feedback send" from the delay into the filter input is cheap and novel.

#### A.1.12 CV/Gate/Accent/Slide I/O and MIDI behaviour

| DF jack | Behaviour | Plugin equivalent |
|---|---|---|
| Gate In | High above 3.5 V; ORed with the internal sequencer, so external and internal notes mix ("out-of-sync notes") [primary, DFM p.14] | Host MIDI notes ORed with the internal sequencer: a "**Merge**" play mode |
| Accent In | High above 2.3 V; e.g. a MIDI-velocity CV [primary, DFM p.11] | Velocity ≥ threshold → accent (DF MIDI default **65**, i.e. one above the 64 of non-velocity keyboards [primary, DFMIDI p.13]; Phoscyon has a configurable threshold [primary, P2M p.17]) |
| Slide In | Two thresholds: 2.3 V = slew only; 4.0 V = slew **and** hold gate, which ties notes [primary, DFM pp.17, 33] | Overlapping notes → slide; **CC65** (portamento) → slide; a user-chosen CC (DF: 1–19) → slide + gate; **CC64** → gate hold [primary, DFMIDI pp.13–14]. Also a "slew without tie" mode (slew on, retrigger envelopes) |
| Filter CV In | DC, about 1 V/oct, rest at 3.3 V, roughly 5 octaves for 0–5 V [primary, DFM p.11] | **Mod wheel (CC1) → cutoff, additive, about 5 octaves**, which is the DF MIDI default [primary, DFMIDI p.14]. Linx liked the mod wheel because he could "freak it out and return to the exact same sound setting" [primary-user-quote, DFP] |
| Accent Out / Gate Out / CV Out | Accent Out is +6 V (0.7 V when off); CV Out is post-slide [primary, DFM pp.3, 15] | MIDI out: notes with accent/plain velocity, overlap for slide; Phoscyon uses configurable velocities and CC64 when "slide used and accent for the consecutive note with same pitch" [primary, P2M p.22] |
| Audio FM In | AC-coupled, linear [primary, DFM p.3] | Sidechain → FM |

The **"69" configuration**: two DFs playing similar patterns, each one's output feeding the other's Filter FM, with volumes raised together so the cross-FM grows [primary, DFM p.30]. In a plugin this needs two instances and a sidechain, or an internal **dual-voice "Twin" mode** where voice B's VCA output is voice A's FM source and vice versa.

#### A.1.13 Other DF details worth copying

- **LEDs** show the normal-MEG and accent-MEG output by brightness, plus a gate LED [primary, DFM p.6]. A GUI should have two small envelope meters (normal and accent MEG) and a gate light. Whittle relies on them to diagnose the accent latch.
- **Output stage clipping.** "With high levels of signal coming out of the VCA and with the Volume pot set at or near maximum, some clipping can occur in the output amplifier. This typically only happens on the loudest (for instance the accented) notes, and can be quite a useful effect" [primary, DFM p.24]. Model a soft output-stage clip that only bites on accents at high Volume. This is not the Muffler.
- **Headroom.** "The Devil Fish can produce extreme output levels. Caution and/or a limiter should be used during live performance" [primary, DFM p.9]. Our mod mode needs an always-on safety soft limiter at the output, with a defeat switch.

### A.2 Other modifications and mod-like products

| Mod / product | What it adds | Sources and confidence | Our take |
|---|---|---|---|
| **Quicksilver 303** (Social Entropy CPU replacement; discontinued per Whittle Dec 2018) | Rewritten OS: MIDI in/out, recording MIDI into patterns, live functions "ALL ACCENT", "CHOP", USB updates | [secondary] Synthtopia 2012 https://www.synthtopia.com/content/2012/04/04/roland-tb-303-cpu-replacement-quicksilver-303/ ; Whittle confirms it is discontinued [primary, DFP] | Copy the **live override** ideas: all-accent, all-slide, chop (gate shortening), mute-step. Phoscyon's "Live Override" Set/Clr for Slide, Accent and Vibrato is the same idea [primary, P2M p.23] |
| **Borg** (Analogue Solutions, discontinued) | Described as "similar in many ways to the Devil Fish but with advantages" | [secondary] Attack Magazine "Ten of the best synth mods" https://www.attackmagazine.com/reviews/the-best/ten-of-the-best-synth-mods/3 ; Vintage Synth Explorer | **No feature list was reachable.** The Wayback Machine has no captures of an Analogue Solutions Borg page. Not actionable |
| **Acidlab 303 mod** (not the Acidlab Bassline clone) | "overdrive, x-mod (cross modulation of oscillator), full accent on/off, full slide on/off, pitch bends, cut-off range expanded, and resonance range expanded, with decay that can be shortened to nice little blips" | [lore] eBay-listing text quoted on MATRIXSYNTH (2009) | Confirms the same core set (drive, X-mod or filter FM, extended ranges, short decay) plus **global accent and slide forcing** and **pitch bend** |
| **x0xb0x / DIY common mods** | Bass boost (C20/C21); env mod 0 and ×3 (R61, R63); filter low end (R47); resonance boost (R97 etc.); resonance-accent increase (short R46); VCF external in; **VCA "on forever"** (gate held high); **variable VCA decay** (R123 1.5 M → pot); filter FM from VCO (saw or square selectable); filter FM from VCA; DF overdrive (R62); muffler-like diode clipper | [lore], compiled by subatomicglue from the x0xd0x wiki (X0X) | Every one of these maps onto a DF-mode control, with three additions: **VCO→filter FM source select**, **gate-hold "drone"**, and **resonance-accent depth** |
| **Waveform switch middle position** | On a stock unit the middle position gives silence: "Beware too of the waveform switch being in mid-position!" | [primary, DFM p.25] | Stock offers no mix. **Square/saw mix**: I found no primary documentation of a common hardware saw+square mix mod; treat it as [lore]. A continuous saw↔square blend is cheap in DSP; offer it as an extended control, with the 303's waveshaper-derived square (see the oscillator doc) |
| **Behringer TD-3-MO** (hardware DF copy) | All DF controls listed above, **plus a three-position sub-oscillator (low/mid/high)** with no level control; "too dominating ... erase[s] all remaining traces of that 303 sound" | [secondary] SOS review; Whittle: sub-osc is the only non-DF element [primary, DFP] | Sub-osc is not a Devil Fish feature. If we add one, give it a **level** knob and a **pre-filter** route (so it gets the 303 filter) as well as a post-filter clean route |
| **Arturia Acid V** "Voice Settings" | Described as corresponding to "common hardware hacks ... like the best-in-class Devilfish mods": sub-osc −2 oct, Bass Boost, pulse width, extended cutoff, Pitch Tracking, Noise Gain, Accent Attack, vibrato up to audio rate, three drawable modulators | [secondary] CDM https://cdm.link/arturia-goes-303-acid-v-bassline/ ; SOS review | Sets user expectations for **PW control, noise or aging, sub-osc, modulators** |
| **AudioRealism ABL3** | 3 engines (ABL2 → ABL3), 3 VCO models; "setup" parameters: **Gate Trim** (VCA release, "authentic, looser or tighter"), **Detune**, **Low Cut**; drive + distortion; per-step accent/slide/octave; randomizer; pattern import (ReBirth, T-8, TB-03, TD-3); WAV-to-pattern transcription | [secondary] vendor page https://www.audiorealism.se/abl3.html ; MusicRadar review summary in search results | No DF mod set. **Gate Trim** and **Low Cut** are worth copying as trims. Pattern import from TD-3/TB-03 formats is a strong P1 |
| **JC-303** (open source) | "Devil Fish" switch toggling normal/accent/amp decay 30–3000 ms, soft attack 0.3–3000 ms, slide 2–360 ms, feedback HPF 350→100 Hz, square tanh-shaper drive 25–80 dB, filter drive 0–9 dB, bass compensation, LFO; off-mode restores stock constants (amp decay 1230, accent decay 200, HPF 150, attack 3, slide 60, shaper 36.9 dB) | [primary-code] `src/JC303.cpp` setParameter and setDevilMod; GPL-3.0 (Open303 core MIT) | A good reference for the **stock-snap switch**. Do not copy code (GPL) |
| **Phoscyon 2** | Mod-inspired: Slide Time, Sweep Amt, Env Attack (filter), Acc Decay, Acc Vol, two couple switches, Vibrato (speed, depth), Ext tuning mode; calibration trims TM3, C21/22, AccEnv, AccAmp, C13, TM5, SQR PW Shift, BA662 Clicks, BA662 Noise | [primary, P2M pp.6–9] | Has **no** overdrive into the filter, filter FM, filter tracking, muffler, VEG decay or sustain, Hi-Res, audio-in, or sub-osc. **This is the gap we fill on Linux** |

---

## Part B: Acid bassline technique

### B.1 The pattern grammar: how flags combine in idiomatic lines

**Hardware facts that shape the grammar.**

- Pitch and time are entered as separate lists. "If you input a rest, rather than silencing or skipping over the corresponding pitch, the sequencer will wait until the next note to move on" [secondary, MusicRadar producer's guide https://www.musicradar.com/news/producers-guide-to-the-roland-tb-303-and-clones]. Editing the time list therefore re-maps every later pitch onto different steps. This is a large source of "happy accident" lines, and musicians use it as a technique (B.5).
- Patterns run up to 16 steps. Length is set with Function+Step [secondary, MusicRadar]. There are "two step modes to give virtually any time signature" [secondary, Music Maker review 1982 via muzines https://www.muzines.co.uk/articles/roland-tb-303-bass-line/4250]. Roland's TB-03 replica defines the triplet grid as "each step is an 8th note triplet" [primary for the TB-03, https://static.roland.com/assets/media/pdf/TB-03_eng03_W.pdf]. That gives 12 steps per 4/4 bar, so 12-step patterns are the natural triplet idiom.
- Transpose: up or down an octave per step from the base octave, plus whole-pattern transpose of 0–12 semitones live in Pattern Play ("press 'Pitch' and one of the 12 keys") [primary, DFM p.15].
- Accent is per step. Slide sits on step *i* and governs the transition into *i+1*, holding the gate [primary, WS].

**What the combinations do, mechanically and musically:**

| Combination | Mechanism | Musical effect |
|---|---|---|
| Plain note | Gate 3.5/6 of a step [primary, WS]; MEG retriggers with normal decay | Staccato pluck; bounce |
| Tie | Gate held across the step boundary; no retrigger | Long note; the MEG keeps decaying, so a tied note *darkens* over time. Contrast between plucks and drones |
| Rest | No gate; the pitch list does not advance | Space and syncopation against the kick |
| Accent | VCA boost (1.55 ms RC), short MEG decay, sweep injection scaled by resonance [primary, WU] | Louder, brighter, snappier; with high resonance, the "wow" |
| Consecutive accents | C13 does not fully discharge [primary, WU; DFM p.5] | Each hit higher: the "can't stand it" scream. Whittle: "you poke it and it squeals a little ... you poke it again and it squeals even more" |
| Slide (same pitch) | Gate held, no retrigger | Same as a tie but keeps the MEG running. Used as a "legato tie" |
| Slide + octave jump | Constant-time slew over 12 semitones [primary, DFM p.16] | **The signature acid bend**: "activate slide on a note, then shift the following note either up or down an octave" [secondary, MusicRadar] |
| Slide into an accented note | Gate not retriggered, so the accent's MEG retrigger depends on implementation; Whittle notes the slide tail stays audible "unless it is an accented note, in which case the Main EG drives the VCA as well" [primary, WS] | Swelling, vocal glide; the accent changes decay mid-note. Lore says "sliding a normal note into an accented note instantly switches decay" [lore, seed appendix]. The voice-engine doc must define this exactly |
| Fast alternating notes + slide | Trill smoothed by the slide RC | Pseudo-vibrato. D16 implements this explicitly as "Vibrato", attributing the technique to "well-recognized 303 artists" [primary, P2M p.8] |

**Densities and statistics.** I found **no measured corpus statistics** for real 303 lines. The Organised Sound paper on acid pattern notation studies the notation community, not distributions [secondary, Cambridge https://www.cambridge.org/core/journals/organised-sound/article/acid-patterns-how-people-are-sharing-a-visual-notation-system-for-the-roland-tb303-to-create-and-recreate-acid-house-music/1FE37195243684AD6B6912FAE99E99E0]. It does document community patterns of **9 steps** and **11/16** [secondary], which shows odd lengths are normal practice. The best available proxy is crowd transcriptions of famous lines. I counted these myself from the raw page and give only counts, not the notes [lore, https://djjondent.blogspot.com/2020/04/famous-303-bassline-patterns-page-1.html]:

| Line (crowd transcription) | Steps | Pitch classes | Accents | Slides | Rests | Ties | Octave-shifted steps |
|---|---|---|---|---|---|---|---|
| Phuture "Acid Tracks" | 8-note loop | 4 incl. an octave note | not given | not given | 0 | 0 | 2 |
| "Confusion" (Pump Panel Reconstruction) | 16 | 2 | 0 | 2 | 4 | – | 6 (down) |
| DJ Tim & Misjah "Access" | 16 | 1 | 2 | 0 | 4 | 0 | 2 (up) |
| Josh Wink "Higher State" | 16 | 2 | **16 (all)** | 6 | 0 | 0 | 1 |
| Fast Eddie "Acid Thunder" (303 version) | 16 | 7 | 2 | 0 | 0 | 8 | 1 |
| Fatboy Slim "Everybody Needs a 303" (variant 1) | 16 | 6 | **16 (all)** | 0 | 0 | 0 | 13 |
| Hardfloor "Funalogue" | **12-step** patterns, chained in pairs | several | ~2 per pattern | ~4–5 per pattern | some | – | yes |

Patterns I read off these, all [lore]:

1. **Accent density is bimodal.** It is either sparse (0–3 of 16, placed for syncopation) or saturated (all or most steps). With all steps accented, the C13 accumulation and short accent decay become a continuous state, and the performance moves to Resonance, Env Mod and Cutoff. Wink's and Fatboy Slim's anthem lines are both transcribed as all-accent. **The plugin must sound great with accent on every step.**
2. **The anthem lines use 1–2 pitch classes**, and the drama is all in the knobs. Chicago lines ("Acid Thunder") are more melodic, with 6–7 pitch classes and many ties.
3. **Slides: 0–6 per 16**, clustered rather than scattered. Roland's tutorial: "If every note slides, the effect loses its punch" [secondary, Roland https://articles.roland.com/?p=92325]. MusicRadar: "Don't overdo it with ties, slides and octave jumps; a simple, repetitive pattern livened up with one or two flourishes often works better" [secondary].
4. **Rests: 0–5 per 16.** Typical placement is just after downbeats, or on the last step to let the loop breathe.
5. **Non-16 lengths are common.** There are 12-step triplet-feel patterns (Hardfloor), 8-step loops (Acid Tracks), 9 and 11 in the community, and Roland recommends 9, 11 and 13 for drift [secondary, Roland].
6. Roland's accent tip: "Place accents before a kick drum to create a syncopated bounce. Too many accents can flatten the groove" [secondary, Roland ?p=92325]. That holds for the sparse mode, and the all-accent anthem lines contradict it. Both are idioms.

The seed report's generator rules (root-weighted 1–4 pitch classes, slide before octave jumps, cluster-then-gap rests, Endless Acid Banger metric weights) are consistent with this and are not repeated here. See the seed report §3 and the sequencer research doc.

### B.2 The knob-riding vocabulary

The 303 is played by hand on top of a static loop. DJ Pierre on Acid Tracks: "We had it synched up, and it was playing some stuff, and I started just twisting knobs ... The track was literally 15 minutes long ... we were recording when we were tweaking it, so it was like we never knew when to stop" [primary-interview, RBMA https://daily.redbullmusicacademy.com/2012/12/dj-pierre-interview]. Norman Cook: "It only had four knobs to twiddle. You could learn the permutations of what happened between the four of them. And the great fun was that you did it all live" [primary-interview, quoted by Roland https://articles.roland.com/everybody-needs-a-303-fatboy-slim/ and in search-result excerpts]. Roland's arc description for that track: "crescendo from a filtered whisper to a squelchy scream" [secondary].

| Move | What happens in the circuit | Sound |
|---|---|---|
| **Cutoff low, slowly up over minutes** | Raises the base of the exponential cutoff; with stock Env Mod coupling the floor also depends on Env Mod (A.1.8) | The macro arc of the track: muffled thump → open snarl. The primary performance axis for all classic tracks |
| **Resonance up** | Raises loop gain; **also turns the second gang that smooths the accent sweep** (WU) | More vowel and whistle; accents change from a spiky blip to the smooth rising "wow". Resonance therefore changes the *shape* of the accent, not just the peak. Players feel this as the knob that "brings out the acid" |
| **Env Mod up** | Deepens the MEG sweep (and lowers the floor in stock) | Bigger "yow" per note. High Env Mod with low Cutoff is the classic squelch: dark between notes, bright on attack |
| **Decay short** | Fast MEG decay on non-accents (≥200 ms stock; 30 ms on DF) | Plucky, clicky, percussive. Roland's "percussive clicks" recipe: "Shorten decay ... Set cutoff high and resonance low ... ultra-short" notes [secondary, Roland Beyond Acid https://articles.roland.com/beyond-acid-pushing-the-tb-303-into-new-sonic-territory/] |
| **Decay long ("opened up")** | Long MEG; tied and slid notes keep sweeping | "Wow" and "talking". With slides, long notes become vowel glides. Tin Man slows a line to **75 BPM** and works with "the frequencies that emerge from the lengthened decay time" [secondary, RA review cited via search, https://ra.co/reviews/8582 and related] |
| **Accent up** | Scales VCA boost and sweep injection together (stock) | With high resonance this is the **scream**. Gives accented notes a voice; turning it down flattens the groove |
| **Accent + Resonance + repeated accents** | C13 accumulation, scaled by the resonance gang | The rising distressed cry. Whittle: "the effect happening more as the filter's resonance is increased" [primary, WU] |
| **Low Cutoff + high Res + high Env Mod + short Decay** | Narrow, fast sweeps from below the fundamental | The textbook squelch. Attack Magazine's starting recipe is the same direction: Distort, Cutoff, Res and Env Mod all turned up from minimum [secondary, https://www.attackmagazine.com/technique/tutorials/how-to-make-an-acid-house-bassline/]. Roland: "Start with the cutoff low, resonance high, and slowly open it up" [secondary] |
| **Hard-acid "low envelope, all drive"** | Cutoff and Env Mod near minimum, resonance moderate, into heavy distortion | Timbre from drive and resonance rather than sweep. Attributed to Emmanuel Top with an MXR [lore, seed appendix citing a KVR thread; I could not confirm it in the KVR thread I fetched, https://www.kvraudio.com/forum/viewtopic.php?p=8644925] |
| **Tuning knob** | VCO pitch only; does not move the filter or tracking [primary, DFM p.15] | Live pitch drift and dives. Reverb Machine notes Da Funk's 303 line spans **F1–F4**, beyond the 303's range, "suggesting transposition via the tuning knob" [secondary, https://reverbmachine.com/blog/daft-punk-homework-synth-sounds/] |
| **Square vs saw** | Saw is the bright default; square is "rounder", hollow | Square suits sub and liquid leads. Roland's "liquid lead": square, long slides, mid cutoff, low resonance [secondary, Roland Beyond Acid]. Several crowd transcriptions list square for anthem lines (Wink, Access) [lore]. The Wink square-vs-saw question is disputed in the seed report |
| **Trimmers** | Internal trims for cutoff and scale | Josh Wink reportedly "opened up the back of the 303 and played with the trim pots on the PCB to get the cutoff and resonance so high" [lore, second-hand, search result citing iumag interview https://iumag.co.uk/josh-wink-exclusive-feature-interview/]. Wink himself confirms "two original, non-modified 303s" [primary-interview, Mixmag https://mixmag.net/feature/josh-wink-tells-us-how-he-really-made-higher-state-of-consciousness]. Phoscyon exposes TM3 and similar trims [primary, P2M]. **Expose trims; they are a real technique** |

**Implication.** Cutoff, Resonance, Env Mod, Decay and Accent must be smooth (per-sample or per-block interpolated, no zipper noise at the extremes), MIDI-learnable, and **high-resolution**: 14-bit MIDI, CLAP/VST3 sample-accurate automation. Roland's own tip, "automating cutoff and resonance together", argues for **macro pairing** [secondary, Roland ?p=92325].

### B.3 Distortion: which units, where, and why resonance into a clipper matters

**Why it matters.** The 303's resonant peak is a narrow, loud spectral component. After a clipper, three things happen:

- The peak intermodulates with the saw harmonics, so resonance becomes a dense "throat" rather than a thin whistle.
- Peaks are compressed, so accented screams sustain at near-constant loudness while their timbre keeps climbing.
- The clipper's own filtering colours where the scream sits.

The seed report's summary of community consensus is that distortion folds resonance into a "full-throated shriek rather than a thin bird-warble" [lore, Gearspace https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/569639-what-distortion-pedal-303-acid.html; returned 403 to me, so via the seed appendix]. MusicRadar: "303 basslines are rarely heard without some kind of effect treatment. Distortion is the classic choice" [secondary]. MusicTech notes distortion was "less common in 80s Chicago house" and that era's grit came from tape, cassette and reel-to-reel [secondary, MusicTech https://musictech.com/guides/essential-guide/how-to-create-a-chicago-style-acid-house-bassline].

**Pre-filter vs post-filter.**

- Pre-filter (DF Overdrive, or hot external input) *quenches* resonance [primary, DFM p.18].
- Post-filter (pedals, mixer) *keeps and then folds* it.
- Post-VCA, pre-FM (Muffler) also shapes Filter FM depth [primary, DFM p.19].

These are three distinct placements, and the routing must allow all three.

**Units** (circuit facts from ElectroSmash analyses [secondary but circuit-derived; fetched via the Wayback Machine because electrosmash.com did not resolve]; artist links are lore):

| Unit | Circuit character (numbers) | Acid association |
|---|---|---|
| **Pro Co RAT** | LM308 op-amp; gain 0 to **67 dB** (1 + 100k/(47∥560)); two RC legs in the gain network give corners near **1.5 kHz** and **60 Hz**; **hard clipping, 1N914 diodes to ground**; LM308 slew limit (≈0.3 V/µs) cuts the gain available to highs (≤30 dB at 10 kHz); filter control sweeps about **475 Hz–32 kHz**; overall mid hump near 1 kHz | The "classic" 303 pedal and a Hardfloor association [lore, Gearspace via seed; Elektronauts]. The "~75% of pre-2000 acid records" claim is folklore |
| **Boss DS-1** | Transistor booster ≈**35 dB**, then an op-amp stage 0 to **26.5 dB**, **hard clip 1N4148 to ground**; **72 Hz** high-pass in the gain stage, so bass is less clipped; tone = 234 Hz LP blended with 1063 Hz HP, giving a **~500 Hz scoop** at noon | "THE 303 distortion"; modded DS-1 (diode or FET swap) is "instant Hardfloor" [lore, Gearspace via seed] |
| **Ibanez Tube Screamer** | **Soft clip in op-amp feedback**, gain ≤**41 dB**; **720 Hz** high-pass in the feedback loop, so bass is barely distorted (frequency-selective clipping); mid hump | MusicRadar's "personal choice" for 303 [secondary] |
| **MXR Distortion+** | ≈**46 dB** max gain (ElectroSmash comparison note) | Emmanuel Top [lore] |
| **Boss DS-2 / HM-2, Sherman Filterbank, mixer-channel overdrive (Mackie preamp, desk inputs)** | No circuit data gathered | **[lore] only.** I found no primary or secondary source tying specific acid records to these. Do not market them as "authentic"; offer them as generic voicings (two-stage turbo mid boost; HM-2-style dual-band EQ fuzz; desk/console soft clip) at P2 |

Daft Punk "Da Funk": Bangalter: "The bassline itself was from a 303 I'd bought in 1993. I'd just made all these random patterns" (quoted via Reverb Machine). The 303 was "run through a distortion pedal". The famous distorted lead is probably *not* a 303, more likely a Juno-106 or MKS-80 into distortion [secondary, Reverb Machine]. **Do not build presets around the idea that the Da Funk lead is a 303.**

**DSP requirements that follow.**

- Distortion is a first-class block after the voice, with at least: **RAT-type** (hard clip, pre-emphasis around 1.5 kHz, slew-limited, post LPF "filter" knob), **DS-1-type** (two-stage gain, 72 Hz pre-HPF, scoop tone), **TS-type** (soft clip with 720 Hz pre-emphasis, mid hump), a **fuzz** (asymmetric), and a **console/tape** soft clip.
- A **low-band bypass crossover** (Linkwitz-Riley at about 80–200 Hz, adjustable) so sub stays clean. This is the modern equivalent of the DS-1 and TS frequency-selective clipping.
- **Pre/post EQ** and a **one-knob compressor pre or post** the drive. Phoscyon 2 has a "Dynamics" knob with a Pre/Post order switch [primary, P2M p.10].
- **Oversampling ≥4×** in the drive.
- **Reorderable**: Phoscyon 2's distortion is fixed first in the chain [primary, P2M p.10: "The first effect ... is the Distortion"]. Making it movable (e.g. delay into distortion) beats parity.

### B.4 The rest of the classic chain

| Process | Evidence | Typical settings (for presets) |
|---|---|---|
| **Delay** | Attack Magazine: "Dotted eighth-note tempo-synced length with built-in high-pass filter" [secondary]. Phoscyon delay: L/R sync with Full/Dot/Triplet, feedback, in-loop LP/BP/HP filter with resonance, spread [primary, P2M p.14]. Plastikman's *Spastik* session archive shows how he "used delays, reverbs and filters" [secondary, search summary of the *Arkives* box set notes] | Dotted 1/8 or 3/16 left, 1/4 right; 25–45% feedback; HP in loop at 300–600 Hz so repeats don't muddy the bass; filter resonance in the loop for dub. **Feedback-into-filter** routing (A.1.11) is a differentiator |
| **Reverb** | Whittle: "Reverb and slowing the thing down (via playing it on a sampler at a lower pitch) probably has quite a bit to do with it too" [primary-opinion, WU]. MusicTech: old-school acid has "tight digital reverb ... short predelay ... zero feedback" [secondary]. Attack: a "Fake long spring" then a cathedral IR with "up to 400Hz" rolled off [secondary] | Two flavours: tight room/plate (0.4–0.9 s, HP 300 Hz) for Chicago; huge dark hall (4–10 s, HP 400 Hz, high damping) for Plastikman-style minimal and slow acid |
| **Chorus / flanger / phaser** | Phoscyon ships a double-line chorus [primary, P2M p.13]. Hawtin on his Yamaha SPX: "a dirty, great flange" (not 303-specific) [secondary, SOS Classic Tracks: Plastikman "Consumed", https://www.soundonsound.com/techniques/classic-tracks-plastikman-consumed] | Flanger and phaser as P1 inserts: slow sweep, synced 1–4 bars |
| **Compression / limiting** | Phoscyon: limiter with soft clip; MusicTech: limit the reverb to exaggerate it [secondary]. DF: use a limiter live [primary, DFM p.9] | Post-drive glue compression; a safety soft clipper always last |
| **EQ** | Attack: low cut ~85 Hz on the saturation path, mids rolled back, highs boosted [secondary] | 4-band parametric (Phoscyon parity) plus HP/LP |
| **Sampler slow-down / lo-fi** | Whittle's comment above; MusicTech on tape/cassette recording of early records [secondary] | A **"varispeed/tape" insert** (pitch-down with time stretch off, wow/flutter, HF loss) is cheap and on-genre |

### B.5 Performance tricks

| Trick | Evidence | Feature implied |
|---|---|---|
| **Live pattern switching** at the end of the pattern | Phoscyon: "The next pattern will start playing when the current pattern comes to the end of its final step", queued with a blue LED [primary, P2M p.20]; Host Note mode triggers patterns from MIDI notes [primary, P2M p.21] | Queued switching (end of pattern / bar / beat / immediate); pattern trigger by MIDI note **and** by CC/program change |
| **Live transposition** | 303 Pattern Play: Pitch + key transposes 0–12 semitones [primary, DFM p.15]; Phoscyon "Transpose" edit wraps notes at the range ends [primary, P2M p.26] | Non-destructive **live transpose by MIDI key** (latch), plus destructive transpose. Decide wrap vs octave-fold |
| **Polymeter** | Roland: shortening to 9, 11 or 13 steps "creates a loop that shifts over time"; "A 13-step bassline over a 4/4 beat will never repeat in the same place" [secondary, Roland ?p=92325]. Community 9- and 11-step patterns [secondary, Organised Sound]. Tin Man is known for "irregular step lengths" [secondary, Electronic Beats https://www.electronicbeats.net/tin-man-your-boring-is-my-hypnotic/] | Length 1–64 (Phoscyon parity); **independent lane lengths** for pitch, accent, slide and gate (beyond parity) so accent cycles can drift against notes |
| **Pitch/time decoupling accidents** | MusicRadar on separate pitch and time lists [secondary] | An optional **"303 entry mode"** where pitch and time are separate lists, so editing time re-flows pitches; plus a **"rotate pitches only"** action |
| **Shift / rotate** | MusicTech: "use the Shift button ... to slide the whole pattern over to the left or right ... completely change the feel" [secondary]; Phoscyon "Shift" [primary, P2M p.26] | Rotate the whole pattern **and** per lane |
| **Long lines by chaining, per-bar articulation edits** | Tin Man: "tap them in over four bars ... keep reprogramming all of the accents and slides per bar over four bars" [primary-interview, Electronic Beats]; Hardfloor "Funalogue" chained 12-step pairs [lore] | 64-step patterns plus pattern chains/songs; per-bar copy with articulation variations |
| **Global overrides** | Quicksilver "ALL ACCENT", "CHOP" [secondary]; Phoscyon Live Override Set/Clr [primary]; Acidlab "full accent on/off, full slide on/off" [lore]; DF manual Accent button [primary] | Momentary and latching **Accent-all, Slide-all, Accent-none, Slide-none, Chop (gate %), Rest-mute**, MIDI-mappable |
| **Accent "poke"** | DF Accent pushbutton [primary]; Whittle's "poke it again" [primary, DFM p.5] | A pad or key that forces accent on the next step (or on the current note) |
| **Layering 303s** | Wink: "two original, non-modified 303s" [primary-interview, Mixmag]; Hardfloor "up to six" [secondary, Wikipedia https://en.wikipedia.org/wiki/Hardfloor]; DF master/slave via CV/Gate/Accent [primary, DFM p.30] | Multi-instance sync is free in a DAW. Add **MIDI out of the sequencer** (Phoscyon parity) and an optional internal **second voice** for octave or cross-FM layering |
| **Tuning sweeps** | Da Funk range inference [secondary]; the tuning knob only affects the VCO [primary, DFM p.15] | A Tuning knob of ±12 semitones continuous (Phoscyon parity: ±12, or Ext mode with semitone + cents [primary, P2M pp.6–7]); smooth, automatable, **not** quantized |
| **Tempo-synced parameter motion** | DF user: LFO into Filter CV "for rhythmic filter sweeps and other crazy fast bell like effects" [primary-user-quote, DFP]; Roland: automate cutoff and resonance [secondary] | Synced LFO/envelope-follower mod matrix (at least 2 LFOs, 1 step-modulator lane), destinations including Cutoff, Res, Env Mod, Decay, Accent, Drive, FM |
| **Slow acid** | Tin Man at 75 BPM [secondary, RA]; VEG sustain on the DF [primary] | Musical behaviour at 60–100 BPM (long decay, VEG sustain) |
| **Mod-wheel filter** | DF MIDI default CC1 → cutoff, additive about 5 octaves [primary, DFMIDI] | Default CC1 → cutoff offset, so a performer can dive and return to the exact panel setting |

---

## Feature recommendations

Prioritised. P0 = ship-blocking for the acid workflow, P1 = differentiators, P2 = polish. "Parity" means Phoscyon 2 has it.

**P0**

1. **Accent engine with C13 accumulation, resonance-gang coupling, short accent MEG decay and 1.55 ms VCA RC**, plus Sweep Amt, Acc Vol, two couple switches and a C13 trim (parity). Serves: B.1 accent runs, all-accent anthem lines, B.2 scream.
2. **Smooth, high-resolution, MIDI-learnable** Cutoff, Res, Env Mod, Decay, Accent and Tuning, with per-sample smoothing and sample-accurate automation, plus optional 14-bit CC. Serves: B.2. This *is* the instrument.
3. **Stock-accurate Env Mod mapping** (floor moves with Env Mod; minimum not zero in 303 mode). Serves: B.2 low-cutoff squelch.
4. **Slide**: constant-time RC calibrated in audible ms (stock 60), gate-hold semantics exactly as in WS, slewed CV also driving filter tracking. Parity: Slide Time. Serves: octave bends.
5. **Post-voice distortion with ≥5 models (RAT, DS-1, TS, Fuzz, Console/Tape), low-band bypass crossover, pre/post compressor, pre/post tone, ≥4× oversampling.** Serves: B.3.
6. **Sequencer**: 1–64 steps, note/tie/rest, accent, slide, octave ±1 (consider ±2), shuffle, step-length/gate %, time scale including **triplet** (parity), **queued live pattern switching**, **live transpose by MIDI key**, MIDI out with accent velocity (parity), host-sync (Phoscyon's "Host Link" option, parity). Serves: B.1, B.5.
7. **FX chain**: tempo-synced delay with in-loop filter (parity), reverb (parity), chorus (parity), 4-band EQ (parity), limiter with soft clip (parity), **all reorderable including distortion**. Serves: B.4.
8. **Global live overrides**: Accent-all, Slide-all, Accent-none, Slide-none, Chop. Parity is Phoscyon's Set/Clr for Slide, Accent and Vibrato. Serves: B.5.

**P1 (Devil Fish layer, the Linux-exclusive edge over Phoscyon 2)**

9. **"Stock / Devil Fish" mode switch** that snaps mod controls to 303-equivalent values (DFM p.8 table), so the extended space never costs authenticity.
10. **Overdrive into the filter (0 to 66.6×)** with a nonlinear ladder that quenches resonance. Serves: DF fat tones, pre- vs post-drive contrast.
11. **Filter FM** (linear, AC-coupled, clamped at 0 Hz), with source select: VCA-post-Muffler (DF), Filter-Out with inverted polarity (DF jack trick), VCO saw or square (x0x). Per-sample cutoff. Serves: edgy accents, chaos and period-doubling.
12. **Filter Tracking**: linear Hz/V, 0 to about 1.2× of 2.7 kHz/V, adjustable pivot, floor clamp so low notes can vanish. Serves: pitch-as-gate.
13. **Separate Normal and Accent Decay** (30 ms–3 s; parity for Acc Decay), **Amp Decay 16 ms–3 s + Amp Sustain** (beyond parity), **Soft Attack 0.3–30 ms on non-accents** and a separate **Filter Env Attack** (parity).
14. **Accent Sweep: Off / Normal / Fast / Slow**, plus a **Hi-Res ×2** switch with self-oscillation only at mid and high cutoff. Serves: "wup/wap/whoa", whistly sweeps.
15. **Muffler** (Off / 1 / 2), post-VCA, pre-FM-tap.
16. **Extended ranges** (cutoff to about 5 kHz panel max and lower; Env Mod 0 to 3.2×) and a **Bass 303/DF switch** (C20/C21).
17. **Sidechain audio into the filter and/or FM; Filter-Out aux bus; internal FX-return into the filter.** Serves: DF external processing and the Linx-style feedback patch.
18. **Host MIDI semantics à la DF**: velocity ≥ threshold → accent (default 65, configurable; parity), overlap → slide, CC65 → slide, CC64 → gate hold, CC1 → cutoff offset (about 5 octaves), **Merge mode** (host notes ORed with the internal sequencer).
19. **Independent lane lengths** (polymetric accent and slide lanes) and **per-lane rotate**. Serves: polymeter and drift.
20. **Mod matrix with ≥2 synced LFOs and a step-mod lane.** Serves: tempo-synced motion. Acid V sets the expectation [secondary].
21. **Vibrato via trill + slide** (parity, P2M p.8), with speed and depth per step.
22. **Calibration trims** (parity: TM3, C21/22, AccEnv, AccAmp, C13, TM5, SQR PW Shift, BA662 Clicks, BA662 Noise), plus ABL3-style **Gate Trim** and **Low Cut**, plus **vintage gate latency** (0–5 ms) and **VEG stock decay** (1.2–4 s, the Whittle vs Open303 conflict).
23. **Pattern import/export**: MIDI drag-out (parity), and **TD-3 / TB-03 / ABL3 pattern import** (ABL3 has this). Serves: reusing community patterns.
24. **Randomiser with per-lane density** (parity), and a smarter generator (see the seed report §4 and the sequencer doc).

**P2**

25. **Twin mode**: two voices with cross-FM (the DF "69"), or octave layering.
26. **Sub-oscillator** (−1/−2 oct, square) **with a level knob and pre/post-filter routing**. This answers the TD-3-MO criticism.
27. **Saw/square continuous blend** and **PW shift** (Acid V parity-ish) [lore basis].
28. **Output-stage clip on accents at high Volume** (DF p.24) and the **500 ms cutoff settle drift** on large steps (DF p.12).
29. **Varispeed/tape insert**, flanger and phaser inserts, and extra drive voicings (DS-2 turbo, HM-2, Sherman-style resonant filter-drive) labelled generically.
30. **GUI**: normal/accent MEG meters and gate LED (DF), C13 charge meter (visualises the accumulation; great for teaching), and **"303 entry mode"** with separate pitch and time lists.

---

## Factory content brief

Write original patterns. Do not copy famous lines note-for-note; the structures below are generic recipes. Each style lists tempo, length, pitch material, articulation density, sound and FX, and what to automate. Default octave notation: base octave around C2 (MIDI 36–47) with ±1 transpose. Aim for about **8–16 patterns per style** and about **10 sound presets per style**. Every scene should pair a pattern bank with matched sounds.

1. **Chicago acid (1986–89)**
   - 118–126 BPM; 16 steps (some 8-step loops).
   - 3–7 pitch classes; chromatic neighbours and minor 3rds; 30–50% of steps **tied** (long-short phrasing); 1–3 sparse accents on off-beats; 0–2 slides; 0–2 rests.
   - Saw. Res ~60–75%, Env Mod ~50%, Decay ~50%, Accent ~40%.
   - FX: light tape or console saturation, *no* pedal distortion; tight digital room (0.5 s).
   - Automate: a slow cutoff ride over 2–5 minutes.
   - Variant: an 8-step loop of 3–4 notes with an octave note, sound changed only by knobs ("15-minute jam" presets with long automation demos).
2. **Anthem / screamer (early–mid 90s)**
   - 126–135 BPM; 16 steps; **1–2 pitch classes**.
   - **Accent on 75–100% of steps**; 4–6 slides clustered in two groups; 0–1 octave hop; no rests.
   - Saw or square. Res 80–95%, Cutoff starting around 10 o'clock.
   - FX: RAT-type drive with low-band bypass; dotted-1/8 delay.
   - Automate: Res and Cutoff together in 32-bar arcs; a breakdown with cutoff closed, then the build.
   - This is where the C13 model earns its keep.
3. **Continental hard acid / acid techno (Hardfloor, Top lineage)**
   - 132–150 BPM; 16 steps plus **12-step triplet patterns** chained in pairs.
   - 2–4 pitch classes; root-heavy; octave-up stabs on off-16ths; 2–4 accents; 2–4 slides (one into an octave); 2–4 rests.
   - Saw. Moderate Res, **low Env Mod, low Cutoff, heavy DS-1- or RAT-type drive**.
   - FX: short delay; compressor post-drive.
   - Layering scenes: two to three voices with offset accent phases, using MIDI out to a second instance.
4. **Minimal / Plastikman-style space acid**
   - 120–130 BPM; 16 steps with **6–10 rests**; 1–3 pitch classes, often low; 1–2 accents; 1–2 slides; ties into silence.
   - Square or saw. Res high, Decay long.
   - FX: **long dark delays with in-loop filter, big hall reverb**, no drive or very light drive.
   - Odd lengths: 11, 13 and 15 against 16-step drums. Mute-and-return performance automation.
5. **Big-beat acid**
   - 120–135 BPM; 16 steps; all-accent or nearly.
   - 4–6 pitch classes with **many octave transposes (≥8 of 16)**; no rests.
   - Heavy fuzz or DS-1; EQ scoop.
   - Big "filtered whisper to squelchy scream" builds.
6. **Melodic / modern acid (Tin Man, Ceephax, Vibert, AFX-adjacent)**
   - 75–130 BPM (include **slow 75–100 BPM** presets).
   - **32–64-step lines** (2–4 bars) with 5–8 pitch classes in a mode (Aeolian, Dorian, Phrygian), stepwise motion, passing tones; **articulation re-programmed per bar** (accents and slides differ bar to bar).
   - Square for liquid tone. Long slides (DF slide 150–300 ms). Long Decay; **Amp Sustain > 0** for held notes; moderate Res; little or no drive.
   - FX: chorus or flanger, plate, dotted delay.
7. **Polymeter studies**
   - Any tempo. Lengths **5, 7, 9, 11, 13, 15**, plus scenes with **independent accent-lane length** (e.g. notes 16, accents 7, slides 5).
   - Simple 1–3-note material so the drift is audible.
8. **Devil Fish extremes** (show off the Linux-exclusive layer)
   - **FM screamers**: Overdrive 4–6, Filter FM high, Muffler 1, accent runs. Include one "edge of chaos" preset where period-doubling flips between bars.
   - **Whistlers**: Hi-Res position 1, Overdrive low, self-oscillating filter with Tracking ~1.0, playing melody from the filter.
   - **Tracking gates**: high tracking so down-octave notes vanish, which makes rhythm from pitch.
   - **Accent-only percussion**: Soft Attack max, Amp Decay min, so only accented notes sound.
   - **Sub-heavy DF bass**: Bass DF, Overdrive 3, Muffler 2, low Res.
   - **Drones**: Amp Sustain full, long ties, slow LFO on cutoff.
9. **Percussive and utility**
   - Roland "percussive clicks" recipe: short Decay, high Cutoff, low Res, short gates scattered.
   - Square sub-bass with cutoff nearly closed, low Res.
   - Clean "init 303" (stock mode, neutral knobs).
   - Calibration variants: a few "different units" via trims (drifted TM3, PW shift, BA662 noise), mirroring Phoscyon's calibration presets.

Every style also needs at least one **"teaching" scene** that isolates a mechanism: four accents in a row at fixed knobs (to hear C13), slide-into-octave, tie vs slide vs plain note, and pre-drive vs post-drive resonance.

---

## Open questions

1. **DF Sweep Speed Fast and Slow component values** are unpublished. Implement Fast as a high-pass (series-cap) injection and Slow as τ ×3–5 with gain ×2, then tune by ear against DF or TD-3-MO recordings. Expose τ and gain as hidden trims.
2. **Muffler circuit** is unpublished. Use the inferred level-dependent soft clipper with bass bypass (A.1.10) and tune against recordings. Both modes need A/B.
3. **DF panel scale and taper.** "Normal Decay no lower than 2" and "Accent Decay fix at about 2.7" do not fit a pure exponential 30 ms–3 s taper on a 0–10 scale (200 ms would land near 4.1). Either the DF dials use another scale or the pot taper differs. Decide our own taper; calibrate the "stock snap" values in **milliseconds** (200 ms), not panel numbers.
4. **VEG stock decay**: Whittle says 3–4 s, Open303 uses 1230 ms. Default to about 3 s per Whittle, expose it as a trim, and A/B with a held-gate test.
5. **Filter Tracking pivot drift** ("higher filter frequency → higher zero point") is described only qualitatively. Ship a fixed pivot trim first; add the drift only if A/B shows it matters.
6. **Accent on a slid-into note**: the exact MEG and accent behaviour mid-slide is under-documented (lore says the decay switches instantly). Resolve in the voice-engine doc from the schematic or measurement. It affects the sound of every all-accent anthem line.
7. **Borg mod**: no feature list could be reached (Analogue Solutions pages not archived). Ignore unless someone supplies the manual.
8. **x0x R46 "resonance accent increase"**: its function is not confirmed on the schematic. Check the service manual. If R46 scales the sweep injection, it maps to our Sweep Amt range.
9. **Square/saw mix** as a "common mod" has no primary source. Ship the blend as an extended control without claiming authenticity.
10. **Pattern statistics** come only from crowd transcriptions [lore]. If the generator needs priors, build a small licensed corpus (e.g. CC-BY-SA patterns from acid-tabs.com, attribution required) and measure it, rather than trusting the counts above.
11. **Distortion artist attributions** (RAT for Hardfloor, MXR for Top, DS-1 as "the 303 distortion") are lore. Name models generically ("Rodent", "DS", "Screamer"), avoid trademarks in UI and marketing, and describe circuits, not artists.
12. **Triplet mode on the original 303**: I relied on the TB-03 manual (8th-note triplet steps). Confirm against the TB-303 owner's manual in the sequencer doc.

---

## Implications for the build

1. Ship **two voice modes in one engine: "303" (stock, default) and "Devil Fish" (extended)**, with a one-click snap from DF to stock-equivalent values (DFM p.8 table, expressed in ms and Hz).
2. The filter **must be nonlinear inside the loop** so that pre-filter overdrive quenches resonance (DFM p.18). A linear ladder plus a post-shaper fails both the Overdrive and the Hi-Res behaviours.
3. **Cutoff modulation runs at audio rate, per sample, at the oversampled rate** for Filter FM, accent sweep and tracking. Linear-Hz inputs (FM, tracking) add *after* the exponential converter and clamp at a floor.
4. Implement the **accent sweep as a diode-charged RC** (τ_charge ≈ 60 ms, τ_discharge ≈ 100 ms from Whittle's values), with the **second gang of Resonance** crossfading direct vs smoothed. Add Fast, Slow and Off modes and a C13 trim. Test: 4 consecutive accented 16ths at 130 BPM must produce monotonically rising cutoff peaks in Normal and falling peaks in Fast.
5. **VCA accent path** gets a 1.55 ms one-pole (47 k × 0.033 µF). Soft Attack (0.3–30 ms) applies to non-accented notes only.
6. **Slide** is an RC with τ = audible_time / 2.7 (stock 22 ms τ = 60 ms audible; DF up to 133 ms τ = 360 ms). It slews the CV feeding both the VCO and filter tracking. Gate timing follows WS exactly (3.5/6 clocks; slide active from 1/16 after slid-note start to the end of the next note).
7. Envelopes: **Filter Decay (normal), Accent Decay, Amp Decay + Amp Sustain, Filter Env Attack, Soft Attack** as separate parameters. Never label an amp-envelope control "Normal Decay" (the JC-303 pitfall).
8. **Env Mod mapping must move the cutoff floor** (Open303 measured mapping as reference) and must not reach zero in 303 mode.
9. Resonance **self-oscillates only at mid and high cutoff**, because of the feedback-path HPF. Hi-Res doubles feedback; do not "fix" low-frequency self-oscillation.
10. Distortion: ≥5 circuit-voiced models with the numbers in B.3, low-band bypass crossover, pre/post compressor, ≥4× oversampling, **position-movable** in the FX chain.
11. FX chain reaches Phoscyon parity (delay with in-loop multimode filter and Full/Dot/Triplet sync, reverb, chorus, 4-band EQ, limiter with soft clip), plus a flanger/phaser, a **delay→filter feedback send**, and an always-last safety limiter in DF mode.
12. Host I/O: optional **sidechain into filter/FM**, optional **Filter-Out aux bus**, MIDI out of the sequencer with configurable accent velocities. Check CLAP, VST3 and LV2 bus support on Linux hosts (Bitwig, Reaper, Ardour) during the architecture phase.
13. MIDI conventions: accent threshold default 65 (configurable); overlap → slide; CC65 → slide; CC64 → gate hold; CC1 → additive cutoff about ±5 octaves; MIDI note → pattern trigger mode; latching key transpose.
14. Sequencer beyond parity: **per-lane lengths** (pitch, gate, accent, slide), per-lane rotate, a **"303 entry" mode** with decoupled pitch and time lists, pattern chains for 64+ step lines, global overrides (Accent-all, Slide-all, none, Chop) as automatable parameters.
15. Parameter design: every performance knob is smoothed, sample-accurate, MIDI-learnable, with 14-bit option, and has a pairable macro (Cutoff+Res).
16. GUI shows normal/accent MEG meters, a gate LED and a **C13 charge meter**, which makes the accent accumulation visible and is a selling point.
17. Calibration trims at Phoscyon parity (TM3, C21/22, AccEnv, AccAmp, C13, TM5, SQR PW Shift, BA662 Clicks and Noise), plus Gate Trim, Low Cut, VEG stock decay, gate latency and Tracking pivot.
18. Factory content follows the nine-style brief above with teaching scenes. Use generic names for distortion models, and no artist or track names in preset titles.
19. Validation plan: record the A/B targets listed in Open questions 1–6 (accent runs, sweep modes, muffler, VEG hold, slide-into-accent) from a TD-3-MO or DF owner. Treat those recordings as the acceptance tests for the DF layer.
