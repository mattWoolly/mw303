# 04: DSP modeling of the TB-303 voice

State of the art for each block, and one recommended algorithm per block that can be coded from this page.

Tags: **[primary]** schematic, service manual, measurement, vendor manual, source code. **[secondary]** reputable analysis. **[lore]** forum consensus, unverified. Numbers I derived or simulated for this document carry **[derived]**; treat them as secondary until checked against SPICE or hardware. Section 6 says how.

## Summary

The filter is the only hard part, and it is smaller than its reputation. The whole VCF core is four capacitor voltages and five tanh functions, and the physical equations (section 1.1) are short enough to solve exactly every sample. A prototype of the recommended solver (trapezoidal integration, Newton on a 4×4 tridiagonal-plus-corner system) converged in 2 to 3 iterations on every test, never failed, and cost 225 ns per oversampled sample on a 2016 Xeon: 4.3 % of one core at 4×48 kHz. For a monophonic synth that is cheap. There is no reason to ship a fitted approximation.

Three findings change what the seed material says:

1. **Stinchcombe's pole and zero constants are in rad/s, not Hz.** His own SPICE block diagram gives them in Hz: 92, 15.5, 6.16, 3.18, 1.18 and 0.708 Hz. The seed report's "poles near 578, 97.5, 38.5, 20, 7.41, 4.45 Hz" is wrong by 2π. The high-pass inside the resonance loop sits at 92 Hz (Stinchcombe, SPICE) or about 100 Hz (a1k0n, measured on an x0xb0x). Open303's 150 Hz is a fitted stand-in.
2. **The bottom ladder capacitor is 18 nF against 33 nF, a ratio of 1.83, not 2.** Self-oscillation then sits at loop gain 17.02 and at 1.384 × f0 instead of 17 and √2 × f0. Small, but free to get right.
3. **The filter's unresonated −3 dB point is 3.25 octaves below its resonant frequency.** Any code that calls the −3 dB point "cutoff" will sound a factor of 9.5 too dark or too bright. The earlier project's ZDF A/B result ("ZDF opens less") was this definition mismatch plus a Moog-scaled feedback gain (k ≤ 4.3 where a diode ladder needs 17), not a property of ZDF.

Recommended voice, in one line each:

| Block | Algorithm | Rate |
|---|---|---|
| Oscillator core | Phase accumulator, linear ramp in volts, static JFET map, 2-point polyBLEP on the reset step | oversampled |
| Square | 2-state nodal model of the Q8 shaper (C10, C11), Newton on 2 unknowns, polyBLEP on the reset step with the step height computed from the circuit | oversampled |
| Filter | Nonlinear TPT diode ladder, 18 nF / 33 nF, Q21 load and coupling state, feedback network as TPT one-poles inside the zero-delay solve, Newton | oversampled |
| VCA | `I_abc · tanh(v/2V_T)` evaluated in the oversampled domain, before decimation | oversampled |
| Envelopes, accent, slide | Exact exponential updates of each RC state, piecewise-linear diode switching | base rate, interpolated |
| Decimator | Two cascaded polyphase IIR half-bands (3 and 8 allpass coefficients), no upsampler anywhere | 4× to 1× |

## 0. Sources and licenses

Read in full (cloned or downloaded):

| Source | What | License |
|---|---|---|
| Open303, `github.com/RobinSchmidt/Open303` (HEAD of 2024-03-29) | `rosic_TeeBeeFilter`, `rosic_Open303`, `rosic_MipMappedWaveTable`, `rosic_BlendOscillator`, envelopes, `rosic_EllipticQuarterBandFilter` | MIT |
| JC303, `github.com/midilab/jc303` (HEAD of 2026-09-21) | `dfl_DiodeLadderFilter.h`, diff of `rosic_Open303.h`, `JC303.cpp` mod ranges | GPLv3; the vendored Open303 files stay MIT |
| sst-filters (Surge XT), `include/sst/filters/DiodeLadder.h` | Linear ZDF diode ladder adapted from Odin 2 | GPLv3 |
| LMMS `plugins/Lb302/Lb302.cpp` | 3-pole filter with tanh feedback | GPLv2+ |
| a1k0n/303, `github.com/a1k0n/303` | `303.js`, `filt303.py`: pole fits and control-current measurements from the author's x0xb0x | No license file. Facts only; copy nothing. |
| Tim Stinchcombe, `timstinchcombe.co.uk/index.php?pge=diode` and `pge=diode2`, including the block-model and component-level schematic images | Transfer functions, SPICE models | © author |
| Zavalishin, *The Art of VA Filter Design* rev. 2.1.2, sections 5.10 and 6.9 (`archive.org/download/the-art-of-va-filter-design-rev.-2.1.2/VAFilterDesign_2.1.2.pdf`) | Diode ladder equations, TPT solution, nonlinear form | Free distribution |
| x0xb0x wiki (Wayback), `ladyada.net/wiki/x0x/voltagecontrolledoscillator` and `waveshaper.png` | VCO core notes (Martin Czech), waveshaper schematic | CC BY-SA 3.0 |
| Robin Whittle, `firstpr.com.au/rwi/dfish/303-unique.html`, `303-slide.html` | Accent sweep, slide timing | © author |
| Kernmayer, *The Sound Of Acid* (FH St. Pölten thesis, 2020), `phaidra.ustp.at/api/object/o:4344/download` | Max/MSP emulation built on Zavalishin's model; reports the model "reaches its limits" at high cutoff with high resonance | thesis |
| Earlier project: `ref-mwAudio303/libs/dsp-core`, spike and ZDF A/B findings | See 1.5 | internal |
| `docs/research/02-tb303-vco-vcf-circuit.md` | Circuit-level VCO and VCF: service-note values, levels, the Q8 shaper, the Q21 load. Its conclusions and this document's were reached separately and agree; where they overlap, its component values win | internal |
| `docs/research/03-tb303-envelopes-accent-slide-vca.md` | Component values and reference model for envelopes, accent, slide, VCA | internal |

Not reached, and what stands in:

- KVR forum threads sit behind a bot wall. Only summaries of karrikuh's "Diode ladder filter" thread (`kvraudio.com/forum/viewtopic.php?p=4878962`) and mystran's "Cheap non-linear zero-delay filters" (`viewtopic.php?p=4932123`) were available, not the posted code. Claims from them are tagged [lore].
- Will Pirkle's AN-6 PDF would not download for the author. JC303's `dfl_DiodeLadderFilter.h` and the Surge/Odin code both implement it and were read instead. *(Verification pass: the Wayback capture `web.archive.org/web/2016/http://www.willpirkle.com/Downloads/AN-6DiodeLadderFilter.pdf` does download, 8 pages, and was read. It confirms the structure in 1.5: input attenuator 1.0 on LPF1 and 0.5 on LPF2 to LPF4, `G4 = 0.5g/(1+g)` and the nested `G3, G2, G1`, the closed-form global loop, self-oscillation at k = 17 with the peak at fc/√2, passband loss "about 12 dB" more than a Moog ladder. Its nonlinear option is a "budget" tanh at the ladder input, **outside** the feedback loop; Pirkle says the full model with NLP blocks in every local feedback path "will be a challenge (if not impossible to process)". So no AN-6 port carries per-stage nonlinearities.)*
- Papers cited from abstracts and prior knowledge, not re-read for this document: Fontana and Civolani, "Modeling of the EMS VCS3 voltage-controlled filter as a nonlinear filter network" (IEEE TASLP 2010; DAFx precursor at `dafx.de/paper-archive/details/y9N6dG2Rw_L1-hVzFviCrw`); D'Angelo and Välimäki (ICASSP 2013; "Generalized Moog ladder filter: Part II", IEEE TASLP 2014); Huovilainen (DAFx-04); Parker, Zavalishin and Le Bivic, "Reducing the aliasing of nonlinear waveshaping using continuous-time convolution" (DAFx-16, `dafx.de/paper-archive/details/vem_XXF5qBbfiWOH2RVVAA`); Bilbao, Esqueda, Parker and Välimäki (IEEE SPL 2017); Holters, "Antiderivative antialiasing for stateful systems" (DAFx-19, `dafx.de/paper-archive/details/lp5GcJOo79OBkfcMfN3N_g`); Välimäki and Huovilainen (IEEE SPM 2007); Välimäki, Pekonen and Nam (JASA 2012); Esqueda, Välimäki and Bilbao, "Rounding corners with BLAMP" (DAFx-16); Brandt, "Hard sync without aliasing" (ICMC 2001); Yu et al., "Differentiable all-pole filters for time-varying audio systems" (DAFx-24, `arxiv.org/abs/2404.07970`, abstract and project page only).
- D16 publishes no technical description of Phoscyon 2's DSP. Section 7 has what the product page says.
- No academic paper models the full TB-303 circuit (WDF, state-space or neural) as far as two searches could find (two more in the verification pass found none either). The closest is Yu et al., which fits a time-varying biquad to recordings of a **hardware TB-303 clone** (the SampleScience "Abstract 303" pack, 100 loops at 120 BPM, resampled to 48 kHz; section 5.2 of the arXiv PDF, read in the verification pass), not of a Roland unit.

Prototype code for the numbers below (linear analysis in Python, solver and shaper in C++) lived in the session scratchpad and is not in the repository. The load-bearing parts are reproduced in sections 1.7 and 2.3.

---

## 1. Filter

### 1.1 The circuit as equations

Component names follow the Roland schematic as reproduced by Stinchcombe (`tb303_vcf_res_part2.gif`): ladder capacitors C18 (0.018 µF, bottom), C19, C24, C26 (0.033 µF each), input pair Q12, ladder pairs Q13 to Q17 and Q23, top pair Q22, output differential amplifier Q21 fed through C25 and C27 (0.1 µF) with R113 and R114 (100 kΩ) to bias, resonance pot VR4 50 kΩ (B). [primary]. Stinchcombe's analysis rounds C18 to 16.5 nF ("half"); his own component-level simulation uses 18 nF. [unverified: the `tb303_comp_level.gif` served on 2026-10-02 carries no component labels, so the 18 nF in his SPICE run could not be re-read.] The values themselves are confirmed from the x0xb0x Eagle schematic (`mainboard.sch`, part/value strings): C18 .018, C19/C24/C26 .033, C25/C27 .1, R113/R114 100K, VR4 50K(B), C14/C15/C17/C23 1 µF, R62 220K, R69 2.2K, R97 10K, R98 2.2K. [primary, x0xb0x schematic]

Derivation. The capacitors sit between the two arms, so the common-mode current is the same at every rung: each diode pair carries a total of I_f, split between arms by the differential voltage across the pair. For ideal exponential junctions the differential current through pair n is `I_f · tanh(Δv_n / 2V_T)`. Each capacitor is charged by half the difference between the differential currents below and above it. Normalizing every differential voltage by 2V_T gives:

```
x   = v_in(Q12a base) / 2V_T            input drive
u   = x − K·e                           e = feedback voltage at Q12b base / (2V_T·K)

dy1/dt = ρ·ω0 · [ tanh(u)       − tanh(y1 − y2) ]
dy2/dt =   ω0 · [ tanh(y1 − y2) − tanh(y2 − y3) ]
dy3/dt =   ω0 · [ tanh(y2 − y3) − tanh(y3 − y4) ]
dy4/dt =   ω0 · [ tanh(y3 − y4) − tanh(y4)      ] − ωL·vb

dq/dt  = ωq · vb                        coupling-cap state (C25/C27)
vb     = y4 − q                         Q21 differential base voltage / 2V_T
w      = tanh(vb)                       Q21 output, normalized
e      = Hfb{ w }                       linear feedback network, unity HF gain (1.3)

ω0 = I_f / (4·C·V_T)      C = 33 nF,  V_T = kT/q = 25.7 mV at 25 °C
ρ  = C / C18 = 33/18 = 1.8333
ωq = 1 / (R_b·C_c)       C_c = 0.1 µF
ωL = 1 / (R_b·2C)
K  = K_max · r            r = resonance pot wiper fraction, 0 to 1
```

y1 is the voltage across C18 (next to the input pair), y4 the voltage across the top capacitor, where the output is taken.

Cross-checks:

