# 02. TB-303 VCO and VCF at circuit level

Scope: the oscillator (saw core, square waveshaper) and the filter (diode ladder, feedback path, coupling capacitors, cutoff control law), with the numbers needed to model them and test the model offline. Envelopes, accent network, slide and the VCA proper are in doc 03; this doc only covers them where they set a level, a scale factor or a coupling corner that the VCO/VCF model needs.

## 0. Evidence tags and sources

Tags on every quantitative claim:

- **[primary]**: Roland service notes (schematic, adjustment page, parts list), the x0xb0x Eagle schematic (a part-for-part clone), Stinchcombe's published derivation and SPICE plots, Open303 source code, Devil Fish manual statements of measured behaviour.
- **[secondary]**: reputable analysis, including measurement-fitted constants inside Open303 and Phoscyon. **[secondary: derived here]** marks numbers computed for this doc from primary component values (hand analysis or a small transient/AC simulation). They are as good as the schematic reading and the idealised device models, and no better. None is bench-verified.
- **[lore]**: forum consensus or single-unit observations without a published method.

Sources actually read:

| Key | Source | Licence / status |
|---|---|---|
| SN | Roland TB-303 Service Notes, first edition, Feb 19 1982, 8 pages (150 dpi scans by Robin Whittle). Page 5 analogue schematic, pages 6-7 adjustment, page 8 env-mod note and parts list. Local copy: `scratchpad/refs/web/sm/` | Roland copyright, reference only |
| X0X | x0xb0x `hardware/mainboard.sch` (Eagle binary; part values extracted with `strings`), https://github.com/a1k0n/x0xb0x | Open hardware by Limor Fried; repo carries no licence file, treat as reference only |
| X0XW | x0xb0x AdaWiki VCO and VCF pages via Wayback (ladyada.net itself is behind a Cloudflare challenge): https://web.archive.org/web/2023/https://www.ladyada.net/wiki/x0x/voltagecontrolledoscillator and `.../voltagecontrolledfilter` | CC BY-SA 4.0 |
| TS1 | Tim Stinchcombe, "Diode Ladder Filters (including the pretension to 18dB)", https://www.timstinchcombe.co.uk/index.php?pge=diode | personal site |
| TS2 | Tim Stinchcombe, "A Comprehensive TB-303 Diode Ladder Filter Model", https://www.timstinchcombe.co.uk/index.php?pge=diode2 (Laplace block model image `tb303_laplace_model.gif`, component-level vs Laplace plot `tb303_lap_v_comp_580.gif`) | personal site |
| TS3 | Tim Stinchcombe, "Analysis of the Moog Transistor Ladder and Derivative Filters", section 3, https://www.timstinchcombe.co.uk/synth/Moog_ladder_tf.pdf | personal site |
| DFM | Robin Whittle, Devil Fish manual (`Devil-Fish-Manual.pdf`) and https://www.firstpr.com.au/rwi/dfish/303-unique.html | Real World Interfaces |
| O303 | Open303, `Source/DSPCode/rosic_Open303.cpp`, `rosic_TeeBeeFilter.h`, `rosic_MipMappedWaveTable.cpp`, https://github.com/RobinSchmidt/Open303 | MIT |
| KVR | KVR "Open303" thread t=262829. Posts 1-180 and four later pages read in full from Wayback captures; pages start=1080/1095/1110 and threads t=261379, p=4878962 read only through a summarising fetch (lossy, quotes may be trimmed) | forum |
| P2 | Phoscyon 2 calibration trims as recorded in `docs/research/01-phoscyon2-feature-inventory.md` section 2; the manual's own wording (p.8-9, `scratchpad/refs/manuals/p2.txt`) was checked in verification | sibling doc; D16 manual |
| RE | RE-303 clone BOM, which keeps Roland's reference designators (`scratchpad/refs/web/re303bom.txt`). Added in verification | clone BOM, reference only |

Not reached: antto's measurement table image (box.net link dead); the original Whittle statement behind the "45 % / 71 % duty" figure (only Olney's citation of it, https://olney.ai/ct-modular-book/tb-303.html); any published swept-sine magnitude response of a stock unit; datasheet h_FE for 2SC1583 and 2SC2291. No bench data of my own.

## 1. Signal path and absolute levels

The model should carry real volts through the VCO and filter, because every nonlinearity in this path is a bipolar junction with a natural scale of 2·V_T = 51.7 mV (27 °C).

| Node | Level | Source |
|---|---|---|
| Rails | +12 V (DC-DC), +5.333 V reference (IC16 pin 7), ground | SN p.5 [primary] |
| Saw at Q28 source (also WAVEFORM switch terminal) | falls from "12 V" to "5.5 V", then snaps back up. 6.5 V p-p as drawn; 5.5 to 6.0 V p-p by analysis (section 2.4) | SN p.5 annotation [primary]; analysis [secondary: derived here] |
| Square at Q8 collector | low 5 V (flat), high 8 V as drawn, period 9 ms at key A. 5.33 V and 8.5 to 8.9 V by simulation | SN p.5 [primary]; section 2.5 [secondary: derived here] |
| Filter input divider | R62 220 k into R70 2.2 k: gain 1/101 (0.0099). Stinchcombe's block 1 carries the same 0.01 | SN, TS2 [primary] |
| At Q12 base | saw ±30 to ±32 mV (0.6 × 2V_T), square ±15 to ±17 mV (0.3 × 2V_T) | [secondary: derived here] |
| Ladder core gain | −1 at DC, differential, across the top capacitor C26 | TS1 [primary] |
| Output amp Q21 + Q19 | ×106 (Stinchcombe SPICE); ×110 from g_m·R_C/2 with 0.526 mA tail and 22 k loads (×112 at exactly 0.526 mA and V_T = 25.85 mV) | TS2 [primary]; [secondary: derived here] |
| Forward gain, VCO to TP6, resonance 0 | 0.01 × 106 = 1.06 (+0.5 dB) before the low-pass and coupling roll-offs | TS2 [primary] |
| TP6 (Q19 emitter) | 5.2 V DC; at max resonance, cutoff centre, saw at low C: ringing bursts peaking at 5.8 V, i.e. 0.6 V peak | SN p.5 [primary] |
| Feedback divider | wiper × 0.98 (Q18 follower) × 2.2 k/(10 k + 2.2 k) = 1/5.55 | TS2 [primary] |
| VCA (BA662 pin 3) input | TP6 × 2.2 k/222.2 k through 0.01 µF, plus resonance wiper × 2.2 k/102.2 k through 0.01 µF | SN p.5, X0X [primary] |

A 0.6 V ring at TP6 returns 0.6 × 0.98 / 5.55 = 106 mV to Q12's feedback base, twice 2·V_T. The input pair is the limiter that sets ring amplitude (section 3.9).

## 2. VCO

### 2.1 Pitch CV, note range, tuning reference

- CV scale is 1 V/octave. DAC is 6 bits into a 200 k R-2R (R74 to R90, selected pairs) referenced to 5.333 V, so one code = 1/12 V and the span is 0 to 5.25 V. [primary: SN p.5; X0XW]
- Schematic annotations: "CV KEY C = 1.000 V, C̄ = 4.000 V"; "TUNE: 110 Hz (set TUNING centre), CV OUT 2.75 V (A key)". Spec page: "CV Out: +1 V to +5 V (1 volt/oct)". [primary: SN p.1, p.5]
- From those two anchors: f = 110 Hz × 2^(CV − 2.75). DAC code n gives CV = n/12 V and MIDI note n + 12.

| CV | Note | f (tune centre) | Where it occurs |
|---|---|---|---|
| 1.000 V | C1, MIDI 24 | 32.70 Hz | lowest pattern note (transpose down) |
| 2.000 V | C2, MIDI 36 | 65.41 Hz | left C key, no transpose; the SN filter-adjust note ("15 ms, key C") |
| 2.750 V | A2, MIDI 45 | 110.00 Hz | SN tuning reference ("9 ms, key A") |
| 3.000 V | C3, MIDI 48 | 130.81 Hz | right C key |
| 4.000 V | C4, MIDI 60 | 261.63 Hz | top of pattern range (transpose up) |
| 5.000 V | C5, MIDI 72 | 523.25 Hz | top of track/pitch-mode transposition (DFM: "1 to 4 volt range can be transposed to as high as 2 to 5 volts") |
| 5.250 V | D#5, MIDI 75 | 622.25 Hz | DAC code 63, reachable only by MIDI retrofits (DFM: 0.75 to 5.25 V) |

  [primary for the volts and the 110 Hz anchor: SN, DFM; the Hz column is arithmetic]
- "A = 440" in software clones is the same tuning: A2 = 110 Hz.
- TUNING knob: "approx. ±700 cents (perfect fifth)". [primary: SN p.1] VR2 is 50 k(B) (linear), wiper through R118 100 k into the converter base node. [primary: SN p.5, X0X] The knob does not affect CV Out or the filter. [primary: DFM]
- Extremes with the knob: 32.70 Hz × 2^(−7/12) = 21.8 Hz to 523.25 Hz × 2^(7/12) = 784 Hz.

### 2.2 Exponential converter and its two trimmers

Circuit [primary: SN p.5, X0X]: CV buffer IC11b (AN6562) with slide capacitor C35 0.22 µF switched by IC12; R107 2.2 k to TP4; then TM5 (4.7 k, "WIDTH") + R106 27 k into the base of Q26b, with R100 560 Ω returning that base to the 5.333 V rail. R100 is marked "POSISTOR" (a PTC part). Q26 is a 2SC1583 matched dual. Q26a is the reference half: base on 5.333 V, collector fed by R103 220 k from +12 V plus R102 100 k from the wiper of TM4 (47 k B, "TUNE", strung between 5.333 V and +12 V), servoed by IC11a (C34 1 nF) through R104 2.2 k into the common emitters. The tuning pot arrives at Q26b's base through R118 100 k. Q26b's collector sinks the timing current from C33.

- Law: I = I_ref · 2^(CV − 5.333 V), with I_ref = 30.3 µA + (V_TM4 − 5.333 V)/100 k. The converter is referenced to the same 5.333 V as the DAC, so the pitch is ratiometric to that rail. I_ref of about 36 µA puts 659 Hz (E5) at CV = 5.333 V, the figure aciddose quotes on KVR. [secondary: derived here; KVR figure is lore and could not be re-located in verification]
- Scale: the divider is 560 / (27 k + TM5 + 2.2 k + 560) = 16.3 to 18.8 mV per volt of (CV − 5.333 V) across TM5's travel, bracketing V_T·ln2 = 17.9 mV per octave at 300 K. TM5 near 1.5 k gives 1.000 V/oct. [secondary: derived here]
- Temperature: the PTC R100 raises the divider ratio with temperature to follow V_T (the usual +3300 ppm/K tempco trick), and the matched pair cancels I_S drift. The scale is first-order compensated; the offset is not perfectly so. No published drift figure for a stock unit exists. [primary for the parts; drift magnitude is lore]
- Calibration targets [primary: SN p.6-7]:
  - TM6 (CV): low C to high C = 1.000 V ± 3 mV; with TRANSPOSE UP another 1.000 V ± 3 mV.
  - TM5 (WIDTH): tapping low C and high C alternately, adjust for 2:1 waveforms.
  - TM4 (TUNE): A key = 110 Hz with TUNING at centre.
  - Confirm 4:1 ± 0.5 % between low C and high C + transpose up (65.41 to 261.63 Hz). ±0.5 % is ±8.6 cents over two octaves; that is the factory tracking tolerance.

### 2.3 Saw core

