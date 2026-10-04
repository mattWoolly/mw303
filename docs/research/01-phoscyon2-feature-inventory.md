# 01 — D16 Group Phoscyon 2: exhaustive feature inventory (parity target)

Status: research deliverable, 2026-10-02. Target version: **Phoscyon 2 v2.1.0** (Windows MSI built 2023-02-20; still the shipping version on d16.pl as of this writing; product page lists macOS 10.13 to macOS 26 support).

This document defines the parity checklist. It is built mainly from four primary sources, and it **corrects several claims in the seed research** (see §0.2).

## 0. Sources and method

### 0.1 Source keys used throughout

| Key | Source | Type |
|---|---|---|
| **[M p.N]** | Official *Phoscyon 2 User Manual*, 59 pp., dated 21 Nov 2022. https://d16.pl/pub/manuals/Phoscyon%202-manual-gb.pdf. Text and all embedded screenshots were read. | primary |
| **[B]** | The **parameter definition table embedded in the shipping v2.1.0 VST3 binary** (`<PluginParameters application='Phoscyon2'>`, 203 params, indices 0–202, with min/max/default/unit/enum/automatable flags), plus the embedded default plugin state. Extracted with `strings` from the free demo installer https://cdn.d16.pl/installers/Phoscyon2/Phoscyon2-2.1.0(x64).msi. This is the most precise source available for ranges and defaults. | primary |
| **[F]** | **Factory content files** in the same installer (12 zipped XML content packs, 884 preset/pattern files). Used to recover file formats, the step bit layout, the 19 distortion "models" and their internal settings, the calibration presets, and the factory-content counts. | primary |
| **[P]** | Product page https://d16.pl/phoscyon2 | primary (vendor) |
| **[C]** | Changelog https://d16.pl/pub/changelogs/Phoscyon%202%20-%20Changelist.txt (lists 2.1.0 only) | primary |
| **[O]** | Press-kit *Product Overview* PDF (v2.1.0, 25 Oct 2023) and 2D GUI renders, https://d16.pl/pub/press/Phoscyon2-PressKit.zip | primary (vendor) |
| **[KVR]** | https://www.kvraudio.com/product/phoscyon-2-by-d16-group/details | secondary |
| **[GN]** | Gearnews review https://www.gearnews.com/d16-phoscyon-2-review/ | secondary |
| **[MT]** | MusicTech review (9/10) https://musictech.com/reviews/software-instruments/d16-group-phoscyon-2-acid-line-review/ | secondary |
| **[MR]** | MusicRadar / Computer Music review (9/10) https://www.musicradar.com/reviews/d16-group-phoscyon-2 | secondary |
| **[SY]** | Syntorial highlights https://www.syntorial.com/highlights/phoscyon-2/ | secondary |
| **[RK]** | Rekkerd release news https://rekkerd.org/d16-group-releases-phoscyon-2-bassline-synthesizer-plugin/ | secondary |
| **[T]** | KVR forum thread "D16 Phoscyon 2 is out now!" https://www.kvraudio.com/forum/viewtopic.php?t=579974 (pages 27, 29, 30, 31 read) | lore |
| **[GUI]** | *(added in verification)* The **GUI layout file `Phoscyon.d16gui`** inside each skin package in the same installer (`LightSkin1180` → `content` → `Phoscyon.d16gui`, XML, 757 lines). It binds every on-screen control to its host parameter, so it shows exactly which knobs each insert's basic view holds. Also the hidden `.filter` files in the Scene and Synthesis content packs, which name the tag groups. | primary |
| **[P1M]** | *(added in verification)* Phoscyon 1 user manual (local copy `…/scratchpad/refs/manuals/p1.txt`). Used only as supporting evidence where v2 is silent. | primary (v1) |
| **[ABL3M]** | *(added in verification)* AudioRealism ABL3 operation manual (local copy `…/scratchpad/refs/manuals/abl3.txt`). | primary (competitor) |
| **[RW]** | *(added in verification)* Robin Whittle, "TB-303 Slide" (1999/2012), https://www.firstpr.com.au/rwi/dfish/303-slide.html (local copy `…/scratchpad/refs/web/303-slide.html.txt`). | primary (hardware measurement) |

Local working copies (scratchpad, not in the project): manual text `…/scratchpad/p2/manual.txt`, parameter table at line ~113629 of `…/scratchpad/p2/vst3.str`, unpacked factory content at `…/scratchpad/p2/msi/x/*/c/`.

**Licensing note.** Factory presets, patterns and skins are D16's copyrighted content. **They are documented here only as reference. Do not ship them or copy them into our product.** File-format knowledge (§8.4) may be used for an *import* feature (interop), but that is a decision for the user.

### 0.2 Corrections to the seed research (`ref-mwAudio303/docs/research/2026-06-09-tb303-research-report.md` §2.3)

1. **"19 hardware-voiced distortion models" are presets, not 19 algorithms.** Each "model" is a factory preset (`.ph2dst`) over **one parametric distortion engine** that has **3 clipper types (Diode, RAT, Overdrive)** plus a pre-HPF, a pre-EQ peak, a tilt EQ, bias, knee, a "Color" resonant filter, a post-HPF and a volume trim [B][F]. See §3.2.
2. **The randomizer has no "Event Threshold" knobs and no "Arp.Chord/scale selector".** Those belong to Phoscyon **1**. In v2, scales are expressed as a **12-pitch-class note mask plus an octave mask**, saved as *randomizer presets* (50 factory presets named "Scale – …", "Chord – …", "Interval – …"). There are four **Amount** knobs (Note, Slide, Acc, Vibr) and a **Draw** button [M p.31–33][F]. See §6.
3. **"No root-note selector" is accurate**, but the precise statement is this: the note mask can be transposed (rotated) with up/down arrows; there is no key/root parameter and no scale dropdown [M p.33][T].
4. **"~13 knobs"**: there are 13 continuous synthesis knobs, plus a Wave switch, an Ext.Mode switch and two coupling LED switches [M p.6–7].
5. **The Bitwig "edit-while-playing desync"** was reported in [T] p.27. Another user resolved it by enabling **Host Link**, which is off by default [M p.21][T]. It is a default-setting issue, not a sequencer bug.
6. **"Double slide merges into a sustained note"** [T] p.31 is the documented semantics: *"Slide – When set, the current step will slide to the next set Note"* [M p.25]. Slide connects across rests to the next note-bearing step. See §4.6.
7. **Apple Silicon:** both the product page and the manual list Apple Silicon. [T] p.30 has conflicting user reports.

---

## 1. Synthesis section (front-panel row 1)

Layout, left to right [M p.6–7][O render]: **WAVE** (2-position vertical switch, saw icon on top, square icon below) | **EXT. MODE** LED-switch above TUNING | TUNING | CUT OFF | RESONANCE (orange LED above = Resonance/Sweep couple) | ENVMOD | DECAY | ACCENT (turquoise LED above = EnvAcc/AccVolume couple) ‖ separator ‖ SLIDE TIME | SWEEP AMT. (orange LED above) | ENV. ATTACK | ACC. DECAY | ACC. VOL. (turquoise LED above) | VIB. SPEED | VIB. DEPTH.

The couple LEDs are drawn in matching colours above both knobs of each pair (orange over Resonance and Sweep Amt.; turquoise over Accent and Acc. Vol.). That makes the coupling visible. (The manual text calls the Resonance/Sweep pair "red LEDs" [M p.7]; the renders show orange-red. Each pair is two GUI buttons bound to the same parameter, so clicking either LED toggles the couple [GUI].)

The default-skin window is **1180 × 877 px** in SEQ/ARP mode [GUI `MainPanel`].

### 1.1 Parameter table (exact host-visible names, indices, ranges and defaults from [B])

All 0–100 % knobs display one decimal place (`decimal_nosign.1`). "Init" is the factory `- Init -` synthesis preset [F]. "First-load" is the state of a fresh instance: Scene *Best Served with Beat* with Synthesis *Multi-Dimensional* [B embedded default state][M p.4 screenshot].

| # | Name (host) | GUI label | Type / range | Param default [B] | First-load value | Behaviour [M p.6–8] |
|---|---|---|---|---|---|---|
| 0 | Waveform | WAVE | enum {Square, Sawtooth} | Sawtooth | Square | VCO waveform select. |
| 1 | Tuning Extended | EXT. MODE (LED) | state Off/On | Off | Off | Off: a single continuous Tuning knob. On: the knob area changes to a **TRANS.** numeric display (coarse) plus a **FINE TUNE** knob [M p.7 image]. |
| 2 | Tuning | TUNING | float −12…+12 semitones, 2 dp | 0 | 0 | Continuous ±12 st. "Middle position is A=440 Hz." |
| 3 | Tuning Coarse | TRANS. | int −12…+12 semitones | 0 | 0 | Active only in Ext.Mode. Drag the numeric display. |
| 4 | Tuning Fine | FINE TUNE | float −100…+100 cents (integer display) | 0 | 0 | Active only in Ext.Mode. |
| 5 | Cutoff | CUT OFF | 0–100 % | 0 | 34 | Base filter cutoff. Physical Hz mapping is not published. |
| 6 | Resonance | RESONANCE | 0–100 % | 100 | 90.4 | Filter resonance. With the R/S couple on, it also sets the accent-sweep shape (see Sweep Amount). |
| 7 | Envmod | ENVMOD | 0–100 % | 0 | 32 | Filter-envelope depth. |
| 8 | Decay | DECAY | 0–100 % | 100 | 66.4 | Filter-envelope decay, **non-accented notes only**. |
| 9 | Accent | ACCENT | 0–100 % | 100 | 100 | Filter-envelope level on accented notes. With the EnvAcc/AccVol couple on, it also sets the accent volume boost. |
| 10 | Slide Time | SLIDE TIME | 0–100 % | 50 | 50 | Glide duration. Also sets the smoothness of vibrato (§1.3). |
| 11 | Resonance/Sweep Couple | orange LED | state | **On** | On | On: Resonance drives the accent "smooth vs spiky" crossfade and Sweep Amt. is inactive. Off: Sweep Amt. drives it manually. |
| 12 | Sweep Amount | SWEEP AMT. | 0–100 % | 100 | 100 | "Crossfade between a smooth envelope (high values) and a spiky envelope (low values) for accented notes." Active only when couple = Off. |
| 13 | Envmod Attack | ENV. ATTACK | 0–100 % | 0 | 0 | Filter-envelope attack time. Not on the original 303. |
| 14 | Accent Decay | ACC. DECAY | 0–100 % | 0 | 0 | Filter-envelope decay for accented notes. The 303's fixed short accent decay is presumably at 0 (inference from factory median = 0). |
| 15 | EnvAcc/AccVolume Couple | turquoise LED | state | **On** | On | On: Accent controls both the filter and the volume accent, and Acc. Vol. is inactive. Off: independent. |
| 16 | Accent Volume | ACC. VOL. | 0–100 % | 100 | 100 | Accent amplitude boost. Active only when couple = Off. |
| 17 | Vibrato Speed | VIB. SPEED | 0–100 % | 50 | 50 | Length of the two alternating vibrato notes. |
| 18 | Vibrato Depth | VIB. DEPTH | 0–100 % | 50 | 50 | Pitch spread of the two alternating notes. |

[primary] for all names, ranges and defaults (B). Note the odd raw parameter defaults (Cutoff 0, Envmod 0, Resonance 100). A user never hears these, because a new instance loads the default scene. We should pick musically sensible init values instead.

**Factory-preset usage statistics** (171 synthesis presets [F]; useful when choosing our default ranges and curves) [primary]:

- Cutoff median **1.2 %**, and **85 of 171 presets sit at exactly 0 %**. Phoscyon's 0 % is clearly a usable sound (the envelope does the work), so the cutoff range starts low. Our Cutoff = 0 must not be a closed, silent filter.
- Resonance median 89.6 %. Envmod median 30.4 %. Decay median 52.4 %. Accent median 88 %.
- Slide Time spans 0–80.8 %, median 50 %.
- Env Attack and Acc. Decay are 0 in most presets.
- Vibrato Speed spans 50–93.2 % (median 76). Vibrato Depth spans 23.6–100 % (median 26.4).
- Distortion Active in **167/171** presets. Calibration "- Custom -" in 170/171.
- R/S couple On in 136/171. EnvAcc/AccVol couple On in 94/171. Ext tuning in 25/171. BA662 Clicks On in 15/171.

### 1.2 Interaction rules (UI)