- Linearized with ρ = 2 and no load, these reduce to `H(s) = 1 / (s⁴/2 + 4s³ + 10s² + 8s + 1 + K)` with s normalized to ω0, which is Stinchcombe's `H_tb` (coefficients 8, 32, 40, 16, 1 in his variable `aC`, with `ω0 = 1/(2aC)`) [primary, pge=diode] and Zavalishin's `1 / (T4(1 + s/ωc) + k)` with `ωc = 2ω0` (eq. 5.27, 5.29). [primary]
- With equal capacitors (ρ = 1) the denominator is `s⁴ + 7s³ + 15s² + 10s + 1`, Stinchcombe's `H_1`. [primary]
- The nonlinear form with ρ = 2 is Zavalishin's nonlinear diode ladder, the equations under Fig. 6.34. [primary]
- Zavalishin's linear form (5.18), where the first stage lacks the 1/2 input gain, is this circuit with ρ = 2. The asymmetry is the half-value capacitor. Pirkle's AN-6 structure (`a1 = 1.0, a2 = a3 = a4 = 0.5`, read from JC303's port) is the same thing. [primary, code]

What the equations say about where distortion comes from. Every nonlinearity is a tanh of a difference of adjacent states, scaled to 2V_T = 51.4 mV. The control current multiplies all five at once, which is why cutoff modulation cannot be bolted onto a linear core afterwards. Urs Heckmann made the same point in the KVR thread: the series nonlinearities "replace the cutoff coefficients", and a single input clipper gets modulation behaviour wrong. [lore, consistent with the equations]

### 1.2 Linear constants [derived, script-checked against Stinchcombe's and Zavalishin's numbers; re-derived symbolically in the verification pass, all entries confirmed (the octave figure is 3.24)]

| Quantity | ρ = 2 (textbook) | ρ = 33/18 (as built) | ρ = 1 (equal caps) |
|---|---|---|---|
| Core poles, units of ω0 | −0.152, −1.235, −2.765, −3.848 | −0.149, −1.207, −2.705, −3.773 | −0.121, −1.000, −2.347, −3.532 |
| Lossless resonance `c_res = ω_res/ω0` | 1.4142 | **1.3838** | 1.1952 |
| Loop gain at self-oscillation, no in-loop high-pass, no load | 17.000 | **17.019** | 18.388 |
| −3 dB point of the open core | 0.149 ω0 | 0.146 ω0 | n/a |
| −3 dB point as a fraction of f_res | 0.105 | **0.105** (3.25 octaves below) | n/a |
| Stinchcombe's `ω_c` (leading coefficient normalized) | 1.189 ω0 | 1.164 ω0 | 1.000 ω0 |

Stinchcombe's normalized poles (−0.13, −1.04, −2.33, −3.24) are the ρ = 2 row divided by 1.189. [primary]

Frequency per microamp at 25 °C: `f0 = I_f/(8π·C·V_T)` = 46.9 Hz/µA, so **f_res = 64.9 Hz per µA** of ladder tail current. Cross-check: a1k0n measured "about 27.6 Hz per mV across R69, plus 103 Hz" on an x0xb0x; if R69 is the 2.2 kΩ tail resistor that is 60.7 Hz/µA, 7 % under the theoretical figure, with his "cutoff" defined by a different fitted model. [primary measurement, definition mismatch]

Passband level falls as 1/(1 + K): −25.9 dB at K = 18.7. The VCA input mix adds the resonance pot wiper back in to compensate, up to about +10 dB (doc 03, section 6.4). Model both; do not auto-normalize.

### 1.3 The networks around the core

Stinchcombe's 10-pole transfer function [primary source, itself "all based on SPICE simulation"]:

```
H(s) = 1.06·s³(s+109.9)(s+34.0)(s+7.41)
       ───────────────────────────────────────────────────────────────────────────────
       D4(s/ωc)·(s+97.5)(s+38.5)(s+4.45)(s+578.1)(s+20.0)(s+7.41) + 18.7·k·s⁴(s+46.5)(s+4.40)
```

The constants are rad/s. His block diagram (`tb303_laplace_model.gif`) writes the same model with every corner in Hz, which decodes it into separate physical blocks:

| Block (his numbering) | Transfer function, corners in Hz | Where | In loop? |
|---|---|---|---|
| 1 input coupling | gain 0.01 · HP(0.708) · (s+17.495)/(s+15.517) · (s+5.407)/(s+6.164) | R62 220 k, C17 1 µF, 2.2 k to bias | no |
| CORE | −1/D4, hard-coded at 820 Hz in his model | ladder | yes |
| 2 output amp | −106 · HP(92) | C25/C27 0.1 µF into Q21 | yes, forward path |
| 3 output coupling | HP(3.18) | C14 1 µF into the 50 k resonance pot | yes, forward path |
| 4 resonance | `k`·0.98·HP(1.18) | VR4 wiper, C15 1 µF | feedback only |
| 5 feedback return | (1/5.55) · HP(0.708) · (s+7.408)/(s+15.517) · (s+0.701)/(s+6.164), HF gain 1/5.55 | Q18, C23, R97, R98 into Q12b base | feedback only |

`V_out` is tapped after block 3. Small-signal loop gain at high frequency is 106 × 0.98 / 5.55 = 18.7 · k. The overall passband gain at k = 0 is 0.01 × 106 = 1.06.

Checks on the decoding: 2π × 92 = 578.1, 2π × 15.517 = 97.5, 2π × 3.18 = 20.0, 2π × 1.18 = 7.41, 2π × 17.495 = 109.9, 2π × 7.408 = 46.5, 2π × 5.407 = 34.0, 2π × 0.708 = 4.45, 2π × 0.701 = 4.40. Nine of the ten match to the printed precision. The tenth does not quite: 2π × 6.164 = 38.73, but the formula prints 38.5 (6.13 Hz), a 0.6 % mismatch that Stinchcombe's own caveat covers ("it should correspond to the transfer function at the top of the page (though at the time of writing this, I am not going to check it"). It is inaudible; use 6.164 Hz. 1/(2π × 50 kΩ × 1 µF) = 3.18 Hz and 1/(2π × 222.2 kΩ × 1 µF) = 0.716 Hz confirm blocks 3 and 1 from component values. [derived] The rad/s reading is independently supported: 578.1 rad/s = 92 Hz agrees with a1k0n's scope-measured "100Hz high-pass ... formed by C27/R113 + C25/R114", whereas 578 Hz would not. (Verification pass: the block-diagram GIFs as served on 2026-10-02 contain no text labels, so the Hz values in the table above were not re-read from the image; they are confirmed only through this arithmetic and the component values.)

What matters for sound:

- **The 92 Hz high-pass is inside the loop and in the forward path.** It filters the output and removes loop gain at low frequency. As the resonant frequency drops below a few hundred Hz the resonance weakens and the peak pulls upward: in Stinchcombe's model the oscillation frequency is 1.20 × his ω_c at 8 kHz, 1.29 × at 820 Hz and 1.80 × at 100 Hz, and the resonance setting needed to oscillate rises from 0.92 to 1.06 to 2.47 (the pot stops at 1). [derived from his formula] a1k0n, with a scope on an x0xb0x: the 303 filter "works almost exactly like the theoretical model says it should, except for a high-pass filter in the resonance feedback loop", "formed by C27/R113 + C25/R114", "100 Hz", and without it the model is "too resonant at lower frequencies and sounds like it's whistling fifths and octaves of the fundamental". [primary measurement]
- **The sub-20 Hz corners form a second resonance at 7.6 to 12 Hz.** Evaluating Stinchcombe's model: at his ω_c = 820 Hz and k = 1 the low peak is at 8.7 Hz and sits 15 dB above the sagged passband. He measured a clone oscillating "comfortably at around 8 Hz" when his test rig added capacitive load, and thinks this peak "might be a large contributing factor to the sound". [primary] It is subsonic. Its audible effect is on transients and on how the nonlinearities are biased during a note onset. It costs four one-pole filters. Include it.
- **Open303 replaces all of this with fixed fitted filters**: feedback HP 150 Hz, pre-filter HP 44.486 Hz, post HP 24.167 Hz, allpass 14.008 Hz, notch 7.5164 Hz with bandwidth 4.7 octaves. [primary, code] The five-digit constants look like a numerical fit to a measured unit. They cannot track resonance the way blocks 4 and 5 do.

Simplified feedback network used below (drops the 0.701/0.708 near-cancellation, error under 1 %): `Hfb(s) = HP(3.18) · HP(1.18) · HP(6.164) · (s + 2π·7.408)/(s + 2π·15.517)`, with `K_max = 18.7`.

### 1.4 The load Stinchcombe's block model leaves out [derived]

A 92 Hz corner with 0.1 µF is 17.3 kΩ of total resistance. Part of that is the ladder itself: the top capacitor node has a source resistance of `2V_T/I_f`, 5.1 kΩ at 10 µA and 17 kΩ at 3 µA. The rest is `R_b`, Q21's input resistance per side (r_π in parallel with 100 kΩ). The block model treats the stages as buffered. The circuit does not: R_b loads the top of the ladder, and the load grows as the cutoff current falls. That is the `−ωL·vb` term in 1.1, with `ωL = 1/(R_b × 66 nF)` and `ωq = 1/(R_b × 0.1 µF)`. One state serves both the load and the in-loop high-pass, and the effective corner `1/(2π·0.1 µF·(R_b + 2V_T/I_f))` falls out of the node equation without being programmed.

Doc 02 (section 3.4) derives the same load independently, puts R_b at 13.6 kΩ for β = 160 and 28 kΩ for β = 400, and finds that β = 160 reproduces Stinchcombe's component-level curve (peak about −9 dB where his block model shows +3 dB). The two derivations agree to within 2 dB on peak heights; doc 02 uses an output-amp gain of 110 where this table uses Stinchcombe's 106.

Resonance setting `r` at which the linearized model starts to self-oscillate (ρ = 33/18, feedback network of 1.3, K_max = 18.7):

| f_res nominal (Hz) | I_f (µA) | No load, 92 Hz coupler | R_b = 28 kΩ | R_b = 17.3 kΩ | R_b = 13.6 kΩ | Peak above own passband at r = 1, R_b = 13.6 kΩ |
|---|---|---|---|---|---|---|
| 97 | 1.5 | 2.94 | 3.47 | 5.3 | none | no peak |
| 194 | 3 | 1.78 | 2.00 | 2.72 | 3.28 | 2.8 dB |
| 387 | 6 | 1.32 | 1.41 | 1.71 | 1.94 | 8.0 dB |
| 800 | 12.4 | 1.10 | 1.14 | 1.27 | 1.37 | 14.6 dB |
| 1614 | 25 | 1.00 | 1.02 | 1.08 | 1.13 | 23.1 dB |
| 3227 | 50 | 0.96 | 0.97 | 1.00 | 1.02 | 39 dB |
| 6454 | 100 | 0.94 | 0.94 | 0.95 | 0.96 | oscillates |
| 12909 | 200 | 0.92 | 0.92 | 0.93 | 0.94 | oscillates |
| **Lowest f_res that oscillates at r = 1** | | 1.6 kHz | 2.0 kHz | 3.0 kHz | 3.8 kHz | |

Read: a stock unit at full resonance self-oscillates only near the top of its sweep, and at 800 Hz its peak stands 15 to 22 dB above its own passband depending on Q21's β. That matches the hardware folklore ("does not quite self-oscillate, except some units at the top of the sweep"). The unloaded column reproduces Stinchcombe's block model: self-oscillation at k = 1.06 for his 820 Hz example.

(Verification pass: the table was recomputed independently from the eigenvalues of the linearized state-space model of 1.1 with the feedback network of 1.3. Every entry reproduces within ±0.01, the "lowest f_res" row gives 1.62, 1.98, 3.04 and 3.79 kHz, and the one cell the author left as "none" (97 Hz, 17.3 kΩ) is 5.3, reachable only far above r = 1.)

R_b is the least certain constant in this document and it varies between units with transistor β. It is a natural trimmer. Section 6 lists the SPICE run that pins it. The solver measurements in 1.6 and 1.9 were made with R_b = 17.3 kΩ.

**Upper limit on R_b: sub-audio motorboating.** [derived, verification pass] In the same linear model, the sub-20 Hz loop (coupler state q plus the feedback network) turns unstable at full resonance once R_b is large. It does so as a 5 to 7 Hz oscillation, with its own threshold separate from the audio-band one. At r = 1 the critical R_b is 43 kΩ at f_res 975 Hz, 38 kΩ at 2.4 kHz, 36 kΩ at 6.5 kHz and 35.3 kΩ at 20 kHz. With r above 1 (the Devil Fish style over-resonance of 1.7) it falls fast: 28 kΩ at r = 1.2, 21 kΩ at r = 1.5 and **15 kΩ at r = 2** (f_res 6.5 kHz). This agrees with doc 02 section 3.4 ("Above about R_b = 35 k ... the model motorboats at 5 to 6 Hz whenever resonance is at maximum"). Stock units are not reported to motorboat, so cap the R_b trimmer at **35 kΩ**, and when r > 1 is allowed either lower the cap to match (about 15 kΩ for r = 2) or let the sub-20 Hz network be switched out in the over-resonance mode. The nonlinearities bound such an oscillation, but it would be heard as a slow pumping of the bias, not as tone.

### 1.5 Existing digital structures

**Open303 `TeeBeeFilter`, mode `TB_303`** [primary, code]. Per oversampled sample (4× the host rate):

```
y0  = in − feedbackHP(k·y4)                 // y4 from the previous sample; one-pole HP at 150 Hz
y1 += 2·b0·(y0 − y1 + y2)
y2 +=   b0·(y1 − 2·y2 + y3)
y3 +=   b0·(y2 − 2·y3 + y4)
y4 +=   b0·(y3 − 2·y4)
out = 2·g·y4
```

This is the linear circuit of 1.1 with ρ = 2, integrated by sequential (Gauss-Seidel style) forward Euler with `b0 ≈ ω0·T`, and a unit delay in the feedback. The header credits "mystran & kunn (page 40 in the kvr-thread)". Tuning and loop-delay errors are absorbed into two fitted functions of `fx = fc/(√2·fs)`: a rational for `b0`, `(0.00045522346 + 6.1922189·fx)/(1 + 12.358354·fx + 4.4156345·fx²)`, and a 6th-order polynomial for `k` that starts at 16.998792. Output gain is `g = ((k/17 − 1)·r + 1)·(1 + r)`, a partial make-up for the passband sag. The resonance knob is skewed by `(1 − e^(−3r))/(1 − e^(−3))`. Cutoff is clamped to 200 Hz to 20 kHz and updated once per host sample. The runtime path is `calculateCoefficientsApprox4`; the `k *= 17/4` line lives in the unused `Exact` path.

Open303's "cutoff" is the resonant frequency: `b0 ≈ 4.378·fc/fs` gives `f0 = 0.697·fc` and `√2·f0 = 0.985·fc`.

Measured against the analog prototype it approximates (ρ = 2, K = 17r through a 150 Hz one-pole, same output gain), at 176.4 kHz [derived]:

| fc (Hz) | r (skewed) | Peak frequency, Open303 / analog (Hz) | Peak level error (dB) | Passband error at fc/8 (dB) |
|---|---|---|---|---|
| 1000 | 1.0 | 1036 / 1034 | −0.5 | −0.4 |
| 2500 | 1.0 | 2528 / 2536 | −1.2 | −1.0 |
| 5000 | 1.0 | 5017 / 5036 | −2.3 | −1.9 |
| 10000 | 1.0 | 10026 / 10037 | −4.5 | −3.7 |
| 20000 | 1.0 | 20061 / 20037 | −8.4 | −6.6 |

So the fits do their job to 5 kHz. The inaccuracies that matter are structural:

1. It is linear. `shape()` exists (clip to ±√2, then `x − x³/6`) but every call site is commented out. No level-dependent resonance, no compression of the peak, no drive.
2. One feedback high-pass at 150 Hz stands in for the 92 Hz forward-path high-pass, the Q21 load and the four sub-20 Hz corners. Low-cutoff resonance behaviour and the low peak are fitted from outside the loop instead of produced by it.
3. ρ = 2 instead of 1.83, and a 200 Hz floor on cutoff.
4. Fits are functions of `fc/fs` only, tuned around 4×44.1 kHz. They are not re-derived per sample rate, and the error table above grows with `fc/fs`.

**Pirkle AN-6 family: JC303 `dfl::DiodeLadderFilter`, Surge XT / Odin 2** [primary, code]. Linear zero-delay diode ladder: each stage a TPT one-pole, the inner coupling resolved by Zavalishin's nested instantaneous gains (`G4 = 0.5g/(1+g)`, `G3 = 0.5g/(1 + g − 0.5g·G4)`, `G2` likewise, `G1 = g/(1 + g − g·G2)`), the outer loop solved in closed form `u = (x − K·Σ)/(1 + K·Γ)`. Surge clamps K to 16 and adds `(1 + 0.3K)` input gain compensation; it has no nonlinearity and no feedback high-pass. JC303 adds a tanh on `u` with drive and several fitted compensations, takes K = 17·r, and has no feedback high-pass either.

JC303's "octave mode" doubles the first stage's cutoff (`alpha2`) on top of a structure that already encodes the half-value capacitor. That is ρ = 4, a capacitor of a quarter. The analog ρ = 4 ladder resonates at 0.837 ωc and self-oscillates at K = 18.04; JC303's own comments report 0.79 and "about 17.8" and then cap K at 0.85 × 17 with a soft knee tuned by ear against the TeeBee. The mode also leaves `G1..G4`, `Σ` and `Γ` computed for uniform stages, so the zero-delay solve is no longer consistent with the stage it feeds. Do not copy it. [derived from the code]

JC303's other changes over Open303: filter drive 0 to 9 dB, passband compensation, an LFO, GuitarML/BYOD neural overdrive, the Aciduino step sequencer, and "Devil Fish style" ranges (normal and accent decay 30 to 3000 ms, feedback HP 350 down to 100 Hz, soft attack 0.3 to 3000 ms, slide 2 to 360 ms, square shaper drive 25 to 80 dB). [primary, code]

**karrikuh (Dominique Wurtz) `DiodeLadderFilter`, KVR 2012** [lore: thread summary, code not read]. Zero-delay linear ladder solved by closed-form expressions, "a tanh-like clipper at the filter input", a feedback high-pass added in version 0.2, MIT license, author recommends at least 2× oversampling. He reported "a notable improvement of the overall sound by using many nonlinearities" at higher CPU cost.

**a1k0n** [primary measurement, simplified model]. Fits five poles and one zero, tabulated against resonance and linear in cutoff, then runs them as a first-order high-pass plus two biquads. Linear, and coefficient-switched every 64 samples. Useful as measurement data, not as a structure.

**LMMS LB302** [primary, code]. Two filters. `Lb302Filter3Pole` is a 3-pole cascade with `tanh` on the feedback and on the output and cubic polynomial coefficient fits, after Josep M. Comajuncosas's Csound TB-303 instrument. `Lb302FilterIIR2` is a single resonant two-pole from Andy Sloane's (a1k0n's) earlier "gsyn" code. Neither is a diode ladder.

**Earlier project (`ref-mwAudio303`)** [primary, code]. Its "baseline" `DiodeLadder` is four buffered one-poles with unit-delay feedback, `k = 4.3·r`, a tanh on the input: a Moog cascade with a 150 Hz feedback high-pass. Its `ZdfDiodeLadder` adds a `0.5·(y[n+1] − y[n])` coupling term resolved by two fixed-point passes, also with `k = 4.3·r`. Neither has the diode ladder's transfer function, so the A/B verdict in `voice-core-zdf-ab-findings.md` compares two approximations and does not carry over.

**Academic.** Fontana and Civolani solve the EMS VCS3 diode ladder as a delay-free nonlinear network by fixed-point iteration and report that 176.4 kHz minimizes total cost because fewer iterations are needed. [secondary, abstract] D'Angelo and Välimäki give an explicit (non-iterative) nonlinear Moog ladder with a delay-free loop. [secondary] Yu et al. (DAFx-24) fit a time-varying biquad to recordings of a hardware TB-303 *clone*. In their words: "Although the original TB-303's circuit contains a 4-pole diode ladder filter, for simplicity and demonstration purposes, we implement our synthesiser using a biquad filter." So it is a control-fitting demonstration, not a circuit model. [primary, arXiv 2404.07970 section 5.2, read in the verification pass]

### 1.6 Solvers

All three candidates use trapezoidal integration (TPT) of the equations in 1.1 and differ in how the implicit equation is solved. Measured in the prototype against a 4th-order Runge-Kutta reference at 128×48 kHz, band-limited additive saw input, error as RMS(model − reference)/RMS(reference) at coincident samples [derived]:

| Test | Solver | 1× | 2× | 4× | 8× |
|---|---|---|---|---|---|
| Saw 110 Hz, peak 0.6, f_res 1 kHz, r = 0.9 | Newton to 1e-9 | −57.3 | −69.3 | −81.4 | −93.4 |
|  | One Newton step from the previous sample | −53.6 | −65.7 | −77.7 | −89.7 |
|  | One secant step (gains tanh(a)/a at the previous sample) | −36.8 | −42.5 | −48.4 | −54.4 |
| Saw 55 Hz, f_res swept 4 kHz to 300 Hz with τ = 100 ms, r = 1.0 | Newton | −23.4 | −36.0 | −48.3 | −60.4 |
|  | One Newton step | −21.3 | −36.6 | −50.1 | −62.8 |
|  | One secant step | −17.1 | −20.5 | −22.8 | −26.1 |
| Saw 110 Hz, peak 3.0 (14 dB hot), f_res 2 kHz, r = 0.95 | Newton | −42.7 | −57.1 | −69.3 | −81.4 |
|  | One Newton step | −41.4 | −55.1 | −67.2 | −79.2 |
|  | One secant step | −25.2 | −30.4 | −35.9 | −41.7 |

Newton and the single Newton step converge at second order (12 dB per doubling), as trapezoidal integration should. The secant variant is my reading of mystran's "cheap" method without his half-sample averaging refinement; it gains only 6 dB per doubling here because its gains lag by a sample. mystran himself says the method "requires 4× oversampling for reasonable results" and suggests doubling the oversampling before adding a corrector. [lore] With one voice to compute there is no need for it.

Newton iteration counts at tolerance 1e-9: average 2.0 to 3.0; worst case 6 (1×, resonance at 12 kHz), 4 at 2× and above. Zero non-converged samples in every run, including a self-oscillating filter and a 14 dB hot input.

How much each nonlinearity contributes (reference model, 4×, difference from the full model relative to signal) [derived]:

| Case | Ladder diodes linearized | Q21 tanh removed |
|---|---|---|
| Saw 110 Hz, peak 0.6, f_res 1 kHz, r = 0.9 | −29.1 dB | −57.9 dB |
| Same, r = 0.5 | −38.5 dB | −58.9 dB |
| Sweep 4 kHz to 300 Hz, r = 1.0 | −15.5 dB | −48.7 dB |
| Peak 1.2, f_res 2 kHz, r = 0.9 | −23.5 dB | −48.8 dB |
| Peak 3.0, f_res 2 kHz, r = 0.95 | −22.2 dB | −40.3 dB |

An input-only clipper with a linear ladder (karrikuh's first version, JC303, the earlier project) is wrong by 3 to 17 % of the waveform. The per-stage diodes are required. Q21's tanh is a 0.1 to 1 % effect and costs one more tanh per iteration; keep it.

### 1.7 Recommended filter algorithm

Zero-delay, trapezoidal, Newton. Unknowns are y1..y4 at the new sample. The coupling state and the feedback network are linear, so their instantaneous responses are affine in y4 and in w and fold into the residual; nothing in the loop is delayed.

Constants:

```
RHO   = 33.0/18.0                 // C_ladder / C18
CRES  = 1.3838                    // f_res / f0 for this RHO
R_B   = 13.6e3                    // Q21 input resistance per side   [trim, 10k..35k; above ~35k the sub-audio loop motorboats at r = 1 (1.4); doc 02 section 3.4]
WQ    = 1/(R_B*100e-9)            // 735 1/s
WL    = 1/(R_B*66e-9)             // 1114 1/s
KMAX  = 18.7                      // loop gain at r = 1               [trim, 17..20; 19.4 if Q21 gain is 110]
FB    = HP 3.18 Hz, HP 1.18 Hz, HP 6.164 Hz, shelf(zero 7.408 Hz, pole 15.517 Hz)
```

Per-rate setup (`fs` is the oversampled rate):

```
gq = tan(WQ/(2*fs));   gL = WL/(2*fs)
Gf[i] = gi/(1+gi),  gi = tan(pi*fc_i/fs)    for the four feedback sections (pole frequencies)
rsh = 7.408/15.517
```

Per sample:

```
process(x, fRes, r):
  // 1. coefficients. Prewarp at the resonant frequency, so the peak is tuned exactly.
  g  = tan(pi * min(fRes, 0.45*fs) / fs) / CRES
  g1 = RHO * g
  K  = KMAX * r

  // 2. affine maps:  vb = aq*y4 + bq ;  e = al*w + be
  aq = 1/(1+gq);  bq = -sq*aq
  al = 1;  be = 0
  for i in 0..2:  c = 1-Gf[i];  be = (be - zf[i])*c;  al *= c          // HP: out = (in - z)(1-G)
  c1 = 1-(1-rsh)*Gf[3];  c2 = (1-rsh)*(1-Gf[3])
  be = be*c1 - c2*zf[3];  al *= c1                                      // shelf

  // 3. Newton, started from a linear extrapolation of the last two solutions
  Y[i] = 2*y[i] - yPrev[i]
  repeat up to 8 times:
      vb = aq*Y4 + bq;  w = tanh(vb);  dw = 1 - w*w
      u  = x - K*(al*w + be)
      t0 = tanh(u); t1 = tanh(Y1-Y2); t2 = tanh(Y2-Y3); t3 = tanh(Y3-Y4); t4 = tanh(Y4)
      d_i = 1 - t_i*t_i

      R1 = Y1 - s1 - g1*(t0 - t1)
      R2 = Y2 - s2 - g *(t1 - t2)
      R3 = Y3 - s3 - g *(t2 - t3)
      R4 = Y4 - s4 - g *(t3 - t4) + gL*vb

      J11 = 1 + g1*d1;      J12 = -g1*d1;                 J14 = g1*d0*K*al*dw*aq
      J21 = -g*d1;          J22 = 1 + g*(d1+d2);          J23 = -g*d2
      J32 = -g*d2;          J33 = 1 + g*(d2+d3);          J34 = -g*d3
      J43 = -g*d3;          J44 = 1 + g*(d3+d4) + gL*aq

      // solve J*D = -R : eliminate upward from row 4, then row 1 closes the corner term
      e4 = -R4/J44;                 f4 = -J43/J44
      m3 = J33 + J34*f4;  e3 = (-R3 - J34*e4)/m3;   f3 = -J32/m3
      m2 = J22 + J23*f3;  e2 = (-R2 - J23*e3)/m2;   f2 = -J21/m2
      E3 = e3 + f3*e2;  F3 = f3*f2;  E4 = e4 + f4*E3;  F4 = f4*F3
      D1 = (-R1 - J12*e2 - J14*E4) / (J11 + J12*f2 + J14*F4)
      D2 = e2 + f2*D1;  D3 = E3 + F3*D1;  D4 = E4 + F4*D1

      m = max|D_i|;  if m > 2: scale all D_i by 2/m          // step limiter, never triggered in tests
      Y += D
      if m < 1e-7: break

  // 4. re-evaluate vb, w, u, t0..t4 at the solution (or correct them to first order)
  // 5. commit states (trapezoidal)
  yPrev = y;  y = Y
  s1 = Y1 + g1*(t0 - t1);  s2 = Y2 + g*(t1 - t2);  s3 = Y3 + g*(t2 - t3)
  s4 = Y4 + g*(t3 - t4) - gL*vb
  sq += 2*gq*vb
  a = w
  for i in 0..2:  v = (a - zf[i])*Gf[i];  lp = v + zf[i];  zf[i] = lp + v;  a = a - lp
  v = (a - zf[3])*Gf[3];  zf[3] = (v + zf[3]) + v
  return w                                    // filter output, normalized; volts = 106 * 2V_T * w
```

Notes for the implementer:

- `g` multiplies the integrator input and the state `s_i = y_i + g_i·f_i` is committed with the current `g`. This is the TPT form and matches the circuit, where I_f scales the current into each capacitor. Cutoff can change every sample with no extra handling.
- Compute `tan()` once per base-rate sample and interpolate `g` linearly across the oversampled sub-steps, or use a polynomial. At 4× the argument stays below 0.36 rad for f_res ≤ 20 kHz (0.356 at 176.4 kHz, 0.327 at 192 kHz).
- `r` above 1 is legal and gives a Devil Fish style over-driven resonance. The solver was tested to r = 1.0 at every cutoff, including self-oscillation. With r > 1 the sub-audio loop can go unstable for R_b above about 15 to 28 kΩ (1.4), so tie the allowed R_b to the r range.
- (Verification pass) The Newton residuals, Jacobian entries, the elimination order in step 3, the TPT state commits for the ladder, the coupling state `sq` and the four feedback sections were re-derived by hand from the equations of 1.1 and agree term by term. The pseudocode is internally consistent. Its accuracy and iteration-count figures (1.6) are the author's prototype results and were not re-run.
- For an "ideal ladder" mode set `gL = 0`, replace the feedback network by a single high-pass, and use `RHO = 2, CRES = √2`. That reproduces Zavalishin's nonlinear model and is the right comparison target for other emulations.
- Double precision throughout. The states are of order 0.01 to 1, the residual tolerance is 1e-7, and the sub-Hz filters have `G` near 1e-5; single precision loses the feedback network.
- Fail-safe: if the loop exits without converging or any state is non-finite, keep the last iterate if finite, else reset the five ladder states to zero and continue. Count these events in debug builds; the target is zero.

### 1.8 Calibration

**Cutoff.** Define the model's frequency input as `fRes`, the lossless resonant frequency, proportional to I_f: `fRes = 64.9 Hz/µA × I_f` (25 °C, 33 nF). Every control contribution (knob, envelope, accent) is an octave offset on `fRes`, because the exponential converter multiplies I_f. The knob-to-`fRes` law belongs to the control-path documents; two anchors to check it against:

- Open303's "measured mapping" constants (`calculateEnvModScalerAndOffset`, `useMeasuredMapping = true`): **nominal** cutoff 313.8 Hz to 2394.4 Hz across the knob, exponential knob law (the VST wrapper maps 0..1 to 314..2394 with `linToExp`). [primary, code; doc 02 attributes the measurements to antto on one unmodified unit] The nominal value is **not** the cutoff with the envelope at rest. Open303 computes `instCutoff = cutoff · 2^(envScaler·(env − envOffset))` with `envOffset = 0.0483c + 0.2944` and `envScaler` running from `0.737 + 3.774e` (c = 0) to `0.864 + 4.195e` (c = 1). At rest (env = 0) that gives **270 Hz to 1949 Hz** at Env Mod 0 and 125 Hz to 719 Hz at Env Mod max. The envelope peak at c = 1, e = 0 is about 3.55 kHz. [derived from the code in the verification pass]
- a1k0n's fits from current measurements on an x0xb0x ("measuring the voltage across R69 ... while holding the gate high or low"), cutoff knob `c` and env-mod knob `e` in 0..1: gate low `f = exp(5.226 + 1.704c − 0.684e) + 103 Hz`, gate high `f = exp(5.559 + 2.178c + 1.992e) + 103 Hz`. Endpoints: 289 Hz (c = 0, e = 0, rest) to 1126 Hz (c = 1, e = 0, rest); gate-high 16.9 kHz at c = 1, e = 1. [primary measurement, one unit, his own definition of cutoff; formulas re-checked against `303.js`]

At rest and Env Mod 0 the two agree at the bottom (270 vs 289 Hz) and disagree at the top by a factor of 1.73 (1949 vs 1126 Hz), not 2. One coincidence is worth chasing: a1k0n's **gate-high** value at c = 1, e = 0 is 2395 Hz, which is Open303's nominal top (2394.4 Hz) to 0.02 %. Either both trace back to the same kind of measurement or the match is luck. Resolve it in the control document, then verify the product against section 6's sweep test.

**Resonance.** `K = KMAX · r_eff`. The pot is linear (50 kΩ B) and its wiper sees about 100 kΩ, so `r_eff = r / (1 + r(1−r)·50 kΩ/102 kΩ)` (doc 02 section 3.4): 0.445 at mid-rotation instead of 0.5. Open303's exponential skew is a tuning choice, not circuit. Calibrate KMAX and R_B together against two numbers: the lowest f_res that self-oscillates at r = 1 (model default 3.8 kHz, table in 1.4), and the service-manual check that at cutoff centre and full resonance the ring at TP6 has a 2 ms period and dies within about four cycles (doc 02).

**Drive.** `x = 0.0099 · v_osc / 2V_T` after a 0.72 Hz high-pass. A 5.5 V peak-to-peak saw gives a peak of 0.53; the square from the shaper model in section 2 swings about 3.5 V and gives 0.33. [derived; oscillator levels are from Martin Czech's description, not a measurement] Expose input level as a trimmer. Adding an offset to `x` models pair mismatch and is the only source of even harmonics in the filter.

**Analog reference for calibration.** The linear target is the state-space model of 1.1 linearized (tanh → identity), not Stinchcombe's formula alone, because the formula omits the load. The nonlinear target is the RK4 integration of 1.1. Both are 60 lines of code and should live in the test suite.

### 1.9 Oversampling and cost

Aliasing of the filter alone, input band-limited to 20 kHz, 48 kHz base rate; worst non-harmonic component below 20 kHz relative to the largest harmonic [derived]:

| Input | f_res, r | 1× | 2× | 4× | 8× |
|---|---|---|---|---|---|
| Saw 1244.5 Hz, peak 0.6 | 3 kHz, 0.9 | −58.8 dBc | −113.9 | below −200 | below −200 |
| Saw 1244.5 Hz, peak 3.0 | 3 kHz, 0.9 | −31.1 | −53.5 | −86.7 | −154.6 |
| Saw 1244.5 Hz, peak 0.6 | 8 kHz, 0.85 | −59.3 | −99.3 | below −200 | below −200 |
| Saw 277.2 Hz, peak 3.0 | 6 kHz, 0.9 | −40.9 | −63.9 | −107.1 | −194.3 |
| Saw 4186 Hz, peak 0.6 | 9 kHz, 0.8 | −36.1 | −77.4 | −171.7 | below −200 |

At stock drive the filter is clean at 2×. With drive it needs 4×. The oscillator's own edges set the real requirement (section 2), and they also want 4×.

CPU, prototype as written (scalar double, `g++ -O2 -march=native`, Xeon E5-2699 v4 at 2.2 GHz, a 2016 server core), including a `tan()` per sample:

| Variant | ns per oversampled sample | % of one core at 2× / 4× / 8× of 48 kHz |
|---|---|---|
| Newton, 2 iterations, rational tanh (max error 9.6e-5) | 225 | 2.2 / 4.3 / 8.7 |
| Newton, 3 iterations, rational tanh | 306 | 2.9 / 5.9 / 11.8 |
| Newton, 2 iterations, `std::tanh` | 519 | 5.0 / 10.0 / 19.9 |
| One Newton step, rational tanh | 135 | 1.3 / 2.6 / 5.2 |

A current desktop core is two to three times faster per thread. Obvious savings not yet taken: reuse the last iterate's tanh values for the state commit (saves 6 of 18 evaluations), move `tan()` to the base rate, replace 5 divisions with reciprocals. Budget 2 % of a modern core for the filter at 4×.

The rational tanh used: `x(135135 + x²(17325 + x²(378 + x²))) / (135135 + x²(62370 + x²(3150 + 28x²)))`, clamped to ±1 beyond |x| = 4.97. Newton then converges to the root of the approximated function, which is fine, but 1e-4 is too coarse to be the shipping default: use an exp-based tanh with error under 1e-7 or a SIMD library tanh evaluating all six arguments in one call.

Recommendation: internal rate at least 176.4 kHz in the default quality (4× at 44.1/48, 2× at 88.2/96, 1× at 176.4/192), half that for an eco mode with a single Newton step, double it for offline render.

---

## 2. Oscillator

### 2.1 What has to be reproduced

- One relaxation core: C33 (0.01 µF) discharged by the exponential converter, snapped back up by the Q24/Q25/Q27 SCR arrangement, buffered by Q28 (2SK30A source follower into R105 10 kΩ). A falling ramp with a fast upward reset. Martin Czech's reading of the schematic: the reset takes the capacitor to about 10.8 V, the base of Q25 to about 11.4 V, and saturation storage in Q25/Q27 "causes the flat portion of the saw wave and the time of this flatness should be independent of osc. frequency". He asks for an owner to verify. [secondary, x0xb0x wiki; unverified]
- The square is that saw through one PNP transistor, Q8 (2SA733), "2 caps, 4 resistors" [primary, x0xb0x schematic drawing; designators and rails as in doc 02 section 2.5, from the service notes. Values re-confirmed in the verification pass from the x0xb0x Eagle `mainboard.sch`: C10 .01 µF, R35 100K, R34 10K, R36 10K, C11 1 µF, R45 22K, C33 .01 µF, Q28 2SK30, R105 10K. Connectivity not re-traced.]:

```
saw ──R35 100k──┬── base(Q8)                 emitter ── R45 22k ── +12 V
saw ──C10 10n──R34 10k──┘                    emitter ── C11 1 µF ── GND
                                             collector ── R36 10k ── 5.333 V,  collector = square out
```

- Reported behaviour: duty about 71 % at the lowest pitch and 45 % at the highest (Robin Whittle via Olney, not bench-traced across pitch) [secondary]; one sharp edge at the saw reset and one soft edge whose width shrinks as pitch rises, plus roughly 50 ms settling "blips" after pitch jumps (aciddose, SPICE, KVR) [lore]. Phoscyon 2 exposes a square pulse-width trimmer for the same reason (doc 01).

### 2.2 The shaper circuit explains all three behaviours [derived]

I simulated the Q8 network (Ebers-Moll transistor, β_F = 200) with a 5.5 V falling ramp. The x0xb0x drawing labels the rails ambiguously; doc 02 settles them from the service notes, whose waveform sketch shows the square sitting flat at 5 V and rising to 8 V. With +12 V on the emitter resistor and 5.333 V on the collector resistor:

| Pitch (Hz) | Collector-high fraction | Emitter ripple (V) | Collector level just before the reset (V) |
|---|---|---|---|
| 32.7 | 0.595 | 1.86 | 8.05 |
| 55 | 0.541 | 1.31 | 7.77 |
| 110 | 0.493 | 0.72 | 8.10 |
| 220 | 0.449 | 0.38 | 8.38 |
| 440 | 0.392 | 0.20 | 8.64 |
| 880 | 0.334 | 0.10 | 8.90 |

(Saw 5.5 to 11.0 V at the base network. With 6.0 to 11.5 V the duty column reads 0.540 down to 0.305.) The off level is 5.33 V and the pulse peaks near 8.8 V right after the knee at both 55 Hz and 440 Hz.

Mechanism: C11 holds the emitter at a self-bias voltage, about 8.4 to 9 V, set by the balance between R45 charging it and the transistor discharging it while on. The transistor turns on when the falling saw crosses one V_BE below the emitter. At high pitch C11 holds still and the duty settles where charge balances, near 35 to 45 %. At low pitch C11 recharges during the off time (τ = 22 kΩ × 1 µF = 22 ms), the threshold rises, and the transistor turns on earlier: the duty widens. The same 22 ms constant is the "50 ms blip" after a pitch change. The soft edge is the transistor passing from cutoff to saturation as the ramp descends through about 0.15 V (gain ≈ β·R36/R35 = 20 for β = 200); the sharp edge is the saw reset. At 55 Hz the knee takes 2.6 % of the period (476 µs) and the top droops from 8.8 to 7.8 V; at 440 Hz the knee takes 30 µs because C10 speeds it up.

Direction and span match Whittle's figures: wider at low pitch, with a low-to-high duty ratio of 1.52 from 32.7 to 440 Hz against his 71/45 = 1.58. Absolute duty is about 10 points lower than his and moves with the saw's DC level, which I do not have from a measurement. Results at 192 kHz and 96 kHz match the 1.5 MHz run to three decimals, so the model is safe at the oversampled rate. Doc 02 ran the same circuit independently (saw 5.5 to 11.5 V, 20 µs reset, β = 300) and gets 0.57 at C1 falling to 0.35 at C5, settled within 27 ms after a two-octave jump. Use its table (V6) as the acceptance target.

Open303, for comparison, bakes `−tanh(69.98·saw + 4.37)` into mip-mapped wavetables: one hard edge, one soft edge of fixed relative width (about 3 % of the period), duty fixed at 46.9 %, no droop. Its 44.5 Hz pre-filter high-pass supplies the tilt. [primary, code] The earlier project applies `tanh(12·(saw + bias(pitch)))` to a polyBLEP saw with a hand-drawn bias curve from 71 % to 45 %: right trend, no dynamics, and the tanh re-aliases the band-limited edge.

### 2.3 Recommended oscillator

Run it at the oversampled rate and feed the filter directly. No upsampler is needed anywhere in the voice.

```
// state: ph (0..1), Ve, vC10, Vb, Vc (Newton warm start), prevOut (one-sample delay)
// constants: V_HI ≈ 11.0, V_LO ≈ 5.5 (saw at the JFET source)        [trim]
//            R35 100k, R34 10k (in series with C10 10n), R45 22k, C11 1u, R36 10k, VE_RAIL 12.0, VC_RAIL 5.333
//            transistor: Is 1e-14, BF 200 [trim: sets knee sharpness], BR 5

tick(freq):
  inc = freq / fs
  ph += inc
  wrapped = (ph >= 1)
  if wrapped:  ph -= 1;  d = ph / inc                 // fraction of a sample since the reset

  Vcap = V_HI − (V_HI − V_LO)·ph                      // linear discharge
  Vs   = jfet(Vcap)                                   // static map: follower gain < 1, soft flattening near the top

  // --- saw output ---
  saw = Vs
  if wrapped:
      h = jfet(V_HI) − jfet(V_LO)                     // step height, + because the reset goes up
      prevSaw += h · (d²/2)                           // polyBLEP, sample before the step
      saw     += h · (−(1−d)²/2)                      // polyBLEP, sample after the step

  // --- square: solve the Q8 node equations for (Vb, Vc) given Vs, Ve, vC10 ---
  solveQ8(Vs, Ve, vC10) -> Vb, Vc, IE:
      IF = Is·(exp((Ve−Vb)/VT) − 1);   IR = Is·(exp((Vc−Vb)/VT) − 1)
      IB = IF/BF + IR/BR;   IC = IF − IR·(1 + 1/BR);   IE = IF·(1 + 1/BF) − IR
      0 = (Vs − Vb)/R35 + (Vs − vC10 − Vb)/R34 + IB          // base node
      0 = IC − (Vc − VC_RAIL)/R36                            // collector node
      // 2×2 Newton, warm-started, |ΔVb| limited to 0.1 V per iteration; 2 to 4 iterations
  square = Vc
  if wrapped:
      // the two capacitor states are continuous through the reset; the output is a static function of Vs
      VcPre  = solveQ8(jfet(V_LO), Ve, vC10).Vc        // just before the reset
      VcPost = solveQ8(jfet(V_HI), Ve, vC10).Vc        // just after
      h = VcPost − VcPre
      prevSquare += h · (d²/2);   square += h · (−(1−d)²/2)

  // --- advance the two capacitor states (not stiff: 22 ms and 0.1 ms) ---
  Ve   += ((VE_RAIL − Ve)/R45 − IE) / (C11·fs)
  vC10 += ((Vs − vC10 − Vb)/R34) / (C10·fs)            // or the exact exponential form

  output prevSaw / prevSquare (one sample late);  prev = current
```

The polyBLEP residual is for a unit step at fractional position `d`: `+d²/2` on the sample before, `−(1−d)²/2` on the sample after. It needs the one-sample delay shown. Multiply by the actual step height, which for the square comes from evaluating the circuit on both sides of the reset.

Why this split works: between resets both outputs are smooth functions of a ramp. The only discontinuity is the reset, its time is known to sub-sample precision from the phase accumulator, and its height is computable. The soft knee is analytic (an exponential junction law), so it needs oversampling only for its high harmonics, which at 2.6 % of a period fall off fast above the 40th harmonic.

Band-limiting options, for the record:

| Method | Residual aliasing | Cost | Fit here |
|---|---|---|---|
| 2-point polyBLEP at the oversampled rate (Välimäki, Pekonen, Nam 2012) | At 192 kHz, components that fold below 20 kHz start above 172 kHz: for a 1 kHz note that is harmonic 172 and up, already 45 dB down before the BLEP's own attenuation. Estimated below −90 dBc for notes to 1 kHz. [derived estimate, verify with test O2] | 2 multiply-adds per reset | **Recommended** |
| 4-point polyBLEP (cubic B-spline) | 15 to 20 dB better near Nyquist | 4 samples touched, 2-sample delay | Use in eco mode (2×) |
| minBLEP table (Brandt 2001) | Table-limited, −100 dB class | Table lookup, ring buffer | Unneeded at 4×; would matter for hard sync, which the 303 lacks |
| BLAMP for slope discontinuities (Esqueda, Välimäki, Bilbao 2016) | Removes the next order of aliasing | Similar to BLEP | The slope jump at the reset is small against the step; skip |
| Mip-mapped wavetables (Open303) | Alias-free by construction | Cheap | Cannot do the pitch-dependent, stateful shaper |
| Naive saw into an oversampled static tanh | Needs 8× or more for a gain-20 shaper | | Superseded by the nodal model |
| DPW (Välimäki, Huovilainen) | Good for an ideal saw | Cheap | Does not extend to the shaped pulse |

Details that are cheap and audible on a real unit [lore-level until measured]:

- Finite reset: add a hold of `t_flat` at the top after each reset and compute `inc` so the period is `1/f` plus nothing (hardware) or minus the hold (tuned). On hardware a fixed hold flattens high notes: 20 µs at 1 kHz is 2 %, 34 cents. Default it to zero pitch error and expose the amount.
- JFET map: gain about 0.9, compression in the top 0.5 V as V_DS approaches zero. Any smooth monotonic map works with the BLEP rule above because the step height is taken after the map.
- Simplified square for eco mode: `Vc = VC_RAIL + sat( β·(R36/R35)·(Ve − 0.6 − Vs), Ve − VC_RAIL )` with `Ve` integrated as above and `sat` a smooth clamp at zero and at the ceiling. It keeps duty tracking, droop and the blip, and drops C10's edge speed-up.

Waveform selection in hardware is a switch between the two outputs into R62; implement it as a switch. A continuous blend is an extra.

---

## 3. Envelopes, accent, slide

Component values, timings and a complete reference model are in doc 03 (sections 3 to 7 and 11). This section is only about discretizing them without error.

**Rule: every RC state is advanced by its exact exponential, with the driving value held over the step.**

```
y += (target − y) · (1 − exp(−T/τ))            // one multiply-add per state per tick
```

Precompute `1 − exp(−T/τ)` when τ or the rate changes. This is exact for a constant target and sample-rate independent to rounding error; forward Euler (`y += (target − y)·T/τ`, as written in doc 03's reference model) is about 0.05 % off in time constant at τ = 22 ms and 48 kHz (the error is ≈ T/2τ = 0.047 %) and unstable when a path resistance goes to zero. For a pure decay, `y *= exp(−T/τ)`.

**MEG.** One state. On trigger set it to the peak (or charge toward it with τ = 0.1 ms for 0.5 to 1 ms, which at base rate is one or two samples; the instant jump is acceptable and click-free because the filter integrates it). Decay with `τ = (68 kΩ + R_pot)·1 µF + 4 ms` (doc 03). Changing τ mid-decay needs only a new coefficient; the state is the capacitor voltage, so there is no discontinuity.

**Accent sweep network.** One capacitor (C13), one diode, resistors. For each diode state the network is linear with one state, so it has a closed form. With `u` = C13 voltage and `w` = wiper voltage, both relative to the 3.3 V rest level, `Ra = r·R_P`, `Rb = (1−r)·R_P`, `Rs = R46 + R_src + Ra`, `R72` to the summing node:

```
diode off:   w = u·R72/(R72 + Rb);                u_inf = 0;       τ = C13·(Rb + R72)
diode on:    Gs = 1/Rs;  G7 = 1/R72;
             w = (s·Gs·Rb + u)/(1 + Rb·(Gs + G7))
             u_inf = s·Gs/(Gs + G7);              τ = C13·(Rb + 1/(Gs + G7))
update:      u = u_inf + (u − u_inf)·exp(−T/τ)
conduction test: diode is on when s > w_off, with s = V_acc − V_D − 3.3 V and w_off the diode-off wiper voltage
```

Written this way the formulas stay finite at r = 1 (`Rb = 0`): `w = u`, `τ_on = C13/(Gs + G7)` (50 ms with doc 03's values: 1 µF × (97 k ∥ 102 k) = 49.7 ms), `τ_off = C13·R72` (102 ms, taking R72 as doc 03's 102 kΩ, i.e. the 100 kΩ part plus the 2.2 kΩ base node; C13 1 µF, R72 100K, R46 47K confirmed in the x0xb0x schematic). No clamp on `Rb` is needed, which removes the 1 kΩ fudge in doc 03's model. The diode state is tested once per tick; at base rate the switching instant is resolved to 21 µs against time constants of 50 ms and up.

Run this state every tick whether or not the current note is accented. The charge that remains is the accent memory; resetting it per note is the bug Open303 has (its accent path is a 15 ms one-pole on the MEG, with no state that outlives the envelope).

**Slide.** One state in the pitch-CV domain (volts per octave or semitones): `cv += (target − cv)·(1 − exp(−T/0.022))` while the slide switch is closed, `cv = target` otherwise. (τ = 100 kΩ × C35; C35 = 0.22 µF is confirmed in the x0xb0x schematic, the 100 kΩ DAC source resistance is from doc 03. Test E3's 51 ms and 101 ms are −ln(0.1)·22 ms and −ln(0.01)·22 ms.) Convert to frequency after the lag. C35 tracks the output while slide is off, so there is nothing to initialize when a slide begins.

**VEG, gate shaping, accent into the VCA.** Same rule, constants from doc 03 section 6. The gate's turn-on delay and its hold (7.8 to 11.9 ms in doc 03's simulation, "8 ms" in the Devil Fish manual) are counters in samples; round to the nearest sample.

**Event timing.** Sequencer and MIDI events carry a sample offset within the block. Split the block at each event offset and run the per-sample loop on the pieces; do not quantize events to block boundaries. Base-rate resolution (21 µs at 48 kHz) is finer than the hardware's own gate jitter (doc 03 gives a 1.35 ms gate extension and a turn-on delay that varies by milliseconds), so sub-sample event timing is not needed.

**Rate.** Envelopes, accent and slide tick at the base rate. The oscillator increment and the filter's `g` are interpolated linearly across the oversampled sub-steps. A step in `fRes` of an octave or more at a note-on is real hardware behaviour (the MEG attack is 0.1 ms), and the TPT filter takes it without artifacts.

---

## 4. VCA and static nonlinearities

**BA662A.** An OTA: `i_out = I_abc · tanh(v_d / 2V_T)`, gain linear in control current. [primary, device law; doc 03 section 6.4] The input divider puts about 1 % of the filter output on the differential pair, plus up to 2.2 % from the resonance-pot path. With the filter model's output scale (`V_out = 106·2V_T·w` = 5.45 V × w, [derived]):

| Condition | w peak (prototype) | V_out peak | v_d / 2V_T | tanh compression |
|---|---|---|---|---|
| Stock saw, r = 0.9, f_res 1 kHz | 0.065 | 0.35 V | 0.07 direct, up to 0.21 with the compensation path | 0.2 % to 1.5 % |
| r = 0, f_res 600 Hz | 0.145 | 0.79 V | 0.15 | 0.7 % |
| Input 14 dB hot, r = 0.95 | 0.228 | 1.24 V | 0.24 direct, 0.72 with compensation | 2 % to 14 % |
| Open filter, saw, r = 0 (doc 02 section 3.9) | about 0.5 | about 2.7 V | 0.53 | 9 % |

(Verification pass: the last row was 3 V / 0.67 / 12 %, which does not follow from w = 0.5. 0.5 × 5.45 V = 2.7 V, and 2.7 V × 0.0099 / 51.4 mV = 0.53, giving 1 − tanh(0.53)/0.53 = 8.6 %. The other rows check.) So the VCA is nearly clean while the filter is closed down, compresses an open unresonated saw by about a tenth, and becomes a soft limiter at high resonance, where the compensation path triples its drive. In the open-filter case Q21's own tanh is no longer negligible either. These levels inherit the uncertainty of the oscillator level and of R_b; treat the VCA drive as a trimmer and calibrate it by THD (test V1).

Where a unit clips, in signal order:

| Stage | Law | Onset | Role |
|---|---|---|---|
| Q8 shaper | saturating PNP | always (it is the square) | waveform |
| Q12 input pair | tanh(x − K·e) | fed-back signal × K reaches 1 at w ≈ 0.05 | **the resonance limiter**; sets peak height and squelch |
| Ladder diode pairs | tanh of state differences | 3 to 17 % effect at stock levels (1.6) | cutoff-dependent colour, modulation behaviour |
| Q21 output pair | tanh(vb) | under 1 % at stock levels | minor |
| BA662A | tanh | see table | limiter at high resonance and accent |
| Output amp, headphone amp | op-amp rails | doc 03 section 9 | only when overdriven |

**ADAA versus oversampling.** First-order antiderivative antialiasing for a tanh is

```
F(x) = |x| + log1p(exp(−2|x|)) − ln 2                    // ln cosh x, overflow-safe
y[n] = (F(x[n]) − F(x[n−1])) / (x[n] − x[n−1])           // if |x[n] − x[n−1]| > 1e-6
     = tanh((x[n] + x[n−1])/2)                           // otherwise
```

(Parker, Zavalishin, Le Bivic 2016; Bilbao et al. 2017). It costs half a sample of delay and a gentle high-frequency roll-off, and it is the right tool for a memoryless shaper at 1×. It is the wrong tool inside the ladder: the half-sample delay sits in a feedback loop and the published stateful extension (Holters 2019) covers a single one-port nonlinearity, not six coupled ones. Since the voice already runs at 4× for the filter, put the VCA's tanh in that domain, before the decimator, and no ADAA is needed in the voice at all. Keep ADAA for the effects section (the distortion models run after decimation and will want 2× to 4× ADAA or their own oversampling).

---

## 5. Whole-voice structure

```
base rate (per sample)                              oversampled rate (×M)
────────────────────────                            ─────────────────────────────────────────────
events → sequencer / MIDI                           for m in 0..M−1:
pitch CV → slide lag ──── freq ───── interpolate →     oscillator core + JFET map + polyBLEP
MEG, accent network, env mod ─ fRes ─ interpolate →    Q8 shaper (square) / waveform switch
resonance knob (smoothed) ─── r ──────────────────→    input coupling: ×0.0099/2V_T, HP 0.72 Hz
VEG, gate shaper, accent → I_abc ─── interpolate →     diode ladder solve (1.7)  → w
                                                       VCA input mix: HP 72 Hz (direct) + r·HP 156 Hz (wiper)
                                                       BA662: I_abc · tanh(v_d)
                                                    decimate ×M (two polyphase IIR half-bands)
output coupling, volume, output amp HP  ←──────────
effects (own oversampling)
```

The VCA input mix and its two high-pass corners are from doc 03 section 6.4, and that document flags the trace as needing confirmation. The component values are confirmed from the x0xb0x schematic: R121 220K, R122 100K, C20 and C21 0.01 µF, R124 2.2K, so 1/(2π·220 k·10 nF) = 72 Hz and 1/(2π·102.2 k·10 nF) = 156 Hz. Which net each one sits on (the topology) was not re-traced. Phoscyon 2 exposes this corner as a calibration trimmer ("C21/22 - Cutoff frequency of the VCO's high pass filter", manual p. 8), which suggests it varies between units. a1k0n's measurements agree qualitatively: "the 303 actually loses quite a lot of low-end bass sound between the filter and the amplifier". [primary measurement, no numbers]

**Oversampling factor.** `M = 4` at 44.1 and 48 kHz, 2 at 88.2 and 96, 1 at 176.4 and 192 (default quality). The internal rate stays between 176.4 and 192 kHz, so tone and CPU are nearly the same at every host rate. Eco: half. Render: double. Every constant in the oversampled domain is derived from `fs·M`, never from fitted tables.

**Resampler.** Decimation only. Polyphase IIR half-bands (two allpass branches, Valenzuela and Constantinides; the design procedure is the one in Laurent de Soras' HIIR library, WTFPL). Designed and verified numerically for this document [derived]:

| Stage | Passband edge | Transition (fraction of the stage's input rate) | Allpass coefficients | Stopband rejection | Group delay, DC / band edge (input-rate samples) |
|---|---|---|---|---|---|
| 4× → 2× at 48 kHz base | 20 kHz of 192 kHz | 0.292 | 3 | 101.8 dB | 2.2 / 2.5 |
| 2× → 1× at 48 kHz base | 20 kHz of 96 kHz | 0.083 | 8 | 127.9 dB | 4.2 / 11.9 |
| 2× → 1× at 44.1 kHz base | 20 kHz of 88.2 kHz | 0.0465 | 8 | 104.1 dB | 3.7 / 17.7 |
| 2× → 1× at 44.1 kHz, render | same | 0.0465 | 10 | 130.0 dB | 4.5 / 21.8 |

Total delay is about 2.6 base-rate samples at low frequency and up to 9 near 20 kHz. Passband ripple is below 1e-9 dB. The phase is not linear, which is irrelevant for a synth voice with no dry path. Report 3 samples of latency to the host or none; both are defensible. A linear-phase FIR half-band for the last stage at 44.1 kHz would need about 138 taps for 100 dB (Kaiser estimate) and 34 base samples of latency, for no audible gain. By the same estimate, the earlier project's 31-tap half-band FIR gives about 28 dB (44.1 kHz) and 44 dB (48 kHz) in the final 2× → 1× stage and should not be reused there. It is adequate for the wide-transition 4× → 2× stage. (Verification pass: the four stopband figures in the table were recomputed with HIIR's closed-form design relations, `PolyphaseIir2Designer::compute_atten`, and reproduce exactly: 101.8, 127.9, 104.1, 130.0 dB. The transition widths follow from 2·(0.25 − 20 kHz/f_in). Group delays were not re-checked.)

Open303 decimates with one 12th-order elliptic "quarter-band" IIR in direct form II, in double precision, and keeps every fourth sample. [primary, code] It works; polyphase allpass sections are cheaper and better conditioned.

**Parameter smoothing.** Smooth knob values, not coefficients: a one-pole with τ ≈ 3 ms on each normalized knob value at base rate, applied before the knob law. Hardware pots have no lag, but hosts deliver automation in steps per block. Do not smooth the envelope, accent or slide signals; their shapes are the instrument. Resonance needs smoothing because `K` jumps excite the loop. Waveform switching is a hard switch in hardware and clicks there too; a 1 ms crossfade is a kindness.

**Denormals.** Set FTZ and DAZ for the audio callback (`juce::ScopedNoDenormals`). The sub-Hz filters in the feedback network and the envelope tails are the states that would otherwise go denormal during silence. Do not add DC offsets or noise for this purpose; a noise floor is a separate, deliberate feature.

**Numerical robustness.**

- Clamp `fRes` to [10 Hz, 0.45·fs·M]. At 1× of 44.1 kHz and 20 kHz resonance, `g` reaches 4.9 unclamped (4.56 at the 0.45·fs clamp) and Newton needed 6 iterations; at 2× and above, 4 is the worst seen.
- Newton: cap at 8 iterations, step limiter at 2 units, non-finite guard with state reset (1.7).
- The Q8 solve uses exponentials: clamp the exponent argument at 40 and limit the base-voltage step to 0.1 V per iteration.
- Double precision for all filter and envelope states. Single precision is acceptable for the audio path after the decimator.
- On sample-rate change or reset, zero all states except C13 and C11, which should start at their rest values (0 offset and about 8.5 V).

**Per-sample cost estimate at 4×48 kHz**, same 2016 core: filter 4.3 %, oscillator with the Q8 solve about 2 to 3 % (estimate, not measured), VCA and coupling filters under 0.5 %, decimator under 0.5 %, control path negligible. About 8 % there, 3 to 4 % on a current desktop core.

---

## 6. Validation

There is no hardware in the loop, so the references are, in order of authority: (a) hardware recordings where they exist (a1k0n's repository includes raw TB-303 and x0xb0x samples, unlicensed, usable for private comparison only); (b) a SPICE netlist of the service-manual circuit; (c) the continuous-time equations of section 1.1 integrated at a high rate; (d) closed-form linear results.

Build (b) first. An ngspice netlist of the VCF as Stinchcombe drew it, swept in I_f and resonance, settles R_b, K_max, the self-oscillation boundary and the low-frequency peak in an afternoon. The same goes for the Q8 shaper with the service manual's rails.

**Filter**

| Test | Procedure | Pass |
|---|---|---|
| F1 linear response | Input 1e-4 peak (linear regime). Log sweep or multitone, 5 Hz to 20 kHz, for f_res ∈ {100, 300, 1k, 3k, 10k} and r ∈ {0, 0.5, 0.9}. Compare magnitude and phase with the linearized state-space model of 1.1. | ±0.25 dB and ±3° up to min(16 kHz, 4·f_res) at 4× |
| F2 tuning | Ideal mode (no load, single feedback HP at 1 Hz), K = 17.1: measure the oscillation frequency by zero-crossing regression. | within 3 cents of `fRes`, 50 Hz to 12 kHz, at every host rate |
| F3 oscillation boundary | Stock mode: find the smallest r that sustains oscillation after an impulse, at the eight currents of table 1.4. | within 0.03 of the table (or of the SPICE result once it exists) |
| F4 nonlinear accuracy | The three cases of table 1.6 against RK4 at 128×. | at 4×: ≤ −75 dB static, ≤ −45 dB swept |
| F5 aliasing | Band-limited saw at 1244.5 Hz, peaks 0.6 and 3.0; Kaiser-windowed FFT (β = 30), mask harmonic bins, report the largest remaining component below 20 kHz. | ≤ −100 dBc and ≤ −80 dBc at 4× |
| F6 solver torture | 60 s of random knob jumps every 10 ms: `fRes` 10 Hz to 0.45·fs, r 0 to 1.2, input peak up to 6. | zero non-finite samples, zero non-converged solves, max iterations ≤ 8 |
| F7 rate invariance | Render one pattern at 44.1, 48, 88.2, 96, 176.4, 192 kHz; resample all to 48 kHz; compare third-octave spectra and envelope. | within 0.5 dB to 16 kHz |
| F8 Stinchcombe overlay | Ideal mode with ρ = 2, no load, his feedback blocks, f_c = 820 Hz, k = 0, 0.2, ..., 1: overlay on his formula. | within 0.2 dB, 2 Hz to 20 kHz |
| F9 step response | Step of 1e-4 at r = 0: the slowest time constant must be `1/(0.149·ω0)` (no load) | within 1 % |

**Oscillator**

| Test | Procedure | Pass |
|---|---|---|
| O1 pitch | Every MIDI note 24 to 96, measure f0. | within 0.5 cent (with `t_flat` = 0) |
| O2 aliasing | Saw and square at 110, 440, 1760 Hz; method of F5 on the oscillator output after decimation. | ≤ −90 dBc at 4× |
| O3 duty table | Collector-high fraction at the pitches of table 2.2 against the same circuit simulated at 32× with no BLEP. | within 0.005 |
| O4 blip | Octave jump at t = 0: fit the duty's settling time constant. | 22 ms ± 10 % |
| O5 against hardware | Single-cycle overlays against scope captures of a real unit at three pitches, once someone supplies them; adjust V_HI, V_LO, β. | visual, then harmonic levels within 1 dB to the 20th |

**Envelopes, accent, slide**

| Test | Procedure | Pass |
|---|---|---|
| E1 exactness | MEG, slide and accent states at t = 10, 50, 200 ms against the closed forms, at 44.1 and 192 kHz. | relative error ≤ 1e-9 (double) |
| E2 accent accumulation | Trains of 2 to 6 accented 16ths at 100, 125, 140, 170 BPM; compare peak sweep per note with doc 03 tables 5.3 and 5.4. | within 2 % |
| E3 slide | Octave slide: 90 % at 51 ms, 99 % at 101 ms. | within 1 ms |
| E4 event timing | Note-on at offsets 0, 1, 37, blockSize−1 in a block: output onset moves by exactly that many samples. | exact |

**VCA**

| Test | Procedure | Pass |
|---|---|---|
| V1 THD | 200 Hz sine through the VCA model at the levels of table 4; third harmonic against `tanh` series. | within 0.5 dB |
| V2 gain law | Output level against I_abc over 40 dB. | linear within 0.1 dB |

**Whole voice**

- Null test against itself across quality modes: eco against default against render, same pattern. Expect −40 dB, −60 dB residuals; a larger residual means a rate-dependent constant.
- Null tests against other emulations or hardware do not null and prove nothing, because resonant phase is chaotic in the details. Compare instead: per-harmonic level trajectories over a note (pitch-synchronous STFT), resonant peak frequency and height against time during an envelope sweep, and accent-train peak cutoff sequences.
- Listening references: Open303 (MIT, embeddable in a test harness as a second voice) and the "ideal ladder" mode of our own filter.

---

## 7. What commercial emulations disclose

| Product | Public statement | What can be inferred |
|---|---|---|
| D16 Phoscyon 2 | "Signal path was completely re-designed to achieve an unprecedented level of emulation accuracy"; "service trim pots, originally designed for calibration, and some tweaks even owners of the original silver box couldn't have dreamt of". [primary, `d16.pl/phoscyon2`, quotes re-checked 2026-10-02] No description of method, oversampling or solver. The manual (read in the verification pass) adds two things. (1) A "Quality" submenu with **separate Real-time and Offline settings**: "The higher the quality, the bigger the impact on the CPU", and the system-requirements note lists "the global quality setting, project sample rate" among the CPU factors. (2) The full calibration list: TM3 (base cutoff), C21/22 (cutoff of the VCO-to-VCA coupling high-pass), AccEnv, AccAmp, C13 (accent time constant), TM5 (volts per octave), SQR PW Shift, BA662 Clicks (on/off), BA662 Noise. [primary, Phoscyon 2 manual pp. 8, 56] | Per-unit calibration parameters are a selling point. That is what a component-parameterized model gives for free: R_b, K_max, β, capacitor ratios, rails. The quality menu implies a variable oversampling or solver effort, the same eco/default/render split as section 5. For parity, the calibration panel of implication 15 should also carry C13, the coupling-HP corner, a V/oct trim, an accent-to-VCA gain, VCA control feedthrough clicks (open question 8) and a VCA noise floor. |
| AudioRealism ABL3 | "Sophisticated analog modeling techniques have been employed to create a DSP-algorithm that accurately emulates every aspect of the original TB-303"; "3 VCO Models"; filter "is actually 24dB/oct with a 18dB response in audible range"; "Ultra natural knob response, tuned to match the original 303"; "3+ Emulation Models ranging from ABL2 to the latest ABL3". [primary, `audiorealism.se/abl3.html`] | Behavioural modeling fitted to reference units, several fitted variants rather than one parameterized circuit. |
| Roland ACB (TB-03, Cloud TB-303) | ACB "reproduces each analog component by thoroughly analyzing each detail of the original design drawings" and models how components "react with each other". [primary, Roland marketing] | Component-level simulation by the party that owns the drawings. No algorithmic detail. |
| u-he | No 303 product. Urs Heckmann on KVR: the diode ladder is about "multiple non-linearities in series", and a single nonlinearity makes "the behavior ... completely off once modulating the cutoff". [lore] Diva's accuracy modes are widely understood to select how hard an iterative zero-delay solver works. [lore] | The approach of section 1.7: solve the implicit nonlinear system per sample. |
| Arturia Acid V (Aug 2023) | Recreated with "component-level accuracy"; "the sequencer has been emulated, too". [secondary, MusicRadar launch report, `musicradar.com/news/arturia-acid-v-303-plugin`; Arturia's own page not fetched] | Another claim of component-level modeling, with no algorithmic detail. Add it to the listening-comparison set. |
| Open-source baseline | Open303: linear fitted Euler ladder, fitted fixed filters, 4× oversampling. JC303: the same plus a linear ZDF option with an input tanh. | Section 1.5. |

Nothing public says any of these solves the full coupled nonlinear ladder with its real feedback network. Nothing says they do not.

---

## Open questions

1. **R_b, the Q21 load.** 13.6 kΩ (β = 160) fits Stinchcombe's component-level plot; 28 kΩ (β = 400) is as plausible for the part. It sets how fast resonance dies at low cutoff and where the unit starts to self-oscillate (2.0 to 3.8 kHz across that range). Run SPICE on the VCF with a 2SC1583 model at 0.26 mA per side; until then expose it as a trimmer (10 to 35 kΩ; above about 35 kΩ the linear model motorboats at 5 to 7 Hz at full resonance, see 1.4) and default to 13.6 kΩ.
2. **K_max.** 18.7 from Stinchcombe's block gains (106 × 0.98 / 5.55); 19.4 if the output amp's gain is 110 as doc 02 computes from the tail current. Trimmer, 17 to 20, calibrated together with R_b.
3. **Saw top level.** The Q8 rails are settled (doc 02), but the saw's top at the JFET source is not: the service sketch says 12 V, which the follower cannot reach, and no scope capture of a stock unit was found. Duty moves about 0.05 per 0.5 V of saw top. Expose `sawHigh` (default 11.3 V per doc 02) as the pulse-width trim.
4. **Reset time and flat top.** Czech predicts a pitch-independent flat segment; nobody in the sources measured it. Parameter `t_flat`, default 0, range 0 to 50 µs.
5. **Q8 β and storage time.** β sets the soft edge's width (gain ≈ β/10). Saturation storage would delay turn-off by microseconds and is not modeled. Trimmer for β (100 to 400).
6. **Knob-to-cutoff law at rest.** Open303's 314 to 2394 Hz is a nominal cutoff, not the rest value. Through Open303's own envelope law the rest range at Env Mod 0 is 270 to 1949 Hz, against a1k0n's 289 to 1126 Hz: a factor of 1.73 at the top (1.8). a1k0n's gate-high value at full cutoff (2395 Hz) equals Open303's nominal top. Needs the control-path analysis (doc 03's Env Mod bias) reconciled with one measured unit.
7. **VCA input mix.** The resonance-wiper compensation path (doc 03, traced on a 150 dpi scan) changes level, tone above 156 Hz and VCA drive at high resonance by up to 10 dB. Confirm the trace.
8. **Control-current feedthrough.** A perfectly balanced ladder rejects the cutoff current; a real one leaks a little of every envelope step into the audio as a thump. Not modeled. If recordings show it, add `ε·d(I_f)/dt` into `x`.
9. **Temperature.** `ω0 ∝ 1/V_T` and the drive scale `∝ 1/V_T`: 0.34 % per kelvin, 6 cents of cutoff per kelvin. A "warm-up drift" feature is one multiplier on V_T.
10. **The sub-20 Hz network under nonlinearity.** The linear analysis puts a stable peak at 8 to 12 Hz. Whether it produces audible onset motion or just DC wander in the nonlinear model has not been listened to. Keep it switchable until someone has.
11. **karrikuh's and mystran's actual code** was not read (the verification pass's direct fetch of the KVR thread also returned only a bot-wall page). If a comparison against their exact formulations is wanted, fetch the KVR threads in a browser. Pirkle's AN-6 has since been read (section 0).

## Implications for the build

1. Implement the filter as section 1.7: nonlinear TPT diode ladder with Newton, in double precision, with ρ = 33/18, the Q21 coupling state, and the four-section feedback network inside the zero-delay solve. Do not port Open303's fitted polynomials and do not reuse the earlier project's ladders.
2. Define the filter's frequency input as the resonant frequency `fRes` (proportional to control current, 64.9 Hz/µA). All cutoff modulation is in octaves on `fRes`. Never label the −3 dB point "cutoff"; it is 3.25 octaves lower.
3. Feedback gain is `K = 18.7 · r_eff`, not a 0 to 4 Moog range. Self-oscillation needs K above 17 and, with the load, only happens above 2 to 4 kHz at stock settings. Offer K_max and R_b (10 to 35 kΩ) as calibration trimmers and allow r above 1 for a modified-unit mode (the Devil Fish doubles the feedback). In that mode limit R_b (about 15 kΩ at r = 2) or bypass the sub-20 Hz network, because the sub-audio loop otherwise motorboats (1.4).
4. Put the 92 Hz high-pass where the circuit has it: in the forward path inside the loop (the `q` state), not as a separate feedback filter. Drop Open303's 150 Hz, 44.5 Hz, 24.2 Hz, 14 Hz and 7.5 Hz fitted filters; the physical blocks replace them.
5. Keep all five ladder nonlinearities and Q21's. An input-only clipper is 3 to 17 % wrong.
6. Generate the oscillator at the oversampled rate with polyBLEP on the reset, and derive the square from the 2-state Q8 circuit model (section 2.3) with the BLEP height computed from the circuit. Pitch-dependent duty, droop and the post-jump blip then need no special code.
7. Run oscillator, filter and VCA nonlinearity in one oversampled loop and decimate once. Default internal rate 176.4 to 192 kHz (4× at 44.1/48). No upsampler, no ADAA in the voice.
8. Decimate with polyphase IIR half-bands: 3 coefficients for 4× → 2×, 8 for 2× → 1× (10 in render mode at 44.1 kHz). Compute coefficients at prepare time from the transition width.
9. Advance every envelope, accent and slide state with exact exponentials at base rate, using doc 03's component values. Solve the accent network in closed form per diode state; keep C13's charge across notes.
10. Split processing blocks at event offsets. Base-rate resolution is enough.
11. Smooth knob inputs with a 3 ms one-pole before their laws. Do not smooth envelopes.
12. Derive every coefficient from component values and the actual sample rate at prepare time. No constant in the voice may be tuned for a particular rate.
13. Ship the reference models in the test suite: the linearized state-space model, the RK4 integrator of section 1.1, and the high-rate Q8 simulation. Tests F1 to F9, O1 to O4, E1 to E4, V1 to V2 are the acceptance set.
14. Before tuning by ear, run ngspice on the VCF and the Q8 shaper from the service-manual schematic and replace the [derived] tables in 1.4 and 2.2 with its output. That one task closes open questions 1, 2 and 5 and narrows 3.
15. Expose the physical constants as the calibration panel: R_b, K_max, ladder capacitor ratio, input level and offset, Q8 β, saw window, `t_flat`, V_T (temperature), VCA drive. This is the Phoscyon-style "build your own unit" feature and it falls out of the model.
16. Use a tanh with error below 1e-7 in the solver (exp-based or SIMD), and reuse the final iterate's values for the state commit. Budget 2 % of a modern core for the filter and 4 % for the voice at default quality.
17. Licensing: the model is derived from circuit equations and public analysis, so the code can carry any license the project chooses. Do not copy from JC303, Surge or LMMS (GPL) or from a1k0n's repository (no license). Open303 (MIT) may be vendored as a test reference. HIIR's design procedure is WTFPL.

## Verification log

Adversarial check, 2026-10-02. Each claim was checked against the primary source opened in this pass (source code, schematic, paper, manual) or recomputed from component values. The document's own citations were not accepted as proof. "Unverified" means it could not be confirmed here, not that it is wrong.

| # | Claim | Verdict | Source checked |
|---|---|---|---|
| 1 | Linear core for ρ = 2: `1/(s⁴/2 + 4s³ + 10s² + 8s + 1 + K)`. Self-oscillation at K = 17 and √2·ω0. Poles −0.152, −1.235, −2.765, −3.848 | confirmed | Symbolic solve of the 1.1 equations (sympy). Stinchcombe `pge=diode` H_tb coefficients 8, 32, 40, 16, 1 and `ω_c = 1/(2^¾ aC)`. Zavalishin rev. 2.1.2 (5.18), (5.19), k = 17 |
| 2 | ρ = 33/18: c_res 1.3838, K_osc 17.019, −3 dB at 0.146 ω0 (0.1055 f_res, 3.24 oct). ρ = 1: 1.1952, 18.388 | confirmed | Same script |
| 3 | `ω0 = I_f/(4CV_T)`; 46.9 Hz/µA → f_res 64.9 Hz/µA. a1k0n 27.6 Hz/mV over R69 2.2 k = 60.7 Hz/µA | confirmed | Stinchcombe "ω_c = 1/(2aC) = I_f/(4CV_T)". `303.js` comment. R69 2.2K in the x0xb0x schematic |
| 4 | Nonlinear diode ladder form (tanh of adjacent differences, ρ = 2) | confirmed | Zavalishin rev. 2.1.2, equations above Fig. 6.34 (p. 201) |
| 5 | Ladder and coupling component values (C18 18 nF, 33 nF ×3, C25/C27 0.1 µF, R113/R114 100 k, VR4 50 k B) | confirmed | x0xb0x `mainboard.sch` part/value strings |
| 6 | Stinchcombe's component-level SPICE uses 18 nF | unverified | Image served without labels |
| 7 | Stinchcombe's 10-pole constants are rad/s; block corners 92, 15.517, 3.18, 1.18, 0.708 Hz | confirmed, with a correction | Formula text on `pge=diode2`. Nine constants match 2π × Hz; 38.5 vs 2π·6.164 = 38.73 is 0.6 % off (corrected in 1.3). Block-diagram labels not readable |
| 8 | In Stinchcombe's model: oscillation at 1.20/1.29/1.80 × ω_c and k = 0.92/1.06/2.47 at 8 kHz/820 Hz/100 Hz; low peak 8.7 Hz, 15 dB above the sagged passband | confirmed | Re-evaluated his published formula numerically |
| 9 | "Comfortably at around 8 Hz" quote; "all based on SPICE simulation" | confirmed | `pge=diode2`, `pge=diode` |
| 10 | Self-oscillation threshold table 1.4 and lowest oscillating f_res (1.6/2.0/3.0/3.8 kHz) | confirmed (one cell filled) | Independent eigenvalue computation of the linearized 1.1 + 1.3 model. 97 Hz/17.3 k is 5.3, not "none" |
| 11 | R_b trimmer range 10 to 45 kΩ | **corrected** to 10 to 35 kΩ | Same model: the sub-audio 5 to 7 Hz loop is unstable at r = 1 above 35 to 43 kΩ, and above about 15 kΩ at r = 2. Agrees with doc 02 section 3.4 |
| 12 | Open303 TB_303 mode: update equations, b0 rational, 6th-order k polynomial from 16.998792, g formula, exp(−3r) skew, 200 Hz to 20 kHz clamp, Approx4 at runtime, `17/4` only in Exact, shape() unused in TB_303 path | confirmed | `rosic_TeeBeeFilter.h` (the `shape()` calls in the non-303 path are also commented out) |
| 13 | Open303 fixed filters 150, 44.486, 24.167, 14.008 Hz, notch 7.5164 Hz, bandwidth 4.7; 4× oversampling; 12th-order elliptic DF-II decimator; square = −tanh(69.98·saw + 4.37) | confirmed | `rosic_Open303.cpp/.h`, `rosic_MipMappedWaveTable.cpp` (dB2amp(36.9) = 69.98), `rosic_EllipticQuarterBandFilter.h` |
| 14 | Open303 "313.8 to 2394 Hz with the envelope at rest"; factor 2 against a1k0n | **corrected** | `calculateEnvModScalerAndOffset`: these are nominal values. The rest range at Env Mod 0 is 270 to 1949 Hz, so the gap to a1k0n is 1.73×. a1k0n's gate-high value at c = 1, e = 0 is 2395 Hz |
| 15 | a1k0n fit coefficients, endpoints 289/1126 Hz and 16.9 kHz; "100Hz", "whistling fifths", "loses quite a lot of low-end" quotes | confirmed | `303.js` lines 38 to 43 and 338 to 342, `README.md` |
| 16 | JC303: GPLv3; nested G1..G4; octave mode ρ = 4 (0.837 ω_c, K 18.04 analytic vs JC303's 0.79 and "~17.8"); 0.85 ceiling and soft knee; Devil Fish ranges 30 to 3000 ms, HP 350 to 100 Hz, 0.3 to 3000 ms, 2 to 360 ms, 25 to 80 dB | confirmed | `LICENSE`, `dfl_DiodeLadderFilter.h`, `JC303.cpp`. ρ = 4 values recomputed |
| 17 | Surge XT diode ladder: K clamped to 16, `(1 + 0.3K)` compensation, GPLv3, from Odin 2 | confirmed | `sst-filters/include/sst/filters/DiodeLadder.h`, `LICENSE` |
| 18 | Pirkle AN-6 structure (a1 = 1, others 0.5) | confirmed | AN-6 PDF itself, via Wayback (newly reached). Its nonlinearity is input-only, outside the loop |
| 19 | LMMS LB302 is a 3-pole with tanh feedback from a Csound instrument | confirmed, expanded | `Lb302.cpp`. It also has a 2-pole IIR mode from Andy Sloane's gsyn |
| 20 | Q8 shaper and VCO component values | confirmed (values only) | x0xb0x schematic strings |
| 21 | Accent τ_on 50 ms and τ_off 102 ms; slide τ 22 ms; E3 51/101 ms | confirmed | C13 1 µF, R72 100K, R46 47K, C35 0.22 µF in the x0xb0x schematic; arithmetic |
| 22 | VCA input corners 72 Hz and 156 Hz | values confirmed, topology unverified | R121 220K, R122 100K, C20/C21 0.01 µF, R124 2.2K in the x0xb0x schematic |
| 23 | Forward Euler 0.1 % off at τ = 22 ms, 48 kHz | **corrected** to 0.05 % | T/2τ = 0.047 % |
| 24 | VCA table, open-filter row (3 V, 0.67, 12 %) | **corrected** to 2.7 V, 0.53, 9 % | Recomputed from w = 0.5 and the stated scale factors. The other rows check |
| 25 | tan() argument below 0.33 rad at 4× | **corrected** to 0.36 rad | π·20 k/176.4 k = 0.356 |
| 26 | Rational tanh max error 9.6e-5, clamp at 4.97 | confirmed | Numerical scan (9.58e-5; f(x) = 1 at 4.972) |
| 27 | polyBLEP residuals +d²/2 before, −(1−d)²/2 after | confirmed | Derived from the standard 2-point polyBLEP |
| 28 | ADAA `ln cosh` overflow-safe form | confirmed | Algebra |
| 29 | IIR half-band stopbands 101.8/127.9/104.1/130.0 dB at 3/8/8/10 coefficients | confirmed | HIIR closed-form attenuation formula. Group delays not re-checked |
| 30 | FIR comparison: 138 taps / 34 samples at 44.1 kHz; 31-tap gives under 50 dB | confirmed; scope narrowed | Kaiser estimate. The 31-tap limit applies to the final stage only |
| 31 | CPU percentages from ns/sample | arithmetic confirmed; timings unverified | 225 ns × 192 kHz = 4.3 %, etc. Prototype not available |
| 32 | Solver accuracy (1.6), nonlinearity contribution, filter aliasing (1.9), Q8 duty table (2.2) | unverified | Prototype code not in the repository. Pseudocode in 1.7 re-derived and consistent |
| 33 | Temperature 0.34 %/K, 6 cents/K; 20 µs flat at 1 kHz = 34 cents | confirmed | Arithmetic |
| 34 | Phoscyon 2 and ABL3 public quotes | confirmed | Live `d16.pl/phoscyon2`, `audiorealism.se/abl3.html` (2026-10-02). Phoscyon 2 manual adds quality modes and calibration list (section 7) |
| 35 | Yu et al. fit TB-303 recordings | **corrected** | arXiv 2404.07970 section 5.2: recordings of a hardware TB-303 clone |
| 36 | Kernmayer thesis: model "reaches its limits" at high cutoff and resonance | confirmed (abstract) | phaidra.ustp.at record o:4344 |

Still unreachable in this pass: the KVR threads (bot wall on direct fetch), the paywalled IEEE/DAFx full texts listed in section 0, and readable labels on Stinchcombe's block-model images. Added in this pass: Pirkle AN-6 (read), the Phoscyon 2 manual's quality and calibration details, Arturia Acid V in section 7, and the R_b stability ceiling.