Circuit [primary: SN p.5, X0X, X0XW (Martin Czech's analysis)]: C33 0.01 µF from the timing node to ground. Q26b sinks a constant current, so the node ramps **down** linearly. Q25 (2SC536F NPN) has its emitter on the node and its base held near 5.333 V + V_BE by Q24 (a saturated NPN acting as a diode, fed through R101 10 k from 5.333 V). When the node falls one V_BE below that base, Q25 conducts, Q27 (2SA733 PNP) turns on, and the Q25/Q27 pair latches like an SCR and recharges C33 from +12 V through D25. Q28 (2SK30A-O, I_DSS 0.6 to 1.4 mA) is a source follower with R105 10 k to ground; its source is the saw output and feeds both the WAVEFORM switch and the square shaper.

- Timing: f = I / (C33 · ΔV). With ΔV ≈ 5.5 V on the capacitor: 1.8 µA at 32.7 Hz, 6.05 µA at 110 Hz, 28.8 µA at 523 Hz. [secondary: derived here]
- Thresholds: reset fires when the node reaches about 5.3 V; the latch pulls it to "12 V minus D25, about 11.4 V", and Q25 lets go once the node passes about 10.8 V (Czech). [secondary: X0XW]
- The oscillator free-runs. It is never reset by gate or note. [lore: antto, KVR; consistent with the schematic, which has no sync path]

### 2.4 Saw waveform

- Polarity at the VCO output: slow linear **fall**, fast **rise** at reset. [primary: SN p.5 waveform sketch]
- Levels: sketch says 12 V top, 5.5 V bottom. The JFET cannot follow to the rail: with the drain on +12 V and 1.1 mA demanded at the top of the ramp (at or above I_DSS for the O grade), the follower compresses the first part of each cycle. Expect the source to swing about 5.5 V to 11.0-11.5 V, with a slightly rounded top right after the reset. [secondary: derived here]
- Linearity: antto, from recordings, "waveform is 99.9 % plain saw ... don't be fooled by what you see on the audio samples, the waveform there has been filtered by the filter (which has the highpass filters)". [lore] Circuit expectation: the Early effect of Q26b makes the ramp a few percent faster at the top (convex), and the follower gain is 0.9 to 0.95. Both are small. [secondary: derived here]
- Reset: Czech predicts a flat portion after the reset from the saturated SCR pair's turn-off latency, of fixed duration independent of pitch, and asked for confirmation that never came. [lore] Charging 55 nC through saturated small-signal transistors takes a few microseconds; storage time adds 5 to 30 µs. A total dead time of 20 µs flattens 523 Hz by 1.0 % (18 cents) relative to an ideal core and 262 Hz by 0.5 %, which is the size of the factory 4:1 tolerance. Treat reset dead time as a tunable 10 to 40 µs with 20 µs default; TM5 calibration absorbs most of it over the keyboard range. [secondary: derived here]
- DC offset: about +8.5 V mean. It is removed by C17 (section 2.8) but it matters for the square shaper, which works on absolute volts.

### 2.5 Square waveshaper (Q8)

This stage is why the 303 square sounds the way it does, and its behaviour falls straight out of six parts.

Circuit [primary: SN p.5; X0X and the RE-303 BOM give identical values (re-read from the SN scan, X0X part strings and RE BOM in verification); aciddose's SPICE netlist on KVR start=1095 uses the same six values (unverified: that page was only read through a summarising fetch, and no archive capture could be retrieved in verification; the same applies to aciddose's "50 ms blip", "100x as wide" and 659 Hz remarks cited below)]:

```
            +12 V
              |
             R45 22k
              |
   C11 1uF ---+--- E
   (to GND)        Q8  2SA733(P)  PNP, h_FE 200-400
  saw --+-- R35 100k ------+-- B
        |                  |
        +-- C10 0.01uF -- R34 10k
                           C --+-- square out (to WAVEFORM switch)
                               |
                              R36 10k
                               |
                            +5.333 V
```

How it works [secondary: derived here]:

1. Q8's emitter sits on a 1 µF capacitor fed from +12 V through 22 k. The emitter voltage V_E is the switching threshold, and it is not fixed. It charges toward 12 V with τ_off = R45·C11 = 22 ms while Q8 is off, and discharges toward about 7.45 V with τ_on = C11·(R45 ∥ R36) = 6.9 ms while Q8 is saturated (emitter current runs through R36 into the 5.333 V rail).
2. Q8 turns on when the falling saw drops to V_E − 0.6 V. The collector then rises from 5.333 V to V_E − V_CEsat. Q8 saturates almost at once: saturation needs only 0.3 mA of collector current, and the speed-up network C10/R34 delivers C10·dV/dt = 55 nA per Hz of base drive during the ramp (6 µA at 110 Hz).
3. The saw reset drives the base positive through C10/R34 and turns Q8 off within the reset time. After the reset the base sits above the saw by about 2.5 V·exp(−t / 1.1 ms) (C10 discharging through R34 + R35), which delays the next turn-on at high pitches.
4. Consequences: the square is **high during the last part of each falling ramp** and low after the reset. Its high level is a drooping exponential that tracks V_E. Its duty cycle depends on pitch because V_E has time to recharge at low notes (earlier turn-on, longer high time) and because of the post-reset base offset at high notes (later turn-on). A pitch change moves the threshold with the 7 to 22 ms time constants, which is the "50 ms blip" aciddose reported from SPICE. antto's empirical model `tanh(hpf1(saw)·−amp)` with hpf1 = 14.7 Hz is the same mechanism fitted from recordings: 14.7 Hz sits between 1/(2π·22 ms) = 7.2 Hz and 1/(2π·6.9 ms) = 23 Hz. [secondary: KVR Oct 2009]

Reference model (two states, solve the two algebraic node equations each oversampled step):

```
Q8 transport model, PNP:   veb = Ve - Vb,  vcb = Vc - Vb
  Icc = Is*(exp(veb/Vt) - exp(vcb/Vt))
  Ibe = Is/BF*(exp(veb/Vt) - 1),  Ibc = Is/BR*(exp(vcb/Vt) - 1)
node B:   (Vs - Vb)/R35 + (Vx - Vb)/R34 + Ibe + Ibc = 0        Vx = Vs - vC10
node C:   Icc - Ibc - (Vc - 5.333)/R36 = 0
state:    C11 * dVe/dt   = (12 - Ve)/R45 - (Icc + Ibe)
          C10 * dvC10/dt = (Vx - Vb)/R34
Is = 2e-14 A, BF = 300, BR = 4, Vt = 25.85 mV; Vs = saw in volts (5.5 -> 11.5 V falling, 20 us reset)
```

Simulated results with those values [secondary: derived here; script `scratchpad/refs/vcovcf/shaper.py`]:

| Note | f (Hz) | Duty (fraction of period high) | High level start to end (V) | p-p (V) | Turn-on edge 10-90 % | H2/H1 |
|---|---|---|---|---|---|---|
| C1 | 32.70 | 0.57 | 8.5 peak, sags to 7.5 | 3.2 | 2.2 ms (7 % of T) | −13.9 dB |
| C2 | 65.41 | 0.50 | 8.85 to 7.91 | 3.6 | 0.22 ms (1.4 %) | −20.7 dB |
| A2 | 110.0 | 0.46 | 8.8 to 8.15 | 3.5 | 0.105 ms (1.2 %) | −18.1 dB |
| C3 | 130.8 | 0.45 | 8.76 to 8.23 | 3.4 | 0.085 ms (1.1 %) | |
| C4 | 261.6 | 0.41 | 8.75 to 8.49 | 3.4 | 0.038 ms (1.0 %) | −11.1 dB |
| C5 | 523.3 | 0.35 | 8.87 to 8.76 | 3.5 | 0.018 ms (0.95 %) | −7.1 dB |
| C6 | 1046.5 | 0.30 | 9.04 to 9.00 | 3.7 | 0.009 ms (0.95 %) | |

- The low level is flat at 5.333 V. The reset edge (high to low) is as fast as the saw reset. The turn-on edge (low to high) lasts about 1 % of the period from C3 upward, so its width scales with 1/f, as aciddose found in SPICE ("at 10 Hz it'll be 100x as wide as 1000 Hz"). Below C2 the base drive from C10 is too small to snap Q8 into saturation and the edge softens to several percent of the period.
- Sensitivity: the saw's top level dominates. Saw 5.3 to 10.8 V gives duty 0.62 / 0.545 / 0.50 / 0.45 / 0.385 at C1 / C2 / C3 / C4 / C5; saw 5.5 to 12.0 V gives 0.54 / 0.47 / 0.43 / 0.39 / 0.34. β from 200 to 400, I_S over a 16:1 range, C11 +20 %, R45 +5 % each move duty by less than 0.015.
- Pitch jump C2 to C4: duty goes 0.496 → 0.407 → 0.390 → 0.398 → ... → 0.409, settled to 0.002 within 7 cycles (27 ms).
- Cross-checks. SN sketch at key A: 5 V to 8 V, close to 50 % [primary]; simulation 5.33 to 8.8 V, 46 %. Whittle via Olney: "about 45 % at high pitches but about 71 % at the lowest pitches" [lore: the original statement was not located]; the simulation reproduces the direction and the 45 % end (C4 = 0.41 to 0.45), and reaches 0.62 at C1 with the lower saw-top assumption. Robin Schmidt, from a recording of one unit: "pulse-width of 50 % ... the lower half-wave has a higher amplitude than the upper" [lore, KVR Sep 2009], which matches C2 (0.50) seen through the later high-pass stages. antto: two units matched "PW around 86-87 %" and "98 %" in his own parameter [lore; KVR p=3732152, 25 Aug 2009, and t=262829, both re-checked in verification]. His parameter is not a duty cycle, and he adds that it may vary with oscillator frequency.
- The 20.6× "shaper gain" (680 k/33 k) quoted in the seed report is the SH-09's pulse shaper, cited by aciddose as an analogy. Open303's `-tanh(dB2amp(36.9)·saw + 4.37)` is a static fit (it gives a fixed duty of about 47 % at every pitch, and the blend oscillator then scales the square by 0.5). Neither is a property of this circuit, which is a saturating switch with a floating threshold.

### 2.6 Relative level of saw and square into the filter

Both reach the filter through the same 220 k / 1 µF input network, selected by S1. [primary: SN p.5]

| | Saw | Square |
|---|---|---|
| p-p at the switch | 6.0 V (6.5 V as drawn) | 3.4 to 3.6 V (3 V as drawn) |
| AC rms | 1.73 V | 1.33 V (C1) to 1.66 V (C5) |
| Fundamental amplitude | 1.91 V | 1.72 V (C1), 1.91 V (C2), 1.97 V (A2), 2.01 V (C4) |

[secondary: derived here] The fundamentals are equal within 1 dB; the square's p-p is 0.55 to 0.6 of the saw's. Open303 scales its square by about 0.5 relative to a unit saw [primary: O303], which is the same ratio.

### 2.7 Drift and temperature

Scale is compensated by R100 and offset by the Q26 pair (section 2.2). Factory tolerance is ±0.5 % across two octaves. Published warm-up or long-term drift numbers do not exist. A per-instance offset of a few cents plus a slow random walk of ±2 to ±5 cents is defensible as an "analog" option and should default to off or very small. [lore]

### 2.8 Coupling before the filter

C17 1 µF in series with R62 220 k into R70 2.2 k: corner 1 / (2π · 1 µF · 222 k) = 0.72 Hz. Stinchcombe's input block has its lowest pole at 0.708 Hz. [primary: SN, TS2] There is no audio-band high-pass between the VCO and the ladder. The high-passed look of recorded 303 waveforms comes from stages after the ladder (sections 3.5, 3.10).

## 3. VCF

### 3.1 Topology and parts

[primary: SN p.5; every value cross-checked against X0X]

| Block | Parts |
|---|---|
| Input pair | Q12 2SC1583 (matched dual). Left base: R62 220 k + C17 1 µF from the waveform switch, R70 2.2 k to bias node B. Right base: R98 2.2 k to node B, feedback via R97 10 k. Node B: R68 2.2 k from 5.333 V, C16 10 µF to ground |
| Ladder | Four capacitors between the two arms: **C18 0.018 µF** (bottom), **C19, C24, C26 0.033 µF**. Diode-connected 2SC536F pairs between levels: Q13/Q15, Q14/Q17, Q16/Q23. Top pair Q22 2SC2291 (dual), collectors to the +12 V filter rail (R95 100 Ω, C28 47 µF), bases common, biased by R115 10 k and C30 10 µF |
| Tail current | Q11 (2SC536F NPN) collector through R69 2.2 k to Q12's emitters |
| Output amp | C25, C27 0.1 µF from the two ends of C26 to the bases of Q21 2SC1583; R113, R114 100 k base returns to a node fed by R116 10 k from 5.333 V with C29 1 µF; Q20 current source (R112, R109 10 k divider, R108 2.2 k emitter); R110, R111 **22 k** collector loads (the 150 dpi SN scan prints "2.2K" for both; X0X, the RE-303 BOM and the gain all say 22 k, and so does the SN's own TP6 annotation: TP6 = 5.2 V DC puts Q19's base, i.e. Q21's collector, near 5.8 V, which needs 0.28 mA × 22 k = 6.2 V of drop from 12 V. With 2.2 k the collector would sit at 11.4 V); Q19 follower, R96 10 k |
| Output and resonance | C14 1 µF from Q19's emitter to VR4a pin 3; VR4 is a dual 50 k(B), pin 1 of gang a grounded |
| Feedback | VR4a wiper, C15 1 µF, R67 2.2 k, Q18 follower (base bias R99 100 k to a node with R66 100 k from 5.333 V and C22 1 µF; emitter load R94 10 k), C23 1 µF, R97 10 k to Q12's right base |
| Control | Q9, Q10 (2SA733 PNP), Q11 (2SC536F NPN), R64, R65 10 k, R71 2.2 k, R73 100 k, TM3 470 k, R63 220 k, R72 100 k (section 3.8) |

The bottom capacitor is 18 nF, not exactly half of 33 nF: the ratio is 0.545. The effect is a 2 % shift (section 3.2).

Device types (added in verification): the "2SC536F" designations in this table and elsewhere come from X0X. The Roland schematic itself carries the note "ALL NPN TRANSISTORS ON THE MAIN BOARD ARE 2SC945(P), ALL PNP TRANSISTORS ... ARE 2SA733(P)" (SN p.4), the SN lead-identification figure (p.7) shows 2SC536F, 2SC2021R and 2SA937Q as production alternates, and the RE-303 BOM lists every such position as "2SC536F or 2SC945P". The duals (2SC1583 for Q12, Q21, Q26; 2SC2291 for Q22) and the 2SK30A(O) at Q28 are the same in all sources. For modelling, treat the ladder diodes and Q11/Q20/Q18/Q19 as generic small-signal NPNs whose type varied in production. [primary: SN, RE]

### 3.2 Linear core: transfer function, poles, and the "18 dB" question

Notation (Stinchcombe): I_f is the tail current, a = 2·V_T / I_f is the small-signal resistance of each ladder diode (each arm carries I_f/2), C = 33 nF.

With C1 = C/2 [primary: TS1, TS3 eq. 30 onward]:

    H_tb(s) = −1 / (8(aCs)^4 + 32(aCs)^3 + 40(aCs)^2 + 16(aCs) + 1)

Normalised to ω_c = 1 / (2^(3/4)·a·C) = I_f / (2^(7/4)·C·V_T):

    H_tb(s) = −1 / (s^4 + 6.727 s^3 + 14.142 s^2 + 9.514 s + 1)

Poles are real: −0.13, −1.04, −2.33, −3.24 (× ω_c). With equal capacitors they would be −0.12, −1.00, −2.35, −3.53 on a slightly different ω_c; halving the bottom capacitor is "closely equivalent to shifting the whole response" by 2^(1/4) = 1.189 and does not move one pole up an octave, because the stages are not buffered from each other. [primary: TS1]

The same polynomial in Zavalishin's form is 8(1+p)^4 − 8(1+p)^2 + 1 with p = aCs. With feedback k the denominator gains "+ k". The loop oscillates when k = 17 at p = j/√2. [primary: algebra on TS1's polynomial; matches Open303's `k = 17·r` scaling]

Definitions the build should adopt [secondary: derived here]:

- **f_res**: the frequency at which the loop phase reaches 180°, i.e. the oscillation/peak frequency at high resonance. f_res = 2^(1/4)·f_c(Stinchcombe) = 1.189·f_c. This is what every 303 "cutoff in Hz" figure in the literature measures (resonant peak on a spectrum).
- With the real 18 n / 33 n parts: f_res = 0.6919 / (2π·a·C), k_osc = 17.02, and

      f_res = 64.5 Hz per µA of I_f at 300 K   (68.4 at 283 K, 61.9 at 313 K)

  so 4.9 µA gives 314 Hz, 7.75 µA gives 500 Hz, 37 µA gives 2.4 kHz, 430 µA gives 28 kHz.

  This linear current-to-frequency law is the ideal-junction result. Stinchcombe's own SPICE check of the TB-303-type ladder found "a tendency for the calculated values to be too high in frequency as the current I_f gets bigger" (TS3 section 3.4, I_f stepped 10 to 500 µA). [primary: TS3] Ohmic base and emitter resistance adds to a at high current, so expect the real top of the range to fall short of 64.5 Hz/µA. The model can include a series resistance r_s per junction (a = 2V_T/I_f + 2r_s; a few tens of ohms flattens the top octave) or simply let the TM3 and knob-span trims absorb it. Added in verification.
- Integrator rate for a state-space model: ω = 1/(2aC) = I_f / (4·V_T·C); f_res = 0.2203·ω (0.2251·ω with an ideal half capacitor).
- Pole positions in units of f_res: 0.1076, 0.872, 1.954, 2.726.

Shape at zero resonance, relative to DC (computed from the polynomial with 18 n / 33 n):

| f / f_res | Magnitude | Slope over the next octave |
|---|---|---|
| 0.105 | −3.0 dB | |
| 0.125 | −3.8 dB | −4.7 dB/oct |
| 0.179 | −6.0 dB | |
| 0.25 | −8.5 dB | −6.7 dB/oct |
| 0.5 | −15.2 dB | −9.4 dB/oct |
| 1 | −24.6 dB | −13.7 dB/oct |
| 2 | −38.3 dB | −18.6 dB/oct |
| 4 | −57.0 dB | −22.1 dB/oct |
| 8 | −79.1 dB | −23.5 dB/oct |
| 16 | −102.6 dB | −23.9 dB/oct |
| 32 | −126.5 dB | −24.0 dB/oct |

The "18 dB" claim, precisely:

- The filter is fourth order with a −24 dB/oct asymptote. Stinchcombe shows it four ways, including SPICE stage gradients of −6.1, −11.9, −17.9 and −24.1 dB/oct. [primary: TS1]
- The asymptote is reached only about four octaves above f_res. In the two octaves above f_res, where the audible action is, the slope is −13.7 then −18.6 dB/oct. With no resonance the −3 dB point is a decade below f_res. That is the "soft, early knee" people hear as 18 dB. [secondary: derived here]
- No published swept-sine measurement of a stock unit states 18 dB/oct. Stinchcombe: "apparently even Roland themselves described the filter" as 18 dB 3-pole. [primary: TS1] The x0xb0x wiki's "4th pole is staggered up an octave" explanation is wrong for the reason above. [X0XW is the source of that lore]
- Dropping the fourth pole changes the response well inside the audio band (TS1, `tb303_3_vs_4.gif`). A three-pole model is not an acceptable shortcut.

Closed loop on the ideal core (no coupling capacitors):

| k / k_osc | k | DC gain | Peak above DC gain | Peak frequency / f_res | Q of dominant pair | Cycles to decay by 1/e |
|---|---|---|---|---|---|---|
| 0.25 | 4.3 | −14.4 dB | +1.9 dB | 0.48 | | |
| 0.50 | 8.5 | −19.6 dB | +7.8 dB | 0.75 | 2.6 | 0.8 |
| 0.60 | 10.2 | −21.0 dB | +10.6 dB | 0.82 | 3.6 | 1.1 |
| 0.70 | 11.9 | −22.2 dB | +13.8 dB | 0.88 | 5.3 | 1.7 |
| 0.80 | 13.6 | −23.3 dB | +18.0 dB | 0.92 | 8.6 | 2.7 |
| 0.90 | 15.3 | −24.3 dB | +24.7 dB | 0.96 | 18.6 | 5.9 |
| 0.95 | 16.2 | −24.7 dB | +31.0 dB | 0.98 | 38.6 | 12.3 |

[secondary: derived here] Two things to carry forward: the passband drops as 1/(1+k), and the peak sits below f_res until the resonance is high.

### 3.3 Large-signal core

The un-linearised form of Stinchcombe's stage equations (TS3 eqs. 23 to 27, before he applies tanh(x) ≈ x) [secondary: derived here from primary equations]. Let v1..v4 be the differential voltages across C18, C19, C24, C26, v_d the differential voltage at Q12's bases, and x = v / (2·V_T):

    x_d  = (v_in/101 − v_fb) / (2 V_T)
    dx1/dt = ω (C/C1) [ tanh(x2 − x1) − tanh(x_d) ]        C/C1 = 33/18 = 1.833
    dx2/dt = ω        [ tanh(x3 − x2) − tanh(x2 − x1) ]
    dx3/dt = ω        [ tanh(x4 − x3) − tanh(x3 − x2) ]
    dx4/dt = ω        [ −tanh(x4)     − tanh(x4 − x3) ]
    ω = I_f / (4 V_T C),   output = v4 = 2 V_T x4   (equals −v_d at DC)

Linearised, this is exactly the "mystran & kunn" structure Open303 runs (`y1 += 2·b0·(y0−y1+y2); y2 += b0·(y1−2·y2+y3); ...`) with y0 = −x_d. [primary: O303 `rosic_TeeBeeFilter.h`, mode TB_303, comment "ala mystran & kunn (page 40 in the kvr-thread)"] Open303 ships it linear: the `shape()` calls are commented out.

At DC the lower stages carry larger voltages than the output: v3 = 2·v4, v2 = 3·v4, v1 = 4·v4. The bottom capacitor sees four times the input differential voltage, so the lower stages leave the linear region first on low-frequency content.

Assumptions: ideal exponential junctions, equal emission coefficients, infinite β, perfect matching. Stinchcombe found in simulation that mixing device types between the top pair and the rest shifts passband gain by up to 6 dB (TS3 section 3.4; he replaced the top pair with 1N4148). The 303 uses three device types (2SC1583, 2SC536F, 2SC2291), so the real core gain may differ from −1 by a dB or two. [primary for the observation; its size in a real 303 is unknown]

### 3.4 Feedback path, loop gain, and how far resonance goes

Path [primary: SN p.5, TS2]: Q19 emitter, C14, VR4a (50 k linear), wiper, C15, R67, Q18 follower, C23, R97 10 k into the R98 2.2 k that ties Q12's right base to the bias node.

- Maximum loop gain in Stinchcombe's model: 106 × 0.98 / 5.55 = **18.7·r**, r = wiper fraction, written by him as the term 18.7·k·s^4(...). [primary: TS1, TS2] Oscillation on the ideal core needs 17.0.
- The pot is linear and the wiper drives two loads: R67 + R99 (about 102 k, C15 and C22 being near-shorts above a few hertz) and the VCA make-up path R122 100 k + R124 2.2 k through 0.01 µF (section 3.10), which is about 102 k above roughly 300 Hz and open at DC. So r_eff = r / (1 + r(1 − r)·50 k/R_L) with R_L ≈ 102 k at low audio frequencies (bow up to 11 % at mid rotation) and R_L ≈ 51 k across the band where the resonance peak sits (bow up to 20 %). (Corrected in verification: this said R_L = 102 k only, with an 11 % bow; the R122 path also hangs on the wiper.) [primary for the parts; arithmetic derived here] Open303's skew `(1 − e^(−3r)) / (1 − e^(−3))` is a tuning choice, not circuit. Whittle confirms the original pot is linear and that later replacement pots, which bunch their travel in the middle, change where sounds sit on the knob. [primary: DFM]
- Two effects pull the real maximum below 18.7, and both depend on cutoff [secondary: derived here; validated at one cutoff against Stinchcombe's component-level plot]:
  1. **Output-amp loading.** Each end of C26 drives 0.1 µF into R_b = r_π(Q21) ∥ 100 k. Q21 runs 0.263 mA per side, so r_π = β × 98 Ω and R_b = 13.6 k at β = 160, 28 k at β = 400, 37 k at β = 600. The top ladder node has source resistance a, so the mid-band gain is multiplied by λ = R_b / (R_b + a), with a = 3.34 kΩ × (1 kHz / f_res). At f_res = 250 Hz and R_b = 20 k, λ = 0.60.
  2. **The same network is a high-pass inside the loop**, corner 1 / (2π · 0.1 µF · (R_b + a)). Its phase lead moves the 180° point up the ladder's skirt, where the ladder has less gain.
- The loaded model, for reproduction: differential half-circuit with node voltages n1..n4 up the ladder and nb at a Q21 base; unit current i into n1; conductance g = 1/a between adjacent nodes and from n4 to ground (the Q22 emitter); capacitors 2·C18 at n1 and 2·C at n2..n4 to ground; 0.1 µF from n4 to nb; R_b from nb to ground. The core-plus-coupler transfer is nb/(a·i). Multiply by 110 and by Stinchcombe's blocks 3, 4, 5 for the loop.
- Stinchcombe's two plots show the size of this. His Laplace model (no loading, fixed 92 Hz corner) at r = 0.99 and f_c = 820 Hz peaks at about +3 dB with the plateau at −25.6 dB. His component-level SPICE run of the full schematic peaks at about −9 dB with the same plateau. [primary: TS2 plot, read to ±1 dB] My loaded half-circuit model with β = 160 gives −7.4 dB at 1040 Hz, plateau −25.8 dB, low peak −13 dB at 9.5 Hz. The 12 dB gap between his two curves is the loading.
  - Verification note: the comparison GIF (`tb303_lap_v_comp.gif`) has no axis labels and no panel titles. Measured in pixels, with the dB scale fixed by the k = 0 maximum to k = 1 plateau spacing (20 dB in the block model), the upper panel's full-resonance peak stands 27 dB above its plateau and the lower panel's 16 dB. That matches the numbers above, but which panel is which is inferred from the page order, and Stinchcombe himself describes the two as agreeing "tolerably well" without mentioning a gap or a cause. Attributing the gap to base loading is this doc's hypothesis [secondary: derived here], not his finding.
  - Supporting evidence for β ≈ 160 (added in verification): Stinchcombe's in-loop coupler pole of 578.1 rad/s (92.0 Hz) at f_res = 975 Hz implies R_b + a = 1/(2π·92 Hz·0.1 µF) = 17.3 k. With a = 3.4 k at that cutoff, R_b ≈ 13.9 k, i.e. r_π ∥ 100 k with β ≈ 165 for whatever transistor model his SPICE run used.
- Loop gain at the 180° crossing with the pot at maximum, and the resulting peak height above the passband, from the loaded model:

  | f_res | β(Q21) = 160 | β(Q21) = 400 |
  |---|---|---|
  | 120 Hz | 0.20, no peak | 0.37, +3 dB |
  | 250 Hz | 0.39, +5 dB | 0.61, +10 dB |
  | 500 Hz | 0.62, +11 dB | 0.81, +18 dB |
  | 1 kHz | 0.82, +19 dB | 0.95, +31 dB |
  | 1.4 kHz | 0.89, +24 dB | 1.00, oscillates |
  | 2 kHz | 0.96, +32 dB | 1.04, oscillates |
  | 2.8 kHz | 1.01, oscillates | 1.07 |
  | 8 kHz | 1.09 | 1.12 |

  [secondary: derived here]
- What real units do:
  - "The maximum resonance level of the Resonance knob on the TB-303 is quite close to self oscillation." [lore: antto, from recordings]
  - Devil Fish doubles the feedback; then "the filter will self resonate at mid & high frequencies when the Resonance pot is past about 2 o'clock", and "the filter will only self oscillate at mid and high frequencies. Surgical intervention, aimed at improving its gain at lower frequencies, made it sound more like a normal filter, so I abandoned this". [primary: DFM] Two o'clock is about 70 % rotation, so stock maximum is roughly 0.7 of critical at mid cutoff on Whittle's units.
  - SN adjust sketch: at cutoff centre and full resonance the ring dies away within about four cycles of the 2 ms period. [primary: SN p.5, p.7] From the table in 3.2 that is a loop gain of 0.6 to 0.8 of critical at f_res = 500 Hz, which is what the loaded model predicts there.
  - Josh Wink and others adjusting internal trimmers to get a screaming sweep is consistent with units sitting a decibel or two either side of oscillation at high cutoff. [lore]
- Working conclusion: resonance at maximum is weak below 200 Hz, moderate at 500 Hz (peak +11 to +18 dB over the passband), strong at 1 kHz, and at or just past the edge of oscillation from roughly 1.5 to 3 kHz upward, depending on Q21's β and resistor tolerances. With a signal present the input pair's compression (3.9) stops a marginal loop from running away, so a unit that is nominally unstable at the top of an envelope sweep whistles for the few milliseconds it spends there and no more.

### 3.5 Every coupling capacitor in or around the loop

| # (TS2) | Parts | Position | Corner(s) | Source |
|---|---|---|---|---|
| 1 | C17 1 µF, R62 220 k, R70, R68, C16 10 µF | input, outside the loop | zeros 0, 5.41, 17.5 Hz; poles 0.708, 6.16, 15.5 Hz (TS2's closed form has 38.5 rad/s = 6.13 Hz for the middle pole; the 0.5 % difference is immaterial); HF gain 0.01. Audio-band effect: none above 30 Hz | TS2 [primary] |
| 2 | C25, C27 0.1 µF into Q21 bases | forward path, inside the loop | single high-pass, 92 Hz in Stinchcombe's fit at f_c = 820 Hz. Physically 1 / (2π·0.1 µF·(R_b + a)): 30 to 110 Hz for β = 1000 to 160, falling a little at low cutoff. Below about 40 Hz the sub-audio loop goes unstable at full resonance (table below), so the plausible stock range is about 45 to 110 Hz | TS2 [primary]; range [secondary: derived here] |
| 3 | C14 1 µF into VR4 50 k | forward path, inside the loop | 3.18 Hz | TS2 [primary] |
| 4 | C15 1 µF into R67 + R99 | feedback path only | 1.18 Hz, gain 0.98·r (C15 into 102 k alone would be 1.56 Hz; TS2's lower figure presumably includes C22, which is not a short at 1 Hz) | TS2 [primary] |
| 5 | C23 1 µF, R97 10 k, R98 2.2 k, node B (R68, C16) | feedback path only | zeros 0, 0.701, 7.41 Hz; poles 0.708, 6.16, 15.5 Hz; HF gain 1/5.55 | TS2 [primary] |
| VCA | C21 0.01 µF + R121 220 k; C20 0.01 µF + R122 100 k; R124 2.2 k | after the loop | 71.6 Hz (direct path), 155.7 Hz (resonance-wiper path) | SN, X0X [primary]; see 3.10 for the conflict with measured fits |

No capacitor sits between ladder stages ("to my untrained eye, looks like there is no HP filter between the filter stages?" "Correct." Stinchcombe, KVR, 10 Oct 2009). [primary]

Where the high-pass sits matters. Block 2 is in the forward path inside the loop. At zero resonance it is a plain series high-pass. With feedback, the closed loop flattens it: the response stays at 1/(1+k) down to where the loop gain runs out, so its corner effectively moves down by (1+k). antto found the same thing by ear in October 2009: results got closer with `x0 = hpf1(input); feedback = hpf2(x0 + k·y4)`, a high-pass after the summing point, than with a high-pass on the feedback signal alone. [lore, consistent with the circuit] Open303's 150 Hz high-pass is on the feedback signal only, which returns full bass below 150 Hz at high resonance. The circuit does not do that.

**Sub-audio stability sets a floor on the in-loop coupler corner** (added in verification). The six sub-audio poles and zeros of blocks 1, 3, 4 and 5 give the loop a second 180° crossing near 5 to 7 Hz, which is where the low peak of section 3.6 comes from. Its loop gain depends strongly on the in-loop coupler (block 2). At full resonance, f_c = 820 Hz, with every other block as Stinchcombe published it:

| In-loop coupler corner | 92 Hz (TS2) | 60 Hz | 45 Hz | 35 Hz | 26 Hz |
|---|---|---|---|---|---|
| Sub-audio loop gain at 180° | 0.49 at 6.5 Hz | 0.71 | 0.88 | **1.06** | **1.29** at 5.6 Hz |

The loaded half-circuit model gives the same picture in terms of R_b: at full resonance and f_res 975 Hz the sub-audio loop gain is 0.40 at R_b = 14 k (β 160), 0.75 at 28 k (β 400), 0.93 at 37 k (β 600), 1.14 at 50 k and 1.29 at 60 k, and it is higher still at f_res 2.4 kHz (1.02 at 37 k). [secondary: derived here, from TS2's published blocks and the doc's own `vcf_loaded.py`] Above about R_b = 35 k, or a coupler corner below about 40 Hz, the model motorboats at 5 to 6 Hz whenever resonance is at maximum. Stock units are not reported to do this; Stinchcombe got an 8 Hz oscillation on his clone only after his test set-up added capacitive load. So the stability margin is real but small, and it bounds β(Q21) and the coupler corner from the side that question 1's "Measured" preset needs.

### 3.6 Stinchcombe's full model

[primary: TS1, TS2. "Based on SPICE simulation, so take it for what its worth"; he confirmed the lower peak on an Analogue Solutions TBX-303 clone but did not finish hardware validation.]

    H(s) = 1.06 s^3 (s+109.9)(s+34.0)(s+7.41)
           / { D4(s/ω_c) (s+97.5)(s+38.5)(s+4.45) (s+578.1)(s+20.0)(s+7.41)
               + 18.7 k s^4 (s+46.5)(s+4.40) }

with D4(x) = x^4 + 2^(11/4) x^3 + 10√2 x^2 + 2^(13/4) x + 1, all constants in rad/s, k = 0..1. Ten poles, six zeros. As blocks (frequencies in Hz), with HP(f) = (s/2πf) / (1 + s/2πf):

    B1 = 0.01 · s·(1 + s/2π17.495)(1 + s/2π5.407)·K1 / [(1 + s/2π15.517)(1 + s/2π6.164)(1 + s/2π0.708)]
    CORE = −1 / D4(s/ω_c)
    B2 = −106 · HP(92)
    B3 = HP(3.18)
    B4 = k · 0.98 · HP(1.18)
    B5 = (1/5.55) · s·(1 + s/2π7.408)(1 + s/2π0.701)·K5 / [(1 + s/2π15.517)(1 + s/2π6.164)(1 + s/2π0.708)]
    Vout/Vin = B1·CORE·B2·B3 / (1 + CORE·B2·B3·B4·B5)

K1 and K5 are the constants that make the high-frequency gain of B1 and B5 equal 0.01 and 1/5.55. Output is the top of the resonance pot (after C14).

Evaluating those blocks at his f_c = 820 Hz (f_res = 975 Hz), level in dB relative to the VCO signal [secondary: derived here from primary blocks; his plot agrees within reading error]:

| k | 32.7 Hz | 65.4 Hz | 110 Hz | 200 Hz | Main peak | Low peak |
|---|---|---|---|---|---|---|
| 0 | −9.3 | −5.6 | −5.1 | −7.3 | none (broad maximum −5 dB near 100 Hz) | none |
| 0.2 | −13.3 | −14.3 | −14.4 | −14.3 | −13.7 dB at 430 Hz | −12.6 dB at 20 Hz |
| 0.4 | −17.4 | −18.6 | −18.8 | −18.6 | −14.1 dB at 706 Hz | −13.0 dB at 13 Hz |
| 0.6 | −20.3 | −21.5 | −21.8 | −21.6 | −12.2 dB at 854 Hz | −12.5 dB at 10.9 Hz |
| 0.8 | −22.5 | −23.7 | −23.9 | −23.8 | −8.0 dB at 957 Hz | −11.5 dB at 9.6 Hz |
| 1.0 | −24.3 | −25.5 | −25.7 | −25.6 | +4.2 dB at 1034 Hz (component-level SPICE: about −9 dB) | −10.2 dB at 8.7 Hz |

"The lower resonant peak is very real ... the test set-up I was using was adding just enough capacitive load to enable the filter to oscillate comfortably at around 8 Hz. I feel the effect of this peak, boosting the bass frequencies as it does, might be a large contributing factor to the sound." [primary: TS2] The 8 Hz figure is that oscillation on his loaded clone. In the model the low loop has a gain of 0.5 at its 0° crossing (6.5 Hz), so a stock unit does not oscillate there; it has a 10 to 15 dB bump at 9 to 13 Hz that moves with resonance. That margin depends on the in-loop coupler corner and on β(Q21); see "Sub-audio stability" in section 3.5. At β = 400 the bump at full resonance is already about 25 dB above the plateau, at 7 Hz.

### 3.7 What resonance does to level and bass

- Inside the filter the passband falls as 1/(1 + k·λ): −14 dB at 20 % rotation, −19 dB at 40 %, −24 dB at 80 %, −25.6 dB at maximum (table above). The x0xb0x wiki describes it the same way: "the peak stays at the same volume and the passband is reduced". [primary: TS2; X0XW]
- The bass does not come back below some corner. It stays 20 to 25 dB down until the 10 Hz bump.
- **Resonance-pot make-up path.** The VCA input sums the filter output through 220 k with the resonance wiper through 100 k (section 3.10; both lines traced on the 150 dpi SN scan to VR4a pin 3 and pin 2, and X0XW says the same). Above about 300 Hz the VCA drive rises by (1 + 2.17·r): +10.0 dB at maximum resonance. Below both corners each path passes the same current (jωC·V, whatever its resistor), so the factor tends to (1 + r): +6.9 dB at 65 Hz and +6.3 dB at 33 Hz at maximum resonance. [primary for the topology; arithmetic derived here] Open303's output gain term `g·(1 + r)` is a +6 dB version of the same idea.
- Net, VCO to VCA input, f_res = 975 Hz, r from 0 to 1: 110 Hz falls by 12.8 dB, 65 Hz by 12.9 dB, 200 Hz by 9.4 dB, while the peak rises by 10 to 25 dB depending on the loop-gain case. [secondary: derived here] This is the thin, nasal high-resonance 303 tone, and it should not be normalised away.
- **Second gang of the resonance pot (VR4b)** carries no audio and no feedback. It is the accent sweep network: accent-pot wiper through D24 and R46 47 k to one end, C13 1 µF on the other end, wiper through R72 100 k into the cutoff summing node. At low resonance the filter gets the fast direct pulse; at high resonance it gets the lagged voltage on C13. Details and numbers are in doc 03. Its scale into the filter is 1.19 octaves per volt (section 3.8). [primary: SN p.5, DFM]

### 3.8 Cutoff control law

Circuit [primary: SN p.5 and p.8 Fig. 2; device types from X0X]:

- Q9 (PNP): base on the R65/R64 divider (2.67 V), collector grounded, emitter forms a 3.3 V bus ("VQ9"). Whittle independently reports 3.3 V at the Devil Fish filter CV input. [primary: DFM]
- Q11 (NPN): emitter on the bus, collector sinks I_f through R69. Q10 (PNP): emitter to Q11's base, collector to the bus, base to the summing node. R71 2.2 k from the summing node to the bus.
- **Cutoff pot VR3 is 50 k(A)**, audio taper (SN parts list: "K161B0-50kA: CUT OFF FREQ, ENV MOD"; X0X: VR3 50K(A)). Pin 3 to +12 V, pin 1 through R47 10 k to the bus. The wiper goes through TM3 (470 k rheostat) and R73 100 k **to Q10's emitter / Q11's base**, not to the summing node.
- Env Mod wiper through R63 220 k and accent sweep through R72 100 k go to the summing node (Q10's base).

Analysis [secondary: derived here]:

    I_f = (I_S11 / I_S10) · I_E10 · exp( V_sum / V_T )
    I_E10 = (V_wiper − V_B11) / (R73 + TM3)
    V_sum = voltage on R71 = 9.69 mV per volt of (Env Mod wiper − 3.3 V) + 21.3 mV per volt of (accent wiper − 3.3 V)

1. **The cutoff knob is linear in current, hence linear in Hz**, and the audio-taper pot supplies the curvature. This is why Whittle's Filter Tracking input, which injects into the same place, is "linear in a volts to Hertz sense". [primary: DFM] With ρ = R47 / (R47 + R_pot) = 1/6, v = V_BE11 / (12 V − 3.3 V) ≈ 0.063 and A(x) the pot's taper function (A(0) = 0, A(1) = 1):

       f_nom(x) = f_max · [ρ + (1 − ρ)·A(x) − v] / (1 − v)

   Range f_max / f_min = (1 − v) / (ρ − v) = 9.0 for nominal parts (3.2 octaves); 6.6 to 12.6 across pot tolerance ±20 % and V_BE 0.50 to 0.60 V. The bottom of the range is a difference of two similar voltages and varies most between units.
2. **Measured range.** Open303 encodes antto's measurements of one unmodified unit (rv0's): nominal cutoff 313.8 Hz to 2394.4 Hz (ratio 7.63). [secondary: O303 `calculateEnvModScalerAndOffset`] Whittle: Devil Fish maximum resonant frequency with no Env Mod is "approximately 5 kHz. This is an octave above the typical maximum for the TB-303", i.e. about 2.5 kHz. [primary: DFM] antto: with Env Mod at minimum and cutoff at maximum the peak starts at 3.2 to 3.5 kHz on each note [lore]; the residual envelope adds +0.57 octave at note-on to 2394 Hz, giving 3.55 kHz, so the three statements agree.
3. **Factory calibration.** TP6, CUTOFF at centre, saw, RES fully clockwise, ENV MOD, DECAY and ACCENT fully counter-clockwise, low C repeating: adjust TM3 for a ring period of 2 ms ± 0.5 ms (500 Hz, 400 to 667 Hz). [primary: SN p.7] With an audio taper of 10 to 20 % at mid rotation, the formula above puts the centre at 0.20 to 0.29 of f_max, so a unit trimmed to 500 Hz at centre has f_max between 1.7 and 2.5 kHz and f_min between 190 and 280 Hz. Open303's pure exponential knob law puts the centre at 867 Hz; a real knob is lower at centre.
4. **TM3 is a scale, not an offset.** R73 + TM3 runs from 100 k to 570 k, a 5.7:1 (2.5 octave) span that multiplies the whole knob range. It also trims out the unit's I_S11/I_S10 ratio, which is the ratio of an NPN and a PNP of unrelated types and is the main reason units need the trimmer. Phoscyon's "TM3 [mV], ±500 mV, base cutoff" is the same control expressed as an equivalent offset. [P2]
5. **Exponential inputs.** One octave is V_T·ln2 = 17.9 mV on R71 at 300 K. Env Mod wiper: 0.541 octave per volt. Accent wiper (R72): 1.19 octave per volt. A 100 k external input, as on the Devil Fish: "if 4.3 volts is applied, this will raise the filter frequency approximately an octave" from the 3.3 V rest. [primary: DFM; scale factors derived here] These inputs are not temperature compensated: octaves per volt scale as 1/T (0.573 at 10 °C, 0.518 at 40 °C).
6. **Env Mod range and offset.** VR5 50 k(A): pin 3 is the MEG output, pin 1 goes through R61 10 k to the bus, so the wiper never delivers less than 1/6 of the envelope. Because the MEG rests below 3.3 V, turning Env Mod up also lowers the resting cutoff; the service notes call this the "gimmick" (SN p.8). [primary] Open303's measured mapping: total sweep 0.74 octave (cutoff low) to 0.86 octave (cutoff high) at Env Mod minimum, 4.51 to 5.06 octaves at maximum, with 29 to 34 % of the sweep below the nominal cutoff. [secondary: O303] The 6.1:1 ratio of those sweeps matches the 6:1 of R61 against the pot. Doc 03 owns the envelope voltages.

   | Knobs (cutoff, env mod) | Peak at note-on | Resting |
   |---|---|---|
   | min, min | 450 Hz | 270 Hz |
   | max, min | 3.55 kHz | 1.95 kHz |
   | min, max | 2.85 kHz | 125 Hz (antto read "about 70 Hz", unsure) |
   | max, max | 23.9 kHz (antto: 27 to 28 kHz) | 717 Hz (antto: 711 Hz) |

   [secondary: computed from O303 constants; antto's readings are lore]
7. **Ceiling.** Q11 saturates when I_f·R69 uses up its headroom, near 0.5 mA, which is f_res of about 30 kHz. antto recorded sweeps to 27 to 28 kHz and recommends 88.2 kHz capture. [secondary: derived here; lore]
8. **Keyboard tracking: none.** The pitch CV has no path to the filter. "In the TB-303 the filter frequency was not affected by the pitch of the note." [primary: SN p.5, DFM]
9. **Temperature.** For a fixed I_f the cutoff scales as 1/V_T: −0.33 %/K, about −5.8 cents/K.
10. Whittle notes a small slow tail after any large step of the filter CV: "99.8 % of the change occurs instantaneously ... residual drift towards the final frequency over the next 500 msec", from capacitors resistively coupled to the summing node (C13 through R72). [primary: DFM]

### 3.9 Where it saturates, and the squelch

All of these are tanh(v / 2V_T) junction pairs. Working levels in units of 2·V_T (51.7 mV) [secondary: derived here unless marked]:

| Stage | Drive at nominal level | Behaviour |
|---|---|---|
| Q12 input pair | saw 0.6, square 0.3, feedback up to 2 or more | tanh(0.6) = 0.54: the saw alone is compressed about 10 % at its peaks. Feedback from a 0.6 V ring at TP6 is 106 mV, where tanh is 0.97. This pair is the loop's limiter |
| Ladder junctions | same order as the input: (x_{j+1} − x_j) equals x_d at DC on every stage | each stage compresses along with the input; lower stages see larger absolute swings |
| Q21 output pair | top-capacitor voltage × λ; up to 0.6 with the filter wide open and no resonance | same tanh again. Its output then has asymmetric headroom: quiescent collector about 6.2 V by calculation (about 5.8 V if the SN's TP6 = 5.2 V annotation is taken literally), tanh limit ±5.8 V, but the collector bottoms near 3 V (Q21 emitters sit near 2.95 V). That leaves only 2.5 to 3 V of downward swing. With no resonance, an open filter and a low saw the collector swing reaches about ±2 to ±3.5 V, at or past that limit on the downward side |
| BA662 input (doc 03) | 35 mV for an open-filter saw with no resonance, 19 mV for a 0.6 V ring at full resonance | tanh(0.67) and tanh(0.36): 12 % and 4 % compression |

- Ring amplitude at TP6 is bounded near 0.5 to 1 V peak by the input pair, in line with the service sketch's 0.6 V. [primary: SN for the 0.6 V]
- "The resonance of the filter is quenched when the filter is driven by a heavy input signal: the filter hits its limits at the high and low extremes of the waveform and cannot resonate at these limits." [primary: DFM] In the model: incremental loop gain is k·sech²(x_d)·(the product of the ladder sech² terms). At the saw's extremes x_d is already 0.6, loop gain is down 30 %, and the resonance only rings freely as the ramp passes through the middle and just after the reset edge re-excites it. That edge-triggered, self-limiting ring is the squelch. A linear filter followed by a clipper does not produce it.
- The Devil Fish Overdrive pot raises the input to "66.6 times normal" [primary: DFM], which is R62 220 k replaced by 3.3 k. It is the same model with a larger input scale, and a direct test that the nonlinearity is in the right place: resonance must vanish under heavy drive.
- Even harmonics and envelope thump: the ladder diodes Q13 to Q17 and Q23 are six unmatched 2SC536F. V_BE mismatch of a millivolt or two unbalances the arms, which adds even-order distortion and lets cutoff changes leak into the audio as a DC shift that the 0.1 µF couplers turn into a thump. [secondary: derived here; no measurement]

### 3.10 Output coupling into the VCA

[primary: SN p.5, X0X, X0XW]

    V(BA662 pin 3) = V_TP6 · 2.2k · [ sC/(1 + sC·222.2k)  +  r · sC/(1 + sC·102.2k) ],   C = 0.01 µF

- Direct path: gain 0.0099, high-pass 71.6 Hz. Resonance-wiper path: gain 0.0215·r, high-pass 155.7 Hz. The service schematic's designator for the second capacitor is hard to read: the 150 dpi scan shows "C21 .01" next to R121 and what looks like "C22 .01" next to R122, but C22 is also the 1 µF at Q18's bias node on the same page. X0X and the RE-303 BOM (which keeps Roland's designators) make the pair C20 and C21, both 0.01 µF, with C22 the 1 µF part. [primary: SN, X0X, RE] Phoscyon's "C21/22" label probably follows the SN reading. [P2]
- A single 71.6 Hz pole is −7.8 dB at 32.7 Hz and −3.4 dB at 65.4 Hz.
- **Conflict.** Three measurement-based references put the effective corner near 45 Hz, not 72 Hz:
  - Whittle: "In the TB-303, 32 Hz was down by 5 dB" (reference level not stated). A single pole that is −5 dB at 32 Hz sits at 47 Hz. [primary: DFM]
  - Open303, fitted to recordings: 44.486 Hz high-pass before the filter, 24.167 Hz high-pass after, plus a 14 Hz all-pass and a 7.5 Hz notch. [secondary: O303]
  - Phoscyon 2's "Ideal TB303" calibration sets "C21/22" to 15.9 nF, which **if** its model uses the 222.2 k direct-path resistance is 45.0 Hz. [P2] Downgraded in verification: the Phoscyon manual describes this trim as "Cutoff frequency of the VCO's high pass filter" (p.9) and its GUI names it "DC Remover Frequency" (doc 01), so Phoscyon places it on the oscillator side, like Open303's 44.5 Hz pre-filter high-pass, not at the VCA input. The resistance in Phoscyon's model is unknown (15.9 nF = 0.1 µF/2π, suggesting C was derived from a target frequency), so 45 Hz is an inference, not a reading. Treat Phoscyon as weak, non-independent support (it may itself be fitted to Open303 or to the same lore).

  The schematic value is confirmed by two independent schematics and a clone BOM, so the parts are not in doubt. The effective response is. Adding the in-loop coupler (30 to 110 Hz, section 3.5) to a 72 Hz pole gives −10 to −18 dB at 32.7 Hz with no resonance (−13.5 dB with the coupler at 55 Hz), against −6.5 dB for Open303's fitted pair. I cannot reconcile these without a measured response from a real unit. See Open questions 1. Note that the low-loss end of that range is not available: an in-loop coupler below about 40 Hz makes the sub-audio loop unstable at full resonance (section 3.5), so the reconciliation, if any, has to come from the VCA coupler or from the fits measuring something different (Whittle does not state his reference level; Open303's fit is to one unit through the whole output chain).
- neotec's working model in the KVR thread used an output high-pass "which varies with q from 30 to 210 Hz" [lore]; a corner that rises with resonance is what the two-path mix produces.
- After the VCA: C38 1 µF into the volume pot and the mixer/output stage add corners of a few hertz (doc 03).
- Polarity: the filter core inverts and the output amp inverts again, so TP6 is in phase with the VCO signal at low frequencies. "The polarity of the main output signal is opposite that of the Filter Audio Out signal." [primary: DFM] Open303 negates its oscillator. Absolute polarity matters only for asymmetric stages and for users layering against hardware.

## 4. Trimmers, calibration targets, unit-to-unit variation

Service trimmers [primary: SN p.6-7 and parts list: 47 kB, 220 kB, 470 kB, 4.7 kB]:

| Trimmer | Value | Sets | Procedure target |
|---|---|---|---|
| TM1 | (tempo) | tempo clock top speed | 8 ms ± 1 ms period at TP1, TEMPO fully clockwise (not VCO/VCF) |
| TM2 | (int clock) | CPU clock | 1.8 ms ± 0.2 ms at TP2 (not VCO/VCF) |
| TM3 "FREQ" | 470 k | VCF cutoff scale (R73 + TM3 = 100 to 570 k) | ring period 2 ms ± 0.5 ms at TP6: cutoff centre, saw, resonance max, env mod / decay / accent min, low C |
| TM4 "TUNE" | 47 k | VCO reference current (offset) | A key = 110 Hz, TUNING at centre |
| TM5 "WIDTH" | 4.7 k | VCO volts-per-octave | 2:1 between low C and high C; confirm 4:1 ± 0.5 % with transpose up |
| TM6 "CV" | 4.7 k | 5.333 V reference, hence DAC volts per semitone | 1.000 V ± 3 mV per octave at CV Out |

Variation that matters, with plausible ranges. The "model parameter" column is what to expose in the calibration panel or an "instance" seed.

| Variation | Cause | Plausible range | Audible effect | Model parameter | Tag |
|---|---|---|---|---|---|
| Cutoff scale | TM3 setting, I_S11/I_S10 | spec allows 400 to 667 Hz at centre (−0.32 to +0.42 oct); trimmer itself spans 2.5 oct | whole knob range shifts | TM3, ±1 oct (Phoscyon: ±500 mV) | [primary] spec; range of trimmer derived |
| Cutoff knob span and low end | VR3 ±20 %, R47, V_BE | f_max/f_min 6.6 to 12.6 | how far the knob closes | ρ and v in the 3.8 formula, or a "knob low end" trim | [secondary: derived here] |
| Cutoff pot taper | A-taper law, mid-point 10 to 20 % | centre 0.20 to 0.29 of f_max | knob feel, parity of knob positions with hardware | taper mid-point | [secondary: derived here] |
| Maximum resonance | Q21 β (160 to 1000), tail current, R97/R98, R110/R111 5 %, core gain from device mixing | loop gain ±1.5 dB; onset of oscillation anywhere from 1.4 kHz to never | scream at the top of sweeps or not | resonance trim, 0.8 to 1.25 × k | [secondary: derived here] |
| Resonance fall-off at low cutoff | R_b = r_π(Q21) ∥ 100 k | 14 to 45 k physically; about 14 to 33 k if the unit must stay stable below 10 Hz at full resonance (section 3.5) | how much peak survives below 500 Hz | "R_b" or β(Q21) | [secondary: derived here] |
| In-loop coupler corner | 0.1 µF ± 10 % with R_b | 30 to 110 Hz physically; 45 to 110 Hz for sub-audio stability | bass at low resonance; low peak position and height | follows R_b; clamp so the sub-audio loop gain stays below 0.9 | [secondary: derived here] |
| VCA coupling corner | C20/C21 nominal 10 nF; fitted models suggest about 45 Hz effective (Open303 pre-filter 44.5 Hz; Phoscyon 15.9 nF on an unknown R) | 45 to 72 Hz direct path | bass weight | C21/C20 in farads, Phoscyon range 7.9 nF to 31.8 µF | [primary] parts; [secondary] fits |
| Square duty | saw top level (JFET I_DSS), 5.333 V and 12 V rails, R45, R36 | ±0.03 to ±0.05 absolute duty | square timbre, even harmonics | "PW shift": offset on the saw top or on V_E's target; Phoscyon ±100 % | [secondary: derived here] |
| Square threshold dynamics | C11 1 µF electrolytic, −10/+50 % new, drifting with age | τ 6 to 10 ms and 20 to 33 ms | droop of the square top, pitch-change blip | C11 | [secondary: derived here] |
| VCO scale | TM5 | spec ±0.5 % over two octaves; Phoscyon TM5 0.9 to 1.1 oct/V | tracking | TM5 | [primary], [P2] |
| VCO tune | TM4, TUNING pot | ±700 cents on the panel | tuning | tune | [primary] |
| Saw reset dead time | SCR storage time | 10 to 40 µs | high notes flat by up to 1 to 2 % before TM5 absorbs it | dead time | [lore], size derived |
| Ladder mismatch | six unmatched 2SC536F | 0 to 3 mV per stage | even harmonics, envelope thump | per-stage offset in x_j | [secondary: derived here] |
| Temperature | V_T | 10 to 40 °C | cutoff −0.33 %/K, env/accent depth −0.33 %/K | temperature | physics |

## Measurable acceptance targets

Each target is something an offline test can compute from the model's output. "Derived" targets come from the simulations in this doc and carry wide tolerances on purpose; tighten them only against hardware recordings.

### VCO

| # | Property | Target | Tolerance | Source |
|---|---|---|---|---|
| V1 | Pitch map at tune centre | MIDI 45 = 110.00 Hz; MIDI 24/36/48/60/72 = 32.70/65.41/130.81/261.63/523.25 Hz | ±1 cent with drift and dead time disabled | SN p.5, p.7 |
| V2 | Tracking with "analog" options on | 65.41 to 261.63 Hz ratio 4:1 | ±0.5 % | SN p.7 |
| V3 | Tuning knob range | ±700 cents | ±50 cents | SN p.1 |
| V4 | Saw polarity and shape at the VCO node | linear fall, fast rise; rise time under 40 µs; deviation from a straight ramp under 5 % of p-p outside the first 5 % of the cycle | as stated | SN sketch; section 2.4 |
| V5 | Square low level | flat within 1 % of p-p | | section 2.5, derived |
| V6 | Square duty (fraction high; high = the part ending at the saw reset) | C1 0.57, C2 0.50, A2 0.46, C3 0.45, C4 0.41, C5 0.35 | ±0.06 absolute; strictly decreasing with pitch | derived; direction and ends agree with Whittle via Olney and SN sketch |
| V7 | Square top droop | exponential toward the low level, τ 6.9 ms; at C2 the top loses 25 % ± 8 % of p-p over the high time; under 5 % at C5 | as stated | derived |
| V8 | Edge asymmetry | reset-side edge under 40 µs; turn-on edge 10-90 % = 1.0 % ± 0.4 % of the period for C3 to C6, 1.4 % at C2, over 4 % at C1 | as stated | derived; aciddose SPICE (lore) for the 1/f scaling |
| V9 | Square H2/H1 at the VCO node | C1 −14 dB, C2 −21 dB, A2 −18 dB, C4 −11 dB, C5 −7 dB | ±4 dB | derived |
| V10 | Level ratio | square p-p / saw p-p = 0.58; fundamentals equal | ±0.08; ±1.5 dB | SN sketch gives 0.46; derived 0.58; O303 uses 0.5 |
| V11 | Pitch-jump transient, C2 to C4 legato | duty undershoots below its settled value on the first cycles and is within 0.005 of settled after 30 ms | ±15 ms | derived; aciddose "blips of 50 ms" (lore) |
| V12 | Free-running phase | oscillator phase continuous across note-on and gate | exact | schematic; antto |

### VCF (small-signal tests at −60 dB input unless stated)

| # | Property | Target | Tolerance | Source |
|---|---|---|---|---|
| F1 | Order | slope between 16·f_res and 32·f_res = −24 dB/oct | ±0.5 dB/oct | TS1 |
| F2 | Core shape, resonance 0, coupling corners disabled | magnitudes of the table in 3.2 at 0.25, 0.5, 1, 2, 4 × f_res: −8.5, −15.2, −24.6, −38.3, −57.0 dB | ±0.5 dB | TS1 polynomial |
| F3 | −3 dB point at resonance 0 | 0.105 × f_res | ±5 % | derived from TS1 |
| F4 | Local slope | −13.7 dB/oct from f_res to 2·f_res, −18.6 dB/oct from 2 to 4·f_res, resonance 0 | ±0.5 dB/oct | derived from TS1 |
| F5 | Oscillation threshold of the bare core | k = 17.0 (ideal half capacitor) or 17.02 (18 n / 33 n), at f_res | ±1 % | TS1 algebra |
| F6 | Current to frequency | f_res = 64.5 Hz/µA at 300 K with 18 n / 33 n | ±1 % | derived from TS1 |
| F7 | Passband drop with resonance, f_res about 1 kHz, measured at 200 Hz, VCO to TP6 | −14.3, −18.6, −21.6, −23.8, −25.6 dB at 20, 40, 60, 80, 100 % rotation | ±1.5 dB | TS2 blocks |
| F8 | Zero-resonance response, f_res 975 Hz, VCO to TP6 | −5 dB broad maximum near 100 Hz; −7.3 dB at 200 Hz | ±1.5 dB (depends on the coupler corner chosen) | TS2 |
| F9 | Peak at full resonance, f_res 975 Hz | 16 dB above the 200 Hz plateau | +16 to +30 dB accepted; default build should land at +19 ± 3 dB | TS2 component-level plot; loaded model |
| F10 | Peak frequency vs resonance | 0.75·f_res at half of critical feedback, 0.92·f_res at 0.8 | ±5 % | derived |
| F11 | Low peak | 10 to 15 dB bump between 8 and 13 Hz at 60 to 100 % resonance; no oscillation there | present, stable | TS2 |
| F11b | Sub-audio stability margin (added in verification) | for every shipped calibration preset, at full resonance and any cutoff, the small-signal loop gain at the 5 to 10 Hz 180° crossing stays below 0.9; no free-running output below 20 Hz with zero input | hard requirement | TS2 blocks; section 3.5 |
| F12 | Resonance vs cutoff, pot at maximum | peak above passband: under 6 dB at f_res 150 Hz, 5 to 10 dB at 250 Hz, 11 to 18 dB at 500 Hz, 19 to 31 dB at 1 kHz | as stated | loaded model; DFM qualitative |
| F13 | Self-oscillation, no input, default calibration | none for f_res below 1.2 kHz at any resonance setting | hard requirement | DFM, antto |
| F14 | Cutoff knob range, env contribution removed | 314 Hz to 2394 Hz | ±15 % at each end; ratio 6.6 to 12.6 accepted across "unit" presets | O303 measured; DFM "about 2.5 kHz" |
| F15 | Factory calibration reproduces | knob 50 %, resonance max, env mod min, saw at C2: ring period 2 ms | ±0.5 ms | SN p.7 |
| F16 | Env Mod extremes | sweep 0.74 to 0.86 oct at minimum, 4.5 to 5.1 oct at maximum; 29 to 34 % of it below nominal; the four corner cases of the table in 3.8 | ±10 % in octaves | O303 measured |
| F17 | Ceiling | f_res reaches at least 24 kHz with cutoff and env mod at maximum, without instability at 44.1 kHz host rate | pass/fail | antto; O303 |
| F18 | No keyboard tracking | f_res independent of note | exact | SN, DFM |
| F19 | Ring amplitude, large signal | saw at C2 (6 V p-p equivalent), knob 50 %, full resonance: ring peak at the TP6-equivalent node = 0.10 × saw p-p | ±40 % | SN sketch: 0.6 V on 6 to 6.5 V |
| F20 | Ring decay in the same test | visible ring under 5 cycles before the next edge | 3 to 8 cycles | SN sketch |
| F21 | Resonance quench under drive | with input raised 36 dB (Devil Fish overdrive), peak-to-plateau ratio at full resonance falls by at least 15 dB compared with nominal drive | pass/fail | DFM |
| F22 | Output mix into the VCA | above 500 Hz, VCA drive rises by 10.0 dB from resonance 0 to 100 % for the same TP6 level; direct-path corner 45 Hz (default) or 71.6 Hz (schematic preset); wiper-path corner 2.17 × the direct corner | ±0.5 dB, ±5 % | SN, X0X; fits |
| F23 | Bass loss with resonance, VCO to VCA input, f_res 975 Hz | 110 Hz falls 12.8 dB and 65 Hz falls 12.9 dB from resonance 0 to 100 % | ±2.5 dB | derived from TS2 + SN |
| F24 | Linearity of resonance control | loop gain at the resonant peak within 20 % of proportional to rotation, sagging most at mid rotation (wiper loaded by about 51 k at audio frequencies) | as stated | SN (50 kB), DFM; loading corrected in verification |

## Conflicts between sources, and which side this doc takes

| Topic | Positions | Verdict |
|---|---|---|
| Filter order | Roland marketing and the x0xb0x wiki: 18 dB / 3-pole. Stinchcombe: 4-pole | 4-pole, −24 dB/oct asymptote, −14 to −19 dB/oct in the first two octaves above the peak |
| Ladder capacitors | Seed report: "values unverified". SN and X0X: 0.018 µF and 3 × 0.033 µF | Verified. Ratio 0.545, not 0.5 |
| Q21 collector loads | 150 dpi SN scan reads "2.2K". Stinchcombe's scan and X0X read 22 k; gain of 106 needs 22 k | 22 k |
| Cutoff knob law | Open303: exponential in rotation. Schematic: linear current through a log pot | Schematic. Offer the exponential law as a UI option only |
| Cutoff pot taper | Seed and some clone docs: linear. SN parts list and X0X: 50 k(A) | Audio taper |
| Feedback high-pass | Open303: 150 Hz on the feedback signal. Circuit and Stinchcombe: 0.1 µF couplers in the forward path inside the loop, 92 Hz in his fit; feedback-only corners are all below 16 Hz | Circuit. Expect the bass to stay down at high resonance |
| Self-oscillation | Seed: "sits below threshold". Stinchcombe's ideal blocks: 18.7 against 17 needed. Component-level SPICE and Whittle: short of oscillation at mid cutoff | Cutoff-dependent: well short at low cutoff, at the edge above 1.5 to 3 kHz |
| Resonance pot value (accent gang) | Whittle's 1999 post: "100k pot". SN parts list, X0X: 50 kB × 2 | 50 k |
| Resonance taper | Open303: exponential skew. SN, DFM: linear pot | Linear, with the loading bow (11 % at low frequency, up to 20 % at the resonant peak) |
| Square duty | Open303: fixed, pitch-independent baked table; `-tanh(70·saw + 4.37)` on a ±1 saw switches at saw = −0.062, so about 47 % (corrected in verification from "50 %"). Whittle via Olney: 71 % low to 45 % high. Circuit simulation: 57 to 62 % at C1, 41 to 45 % at C4, 35 to 39 % at C5 | Pitch-dependent, from the Q8 model |
| Shaper gain | Seed: 20.6× or 36.9 dB tanh. Circuit: saturating switch, floating threshold | Circuit |
| Pre-filter high-pass | Open303: 44.5 Hz before the filter. Circuit: 0.72 Hz | Circuit; the audible corners are after the ladder |
| VCA coupling corner | Schematic: 71.6 Hz. Whittle, Open303, Phoscyon: about 45 Hz effective (Phoscyon's figure is an inference and it places the high-pass on the VCO side) | Unresolved (Open question 1) |
| Transistor types | X0X: 2SC536F. SN p.4 note: all main-board NPN 2SC945(P), PNP 2SA733(P); RE-303: "2SC536F or 2SC945P" | Production varied; not a modelling parameter beyond the mismatch trims |

## Open questions

1. **Low-frequency response of a stock unit at zero resonance.** Schematic values give two stacked high-passes (in-loop coupler 30 to 110 Hz, VCA coupler 71.6 Hz); Whittle's −5 dB at 32 Hz, Open303's fitted 44.5 Hz + 24 Hz pair and Phoscyon's 45 Hz default all show about half that loss. Handling: model both corners physically and expose them (C20/C21 in farads, R_b in ohms). ~~Ship two calibration presets: "Measured" (C20 = C21 = 15.9 nF and a coupler corner of 26 Hz, equivalent to R_b = 60 k ...) as the default, and "Schematic nominal" (10 nF, R_b = 28 k).~~ **Corrected in verification:** a 26 Hz in-loop coupler (R_b = 60 k) makes the sub-audio loop unstable at full resonance (loop gain 1.29 at 5.6 Hz in both TS2's block model and the loaded model, section 3.5), so that preset would motorboat. The bass deficit cannot be closed from inside the loop. Ship "Measured" as C20 = C21 = 15.9 nF (45 Hz direct-path corner) with R_b no higher than about 30 k (coupler about 50 Hz, sub-audio loop gain under 0.85), and accept the residual extra loss at 33 Hz relative to Open303 until a recording settles it; keep "Schematic nominal" as 10 nF with R_b = 28 k (sub-audio loop gain 0.75 to 0.83). Both must pass F11b. Settle it by recording a real 303, RE-303 or x0xb0x: saw, C1 and C2, cutoff fully open, resonance at zero, and fit a two-pole high-pass to the spectrum of the harmonics.
2. **β of Q21 (2SC1583) and of the SPICE model Stinchcombe used.** It sets both the coupler corner and how fast resonance fades at low cutoff. Handling: one "R_b" trim, 14 k to 45 k for physical values. ~~extended to 100 k as a fit parameter~~ (corrected in verification: above about 35 k the sub-audio loop goes unstable at full resonance, so cap the trim near 35 k, or let it go higher only if the sub-audio loop gain is separately limited, and enforce F11b). Stability also argues for a lowish β in real units: a stock unit with β(Q21) much above 500 would be near motorboating at full resonance, which nobody reports. Whittle's remark that the filter "will only self oscillate at mid and high frequencies" wants a visible fade, which the low-β end gives. If recordings show strong fade and little bass loss, the extra bass has to come from outside the loop (VCA coupler), not from a lower in-loop corner; a separate multiplier on the coupler capacitance does not help because it raises the sub-audio loop gain the same way.
3. **Absolute maximum loop gain.** 18.7 from Stinchcombe's blocks, 19.4 from my bias-point arithmetic, about 0.7 of critical from Whittle's two-o'clock remark, +16 dB peak in the component-level SPICE plot. Handling: resonance trim with default chosen so that F9 reads +19 dB and F13 holds; tune against recordings of full-resonance sweeps.
4. **Saw top level and JFET compression.** The square's duty moves 0.05 for 0.5 V of saw top. No scope capture of Q28's source on a stock unit was found; the service sketch says "12 V", which the follower cannot reach. Handling: saw top is a parameter (default 11.3 V equivalent), mapped to the "PW shift" trim. Note the section 2.5 duty table and V6 were simulated with an 11.5 V top (re-run in verification: C2 0.497, C4 0.410, and 0.392 at C4 with a 12 V top, matching the table); at the 11.3 V default expect duties about 0.02 higher, still inside V6's tolerance. Pick one value and re-centre V6 on it.
5. **Reset dead time and flat top.** Predicted by Czech, never measured. Handling: parameter, 20 µs default, compensated in the default tuning so V1 holds.
6. **Cutoff pot taper curve.** Only "50 kA" is known. Handling: A(x) = (b^x − 1)/(b − 1) with b = 32 (15 % at centre) as default; expose the mid-point. Check against F15: a unit calibrated to 500 Hz at centre must still reach 1.7 to 2.5 kHz at maximum.
7. **Core gain with mixed device types.** Stinchcombe saw up to 6 dB with diodes at the top; the 303's 2SC2291 top pair against 2SC536F diodes and a 2SC1583 input pair is a milder case, size unknown. Handling: fold into the resonance trim.
8. **Where the original "45 % / 71 %" duty statement comes from** and how it was measured (which polarity, which node, which notes). Handling: keep V6's tolerance wide until a recording of the raw square is analysed; the Borg/x0xb0x raw-oscillator taps mentioned on KVR are the easiest source.
9. **Q21's output clipping at zero resonance.** The arithmetic says an open filter and a low saw push Q21's collector close to its lower limit. No source mentions audible clipping there. Handling: implement the tanh and a soft lower bound; check that the zero-resonance open-filter saw stays under 3 % THD, and revisit if it sounds wrong.
10. **Low-frequency secondary peak in real units.** Confirmed by Stinchcombe on a clone only. It is below the lowest note and audible mostly as envelope "bounce" after cutoff jumps. Handling: it comes for free if blocks 3, 4 and 5 are implemented; add a switch to disable it and compare.
11. **Final output polarity** relative to the VCO node through VCA, mixer and output stages. Handling: doc 03 traces the later stages; fix polarity by matching a hardware recording's reset-edge direction.

## Implications for the build

1. Carry volts. Generate the saw as a voltage (default 5.5 V to 11.3 V, falling, fast rise) and feed the filter v_in/101. All filter nonlinearities are tanh(v / 2V_T) with 2V_T = 51.7 mV; no separate "drive" constant is needed for stock behaviour, and Devil Fish overdrive is a multiplier on v_in (up to 66.6).
2. One oscillator, free-running phase, never reset on note-on. Band-limit the reset edge (polyBLEP or minBLEP) using the actual step height.
3. Build the square from the Q8 circuit model in 2.5 (two states: V_E on C11 and the C10 voltage; two algebraic unknowns), run inside the oversampled loop. Do not use a fixed-duty pulse, a static tanh, or a wavetable. The reset-side edge needs BLEP correction with step height V_C(just before reset) − 5.333 V; the turn-on edge is slow enough (1 % of the period) to leave to oversampling. A behavioural equivalent is acceptable if it passes V6 to V11.
4. Level-match the two waveforms by construction (square 5.33 V to about 8.8 V, saw 6 V p-p) and let the common 1/101 divider follow. Do not normalise them to equal peak.
5. Implement the filter as the five-equation tanh system in 3.3 with ω = I_f/(4·V_T·C) and the first-stage factor 33/18 = 1.833. Solve it implicitly (Newton on the four states, or a one-step-delay-free linearised solve) at 4× oversampling or more; 8× for the top quality tier, since f_res reaches 24 kHz.
6. Tune by the resonant peak, never by a −3 or −6 dB point. f_res = 0.2203·ω, so set ω = 4.540·f_res (ω in rad/s, f_res in Hz; 4.443·f_res for an ideal half capacitor). The earlier project's finding that its ZDF core "halved the corner" compared −6 dB points of two differently scaled cores; with zero resonance the −3 dB point of this filter is at 0.105·f_res by design (F3).
7. Close the loop through the real path: output amp gain 106 to 110 with its own tanh, loading factor λ = R_b/(R_b + a) and in-loop coupler high-pass 1/(2π·0.1 µF·(R_b + a)) with a = 2V_T/I_f, then HP 3.18 Hz, pot fraction with loading, ×0.98, HP 1.18 Hz, block 5, into the input pair's other base. Feedback gain constant 18.7 to 19.4 before λ, scaled by the resonance trim.
8. Do not put a 150 Hz high-pass on the feedback signal as Open303 does. If an "Open303 voicing" is wanted for comparison, make it a separate filter mode.
9. Resonance knob: linear in rotation with the loading bow (wiper loaded by R67 + R99 and by the R122 VCA path, about 51 k at audio frequencies). No exponential skew.
10. Cutoff knob: linear-in-Hz law through an audio-taper curve, formula in 3.8 item 1, defaults f_min 314 Hz and f_max 2394 Hz. Offer "exponential knob" as a UI preference that changes only the mapping from knob position to the same range.
11. Sum envelope and accent in the exponent with scale factors 0.541 and 1.19 octaves per volt relative to a 3.3 V bus, so that doc 03's voltages plug in directly. Keep the 1/6 Env Mod floor and the downward shift of the resting cutoff as Env Mod rises.
12. No keyboard tracking in stock mode. A Devil Fish style tracking control should be linear in Hz per volt, injected with the cutoff knob's current.
13. Cap f_res at about 30 kHz by modelling Q11's saturation softly (I_f limited near 0.5 mA) and by the oversampled Nyquist, whichever is lower.
14. VCA input mix: direct path through C21/R121 plus resonance-wiper path through C20/R122 into 2.2 k, then the BA662 tanh. This is the resonance make-up gain (+10 dB at high frequencies) and a resonance-dependent high-pass. Expose C20/C21 as one capacitance in farads with Phoscyon's range for parity.
15. Calibration panel mapping: TM3 (cutoff scale, ±1 octave), TM4/TUNE, TM5 (0.9 to 1.1 oct/V), C20/C21 (farads), R_b or "Q21 gain" (resonance fade and coupler corner; cap near 35 k or guard with F11b), resonance trim (0.8 to 1.25), PW shift (saw top level, ±0.6 V), C11 (square threshold time constants), reset dead time, ladder mismatch (0 to 3 mV), temperature (10 to 40 °C, moving V_T everywhere at once). Ship named presets including "Schematic nominal", "Measured (Open303 unit)" and a "hot" unit that oscillates above 1.5 kHz. Every preset must pass F11b (no sub-audio motorboating at full resonance).
16. Unit-to-unit variation should be a seeded draw over the table in section 4, not independent random detune. Default instance: nominal parts, drift off.
17. Write the acceptance tests in the previous section as offline unit tests against the DSP core before any ear tuning. F1 to F6 and V1 are exact and should gate every change; the derived targets gate regressions and get re-centred when hardware recordings arrive.
18. Record or obtain three reference captures early, because they resolve Open questions 1 to 4 at once: raw saw and square at the VCO node at C1, C2, C4 (x0xb0x or RE-303 is sufficient for the circuit, a TB-303 for confirmation); saw through the filter at zero resonance, cutoff maximum, C1 and C2; full-resonance sweeps of the cutoff knob with no envelope, with and without input.

## Verification log

Adversarial fact-check pass, 2026-10-02. Each claim was re-checked against the source named, opened directly (SN scans in `scratchpad/refs/web/sm/`, X0X part strings, RE-303 BOM, Open303 source, Stinchcombe's pages and PDF fetched fresh, Devil Fish manual text, KVR captures). Derived values were recomputed independently (`scratchpad/fc02/lad.py`, `lad2.py`, `ts2.py`; the author's `shaper.py` and `vcf_loaded.py` re-run).

| # | Claim | Verdict | Source checked |
|---|---|---|---|
| 1 | Ladder caps C18 0.018 µF, C19/C24/C26 0.033 µF; C25/C27 0.1 µF; C17, C14, C15, C23 1 µF | confirmed | SN p.5 scan; X0X strings; RE-303 BOM |
| 2 | Input divider R62 220 k / R70 2.2 k = 1/101; C17 corner 0.72 Hz | confirmed (arithmetic 0.717 Hz) | SN, X0X |
| 3 | H_tb = −1/(8p⁴+32p³+40p²+16p+1), normalised coefficients 6.727/14.142/9.514, poles −0.13/−1.04/−2.33/−3.24 | confirmed; ladder ODEs of 3.3 re-derived symbolically and reproduce the polynomial | TS1 text; sympy |
| 4 | k_osc = 17 at f_res = 2^(1/4)·ω_c; 18 n/33 n: f_res = 0.6919/(2πaC), k_osc 17.02, 64.5 Hz/µA at 300 K (68.4 / 61.9 at 283 / 313 K), f_res = 0.2203·ω | confirmed (0.69190, 17.019, 64.54) | recomputed |
| 5 | Zero-resonance magnitude/slope table, −3 dB at 0.105·f_res | confirmed to 0.05 dB | recomputed |
| 6 | Closed-loop table (k/k_osc 0.25 to 0.95) | confirmed; peak frequency at 0.6 corrected 0.83 → 0.82 | recomputed |
| 7 | Stage gradients −6.1/−11.9/−17.9/−24.1 dB/oct; "apparently even Roland themselves" 18 dB; "closely equivalent to shifting the whole response"; 4th pole matters in-band | confirmed | TS1 |
| 8 | TS2 full transfer function and the k = 0..1 level table at f_c = 820 Hz (−9.3/−5.6/−5.1/−7.3 at k = 0, +4.2 dB at 1034 Hz at k = 1, low peak −10.2 dB at 8.7 Hz) | confirmed by direct evaluation of TS2's closed form | TS2; `ts2.py` |
| 9 | Stinchcombe Laplace vs component-level plot: peak +3 vs −9 dB, "12 dB gap is the loading" | numbers confirmed by pixel measurement (27 vs 16 dB above plateau); attribution downgraded to this doc's hypothesis, panels unlabelled, Stinchcombe calls agreement "tolerable" | TS2 GIF `tb303_lap_v_comp.gif` |
| 10 | Mixed device types shift passband by about 6 dB (1N4148 top pair) | confirmed | TS3 section 3.4 |
| 11 | Loaded-model resonance table (β 160 / 400, f_res 120 Hz to 8 kHz) | confirmed (re-run) | `vcf_loaded.py` |
| 12 | "Measured" preset: in-loop coupler 26 Hz / R_b 60 k as default | **corrected**: unstable sub-audio loop (gain 1.29 at 5.6 Hz); R_b capped near 35 k, coupler ≥ 40 to 45 Hz; F11b added | TS2 blocks with modified coupler; `vcf_loaded.py` |
| 13 | Feedback divider 1/5.55, max loop gain 106 × 0.98/5.55 = 18.7 | confirmed (18.72) | TS1/TS2 term "18.7ks⁴" |
| 14 | Resonance pot loading bow "within 11 %" | **corrected**: R122 VCA path also loads the wiper; up to 20 % at audio frequencies | SN, X0XW ("through a .01uF cap and 100K resistor"), arithmetic |
| 15 | R110/R111 22 k (scan reads 2.2 k) | confirmed, with new evidence: SN's TP6 = 5.2 V DC is only possible with 22 k | SN p.5, X0X, RE-303 BOM |
| 16 | Q20 tail 0.526 mA | confirmed (5.333 V × 20/30 = 3.56 V node, half of it on Q20's base, 1.18 V across 2.2 k = 0.535 mA less base currents) | SN p.5 |
| 17 | VCA coupling C20/C21 0.01 µF, R121 220 k, R122 100 k, R124 2.2 k; 71.6 Hz and 155.7 Hz; +10.0 dB HF make-up; net bass loss 12.8 dB at 110 Hz | confirmed (71.63, 155.73 Hz; 10.03 dB; 12.8 dB) | SN scan, X0X, RE-303 BOM, arithmetic |
| 18 | Phoscyon "C21/22" = 15.9 nF = 45 Hz VCA coupler | **downgraded**: Phoscyon manual calls it "the VCO's high pass filter"; resistance unknown, 45 Hz is an inference | Phoscyon 2 manual p.9; doc 01 |
| 19 | Open303 constants: 44.486 Hz HP1, 24.167 Hz HP2, 14.008 Hz all-pass, 7.5164 Hz notch, 150 Hz feedback HP, c0 313.8 / c1 2394.4 Hz, k = 17/4 scaling, skew (1−e^(−3r))/(1−e^(−3)), shape() commented out, square ×0.5, output g·(1+r) | confirmed | `rosic_Open303.cpp`, `rosic_TeeBeeFilter.h`, `rosic_BlendOscillator.h`, `rosic_MipMappedWaveTable.cpp` |
| 20 | Open303 square "fixed 50 %" | **corrected** to fixed ≈ 47 % (tanh switch point at saw = −0.062) | `rosic_MipMappedWaveTable.cpp` |
| 21 | Square shaper parts R45 22 k, C11 1 µF, R35 100 k, R34 10 k + C10 0.01 µF, R36 10 k to 5.333 V; τ_off 22 ms, τ_on 6.9 ms; duty table | parts confirmed; τ_on 6.875 ms, V_E target 7.42 V ideal (7.45 with V_CEsat); duty at C2/C4 re-simulated (0.497/0.410) | SN scan crop, X0X, RE BOM; `shaper.py` re-run |
| 22 | Pitch anchors: A key 110 Hz at 2.75 V, CV 1-5 V, TUNING ±700 cents, 9 ms key A, 15 ms key C, TM6 1.000 V ± 3 mV, 4:1 ± 0.5 %, TM3 ring 2 ms ± 0.5 ms, TM1 8 ms ± 1, TM2 1.8 ms ± 0.2 | confirmed | SN p.1, p.5, p.6-7 (OCR text and scans) |
| 23 | Expo converter scale 16.3 to 18.8 mV/V across TM5, TM5 ≈ 1.5 k for 1 V/oct; R100 "POSISTOR" | confirmed (16.25 / 18.82 mV/V, 1.49 k) | SN scan, X0X, arithmetic |
| 24 | Cutoff summing: 9.69 mV/V (Env Mod), 21.3 mV/V (accent), 0.541 / 1.19 oct/V; Q9 bus 3.3 V; VR3 50 k(A), TM3 + R73, R47 10 k; range ratio 9.0 | confirmed (topology traced on SN scan; arithmetic reproduced) | SN p.5, X0X |
| 25 | Devil Fish quotes: 32 Hz down 5 dB; 66.6× overdrive; ≈ 5 kHz, "an octave above the typical maximum"; double feedback, self-resonance past about 2 o'clock; "only self oscillate at mid and high frequencies"; quench under heavy drive; 99.8 % / 500 ms; 4.3 V ≈ +1 octave; linear "volts to Hertz"; no keyboard tracking; tuning knob has no effect on CV Out or filter; output polarity opposite Filter Audio Out; 1-4 V transposable to 2-5 V; 0.75 to 5.25 V; resonance pot "dual 50k ... linear" | confirmed verbatim | DFM text (`dfman.txt`, `303-unique`, index) |
| 26 | KVR: antto 14.7 Hz hpf1, "99.9 % plain saw", "quite close to self oscilation", 3.2-3.3 kHz, 711 Hz, "70Hz" unsure, hpf2 scheme; Schmidt "lower half-wave ... higher amplitude"; Stinchcombe "Correct." | confirmed | KVR Wayback captures (`thread_all.txt`, `later_pages.txt`) |
| 27 | aciddose: 659 Hz at 5.333 V, "50 ms blip", "100x as wide", SPICE netlist values | unverified (summarised page only; no archive capture retrievable) | Wayback availability API returned nothing |
| 28 | x0xb0x wiki: "4th pole staggered up an octave", "peak stays at the same volume" | confirmed as quoted (and the wiki also misstates C18 as "0.18 uF") | X0XW Wayback capture |
| 29 | Transistor types "2SC536F" | qualified: SN says 2SC945(P)/2SA733(P), alternates 2SC536F/2SC2021R/2SA937Q | SN p.4 note, p.7 figure, parts list; RE-303 BOM |
| 30 | Whittle's "45 % / 71 %" duty statement | unverified (still not located; a targeted search found only antto's KVR figures) | web search; KVR p=3732152 |
| 31 | Ideal-junction I_f-to-frequency law holds to the top of the range | qualified: Stinchcombe's SPICE shows the ideal model reads high at large I_f; note added in 3.2 | TS3 section 3.4 |