- **Fine-adjust:** Ctrl-drag (Win) or Cmd-drag (mac) [M p.3].
- **Double-click** resets a control to the default value, **or to the value from the most recently loaded preset** [M p.3]. This is a non-standard rule; we should match it.
- **Right-click** opens a context menu showing the parameter name and its value (display only, not editable in v2.1.0 [M p.3]), plus MIDI CC "# —" with **Learn** and **Clear** [M p.55–56].
- v2.1.0 added a **value tooltip** that replaces the old status bar [C].

### 1.3 Vibrato (the "Vibrato novelty")

Vibrato is not an LFO. It reproduces the 303 programming trick of rapidly alternating two notes with slide between them [M p.8][P]:

- **Vib. Speed** sets the duration of each alternating note.
- **Vib. Depth** sets the pitch spread of the two notes "around the base note".
- **Slide Time** sets how smooth the resulting oscillation is.

Vibrato is applied per step (Vibrato flag) in SEQ/ARP. In EXT mode, the **Mod Wheel** controls vibrato depth [M p.17]. The physical mappings (ms per alternation, semitone spread at 100 %) are **not published**. See Open questions.

---

## 2. Calibration ("service trim pots") section

Reached by clicking **OPEN** on the logo cover, top right. An animated cover slides away to show a green PCB with 9 trimmers and a blue LCD calibration-preset display [M p.8–9]. The animation can be disabled under Cog → GUI → *Disable Calibration Cover Anim* [M p.9]. Values are **saved with synthesis presets** [GN][F] and have their own preset browser (9 factory presets).

| # | Name (host) | GUI label (unit) | Range [B] | Param default [B] | "Ideal TB303" preset [F] | What it changes [M p.9] |
|---|---|---|---|---|---|---|
| 19 | Calibration Preset | LCD | string | — | — | Opens the calibration browser. |
| 20 | TM3 Cutoff Tuning | TM3 [mV] | −500…+500 mV | 0 | 0 | "The base cutoff frequency" (filter offset trim). |
| 21 | C21/C22 DC Filter | C21/22 [F] | **31.8 µF … 7.9 nF** (min > max: the knob runs from large to small capacitance) | 15.9 nF | 15.9 nF | "Cutoff frequency of the VCO's high-pass filter" (coupling-cap HPF corner). |
| 22 | Accent Filter Env | ACCENV [mV] | 0…4000 mV | 1951 | 1951 | Filter-envelope gain on accented notes. |
| 23 | Accent Amp Env | ACCAMP [mV] (label) / V (param) | 0…3.5 V | 1.1 | 1.0984 | Amplitude-envelope gain on accented notes. The label says mV but the parameter unit is V. |
| 24 | C13 Accent Capacitor | C13 [F] | **1.64 mF … 458 nF** as written in [B] (min > max) | 915.35 nF | 926.035 nF | "Time constant of filter envelope for accented notes – how quickly the envelope responds" (the accent-sweep capacitor). The 458 nF end is default/2 (accent sweep about 2× faster). Whether the 1.64e-3 end is a typo for 1.64 µF (1.8× slower) or real (about 1800× slower) **cannot be settled from the files**: C21/C22 has a similar 2000:1 span, so a wide range may be deliberate. Measure the knob on the demo. Factory values span **0.74–1.36 µF** (synthesis presets 0.74–0.95; calibration presets 0.92–1.36). |
| 25 | TM5 Octave Width | TM5 [oct./V] | 0.9…1.1 oct/V (4 significant figures) | 1.0 | 1.0 | Exponential-converter scaling: "an octave's span expressed in volts". |
| 26 | Square PW Shift | SQR PW SHIFT | −100…+100 % | 0 | 0.5 | Square-wave pulse width. |
| 27 | BA662 Clicks | BA662 CLICKS (slide switch) | enum Off/On | **On** | **Off** | Exposes or obscures the click of the original BA662 VCA. |
| 28 | BA662 Noise | BA662 NOISE | 0…100 % (integer) | 0 | 0 | VCA static noise. "Audible only when VCO feeds the circuit with sound" (gated by the VCA, not a constant hiss). |

All [primary]. The trimmers use **physical units (mV, F, oct/V)**, not percentages. The circuit-modelling researchers should map them to real 303 component locations (TM3, TM5, C13, C21/C22). This doc only records the control surface.

Verification notes on the capacitor trims:

- **C21/C22 is internally a frequency control.** The GUI layout names this knob "DC Remover Frequency" [GUI]. The bounds are clean ratios of the default: 31.8 µF = 2000 × 15.9 nF and 7.9 nF ≈ 15.9 nF / 2. For a fixed resistance the corner is proportional to 1/C, so the knob moves the VCO high-pass corner from **about 1/2000 of the default up to about 2× the default**. 15.9 nF = 0.1 µF / 2π, which suggests D16 derived C from a target frequency (C = 1/(2πRf)). The resistance, and so the default corner in Hz, is not recoverable from the files. Factory synthesis presets use the full 7.9 nF–31.8 µF span.
- **Phoscyon's "ideal" values are not the 303's nominal parts.** The RE-303 clone BOM, which keeps Roland's reference designators, lists C13 as 1 µF/50 V electrolytic (Phoscyon's "Ideal TB303" uses 926 nF, about −7 %), C21 as 0.01 µF film and C22 as 1 µF electrolytic (`…/scratchpad/refs/web/re303bom.txt` lines 81 and 114). Phoscyon's single "C21/22" value of 15.9 nF matches neither part, so treat it as a lumped effective high-pass parameter, not a part value. The Devil Fish manual confirms that C13 is the accent-sweep capacitor whose leftover charge produces the stacking accent sweep (`…/refs/web/dfman.txt` lines 158–184).

**The 9 factory calibration presets** [F] (TM3 mV / C21-22 / AccEnv mV / AccAmp V / C13 / TM5 / PW % / Clicks / Noise %):

| Preset | TM3 | C21/22 | AccEnv | AccAmp | C13 | TM5 | PW | Clicks | Noise |
|---|---|---|---|---|---|---|---|---|---|
| Ideal TB303 | 0 | 15.9 nF | 1951 | 1.098 | 926 nF | 1.000 | 0.5 | Off | 0 |
| Plain TB303 | 0 | 15.9 nF | 1951 | 1.098 | 926 nF | 1.000 | 0.5 | On | 0 |
| Plain TB303 with Noisy VCA | 0 | 15.9 nF | 1951 | 1.098 | 926 nF | 1.000 | 0.5 | On | 30.8 |
| Lower Cutoff | −76 | 15.9 nF | 1951 | 1.098 | 938 nF | 1.000 | 0.5 | Off | 0 |
| More Expressive | 0 | 12.1 nF | 1999 | 1.494 | 926 nF | 0.998 | 6.1 | Off | 0 |
| Noisy Old One | −60 | 11.3 nF | 2063 | 1.10 | 915 nF | 1.000 | 0 | On | 20.8 |
| Ramon's TB | −40 | 20.5 nF | 2127 | 1.882 | 973 nF | 1.000 | 0.5 | On | 7.6 |
| Long Accent Tail | −72 | 10.8 nF | 3567 | 2.45 | 1362 nF | 1.000 | 0 | On | 13.6 |
| Sub-bass Monster | −372 | 36.5 nF | 3344 | 1.408 | 915 nF | 1.002 | 1.6 | Off | 0 |

Across the calibration presets *and* the 171 synthesis presets, factory usage is: TM3 **−500…+64 mV** (synthesis presets reach the −500 mV end), C13 **0.74–1.36 µF**, and C21/C22 over its **full 7.9 nF–31.8 µF** range. (Corrected in verification: this line said TM3 −400…+64 mV and C13 0.9–1.4 µF, which counted the calibration presets only.)

---

## 3. Effects: distortion, dynamics, inserts, master

### 3.1 Signal flow [M p.10–11, p.16]

```
VOICE → DISTORTION block [Dynamics(pre) → Distortion → Dynamics(post)] → INSERT 1 → INSERT 2 → INSERT 3 → INSERT 4 → INSERT 5 → Output Volume → out (stereo)
                                                                           (5 reorderable slots: EQ, Delay, Reverb, Chorus, Limiter)
BYPASS FX bypasses the distortion block AND all inserts.
```

- The distortion block is **always first and cannot be moved**. Reviewers list this as the main con [MT].
- Everything runs in series. There are no sends or parallel routing [M p.10].
- The output is stereo (two-column meter; stereo chorus, delay and reverb).

### 3.2 Distortion

Front-panel controls [M p.10][B]:

| # | Name | GUI | Range | Default | Behaviour |
|---|---|---|---|---|---|
| 29 | Distortion Active | ACTIVE LED | state | Off | On/off. |
| 30 | Distortion Model | MODEL display | string (preset name) | — | Opens a browser of distortion "models" (§3.2.1). |
| 31 | Distortion Dynamics | DYNAMICS | 0–100 % | 0 | "Equalizes the signal level with one-knob compression. Compression increases as the value increases." |
| 32 | Distortion Dynamics Ord | ORDER switch (PRE bottom / POST top) | enum Pre/Post | Pre | Compressor before or after the clipper. |
| 33 | Distortion Drive | DRIVE | 0–100 % | 10 | Distortion strength. Internally it interpolates between the model's *Preamp Min* and *Preamp Max* (inference from the parameter names; confidence medium). |
| 34 | Distortion Color | COLOR | −100…+100 % | 0 | "Bigger value = brighter. Middle neutral." Internally it sweeps the model's *Color* filter (freq/reso/range). |

Dynamics, Drive and Color display as integers (`format='integer'`), unlike the synthesis knobs [B]. On a fresh instance the default scene has distortion **on**, model *Diode Clean*, **Dynamics 100**, Drive 0, Order Pre, and the Reverb insert is also on [B embedded default state]. A new Phoscyon instance is therefore already compressed and distorted.

#### 3.2.1 What a "model" actually is

These are the **hidden internal parameters** (indices 185–202, absent from the GUI [GUI]). All are marked automatable in [B] **except #188 Distortion Clipper Mode**, which has no `auto` flag (corrected in verification):

| Param | Range | Meaning (from names; behaviour inferred) |
|---|---|---|
| Distortion Clipper Mode | enum **Diode / RAT / Overdrive** | Clipper topology |
| Distortion Preamp Min / Max | 0–100 % each | Drive-knob mapping endpoints |
| Distortion Overdrv Knee | 0–100 % | Clipping-knee hardness |
| Distortion Bias | −100…+100 % | Asymmetry (even harmonics) |
| Distortion Pre HP Freq | 0–1000 Hz | High-pass before the clipper (keeps the low end clean) |
| Distortion Pre EQ Active / Gain / Freq / Width | on/off, ±24 dB, 20–1000 Hz, 0.5–4 oct | Peaking EQ before the clipper |
| Distortion Tilt Active / Amount / Freq | on/off, 0–24 dB, 100 Hz–10 kHz | Tilt EQ |
| Distortion Color Freq / Reso / Range | 20 Hz–20 kHz, 0–100 %, 0–100 % | The resonant filter swept by the Color knob |
| Distortion Post HP Freq | 0–1000 Hz | High-pass after the clipper |
| Distortion Volume | −inf (−320)…+24 dB | Level-matching trim |

**The 19 factory models** [F] (Clipper | PreampMin–Max % | Knee | Bias | PreHP Hz | PreEQ (on? gain dB @ Hz, width oct) | Tilt (on? dB @ Hz) | Color f Hz / reso / range | PostHP Hz | Vol dB):

| Model | Clip | Preamp | Knee | Bias | PreHP | PreEQ | Tilt | Color | PostHP | Vol |
|---|---|---|---|---|---|---|---|---|---|---|
| Diode Clean | Diode | 0–74.4 | 100 | 0 | 0 | off | off | 2000/44.8/100 | 0 | −1.42 |
| Diode + Bias | Diode | 0–66.4 | 100 | −12.7 | 69 | on +9.0@100,1.29 | on 5.1@1k | 2163/17.2/82.4 | 252 | +2.46 |
| Diode + Low-Boost | Diode | 4.4–66.4 | 100 | 0 | 51 | on +3.3@166,1.88 | off | 3738/20/100 | 16 | +0.55 |
| Diode + Low-Cut Medium | Diode | 0–74.4 | 100 | 0 | 95 | off | off | 2000/44.8/100 | 60 | −1.42 |
| Diode + Low-Cut Extreme | Diode | 2.8–82.4 | 100 | 0 | 974 | off | off | 785/4.4/100 | 0 | −0.24 |
| Diode + Mid-Punch | Diode | 0–64.8 | 100 | 0 | 0 | on +6.3@366,3.04 | on 5.4@421 | 3268/34.8/100 | 24 | −0.44 |
| Diode + Tilt | Diode | 0–60.4 | 100 | 0 | 0 | off | on 12.3@139 | 4345/20/100 | 0 | −0.84 |
| Diode Whistle | Diode | 5.6–63.6 | 48.4 | 0 | 0 | on +6.7@172,1.53 | on 24@679 | 2517/20/100 | 304 | +1.93 |
| Overdrive Clean | Overdrive | 3.6–98.8 | 100 | 0 | 0 | off | off | 2000/20/100 | 0 | −2.00 |
| Overdrive Colorized | Overdrive | 24.4–100 | 100 | 0 | 218 | off | off | 244/67.2/53.2 | 44 | −4.76 |
| Overdrive Dirty | Overdrive | 10–100 | 50.8 | 0 | 0.3 | off | on 3.1@575 | 1608/25.2/100 | 0 | +1.93 |
| Overdrive + Bias | Overdrive | 9.2–82.4 | 100 | −10.0 | 92 | off | off | 3121/26.4/100 | 108 | −0.80 |
| Overdrive + Low-Cut Medium | Overdrive | 15.2–91.6 | 100 | 0 | 95 | off | off | 2000/44.8/100 | 60 | −1.42 |
| Overdrive + Low-Cut Extreme | Overdrive | 15.2–91.6 | 100 | 0 | 1000 | off | off | 4077/44.8/100 | 60 | 0 |
| Overdrive + Tilt Subtle | Overdrive | 5.6–94.8 | 58 | 0 | 83 | off | on 7.0@1k | 3905/20/100 | 0 | −0.56 |
| Overdrive + Tilt Strong | Overdrive | 5.6–94.8 | 100 | 0 | 83 | on +3.5@127,1.24 | on 12.0@1k | 3905/20/100 | 0 | −0.56 |
| Fuzzy Overdrive | Overdrive | 8.8–84.8 | 50.8 | 0 | 17 | on +4.6@257,3.36 | on 1.4@1k | 2108/20/100 | 0 | +1.09 |
| RAT Clean | RAT | 0–75.2 | 100 | 0 | 0 | off | off | 2000/20/100 | 0 | −1.71 |
| RAT + Low-Cut | RAT | 4.8–71.6 | 50 | 0 | 103 | off (−5.0@554) | on 7.5@286 | 2579/20/100 | 24 | −0.86 |

[primary F]. Factory usage: Diode Clean is used in 39/171 synthesis presets, then Diode + Tilt (15), Diode + Low-Boost (13) and so on.

Model presets are **factory-only**. The browser defaults to the Factory source and the GUI has no distortion Save button. Users cannot reach the internal parameters except through host automation, and even that cannot change the clipper type, because #188 is not automatable. **This is the improvement opportunity:** expose a "custom model" editor and add more clipper types (e.g. DS-1, Tube Screamer, fuzz). The current models are only *named after* hardware ("RAT"). D16 says they are "based on famous hardware units that are notorious with the most recognizable 303 artists" [P], but only RAT is named.

**Quality/oversampling:** v2.1.0 fixed "Distortion oversampling at quality settings higher than Draft" [C]. So the global Quality setting (§10) controls distortion oversampling, with no oversampling at Draft (inference).

### 3.3 Insert effects (5 slots, reorderable)

The panel has a tab row with each effect name and an LED on-toggle (**click the LED to enable, drag the tab to reorder**) [M p.11]. Below the tabs, each effect has a **SAVE** button, a **PRESET** display (opens that effect's own browser; shows "- Custom -" when edited) and a **MORE / LESS** button that toggles between a 4-knob basic view and the full view [M p.12].

**Basic-view (LESS) controls per insert**, recovered from the GUI layout [GUI] (filled in during verification; the manual documents only Reverb's):

| Insert | Basic view | Full view adds |
|---|---|---|
| EQ | 4 **Gain** knobs (Low, Mid-Low, Mid-High, High) | Freq and Width for each band |
| Delay | Feedback, FB Filter Cutoff, **FB Filter Type** (enum display), FX, Stereo Spread | Sync toggle, L/R n/d displays + scale, Delay Time, FB Filter Reso |
| Reverb | Pre Delay, Early/Late Mix, Late Feedback, FX | Early/Late Size, Early/Late Diffusion, Tail Modulation, Early/Late Brightness (numeric displays) |
| Chorus | Modulation Rate, Modulation Depth, Stereo Spatial, FX | Output Volume, HP Cutoff, Fatness ("Mode"), Offset |
| Limiter | Attack, Release, Threshold, Gain + **Soft Clip** toggle | Knee, Ratio |

So the basic views are not all "4 knobs": Delay has 4 knobs plus the filter-type selector, and Limiter has 4 knobs plus the soft-clip toggle. Delay Sync is reachable only in the full view.

Chain order is stored as parameter **#98 "FX Chain Order"** (int): five digits listing effect indices in processing order, with EQ=0, Delay=1, Reverb=2, Chorus=3, Limiter=4 and a leading 0 dropped. Verified: the default scene's value `20134` matches the GUI tab order Reverb, EQ, Delay, Chorus, Limiter [B][F][M p.4]. Factory scenes use many orders (e.g. 04312, 40312, 04231). Parameter #108 "Selected Insert" (enum EQ;Delay;Reverb;Chorus;Limiter) records which tab is open.

**EQ — 4-band parametric** (bands LOW, MID-LOW, MID-HIGH, HIGH; rows FREQ / WIDTH / GAIN) [M p.12][B]:

| Param | Range | Default (param / Init preset) |
|---|---|---|
| EQ Active | on/off | Off |
| {Low, Mid-Low, Mid-High, High} Gain | −24…+24 dB | 0 |
| … Freq | 20 Hz–16 kHz (shown as Hz or kHz) | 100 / 1000→550 / 5000→3500 / 10000 |
| … Width | 0.5–4.0 oct | 2.0 / 1.50 / 2.63 / 3.53 (Init) |

All four bands appear to be peaking (they all have Width; there are no shelf switches). This is unverified: the GUI's internal knob names are "EQ Low / Mid / High / Treble" [GUI], which says nothing about shape. 10 factory presets.

**Delay — tempo-sync with in-loop multimode filter** [M p.14][B]:

| Param | Range | Default |
|---|---|---|
| Delay Active | on/off | Off |
| Delay Sync | on/off | Off (param) / On (Init) |
| Delay L/R Rate Nominator | int 1–32 | 3 |
| Delay L/R Rate Denominator | int 1–32 | 16 |
| Delay L/R Scale | Full / Dotted / Triplet | Full |
| Delay Time (unsynced) | 0.01–1.00 s | 0.2 |
| Delay Feedback | 0–100 % | 50 (param) / 13.2 (Init) |
| Delay FB Filter Type | Off / LP / BP / HP | Off |
| Delay FB Filter Cutoff | 10 Hz–16 kHz | 8000 / 16000 (Init) |
| Delay FB Filter Reso | 0–100 % | 0 |
| Delay Stereo Spread | −100…+100 % | 0 |
| Delay FX | 0–100 % dry/wet crossfade | 50 / 30.4 (Init) |

Independent L/R synced times are set by dragging the numerator and denominator digits separately; a click on the right part of the display picks the modifier. Synced times up to **32 bars** are possible. If the buffer would overrun at slow tempos, the plugin shows a "Delay Buffer Size Exceeded" warning [M p.14][B strings]. 9 factory presets (e.g. *Polyrhythmia 2*: L 3/16, R 5/16, HP filter).

**Reverb — algorithmic, independent Early/Late** [M p.15][B]:

| Param | Range | Default (param / Init) |
|---|---|---|
| Reverb Active | on/off | Off |
| Reverb Pre Delay | 0–1000 ms | 200 / 0 |
| Reverb Early Size | 1.1–20.5 m | 1.47 / 10.4 |
| Reverb Late Size | 5.1–51.1 m | 12.12 / 27.9 |
| Reverb Early / Late Diffusion | 0–100 % | 20 / 48.4, 50.8 |
| Reverb Early / Late Brightness | int 0–100 (numeric displays) | 50 / 100 |
| Reverb Late Feedback | 0–100 % | 50 / 18.8 |
| Reverb Early/Late Mix | 0–100 % crossfade | 50 / 51.2 |
| Reverb Tail Modulation | 0–100 % | 20 / 0 |
| Reverb FX | 0–100 % dry/wet | 50 / 47.2 |

Basic view: PREDELAY, EARLY/LATE, FEEDBACK, FX [M p.4]. 8 factory presets (Room, Hall, Cathedral, Cellar, Gymnasium, Warehouse, Underground Club, Init).

**Chorus — double-line** [M p.13][B]:

| Param | GUI | Range | Default (param / Init) |
|---|---|---|---|
| Chorus Offset | OFFSET | 0–100 % | 0 |
| Chorus Fatness | MODE | 0–100 % | 100 / 0 — "volume of the second delay line; 0 = single line" |
| Chorus HP Cutoff | HI PASS | 0–100 % | 0 — low cut on the wet signal |
| Chorus Output Volume | VOLUME | −inf…+12 dB | 0 |
| Chorus Modulation Depth | DEPTH | 0–100 | 20 / 37.2 |
| Chorus Modulation Rate | RATE | 0–100 % | 20 / 36 |
| Chorus Stereo Spatial | SPATIAL | 0–100 % | 50 — L/R LFO phase offset |
| Chorus FX | FX | 0–100 % dry/wet | 50 |

7 factory presets.

**Limiter — with soft clip** [M p.13][B]:

| Param | Range [B] | Manual says | Default (param / Init) |
|---|---|---|---|
| Limiter Knee | 0–100 % (soft/hard) | — | 100 |
| Limiter Ratio | **2…40 :1** | "1:1 to 1:40" | 40 |
| Limiter Attack | 0.1–100 ms | same | 10 / 0.1 |
| Limiter Release | 25–500 ms | same | 100 / 145.6 |
| Limiter Threshold | −48…0 dB | same | −12 / 0 |
| Limiter Gain | 0…+24 dB make-up | same | 0 |
| Limiter Soft Clip | on/off — clips "leaks" above 0 dBFS; usable as saturation via Gain | — | Off |

The ratio conflicts between sources. I believe the binary (2:1 minimum), because it is the shipping code. A 1:1 ratio would be a no-op anyway. 5 factory presets.

### 3.4 Master section [M p.16][B]

| # | Param | GUI | Range | Default |
|---|---|---|---|---|
| 100 | Bypass FX | BYPASS FX LED | state | off |
| 99 | Output Volume | VOLUME (large knob, scale −inf…0 dB…+12 dB) | −320 (=−inf)…+12 dB | 0 dB |
| — | (meter) | OUTPUT: stereo LED ladder, marks +6 +3 0 −3 −6 −9 −12 −15 −18 −21 −24 dB | — | — |
| 97 | Pitch Bend Range | Synthesis cog → "Synthesis settings" dialog | int 0–12 semitones, symmetric | 2 |

---

## 4. Sequencer (SEQ play mode)

### 4.1 Data model

**Pattern** = name + **rate** + **modifier** + **shuffle** + **step length** + **1–64 steps** [M p.25][B #118–123][F]:

| Field | Range | Notes |
|---|---|---|
| Length | int **1–64** steps | Footprint icon. ◀ ▶ steppers. |
| Rate ("Scale" in GUI) | enum **1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1/1** | Default 1/16. Factory uses 1/16 (277), 1/8, 1/32. |
| Modifier | **Full / Dotted / Triplet** | Default Full. |
| Shuffle | int **0–100 %** | Default 0. Factory 0–85. The exact swing curve is not published. |
| Step Len. | int **0–100 %** | Gate length as a fraction of the step. GUI default 50 ("- Empty -" patterns). Factory median ≈ 50–55, range 9–86. |

Shuffle and step length are **per pattern**, not global. This matters for pattern switching.

**Step** = {note name 0–11 or none (rest), octave −1/0/+1/+2/+3, Slide, Accent, Vibrato} [M p.24–25]:

- **Rest:** an empty note name silences the step "no matter if an Octave has been set — like a disabled Gate in a TB-303". The octave and flags are still stored on rests. (Verified: of 327 rest steps in the 279 factory patterns, 23 carry a non-zero octave and 10 carry Slide/Accent/Vibrato flags [F].)
- **There is no Tie.** The real 303 has note/tie/rest in Time mode. Phoscyon has only gate on/off per step, so a held note is written as a Slide into the same pitch (which is why MIDI out needs CC#64, §7). Pitch and time are also **not** separate lists as on the 303; each step holds its own pitch.
- An **unset octave means octave 0** (base). The piano-roll octave lane shows rows +3, +2, +1, −1; 0 is "no row lit" [M p.24 images].
- **Pitch range** is 12 notes × 5 octaves (−1…+3) = 60 semitones. That is wider than the 303's 3 octaves (13 keys plus up/down). Factory patterns use only octave codes −1…+2; +3 never appears [F].
- **Slide:** "the current step will slide to the next **set** Note". The slide connects to the next note-bearing step, skipping rests (§4.6).
- **Accent:** the current step is accented.
- **Vibrato:** the current step plays with vibrato (§1.3).

**On-disk step encoding** (3 hex digits per step, `|`-separated) [F, decoded and verified against the manual screenshot of "Best Served with Beat"] [primary]:

```
value = (octaveCode << 8) | (noteIndex << 4) | flags
octaveCode: 2 = −1, 3 = 0, 4 = +1, 5 = +2, 6 = +3
noteIndex : 0 = C … 11 = B           (ARP patterns: pitch index 0..9 ⇒ displayed 1..10)
flags     : bit0 (1) = Accent, bit1 (2) = Slide, bit2 (4) = Vibrato, bit3 (8) = Gate (note set)
empty step = "300"  (octave 0, C, gate off)
```

Example: `29a` = octave −1, A, gate + slide. `359` = octave 0, F, gate + accent.

Re-verified independently: all 16 steps of the default pattern (`300|29a|398|2a8|2aa|359|35a|319|300|348|358|2a8|2aa|3a8|3aa|299`) match the grid in the manual's p.4 screenshot (rendered at 400 dpi), including octave, note row, Slide and Accent. **The Vibrato bit (4) is inferred by elimination.** No screenshot with a vibrato step was checked; 12 factory steps use it.

### 4.2 Pattern storage, banks, selection

- **48 pattern slots** = **4 banks (A–D) × 12 patterns (1–12)**. This storage is shared by both trigger modes [M p.19–20]. ARP mode has a **separate** 48-slot storage [M p.34][F: every scene holds 48 sequencer + 48 arp patterns].
- Default selected pattern is **A-1** [M p.20].
- The **Pattern Selector** (bottom right) has LIVE and HOST NOTE tabs, a **START/STOP** button (red, with LED), a cog (*Host Link Active*), a 4×3 grid of pattern buttons (1–12) and a row of bank buttons (A–D) [M p.20].
- **LED semantics:** red = selected/playing; **blue = queued** (waiting to start); with Follow off, blue = being edited and red = playing [M p.20, p.22].

### 4.3 Trigger modes and switching

**Live mode** (default):

- Click Bank then Pattern. Within the same bank, a Pattern click alone is enough.
- **Switching is quantized to the end of the current pattern** ("the next pattern will start playing when the current pattern comes to the end of its final step"). The queued pattern shows a blue LED [M p.20].
- Patterns loop.
- Playback starts on **host Play** or on the plugin's **Start/Stop** button [M p.18]. [MR] lists "DAW sequencer has to be running to access sequencer playback" as a con. Treat the Start/Stop button as dependent on host transport (unverified; see Open questions).

**Host Note mode:**

- **48 MIDI notes, one per pattern**, "starting from the C note of the second lowest octave", spanning 4 octaves [M p.21]. Supporting evidence for base = MIDI 12: Phoscyon 1's Host mode used "96 available MIDI notes (whole MIDI keyboard range without the first and last octave)" [P1M], meaning the first octave (0–11) was skipped there too. Still unconfirmed for v2.
- The GUI relabels the selector: 12 keys **C…B** select the pattern within a bank, and 4 buttons **0, 1, 2, 3** select the octave, which maps to the bank [M p.22 image].
- **Note On starts that pattern; Note Off stops it.** Host transport Start has no effect. Patterns play only while a note is held [M p.21].
- Not available in ARP mode [M p.34].

**Pattern Follow** (button, Host Note tab; param #111, default **on**): the playing pattern is also the edited one [M p.22].

**MIDI CC trigger:** "Use a pattern trigger mode that suits you: MIDI notes, MIDI CC or GUI" [P][O]. This works through the **MIDI-learnable "Pattern Control" parameters** [B #124–169]:

- Sequencer Start/Stop.
- Select Pattern 1–12 and Select Bank A–D.
- Prev/Next Step.
- Transpose Up/Down, Shift Left/Right.
- Clear/Copy/Paste.
- Toggle Note C…B, Toggle Octave −1/Base/+1/+2/+3, Toggle Accent/Slide/Vibrato.

The toggle parameters allow **hardware step-entry from a MIDI controller**.

**Host Link Active** (cog in the Pattern Selector; param #109, default **off**) [M p.21]:

- Off: the cursor syncs to the host once at start, then the internal clock runs. Host loops and jumps are ignored.
- On: the cursor is continuously re-aligned to the host position.
- [T] p.27: with it off, editing a playing pattern in Bitwig/Reaper drifted until restart. Turning it on fixed this. **Our build should default to always-aligned.**

### 4.4 Editing operations

Pattern-properties column ("EDIT" tab) [M p.25–26]:

- **Shift ◀ ▶:** rotate all steps (with all attributes) by one step, with wraparound. The manual implies wraparound but does not say "wrap" explicitly.
- **Transpose ▲ ▼:** ±1 semitone per click. "Notes are wrapped at each end of the keyboard note range… a B3 note changes to C1 when transposed up and a C1 note changes to B3 when transposed down." The note names in that sentence do not match the −1…+3 octave UI. See Open questions.
- **Above the selector: COPY / PASTE / CLEAR** for the **whole pattern** only. A single-pattern clipboard buffer. There is no step-range copy.
- **Pattern name bar:** EXPORT, SAVE, name display (click to open the Pattern Browser), ◀ ▶ to step through browser content line by line [M p.26].
- **Loading patterns** [M p.27]:
  - from the browser with OK, into the selected slot;
  - by dragging one pattern onto a slot button;
  - by dragging **several patterns onto consecutive slots** at once.

**Piano-roll grid** (default view) [M p.24]:

- Columns are steps, with heavy lines every 4 steps.
- Three lanes: **Octave lane** (+3, +2, +1, −1), **Note lane** (12 rows, B at top to C at bottom, keyboard legend on the left), and **Attribute lane** (SLIDE, ACCENT, VIBRATO).
- Click or **drag ("draw")** to set and unset cells. Set cells are dark red (dark teal in ARP mode).
- A horizontal scrollbar under the grid scrolls patterns longer than 16 steps (16 columns are visible).
- A **red highlighted column** shows the play cursor [M p.20].

**Keyboard view** (Cog → GUI → Sequencer View → Keyboard; param #114) is a TB-303 / x0x-style editor [M p.29–31]:

- **STEP** LEDs 1–16 and a **PART** selector 1–4 (4 × 16 = 64).
- STEP POS. ◀ ▶.
- A 12-key mini keyboard with LEDs (7 white, 5 black).
- **OCTAVE** LEDs −1, 0, +1, +2, +3.
- SLIDE / ACCENT / VIBRATO buttons.
- SHUFFLE and STEP LEN knobs (rotary here; numeric steppers in the piano roll). In the layout they are stepped rotary switches (`ImgSwitch` with a knob image), matching the integer 0–100 parameters [GUI].
- SCALE display, PATTERN LENGTH 7-segment display, TRANSPOSE ▼▲, SHIFT ◀▶, AUTO INC., EXPORT, pattern name, and a **Note Preview** toggle (♪ icon).
- **Auto Inc.:** set the octave and attributes first, then press a note key. That writes the step and advances; after step 16 it wraps to 1 [M p.30–31]. Whether it wraps at 16 or at the pattern length is unclear (the manual says 16).
- **Note Preview:** hear notes while entering them. This exists only in Keyboard view [M p.31].
- The **Randomizer is not available** in Keyboard view [M p.29].

### 4.5 Live Override [M p.23][B #177–184][P]

A panel under the properties column with 4 columns (**NOTE, SLIDE, ACC, VIBR**), each with **SET** and **CLR** buttons:

- **Click and hold** to force that attribute on for every step (SET) or off for every step (CLR). This is momentary.
- It is **non-destructive**: pattern data is unchanged.
- The 8 parameters are host-automatable and MIDI-learnable.
- NOTE SET presumably forces the gate on (rests play their stored pitch) and NOTE CLR mutes. That is inference; the manual only says "particular notes".
- Live Override also appears in ARP mode [M p.34 image].

### 4.6 Slide, accent and gate semantics visible at the surface

- **Slide across rests:** a step with Slide glides to the **next note-bearing step**, holding the note through intervening rests [M p.25]. Consequences reported in [T] p.31: a single slid note in an otherwise empty pattern sustains for the whole loop, and an ABL3 trick (slide on step 1 into an accented octave-down step 2 for a pitch-bend effect) cannot be reproduced. This is a **behavioural divergence to decide on deliberately**. The circuit and sequencer researchers should confirm real 303 behaviour; offer a "303-strict" vs "Phoscyon" option.
- Per-step accent depth does not exist. Accent is a flag, and its depth comes from global knobs (Accent, Acc. Decay, Acc. Vol., Sweep).

**Hardware reference points for the slide/gate decision** (added in verification; these come from the 303, not from Phoscyon):

- **Gate length.** On a real 303 a 1/16 step is 6 clock pulses. The gate rises at the start of pulse 0 and falls half-way through pulse 3, so it is **3.5/6 ≈ 58 % of the step** [RW]. Phoscyon's Step Len is free (0–100 %): "- Empty -" patterns use 50, the first-load pattern 40, the factory median 52. A "303-strict" preset should use about 58 %.
- **Slide timing.** The slide flag sits on the *source* note, as in Phoscyon. The source note's gate stays high into the next note (no retrigger). The slide circuit switches on 1/16 after the slid note starts and stays on **until the end of the following note**, so the pitch glides *during* the target note [RW].
- **Rests.** On the 303, rests and ties sit in the Time list and do not consume Pitch-list entries, so "slide to the next note" in pitch terms skips rests. Whittle's write-up does not cover the gate when a rest directly follows a slid note. **This is still unverified on hardware.** Keep the §4.6 switch.
- **ABL3 uses the opposite flag placement.** Its manual says "When Slide is activated the sequencer will tie the step to the *previous* step via a smooth glide" [ABL3M]. This is why the [T] p.31 ABL3 trick does not carry over directly. If we offer ABL2/3 pattern import, check whether slide flags must move one step earlier. (Phoscyon imports ABL patterns [M p.52], but how it converts them is unknown.)

### 4.7 Export / import of patterns [M p.28–29, p.52][B strings]

**EXPORT** button: a dark overlay over the editor with two drag sources:

- **"Drag MIDI file"**: drop into a DAW track as a MIDI clip, or onto disk as a .mid file.
- **"Drag pattern"**: drop the native `.ph2seq` file, onto disk or **into another Phoscyon instance**.

The overlay also holds **Accented note velocity** (default 96) and **Non-accented note velocity** (default 32) for the export. A ✕ closes it.

The Pattern Browser can also drag patterns out, but only in native format.

**Import into the Pattern Browser:**

- Native Phoscyon 2 patterns.
- **Phoscyon 1.x banks** (`.phptrb`, "v1.6"). These become a separate **read-only "Legacy" source**: "You can load it, edit and save in new format."
- **AudioRealism ABL2/ABL3 patterns**, which land in User patterns.

There is **no import of MIDI files** into patterns (not documented).

Not supported anywhere [M; confirmed absent]:

- pattern chaining or song mode;
- play direction (fwd/rev/ping-pong/random). This was requested in [T] p.27; Roland Cloud TB-303 has Forward/Reverse/Fwd&Rev/Invert/Random modes;
- per-step parameter locks or automation lanes;
- step-range copy (requested in [T] p.29);
- MSEG (requested in [T] p.29).

---

## 5. Arpeggiator (ARP play mode) [M p.34–36][F]

It is a **template arpeggiator**, not a classic up/down/random arp. D16 markets it as "not typical, boring styled arpeggiator with few travel modes" [P]. There are **no travel modes at all**.

- Patterns use the **same structure** as sequencer patterns (rate, modifier, shuffle, step len, 1–64 steps, Slide/Accent/Vibrato flags, octave lane −1…+3). The note lane is replaced by **PITCH INDEX 1–10** (10 rows).
- **Indexing:** all held keys are sorted by pitch, lowest = 1, up to **10 notes**.
- At each step the note with that index is played, offset by the step's octave.
- **Missing index:** if the step's index exceeds the number of held notes, **index 1 (the lowest note) is played**. Example: pattern 1-2-3 with C-E held plays C, E, C.
- **Cursor:** starts at step 1 on the first key press. **Keeps running while at least one key is held**, even as the chord changes, with no restart. Resets when all keys are released.
- Pace comes from the pattern Rate (tempo-synced like the sequencer). Whether the host transport must be running is not stated.
- **Edit functions:** the same as the sequencer except **Transpose is relabelled "INVERSION"**. It shifts indices up or down (presumably wrapping 1…10; the manual says only that it "works like the Transposition").
- **Storage:** a separate 48-slot bank (A–D × 1–12) with the same selector and copy/paste/clear. Separate **arp-pattern presets** (`.ph2arp`, 67 factory) that are **not interchangeable** with sequencer patterns.
- **Restrictions:** **no Randomizer, no Export, no Host Note mode** in ARP.
- Accent and slide come from the **pattern flags**. Whether input velocity also triggers accent in ARP is undocumented (probably not).
- Live Override works in ARP.

---

## 6. Randomizer ("Music Roulette") [M p.31–33][B #170–176][F]

The **RANDOM** tab sits next to EDIT in the properties column. It works **only in SEQ mode and only in Piano Roll view**. It is "not a special mode": you can edit the grid while the RANDOM tab is open and switch tabs freely.

**Controls:**

- **PRESET** display (randomizer template browser; right-click → Save / Init / Reload).
- **TRANSPOSE ▲▼** shifts the note template by a semitone (rotation of the 12-pitch-class mask; wrap assumed).
- **DRAW** generates a new random pattern into the current slot.
- **AMOUNT** knobs: **NOTE, SLIDE, ACC, VIBR**. Each is 0–100 %, default 50 [B]. They set the density of each part. A knob is greyed out when its part is excluded from the template.

**Template editing:**

- With RANDOM active, the **left legend column of the grid becomes a toggle surface**.
- Click note names (12 pitch classes) to include or exclude them from the pool.
- Click octave labels (−1, 0, +1, +2, +3) to set the octave pool.
- Click attribute labels (SLIDE, ACCENT, VIBRATO) to choose which attributes are randomized.
- Selected items show red; pool notes are tinted pink on the keyboard legend [M p.31 image].

**Real-time density:** "Every time a new sequence is drawn, you can use the Amount knobs to precisely set the density of the generated sequence individually for each of the randomized parts" [M p.32]. "Real-time density control" [P]. The behaviour implies that each Draw stores per-step random values and the Amount knobs threshold them live, without re-rolling. This matches the v1 Event Threshold mechanism [secondary inference; the v2 algorithm is unpublished].

**Default template name:** the parameter default is "Classic GOA" [B] and the GUI placeholder is "Classic Geja" [GUI]. **Neither ships** in the 50 factory templates [F]; both look like development leftovers. The first-load scene's randomizer template is what a user actually sees.

**Seed:** there is a hidden **Randomizer Seed** parameter (int, default "random") [B]. v2.1.0 fixed "Randomizer initialization" [C], which was the launch-era repeating-sequence bug [seed report]. There is no seed UI, no history, and no undo of a Draw (not documented).

**Randomizer preset format** (`.ph2rnd`): `notes="0|2|5|8" octaves="2|3|4|5" attributes="1|1|0"`. Notes are pitch classes. Octaves use the same 2…6 code as steps. Attributes are Slide|Accent|Vibrato flags. Amount values are not stored in the preset.

**50 factory templates:**

- 303 – Melodic 1–3, 303 – Dissonant 1–3.
- Scales: Major Ionian, Lydian, Mixolydian, Minor Aeolian, Dorian, Phrygian, Locrian, Major/Minor/Dorian Pentatonics, Blues Hexatonic/Heptatonic, Octatonic, Whole Tone.
- Chords (all on C): C, Cm, C6, Cm6, C7, Cmaj7, Cm b7, Cm b6, C b6, C b5 b7, C dim, C aug, C sus2, C sus4.
- Intervals: C 2, b3, 3, 4, b5, 5.
- One Note on 1 or 3 Octaves, Eastern Acid, Slide + Accents, and others.
- `- Init -` = all 12 notes, all 5 octaves, all attributes.

**Every scale is rooted on C. The key is changed only by Transpose clicks** (the gap noted in [GN]).

What is randomized: note name within the pool, octave within the pool, gate (Note amount), Slide, Accent and Vibrato (each by its amount). Parts not selected are left untouched [M p.32]. Pattern length, rate, shuffle and step length are not randomized (not documented).

---

## 7. Play modes and MIDI behaviour

**Play Mode** rotary switch (EXT / SEQ / ARP; param #107, default **Sequencer**) [M p.16][B]:

- In **EXT** mode the whole sequencer panel is **hidden and the plugin window shrinks** (press-kit render: 3964×1586 px in EXT vs 3964×2946 px in SEQ) [O].
- [T] p.29: the Play Mode knob was "nearly impossible" to turn in Logic. Make mode selection click-friendly.

**EXT (external MIDI)** [M p.17–18]:

- **Monophonic.** A new note immediately replaces the old one.
- **Overlapping (legato) notes slide**, using Slide Time.
- Note Off ends the note.
- **Accent when velocity exceeds a threshold.** The *Accent velocity threshold* (Cog → MIDI → Note Velocity Settings → "External mode") is int **1–126, default 64** [B]. "Exceeds" suggests strictly greater.
- **Pitch Bend** range 0–12 semitones, default 2 (Synthesis cog → Synthesis settings).
- **Mod Wheel (CC#1) → vibrato depth.**
- The note-priority policy when releasing back to a still-held note is not documented. Plain "last note" with no retrigger-to-held is assumed.
- **MIDI passes through unchanged** in EXT mode [M p.22].

**MIDI output** (SEQ in Live or Host Note mode [M p.22], and **ARP**: the 2.1.0 changelog fixes "Occasional hanging MIDI Notes generated by Arpeggiator" [C], which implies ARP output reaches MIDI out; confidence medium) [M p.22–23][SY]:

- Notes are sent with velocity **Accented 96 / Non-accented 32**. These are settable under Note Velocity Settings → "Sequencer mode": accented int 2–127, non-accented int 1–126 [B].
- **CC#01** is emitted for vibrato steps.
- **CC#64 (sustain)** is used "when there's slide used and accent for the consecutive note with same pitch". Same-pitch slides cannot be expressed by note overlap, so sustain carries them.
- Slides are otherwise sent as overlapping notes (inference, consistent with EXT-mode input semantics). [T] p.29 confirms Phoscyon-to-Phoscyon chaining keeps accents and slides, and that ABL3 did not interpret the 96/32 velocities the same way.
- **Enable MIDI Out** toggle (Cog → MIDI; default **on**) avoids MIDI loopbacks [M p.23].

**MIDI Learn / CC maps** [M p.55–56]:

- Right-click a control → expand ▾ → **Learn** (pending, then move a controller, then OK/Cancel) or **Clear** (then OK).
- **Cog → MIDI → Save Map / Load Map** stores the CC map as a file (`ccmap`).
- A MIDI CC map is part of the default state (§8.5).
- Bug fixed in 2.1.0: "Recalling of MIDI CC assignation for some parameters" [C].

---

## 8. Preset system

### 8.1 Preset hierarchy [M p.37]

```
Scene (.ph2scn)  = all Synthesis params + Play Mode + 48 SEQ patterns + 48 ARP patterns + randomizer template
 └─ Synthesis (.ph2snt) = all sound params incl. Calibration, Distortion, all 5 inserts, FX order, Output Volume, Pitch Bend Range
     ├─ Calibration (.ph2clb)
     ├─ Distortion model (.ph2dst)  [factory-only in practice]
     ├─ EQ (.ph2eql) · Delay (.ph2dly) · Reverb (.ph2rvb) · Chorus (.ph2chr) · Limiter (.ph2lmt)
Sequencer pattern (.ph2seq) — loadable only in SEQ
Arpeggiator pattern (.ph2arp) — loadable only in ARP
Randomizer template (.ph2rnd) — loadable only in SEQ
```

The top bar shows **SCENE** (left) and **SYNTHESIS** (centre, with its own cog for Synthesis settings). Each has a name display (click opens the browser) and an orange SAVE button. Hovering the right side of the display reveals Prev/Next. Right-click gives **Init / Reload / Save** [M p.5, p.38].

### 8.2 Browser [M p.38–54]

- Three columns:
  - **Sources** (Factory = read-only, User; plus Legacy for imported Phoscyon 1 banks).
  - **Filter**: tag **Groups**, each with an **Any/All** operator. Groups cascade top to bottom (an intersection). There is a global filter on/off switch.
  - **Results**: click loads, double-click loads and closes.
  - **Info pane**: name, "+N more selected", Tags, Author, Description.
- Search box, filtered live.
- **Favorites** (heart): **global** across instances.
- **Pins**: **per instance**, saved in the project.
- **OK** keeps the load. **Cancel reverts** every parameter change the browsing caused (audition-then-revert). ✕ = OK.
- Multi-select: Ctrl/Cmd-click, Shift-click, right-click → Select All.
- Rename (single), Delete (trash icon or menu), batch tag editing with **tri-state tag chips** (set / unset / mixed, with an asterisk on changed tags and OK/Cancel to commit), Author and Description editing (user content only).
- Set/Clear Favorite and Pin/Unpin on a selection.
- **Export** by dragging presets out (one native file each). **Import** by dropping files in (they become User presets).
- **Custom tags and groups:** add a tag to a group, rename or move or remove user tags, add or rename or remove user groups, move groups up and down. Factory groups always come first. Factory tags cannot be removed from factory content. An "Unassigned" bucket collects foreign tags.
- Folding sections and resizable columns.

### 8.3 Factory content (v2.1.0 installer, counted) [F]

| Type | Count |
|---|---|
| Scenes | 250 (216 SEQ, 34 ARP) |
| Synthesis | 171 (103 Square, 68 Saw) |
| Sequencer patterns | 279 (lengths 5–64, mostly 16 and 8) |
| Arpeggiator patterns | 67 |
| Randomizer templates | 50 |
| Distortion models | 19 |
| EQ / Delay / Reverb / Chorus / Limiter | 10 / 9 / 8 / 7 / 5 |
| Calibration | 9 |
| **Total** | **884** ("over 800" [P]) |

**Tag groups** (recovered in verification from the hidden `c/.filter` files in the content packs [GUI/F]):

- Scenes: **Play Mode** {Seq, Arp}, **Waveform** {Square, Saw}, **Distortion** {None, Light, Medium, Strong}.
- Synthesis: **Waveform** {Square, Saw}, **Distortion** {None, Light, Strong, Medium}.
- The other ten content types ship **no** factory tag groups, so their browsers start with an empty filter.

A `.filter` file is `<Filter><group name=".." tags="A|B|C"/>…</Filter>`.

Content includes the "Mr Egg" blind-test patterns (Phoscyon 1 vs real 303). Default-state skin is 1180.

### 8.4 File formats [F][B]

- Every preset is a **plain UTF-8 XML** file: `<Preset version="2.0.0" author=".." tags="A|B|C"><Parameters><param name="<host param name>" value=".."/>…</Parameters></Preset>`.
- Patterns: `<Pattern author rate modifier shuffle steplen steps="..."/>` (encoding in §4.1).
- Scenes add `<ExtraData><Sequencer><Patterns>×48</Patterns></Sequencer><Arpeggiator><Patterns>×48</Patterns></Arpeggiator><RandomizerPreset .../></ExtraData>`.
- Factory packs are zip-in-zip (`content` + `hash` + `__desc__`) files named `*Factory*.d16pkg`. `__desc__` is `<Content product="Phoscyon 2" module="Presets" type="Factory" name="Initial" organization="D16" date="2022-08-17"/>`.
- Skins are zip-in-zip as well: `content` holds the pre-rendered PNGs, 4 fonts, `!.smrescale` (Catmull-Rom rescale settings, desiredWidth 1180) and the layout XML `Phoscyon.d16gui` [GUI].
- Plugin state (host chunk) = `<PluginState>` with ProcessingQuality, ParametersState (the scene), NotPresetableParams, MidiControlMap, Editor (skin, systemScale, view settings) and Browsers (per-browser filter state).

### 8.5 Default state [M p.57–58]

Cog → **Default State → Save current / Restore factory**. The saved default covers:

- sound (default preset);
- views;
- preset filters;
- quality settings;
- MIDI CC map;
- GUI settings.

---

## 9. GUI

### 9.1 Layout (SEQ mode, default skin), top to bottom [M p.4 full screenshot][O]

1. **Header row:**
   - Cog (top-left: MIDI ▸ {Load Map, Save Map, Note Velocity Settings, Enable Midi Out}; GUI ▸ {Theme, Size, System Scale, Sequencer View, Disable Calibration Cover Anim}; Quality ▸ {Real-time, Offline}; Default State ▸ {Restore factory, Save current}; About).
   - SCENE preset block.
   - SYNTHESIS preset block with its cog.
   - Logo plate "Phoscyon2 AcidLine" with the **OPEN** cover handle (calibration).
2. **Synthesis row:** §1, with a separator between the 6 "original" knobs and the 7 "mod" knobs.
3. **FX row:** PLAY MODE rotary | DISTORTION block (Active LED, MODEL display, DYNAMICS, PRE/POST ORDER switch, DRIVE, COLOR) | INSERT block (5 tabs with LEDs, SAVE, PRESET, MORE, 4 knobs or full panel) | MASTER (BYPASS FX, VOLUME, stereo OUTPUT meter).
4. **Sequencer panel** (dark-framed "screen"): EXPORT | SAVE | pattern name ◀▶; grid (octave/note/attribute lanes, scrollbar); right column EDIT|RANDOM tabs, properties, SHIFT, TRANSPOSE (or INVERSION), LIVE OVERRIDE.
5. **Pattern selector** (right of the sequencer): COPY/PASTE/CLEAR, LIVE|HOST NOTE tabs, START/STOP with cog, 12 pattern buttons with LEDs, 4 bank buttons.

### 9.2 Visual feedback

- Step play cursor: a red column in the piano roll; the step LED in Keyboard view.
- Pattern LEDs: red playing, blue queued or edited.
- Start/Stop LED.
- Active-effect LEDs.
- Couple LEDs.
- Output meter.
- Calibration LCD.
- 7-segment pattern-length display (Keyboard view).

### 9.3 Sizing and themes [M p.57][P][B][F]

- **Six fixed skin sizes**: 708, 944, **1180 (default)**, 1534, 1888 and 2360 px wide, which is 60/80/100/130/160/200 % of 1180. These are pre-rendered bitmap skins, about 3–33 MB each.
- Skins are bitmaps, not vector graphics, so there is no free drag-resize.
- **System Scale** factor for HiDPI. A confirm/revert countdown appears after size or scale changes ("Previous size will be restored in…").
- **Light and Dark themes** (Dark added in 2.1.0 [C]).
- Font: D16GroupClassic.ttf.
- [T] p.1: users ask D16 for resizable GUIs.

---

## 10. Audio and plugin

| Aspect | Phoscyon 2 | Source |
|---|---|---|
| Formats | VST2, VST3, AU, AAX (Win 32/64-bit, macOS 64-bit Intel and Apple Silicon; AAX ARM64 since 2.1.0). **No standalone, no CLAP, no LV2, no Linux.** | [P][M p.2][C][MR] |
| Framework | JUCE (JUCE class names and strings in the binary) | [B] |
| Sample rate | ≥ 44.1 kHz | [P] |
| Output | 1 stereo output. MIDI out. | [M][B] |
| Quality | **Processing Quality enum: Draft / Normal / High / Ultra**, set **separately for Real-time and Offline**, both default **Normal**. Higher = more CPU. Controls at least distortion oversampling. | [B][M p.56][C] |
| CPU guidance | Win 3.0 GHz SSE (3.7 multicore recommended), 8 GB RAM. MR quotes 3.2/4.0 GHz. | [M p.2][MR] |
| Automation | 203 parameters (indices 0–202): **118 carry `auto='true'`, 85 do not**. Automatable: all sound and FX knobs/switches, the 8 Live Override params, the hidden distortion internals 185–202 **except #188 Clipper Mode**, and **#119 Pattern Note Rate and #120 Pattern Note Scale**. Not automatable: #1 Tuning Extended, all `* Preset` name params, #97 Pitch Bend Range, #98 FX Chain Order, #101–117 (settings/editor state, incl. Play Mode and Host Link), #118 and #121–123 (pattern name/length/shuffle/step length), #124–169 pattern control, #170–176 randomizer. The non-automatable controls serve MIDI learn. (Corrected in verification: the earlier text said all of 118–176 lacked the flag and all of 185–202 had it.) | [B] |
| Copy protection | 30-day online-activated trial, `.d16key` licence | [B][RK] |
| Price | €119; €89 intro; upgrade from v1 ≈ €29 | [P][GN][MT] |
| Release | v2.0: 29 Aug 2022. v2.1.0: Feb 2023. The 2.1.0 changelog, verbatim headings: Added: AAX Arm64 (macOS), Dark GUI theme. Fixed: distortion oversampling above Draft, Randomizer initialization, recall of MIDI CC assignments for some parameters, **occasional hanging MIDI notes generated by the Arpeggiator**, **an automation issue in Ableton Live**. Changed: options moved under the gear icon, value tooltip replacing the status bar, minor GUI improvements, code maintenance. The official changelist covers 2.1.0 only (re-fetched in verification; it is byte-identical to the local copy). | [RK][GN][C] |

---

## 11. Reception: praise and complaints

**Praised:**

- Sound "indistinguishable from an original 303" [MR 9/10], "gorgeous" [MT 9/10], "in no way inferior to … ABL3" (Buenasideas, quoted on [P]).
- Distortion is "actually very musical" [MR]; "excellent and deep effects" [MT]; "instant acid from one plugin without the need for extensive post-processing" (Buenasideas).
- The sequencer: "intuitive sequencer and arpeggiator" [MT]; "finally… the sequencer can now be programmed easily" (Synthmania, quoted on [P]). Users chose it to avoid DAW piano rolls [T].
- The calibration panel and trims are "essential" for specific sounds [T p.30].
- Preset management is "robust" [MT]. "Midi clock is absolutely tight" (Synthmania). The extended parameters work like a Devil-Fish-style "super modification" (Bonedo, Synthmania).

**Complaints, gaps and requests** (our improvement targets):

1. Distortion position is fixed in the chain [MT].
2. No standalone version [MR].
3. Sequencer needs the DAW transport running [MR].
4. No root note or key in the randomizer; scales are C-rooted masks [GN][T p.31 "manually select the notes you want by clicking the piano keys"].
5. Arpeggiator cannot randomize or export [GN][M].
6. No play-direction modes [T p.27].
7. No copying of bar 1 to bar 2 (no step-range copy) [T p.29].
8. No MSEG or per-step modulation [T p.29].
9. "Only thing missing imho is extended reso range" [T p.30].
10. Slide across rests sustains; the ABL3 slide-into-accent trick is impossible [T p.31].
11. The sequencer drifts when Host Link is off by default [T p.27].
12. Fixed-size bitmap GUI; users want resizable [T p.1].
13. The Play Mode knob is hard to turn in Logic [T p.29].
14. Some presets push high-frequency accent transients through the limiter into "bad digital distortion" [T, via search summary; lore].
15. Support responsiveness [T p.31].
16. No Linux (our raison d'être).

**Competitor features Phoscyon lacks (for context):**

- Roland Cloud TB-303: Forward/Reverse/Fwd&Rev/Invert/Random play modes, pattern chaining, 6-octave range with user-defined scales, 64 patterns per bank (https://www.roland.com/uk/products/rc_tb-303/, via search summary) [secondary]. Checked against the Roland Cloud TB-303 owner's manual (`…/refs/manuals/TB-303-cloud.txt`): **64 patterns + 64 patches per bank, unlimited banks** and **user scale "constituent notes" plus a key-range lower/upper limit** for the randomizer are confirmed. Patterns are **16 steps with 8 variations**, a PLAY MODE knob, RANDOMIZE GENERATE/UNDO and MODIFY/UNDO, and "Position Lock to DAW". The extracted manual text names the PLAY MODE knob but does not list its modes; the mode names above come from [T] p.27 and the product page summary (unverified).
- AudioRealism ABL3, per its manual [ABL3M]: a **resizable GUI**; a **Wave analyzer that transcribes a 303 pattern from audio**; Undo; an "Alter" button that shuffles step order; MIDI step record; Host Sync vs MIDI Trigger sync modes; 1–64 steps; vibrato set by Up+Down on a step; no pitch bend. It **imports technoBox2 (iOS) and ReBirth 338 patterns** and exports MIDI. The seed report's "TB-03/TD-3/T-8 import" is **not in this manual** (it may come from a later version; unverified).

---

## Open questions

1. **Physical mappings of the 0–100 % knobs** (Cutoff → Hz, Decay and Acc. Decay → ms, Env Attack → ms, Slide Time → ms, Vib Speed → ms per alternation, Vib Depth → semitones, Sweep → accent RC behaviour) are unpublished. *Handle by:* deriving them from the circuit research (other docs). Where Phoscyon goes beyond the circuit (attack, accent decay, vibrato), choose ranges that cover the factory-usage spread in §1.1, and if possible tune by ear or measurement against the free 30-day demo (Windows VST3 under yabridge/Wine or a Mac). Keep all curves as data tables so they can be retuned without code changes.
2. **Shuffle curve.** What does 100 % mean (off-beat 16th delayed by 1/3 step? 1/2 step?), and does it apply to even or odd steps? *Handle by:* measuring the demo's MIDI out at shuffle 25/50/75/100. Meanwhile implement 0–100 % mapping to a 0…(2/3 of a step) delay of every second step (MPC-style 50 %…75 %), as a data constant.
3. **Step Len. 0 %.** Is there a minimum gate? *Handle by:* clamping to a ≥ 1 ms minimum gate. Step Len should not affect slid steps (slid steps hold through).
4. **Transpose wrap range.** The manual's "B3 → C1" does not match the −1…+3 octave UI. *Handle by:* wrapping over the full 60-note range (octave −1 C … octave +3 B), and confirming on the demo.
5. **Host Note base note.** "C of the second lowest octave" most likely means MIDI note 12 (patterns at notes 12–59), with the GUI labelling octaves 0–3. Phoscyon 1 likewise skipped the first MIDI octave in Host mode [P1M]. *Handle by:* default base = 12, with a user setting for the base note. Verify on the demo.
6. **Start/Stop without host transport.** The manual says the GUI button starts playback; [MR] says the DAW must run. *Handle by:* supporting both, with an internal clock when the host is stopped (also needed for our standalone).
7. **Accent threshold comparison** (> or ≥ 64) is ambiguous. *Handle by:* treating velocity > threshold as accent and documenting it.
8. **Live Override NOTE SET/CLR exact semantics** (force gate on rests? mute?). *Handle by:* NOTE SET = all steps gated with their stored pitch; NOTE CLR = all steps muted. All override buttons are momentary but also latchable via automation.
9. **Randomizer density algorithm** (threshold over stored per-step random values vs probability re-roll). *Handle by:* implementing the threshold design (stable as knobs move) and adding a seed, re-roll and history.
10. **Whether ARP uses input velocity for accent** and whether ARP needs the host transport. *Handle by:* ARP accent = pattern flag OR (optional) input velocity > threshold, as a user option.
11. ~~**FX "basic view" knob selection**~~ **Resolved in verification** from the GUI layout file; see the basic-view table in §3.3.
12. **EQ band shapes** (all bells vs shelves on Low/High). *Handle by:* bells by default, with an optional shelf toggle on the outer bands.
13. **Distortion internals:** the clipper transfer curves (Diode, RAT, Overdrive), the Color filter topology and the Dynamics compressor's time constants are unknown. Only parameter names and preset values are recovered. *Handle by:* the effects researcher designing these from first principles (RAT op-amp plus hard diode clipping, Tube-Screamer-style soft feedback clipping, diode-pair clipper), using our §3.2.1 parameter set as the voicing interface.
14. **Exact slide behaviour vs the 303 when the next step is a rest.** Defer to the sequencer/circuit research doc. Ship a switch. Known hardware timing (gate 3.5/6 of a step; slide active until the end of the target note) is now in §4.6 [RW]. The slid-note-then-rest gate behaviour is still unmeasured.
15. **C13 upper bound** (1.64 mF as written, or 1.64 µF) and the **taper** of both capacitor knobs. *Handle by:* measuring the knob display at several positions in the demo.
16. **ABL2/3 import slide mapping.** ABL3 places slide on the destination step, Phoscyon and the 303 on the source step (§4.6). *Handle by:* checking with real ABL pattern files before shipping any importer.

## Implications for the build

1. **Mirror the parameter surface 1:1 in naming and range wherever it does not conflict with authenticity:** the 13 synthesis knobs + Wave + Ext tuning (coarse ±12 st, fine ±100 ct) + 2 couple switches, with Phoscyon's coupling semantics (Resonance→Sweep, Accent→Acc.Vol). Users migrating from Phoscyon will look for exactly these controls.
2. **Implement the calibration panel with physical units** (mV, F, oct/V, %) and the same nine trims: TM3 −500…+500 mV, C21/C22 7.9 nF–31.8 µF, AccEnv 0–4000 mV, AccAmp 0–3.5 V, C13 at least 0.46–1.64 µF (the upper bound is uncertain; see Open question 15), TM5 0.9–1.1 oct/V, PW ±100 %, VCA clicks on/off, VCA noise 0–100 % (VCA-gated). Ship the calibration as its own preset type and save it inside synthesis presets.
3. **Build distortion as one parametric engine with "voicing presets", not N hard-coded algorithms.** Expose the voicing (clipper, pre-HP, pre-EQ, tilt, bias, knee, drive min/max, color filter, post-HP, trim) in an "advanced/custom model" editor, which Phoscyon lacks. Ship at least the 3 clipper families plus more (DS-1-style, Tube Screamer-style, fuzz) and ≥ 19 voicings. Front panel: Active, Model, Dynamics (one-knob comp, Pre/Post), Drive, Color (±100, centre neutral).
4. **Make the whole FX chain, distortion included, reorderable.** This fixes the #1 review complaint. Keep the 5 inserts with Phoscyon-equivalent parameter sets and ranges (§3.3). Each insert gets its own preset browser and a basic/full view toggle. Store the chain order as a stable, versioned list in state, not a packed int.
5. **Sequencer data model:** 1–64 steps; per-pattern rate (1/64…1/1 × Full/Dotted/Triplet), shuffle 0–100 %, step length 0–100 %. Each step holds note 0–11 or rest, octave −1…+3, and flags Slide/Accent/Vibrato/Gate. Preserve octave and flags on rests. 4 banks × 12 = 48 slots for SEQ and a separate 48 for ARP, saved in the scene.
6. **Pattern switching:** queue to the end of the current pattern (blue "queued" LED); Live (GUI), Host-Note (48 notes, configurable base, Note On starts and Note Off stops) and MIDI-CC (learnable select-pattern/bank/start-stop parameters). Add Pattern Follow. **Default to continuous host alignment** (Phoscyon's Host Link on), and run an internal clock when the host is stopped.
7. **Two editor views:** Piano Roll (draw-drag, octave/note/attribute lanes, scrolling past 16 steps, play cursor) and x0x Keyboard view (step/part LEDs, mini keyboard, octave LEDs, Auto Inc, Note Preview). Edit ops: shift ◀▶ (wrap), transpose ±1 (wrap over 60 notes), whole-pattern copy/paste/clear, multi-slot drag-load. **Add** step-range copy/duplicate (e.g. "copy bar 1 to 2"), undo/redo, and play direction (fwd/rev/ping-pong/random).
8. **Live Override:** 4 parts × SET/CLR, momentary, non-destructive, automatable and MIDI-learnable, available in SEQ and ARP.
9. **Template arpeggiator:** pitch-index patterns 1–10, up to 10 held notes sorted low→high, a missing index falls back to index 1, the cursor persists while any key is held and resets when all keys are up, an Inversion edit, and a separate preset type. **Go beyond Phoscyon:** allow randomizing and MIDI-exporting arp output.
10. **Randomizer:** a 12-pitch-class mask + octave mask + attribute toggles, Draw, and 4 live Amount (density) knobs that threshold stored per-step values without re-rolling. Template presets cover scales, chords and intervals, with transpose. **Add:** an explicit root/key selector, a scale dropdown, a visible seed + re-roll + history (prev/next), per-step locks, and generation in Keyboard view too.
11. **MIDI:**
    - EXT mode: monophonic, overlap = slide, velocity > threshold (default 64) = accent, pitch bend 0–12 (default 2), mod wheel → vibrato depth.
    - MIDI out from the sequencer: accent velocity 96 / normal 32 (configurable), CC#1 for vibrato, CC#64 for same-pitch slides, overlap for slides, an enable toggle.
    - MIDI learn on every parameter, with save/load of CC maps.
12. **Vibrato as an alternating-note trick** (Speed = note length, Depth = spread, smoothed by Slide Time), applied per step via the Vibrato flag. A real-LFO mode could be an optional extra.
13. **Export:** drag a MIDI clip out (with export velocity fields), drag a native pattern out, and accept drag-in between instances. **Add MIDI-file import** into patterns (Phoscyon lacks it). Optionally import Phoscyon 2 `.ph2seq`/`.ph2scn`, ABL3 and TD-3 patterns (pending the user's legal call). An ABL importer must handle ABL3's destination-step slide flag (Open question 16).
14. **Presets:** a hierarchy of Scene ⊃ Synthesis ⊃ {Calibration, Distortion voicing, each insert} + Seq pattern / Arp pattern / Randomizer template. Use human-readable files (XML or JSON) with a version field.
    - Browser: Factory/User sources, tag groups with Any/All cascade, search, global Favorites, per-instance Pins, audition with Cancel-revert, batch tag editing, author/description, drag export and import.
    - **Write our own factory content.** Do not copy D16's.
15. **Global options:** separate Real-time and Offline quality tiers (Draft/Normal/High/Ultra → oversampling factors), Default State save/restore, and "double-click resets to the last-loaded preset value".
16. **GUI:** vector or freely scalable rendering (not fixed bitmap sizes), Light/Dark themes, HiDPI. Collapse the sequencer panel in EXT mode. Make the mode selector easy to operate (not a hard-to-turn rotary). Add value tooltips, fine-drag, and right-click value entry (Phoscyon's is display-only).
17. **Platform:** Linux-native VST3 + CLAP + LV2 + **standalone** (closes two Phoscyon gaps at once). Stereo out plus MIDI out.

## Parity checklist

Tags: **MUST** = core to the Phoscyon 2 experience; **SHOULD** = expected by Phoscyon users; **NICE** = minor or polish. "(+)" marks an improvement over Phoscyon.

**Synthesis**
1. MUST — Wave switch Saw/Square.
2. MUST — Tuning continuous ±12 semitones, centre A=440.
3. SHOULD — Ext. tuning mode: coarse ±12 st stepper + fine ±100 cents knob.
4. MUST — Cutoff knob 0–100 %.
5. MUST — Resonance knob 0–100 %.
6. MUST — Envmod knob 0–100 %.
7. MUST — Decay knob (non-accented notes only).
8. MUST — Accent knob (filter accent level).
9. MUST — Slide Time knob.
10. MUST — Sweep Amt. knob (smooth↔spiky accent envelope crossfade).
11. MUST — Resonance/Sweep couple switch (default on; Resonance drives the sweep crossfade).
12. MUST — Env. Attack knob (filter-envelope attack).
13. MUST — Acc. Decay knob (separate accent decay).
14. MUST — Acc. Vol. knob (accent amplitude boost).
15. MUST — EnvAcc/AccVolume couple switch (default on; Accent drives both).
16. MUST — Vib. Speed knob (alternating-note duration).
17. MUST — Vib. Depth knob (alternating-note spread; smoothed by Slide Time).
18. SHOULD — Coupling LEDs drawn over both knobs of each pair; inactive knob visibly disabled.

**Calibration**
19. MUST — Hidden calibration panel (opened from a cover/handle), 9 trims:
20. MUST — TM3 cutoff offset, −500…+500 mV.
21. MUST — C21/C22 VCO high-pass capacitor, 7.9 nF–31.8 µF.
22. MUST — ACCENV accent filter-envelope gain, 0–4000 mV.
23. MUST — ACCAMP accent amplitude gain, 0–3.5 V.
24. MUST — C13 accent time-constant capacitor, at least ~0.46–1.64 µF (Phoscyon's raw upper bound reads 1.64 mF; to be confirmed).
25. MUST — TM5 octave width, 0.9–1.1 oct/V.
26. MUST — Square PW shift, ±100 %.
27. MUST — BA662 (VCA) clicks on/off.
28. MUST — BA662 (VCA) noise 0–100 %, gated by the VCA.
29. SHOULD — Calibration presets (own browser), ≥ 9 factory units (ideal, plain, noisy, low-cutoff, long accent tail, sub-bass…).
30. NICE — Cover-open animation with a disable option.

**Distortion and FX**
31. MUST — Distortion block: Active, Model selector, Drive 0–100, Color ±100 (centre neutral).
32. MUST — One-knob Dynamics compressor with Pre/Post order switch.
33. MUST — ≥ 3 clipper families (diode, RAT-style, overdrive) with voicing parameters (pre-HP, pre-EQ, tilt, bias, knee, drive range, color filter, post-HP, trim).
34. MUST — ≥ 19 factory distortion voicings.
35. SHOULD (+) — User-editable / savable custom distortion voicings.
36. SHOULD (+) — Additional clipper families (DS-1, Tube Screamer, fuzz).
37. MUST — 5 insert effects: EQ, Delay, Reverb, Chorus, Limiter, each with an on/off LED.
38. MUST — Insert chain reorderable by dragging tabs.
39. SHOULD (+) — Distortion also reorderable within the chain.
40. MUST — EQ: 4 bands (Low, Mid-Low, Mid-High, High), Freq 20 Hz–16 kHz, Width 0.5–4 oct, Gain ±24 dB.
41. MUST — Delay: sync on/off; independent L/R n/d (1–32 / 1–32) with Full/Dotted/Triplet; free time 0.01–1 s; Feedback; in-loop filter Off/LP/BP/HP with cutoff 10 Hz–16 kHz and reso; stereo spread ±100; dry/wet.
42. SHOULD — Delay buffer-overrun protection and warning.
43. MUST — Reverb: pre-delay 0–1000 ms; Early size 1.1–20.5 m and Late size 5.1–51.1 m; per-section diffusion and brightness; late feedback; early/late mix; tail modulation; dry/wet.
44. MUST — Chorus: offset, second-line level ("fatness"), wet HPF, output volume, depth, rate, stereo spatial phase, dry/wet.
45. MUST — Limiter: knee, ratio 2–40:1, attack 0.1–100 ms, release 25–500 ms, threshold 0…−48 dB, make-up 0–24 dB, soft-clip toggle.
46. SHOULD — Per-insert preset browser + save; basic / full view toggle, with Phoscyon's basic sets: EQ = 4 gains; Delay = feedback, FB cutoff, FB type, FX, spread; Reverb = predelay, early/late, feedback, FX; Chorus = rate, depth, spatial, FX; Limiter = attack, release, threshold, gain, soft clip.
47. MUST — Master: Output Volume −inf…+12 dB, Bypass FX (all FX incl. distortion), stereo level meter.
48. SHOULD — Pitch-bend range setting 0–12 st (default 2).

**Sequencer**
49. MUST — Patterns 1–64 steps.
50. MUST — Per-step note (C–B or rest), octave −1…+3, Slide, Accent, Vibrato; octave and flags preserved on rests.
51. MUST — Per-pattern rate 1/64…1/1 × Full/Dotted/Triplet.
52. MUST — Per-pattern shuffle 0–100 %.
53. MUST — Per-pattern step (gate) length 0–100 %.
54. MUST — 4 banks × 12 = 48 SEQ pattern slots per scene.
55. MUST — Pattern selector with bank/pattern buttons, playing (red) and queued (blue) LEDs.
56. MUST — Pattern switching quantized to the end of the current pattern.
57. MUST — Live trigger mode (GUI) and Start/Stop button.
58. MUST — Host Note trigger mode: 48 MIDI notes → patterns; Note On plays, Note Off stops.
59. SHOULD — Configurable Host Note base note.
60. MUST — MIDI-CC pattern control: learnable select pattern 1–12, bank A–D, start/stop.
61. SHOULD — MIDI-learnable edit controls: prev/next step, transpose, shift, clear/copy/paste, toggle note C–B, toggle octave, toggle accent/slide/vibrato.
62. MUST — Host sync with continuous position alignment (Host Link).
63. SHOULD (+) — Continuous alignment as the default; internal clock when the host is stopped.
64. SHOULD — Pattern Follow toggle (edit the playing pattern vs a separate edit target).
65. MUST — Piano-roll editor: octave lane, 12-row note lane, attribute lane, click/drag drawing, play cursor, scroll past 16 steps.
66. MUST — Keyboard (x0x) editor view: step 1–16 LEDs, part 1–4, step pos ◀▶, mini keyboard, octave LEDs, Slide/Accent/Vibrato buttons.
67. SHOULD — Keyboard view: Auto Increment entry.
68. SHOULD — Keyboard view: Note Preview.
69. MUST — Shift ◀▶ (rotate with wrap, all attributes).
70. MUST — Transpose ±1 st with wrap.
71. MUST — Whole-pattern Copy / Paste / Clear.
72. SHOULD (+) — Step-range copy/duplicate, undo/redo.
73. MUST — Live Override: Note/Slide/Accent/Vibrato × Set/Clr, momentary, non-destructive, automatable.
74. MUST — Slide glides to the next note-bearing step without retriggering the envelope.
75. SHOULD (+) — Option for 303-strict slide-into-rest behaviour vs Phoscyon behaviour.
76. SHOULD (+) — Play direction modes (fwd, rev, ping-pong, random).
77. NICE (+) — Pattern chaining / song mode.
78. NICE (+) — Per-step parameter locks / MSEG lane.
78a. SHOULD (+) — Explicit Tie (303 Time-mode note/tie/rest) in addition to same-pitch slide. Phoscyon has no tie.
78b. SHOULD (+) — "303-strict" timing preset: gate ≈ 58 % of step (3.5/6 clocks), slide active through the end of the target note [RW].

**Export and import**
79. MUST — Export overlay: drag pattern as a MIDI clip (DAW or disk) with configurable accent/non-accent velocities (default 96/32).
80. MUST — Drag pattern in native format to disk or to another instance.
81. MUST — Drag-and-drop load of one or many patterns onto pattern slots.
82. SHOULD (+) — Import MIDI files into patterns.
83. NICE — Import ABL3 / TD-3 / Phoscyon patterns (pending legal review).

**Arpeggiator**
84. MUST — ARP play mode with template patterns: pitch index 1–10 + octave −1…+3 + Slide/Accent/Vibrato, same rate/length/shuffle/step-len.
85. MUST — Arp indexing: up to 10 held notes sorted low→high; missing index → index 1; cursor persists while any key is held, resets when all are released.
86. MUST — Separate 48-slot arp pattern storage + arp-pattern presets.
87. SHOULD — Inversion (index shift) edit op.
88. SHOULD (+) — Randomize and MIDI-export for arp output.

**Randomizer**
89. MUST — Randomizer tab in the sequencer; works alongside editing.
90. MUST — Template: 12-pitch-class mask, octave mask (−1…+3), attribute toggles (Slide/Accent/Vibrato), edited by clicking grid legends.
91. MUST — Draw button.
92. MUST — Four live density Amount knobs (Note/Gate, Slide, Accent, Vibrato) that adjust without re-rolling; disabled when the part is excluded.
93. MUST — Template transpose ▲▼.
94. MUST — Randomizer template presets (≥ 50: scales, chords, intervals, 303-melodic/dissonant).
95. SHOULD (+) — Root/key selector + scale dropdown.
96. SHOULD (+) — Visible seed, re-roll, generation history, per-step lock.
97. NICE (+) — Randomizer available in Keyboard view.

**Play modes and MIDI**
98. MUST — Play-mode selector EXT / SEQ / ARP.
99. SHOULD — Sequencer panel hidden in EXT mode.
100. MUST — EXT mode: monophonic; legato overlap = slide; velocity > threshold = accent (threshold setting, default 64).
101. MUST — Pitch bend in EXT mode.
102. MUST — Mod wheel → vibrato depth in EXT mode.
103. MUST — MIDI out of sequencer (and arpeggiator) notes: accent and non-accent velocities configurable (96/32).
104. SHOULD — MIDI out: CC#1 for vibrato, CC#64 for same-pitch slides, overlapping notes for slides.
105. SHOULD — Enable/disable MIDI out.
106. SHOULD — MIDI thru in EXT mode.
107. MUST — MIDI Learn / Clear on every control via right-click.
108. SHOULD — Save/Load CC map files.

**Presets**
109. MUST — Scene presets (sound + play mode + 48 SEQ + 48 ARP patterns + randomizer template).
110. MUST — Synthesis presets (all sound incl. calibration, distortion, FX, chain order).
111. MUST — Sequencer-pattern and arp-pattern presets.
112. MUST — Preset browser: Factory/User sources, tag groups with Any/All cascade, name search, OK / Cancel-revert audition. Factory tag groups at minimum: Play Mode, Waveform, Distortion intensity.
113. SHOULD — Prev/Next preset arrows; Init / Reload / Save context menu.
114. SHOULD — Favorites (global) and Pins (per instance).
115. SHOULD — Batch tag editing (tri-state), author/description, rename/delete, user tags and groups.
116. SHOULD — Drag-export and drag-import of preset files.
117. MUST — ≥ 800 original factory presets and patterns across types (Phoscyon ships 884).
118. SHOULD — Default State save / restore factory.
119. SHOULD — Double-click resets to the last-loaded preset value.

**GUI**
120. MUST — Right-click parameter menu (value display + MIDI learn).
121. MUST — Ctrl/Cmd fine-drag.
122. SHOULD (+) — Typed value entry in the right-click menu.
123. MUST — Multiple GUI sizes, 60–200 % (Phoscyon: 6 fixed sizes).
124. SHOULD (+) — Free or vector scaling.
125. MUST — HiDPI system-scale handling.
126. SHOULD — Light and Dark themes.
127. SHOULD — Value tooltips.

**Audio and platform**
128. MUST — Quality tiers Draft/Normal/High/Ultra, separate for real-time and offline (distortion oversampling at minimum).
129. MUST — Stereo output; sample rates ≥ 44.1 kHz.
130. MUST — Host automation of all sound, FX and live-override parameters.
131. MUST — Linux-native VST3 + CLAP + LV2.
132. SHOULD (+) — Standalone app with internal clock.

## Verification log

Adversarial check, 2026-10-02. Every primary file was re-opened independently, not taken from the author's notes:

- The parameter XML was re-extracted with `strings` from `msi/VST3Binary` (SHA-1 `74e0f1a1…`).
- The manual text was re-extracted with `pdftotext` from the PDF shipped inside the MSI, which is byte-identical to the d16.pl download.
- The factory statistics were recomputed by parsing all 884 content files.
- The changelog was re-fetched live.

| # | Claim | Verdict | Source checked |
|---|---|---|---|
| 1 | 203 params; names, ranges, defaults of synth params 0–18 (e.g. Cutoff default 0, Resonance 100, couples On) | confirmed | [B] re-extracted XML |
| 2 | Calibration ranges: TM3 ±500 mV, C21/22 31.8 µF…7.9 nF (default 15.9 nF), AccEnv 0–4000 mV (1951), AccAmp 0–3.5 V (1.1), C13 1.64e-3…458e-9 F (915.35 nF), TM5 0.9–1.1, PW ±100, Clicks default On | confirmed | [B] |
| 3 | C13 1.64e-3 bound "looks like a typo" | unverified (downgraded) | [B]: C21/22 has a deliberate-looking 2000:1 span, so a wide C13 span may also be intentional |
| 4 | Working range "TM3 −400…+64 mV, C13 0.9–1.4 µF" | corrected → TM3 −500…+64, C13 0.74–1.36 µF, C21/22 full range | [F] all 171 synthesis + 9 calibration presets |
| 5 | Ideal TB303 = 15.9 nF / 1951 mV / 1.0984 V / 926.035 nF; all 9 calibration preset rows | confirmed | [F] `.ph2clb` files |
| 6 | Phoscyon's calibration values equal 303 part values | corrected (not claimed outright; added note): 303 C13 = 1 µF; C21 = 0.01 µF, C22 = 1 µF per RE-303 BOM; C21/22 is a lumped "DC Remover Frequency" | RE-303 BOM; [GUI] knob name; DF manual |
| 7 | 19 distortion models, every table cell (clipper, preamp, knee, bias, HP, EQ, tilt, color, vol) | confirmed | [F] `.ph2dst` files |
| 8 | Hidden distortion internals 185–202 all automatable | corrected → #188 Clipper Mode has no `auto` flag | [B] |
| 9 | Params 118–176 carry no `auto` flag | corrected → #119 Note Rate and #120 Note Scale are automatable; 118 automatable / 85 not | [B] |
| 10 | Limiter ratio 2–40 in binary vs "1:1 to 1:40" in the manual | confirmed (conflict is real) | [B], [M p.13] |
| 11 | Step encoding (octave << 8) OR (note << 4) OR flags; Accent=1, Slide=2, Gate=8 | confirmed (all 16 default-pattern steps vs the 400 dpi p.4 screenshot) | [B] default state, [M p.4] |
| 12 | Vibrato flag = 4 | unverified (inferred by elimination) | [F] 12 steps use it; no screenshot checked |
| 13 | Octave and flags preserved on rests | confirmed (23 rests with octave, 10 with flags) | [F] 279 patterns |
| 14 | Factory counts 884 = 250/171/279/67/50/19/10/9/8/7/5/9 | confirmed | [F] |
| 15 | Factory stats (cutoff median 1.2, reso 89.6, envmod 30.4, decay 52.4, accent 88; distortion on 167/171; couples 136 and 94; Diode Clean 39) | confirmed; added "85/171 at Cutoff 0" | [F] |
| 16 | Quality Draft/Normal/High/Ultra, separate Real-time and Offline, both Normal | confirmed | [B] default `<ProcessingQuality>`; [M p.56] |
| 17 | Accent threshold 1–126 default 64; export/out velocities 96/32 with ranges 2–127 and 1–126 | confirmed | [B] #103–105 |
| 18 | Host Link default off; off = sync once, on = continuous; Bitwig/Reaper desync fixed by enabling it | confirmed | [M p.21]; [T] p.27 re-read (kmonkey, RPH) |
| 19 | Slide "to the next set Note"; single slid step sustains across empty pattern; ABL3 trick fails | confirmed | [M p.25]; [T] p.31 re-read (gentleclockdivider) |
| 20 | 303 slide-into-rest behaviour | unverified; added hardware gate/slide timing | [RW] |
| 21 | Host Note: 48 notes from "C of the second lowest octave"; Note On/Off; transport ignored | confirmed (text); base = MIDI 12 still unverified, supported by P1 | [M p.21]; [P1M] |
| 22 | Transpose wrap text "B3 → C1" | confirmed as quoted (contradiction is in the manual itself) | [M p.26] |
| 23 | Start/Stop: manual says GUI button starts playback; MR lists "DAW sequencer has to be running" | confirmed (conflict is real, unresolved) | [M p.18]; MusicRadar re-fetched |
| 24 | ARP: 10-note index, missing index → 1, cursor persists while keys held; no Randomizer/Export/Host Note; 48 slots | confirmed | [M p.34–36] |
| 25 | ARP sends MIDI out | added (medium) | [C] "hanging MIDI Notes generated by Arpeggiator" |
| 26 | Basic-view knobs for EQ/Delay/Chorus/Limiter unknown | gap filled (§3.3 table) | [GUI] `Phoscyon.d16gui` |
| 27 | Tag group names not recoverable | gap filled: Play Mode / Waveform / Distortion | [F] hidden `c/.filter` files |
| 28 | Six skin sizes 708…2360 px, 3–33 MB; EXT render 3964×1586 vs SEQ 3964×2946 | confirmed | MSI skin packages; press-kit PNGs |
| 29 | Legacy `.phptrb` import is read-only, "load it, edit and save in new format"; ABL2/3 import | confirmed | [B] strings; [M p.52] |
| 30 | Distortion fixed first (MT con); randomizer has no root note (GN); arp has no randomize/export (GN); price €119 / €89 intro / €29 upgrade | confirmed | MusicTech, Gearnews re-fetched |
| 31 | Product page: macOS 10.13–26, ≥44.1 kHz, "over 800", trigger by "MIDI notes, MIDI CC or GUI", "19 … models based on famous hardware units" | confirmed | [P] re-fetched; [O] |
| 32 | ABL3 imports TB-03/TD-3/T-8/ReBirth | corrected → manual lists technoBox2 and ReBirth 338 only; TB-03/TD-3/T-8 unverified | [ABL3M] |
| 33 | Roland Cloud TB-303 play-direction modes | unverified (manual text names a PLAY MODE knob without listing modes); 64 patterns/bank and user scales confirmed | Roland Cloud TB-303 manual |
| 34 | Randomizer default preset | added: "Classic GOA" ([B]) / "Classic Geja" ([GUI]) are not shipped | [B], [GUI], [F] |
| 35 | Win 32-bit support | unverified (manual says "32bit or 64bit" hosts; only the x64 MSI was examined) | [M p.2] |

Not re-checked: Sound On Sound, Bedroom Producers Blog and Gearspace reviews (still unfetched); Syntorial; Rekkerd; KVR thread pages 1, 29 and 30; the press-kit claim of 25 Oct 2023; the "Mr Egg" pattern provenance. Derived-value arithmetic was recomputed where it exists: the 60–200 % skin ratios, C21/22 bound ratios (2000×, 0.497×), C13 bound ratios (0.5×, 1792×) and the 303 gate fraction (3.5/6 = 58.3 %).
