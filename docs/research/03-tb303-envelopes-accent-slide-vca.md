# 03: TB-303 envelopes, accent, slide, VCA, gate timing, output

Research date 2026-10-02. Scope: everything in the TB-303 voice that is not the oscillator or the filter core. The filter's exponential converter appears here only as far as it sets how many octaves a volt of envelope or accent is worth.

Most of this document is read directly off Roland's 1982 service-notes schematic and then simulated. Where the result disagrees with the seed report, with Open303, or with forum lore, the disagreement is stated and resolved.

## 0. Sources, tags, and what could not be reached

Confidence tags used throughout:

- **[primary]**: schematic, service notes, parts list, vendor manual, logic-analyzer or audio measurement, source code.
- **[derived]**: computed or simulated in this document from [primary] component values, with the assumptions stated. Treat as strong secondary evidence until checked against a hardware recording.
- **[secondary]**: reputable analysis by someone else.
- **[lore]**: forum consensus, unverified.

| ID | Source | Used for |
|---|---|---|
| SN | Roland TB-303 Service Notes, first edition, Feb 19 1982. https://archive.org/details/synthmanual-roland-tb-303-service-notes (PDF pages 4, 5, 8 read as images at 150 dpi). Same scans at http://machines.hyperreal.org/manufacturers/Roland/TB-303/schematics/ | All component values and topology |
| BOM | RE-303 BOM v1.2 (replica PCB that keeps Roland designators). https://sunshine-jones.com/wp-content/uploads/2018/07/RE-303-BOM_1.2_official.pdf | Cross-check of every value read off the scan |
| JS | Julian Schmidt, "Analysis of the µPD650C-133 CPU timing", 2017, Saleae capture of an original CPU. http://sonic-potions.com/Documentation/Analysis_of_the_D650C-133_CPU_timing.pdf | Gate, accent, slide, latch timing |
| RW-U | Robin Whittle, "TB-303's unique characteristics", 1999. https://www.firstpr.com.au/rwi/dfish/303-unique.html | Accent prose description |
| RW-S | Robin Whittle, "TB-303 Slide", 1999, with his own 2012 note that parts may be wrong. https://www.firstpr.com.au/rwi/dfish/303-slide.html | Slide flag semantics |
| DFM | Devil Fish user manual. https://www.firstpr.com.au/rwi/dfish/Devil-Fish-Manual.pdf | Stock timings measured by Whittle |
| X0X | x0xb0x wiki "envelopemods" (Wayback copy of https://wiki.ladyada.net/x0x/envelopemods) and firmware https://github.com/a1k0n/x0xb0x (MIT) | Mod points confirm which part does what; note/gate code |
| O303 | Open303, https://github.com/RobinSchmidt/Open303 (MIT), commit 313bf0d | How the reference emulation models these, and where it is off |
| KVR | KVR DSP forum: t=262829 (pages start=45, 60, 75, 90, 1080, 1095, 1110), t=258816, t=562991, t=629426 | antto, aciddose, mystran, Robin Schmidt ("Music Engineer") measurements |

Not reached, or reached only partly:

- The KVR threads are behind Cloudflare. They were read through a summarizing fetcher, page by page, so quotes are short and some context is lost. Only 7 of roughly 77 pages of t=262829 were read. antto's oscilloscope images were not retrievable.
- wiki.ladyada.net returns 403. Only the envelopemods page was recovered (Wayback). The x0xb0x EAGLE schematic in the repo is a binary file and was not parsed; Roland's own schematic was used instead.
- The June 2012 synth-diy mailing list thread that made Whittle add his "perhaps some of this is wrong" note to RW-S was not found.
- No published oscilloscope capture of the C13 node or the VCA control current on a stock unit was found. Every accent curve below is [derived].
- No documented noise floor or absolute output level was found.

The simulation scripts (Python, forward Euler at 25 to 50 µs, Shockley diodes with Is = 2.52 nA, n = 1.752) lived in the session scratchpad and are not part of the repo. Section 11 gives the same model as equations so the results can be reproduced.

## 1. What changes relative to the seed report and to Open303

| Topic | Seed report / Open303 / lore | What the schematic and measurements say |
|---|---|---|
| MEG decay range | "200 ms to 2 s", used by Open303 as time constants (`accentDecay = 200`, knob 200 to 2000) | Roland's 200 ms and 2.5 s are times to the **10 % point** (SN p.5 drawing). Time constants are **68 ms to 1.07 s** from the part values. Accent decay is τ ≈ 72 to 87 ms (72 ms from the parts, 87 ms if Roland's 200 ms 10 % figure is taken literally), 2.3 to 2.8 times faster than Open303. |
| Gate length | 3.5 of 6 clocks (RW-S), "about 75 to 80 %" (seed) | CPU gate is **3 of 6 clocks plus about 1.35 ms** (JS, logic analyzer). The analog gate then adds a 1 to 5 ms turn-on delay and a roughly 16 ms release. |
| Accent sweep vs resonance | "higher resonance deepens the accent sweep" (seed, and seed code `×(1+res)`) | Resonance does not add depth. It crossfades between a direct spike (res min, about 3.0 oct peak) and a lagged bump from C13 (res max, about 1.2 oct peak at 40 ms). Peak control voltage is **lower** at high resonance. |
| Accent accumulation | Seed code: instantaneous injection per onset, 180 ms leak. Open303: 15 ms one-pole, no carry-over. | Charge path 97 kΩ, discharge path 102 kΩ (res max) to 152 kΩ (res min) on 1 µF. Second accent peaks 17 to 33 % higher than the first (100 to 170 BPM), saturating by the third or fourth. |
| Accent knob | Linear depth | The sweep path has a **3.3 V bias plus a diode**. Below about 45 % of the Accent knob there is no filter accent at all, only loudness and the forced short decay. |
| "Gimmick circuit" | Seed: three transistors that dip cutoff at the end of a non-accented decay | "Gimmick" is Roland's own word (SN p.8) for the Q9/Q10/Q11 Env Mod bias: the envelope's rest level sits a third of the way below the knob's cutoff. There is no separate end-of-decay dip circuit. |
| MEG into VCA on normal notes | Open303 adds `0.45*mainEnv` to the amp envelope on every note | No such path exists. MEG reaches the VCA only through the accent switch. |
| Slide τ | Open303: 12 ms, in Hz | 100 kΩ × 0.22 µF = **22 ms**, acting on the 1 V/oct CV, so exponential in semitones. antto's model uses the same number (7.23 Hz one-pole). |
| VCA attack | "about 3 ms" (DFM) | Turn-on **delay** of 0.4 to 5.4 ms that depends on how long the gate was off, then a rise of about 0.4 to 0.8 ms. |
| Resonance pot gang 2 | "100k pot" (RW-U) | 50 kΩ linear dual (SN parts list, BOM, DFM). |

## 2. Circuit map

Rails: +12 V (envelopes, gate, VCA control), +6 V, +5.333 V reference, single supply, ground-referred. [primary, SN p.5]

CPU lines into the voice (SN p.4 and p.5, connectors 1 to 9) [primary]:

| CPU pin | Connector | Goes to | Function |
|---|---|---|---|
| PD0..PD3, PF0, PF1 | 1 to 6 | IC9 (4174 hex latch) D inputs, then IC10 (4050) and a 100k/200k R-2R ladder | 6-bit pitch, 1/12 V per code, DAC output resistance 100 kΩ |
| PE0 | 7 | IC13 (4013) D input | Accent data |
| PI1 | 8 | IC9 clock, IC13 clock, **and** R59 into Q29/Q30 | One line does three jobs: pitch latch strobe, accent latch strobe, slide on/off level |
| PI2 | 9 | R146 into Q37 | Gate |

The shared PI1 line is the root of several behaviours. Pitch and accent can only change on a rising edge of the slide line, and the accent state is held in a flip-flop that only updates when a note is latched.

Block-level signal names used below:

- **MEG**: main envelope generator. C62, charged by Q38, buffered by Q41/Q40.
- **VEG**: volume envelope generator. C42, discharged by Q32, recharged by R123.
- **ACCENT**: IC13 Q (pin 12) through R141/R140 into Q35, an NPN inverter with R139 (100 k) to +12 V, drives two sections of IC12 (4066). Because Q35 inverts, the switches close when the latched bit is **low**: the CPU's accent data is active-low. x0xb0x firmware says the same ("Note that accent is active low"), and JS notes his Fig. 14 is inverted for this reason. Polarity does not matter to an emulation, but anyone reading logic-analyzer captures should know it. [primary, SN p.5 re-read for this check; X0X `synth.c`]
- **VQ9**: 3.3 V reference bus at the emitter of Q9 (R65/R64 divide 5.333 V to 2.67 V, plus one V_EB). Every filter control input is a current into a 2.2 kΩ resistor (R71) returned to this bus. DFM independently reports 3.3 V on the Devil Fish filter CV input.

## 3. Main envelope generator (MEG)

### 3.1 Circuit [primary, SN p.5; values confirmed in BOM]

- Trigger: Q37 collector falls when the CPU gate rises. C54 (0.047 µF) and R148 (10 k) differentiate that edge into Q38 (PNP, emitter to +12 V through R150 22 Ω). Q38 saturates for roughly 0.5 to 1 ms.
- Charge: Q38 collector -> D37 -> R152 (100 Ω) -> C62 (1 µF, 35 V tantalum). Charge τ = 0.1 ms. C62 reaches about 11.2 V (12 V minus V_CE(sat) minus one diode). [derived]
- Buffer: Q41/Q40 Darlington follower with Q39 (2SK30A-Y, I_DSS 1.2 to 3 mA) as current sink. Output = V_C62 minus about 1.15 V, so **MEG peak ≈ 10.0 V**. [derived; antto's "subtract 31.37 % of the envelope height" with VQ9 = 3.27 V implies 10.4 V, KVR t=562991]
- Discharge: C62 -> R138 (68 k) -> VR6 (1 MΩ, A taper, wired as a rheostat) -> D28 -> D29 -> D26 -> ground. (The 150 dpi scan prints the middle diode as "D27", which is also the VCA accent diode's designator. The BOM lists D26, D28, D29 in this group, so read it as D29.) R117 (22 k from 5.333 V) joins at the D28 cathode and keeps D29 and D26 biased at about 0.2 mA, so C62 discharges toward about 1.5 to 1.7 V.
- Accent: one 4066 section (IC12 pins 3/4, control pin 5 = ACCENT) is wired **across VR6**. When ACCENT is high the pot is shorted and only R138 remains.

The three diodes roughly cancel the two V_BE of the Darlington, so the buffered output decays to about zero instead of running into the floor at the 10 % point.

### 3.2 Timing and shape

| Quantity | Value | Tag |
|---|---|---|
| Attack (trigger to peak) | under 0.5 ms; RC = 0.1 ms | [derived]; antto: "there's no attack, the TB-303 envelope has only decay" (KVR t=258816) |
| Decay τ, knob min, or any accented note | R138·C62 = 68 ms; fitted on the simulated curve 72 ms. Roland's 200 ms 10 % time and antto's 197 ms both imply τ_eff ≈ 85 ms. Treat **72 to 87 ms** as the stock range (C62 is a ±10 % tantalum, R138 ±5 %). | [derived] vs [primary] |
| Decay τ, knob max | (68 k + 1 M)·1 µF = 1.07 s | [derived] |
| Time to 10 %, knob min | 171 ms simulated; Roland states 200 ms | [derived] vs [primary, SN p.5] |
| Time to 10 %, knob max | 2.4 s simulated; Roland states 2.5 s | [derived] vs [primary, SN p.5] |
| Time to 50 % / 5 %, knob min | 50 ms / 226 ms | [derived] |
| Peak level | about 10 V, the same on every trigger | [derived] |
| Rest level | 0 V (follower cut off), within a few hundred mV depending on diode and V_BE matching | [derived] |

Shape: a single exponential. The diode stack makes the capacitor discharge toward about 1.5 V and the buffer subtracts about 1.15 V, so the output looks like an exponential toward a slightly positive asymptote for most of its travel and then closes to zero. A plain exponential to zero with τ_eff = (68 k + R_pot)·1 µF + 4 ms is within 5 % of the simulated curve down to the 5 % level. [derived]

DFM says "200 ms to 2 seconds" and "On accented notes, the decay time was fixed to 200 ms". Those match Roland's 10 % figures. They are not time constants. Robin Schmidt's fit to the amplitude of one of autodafe's TB-303 samples found a fast component with τ = 58 ms at weight 0.76 (KVR t=262829 start=60, re-checked: "tau1 = 1230 ms, tau2 = 58 ms, c1 = 1, c2 = 0.76"). The thread does not say whether the sampled note was accented or what the knobs were set to. If it was accented, the fast component is this envelope seen through the VCA accent threshold (section 6.3). If it was not, it is more likely the filter's MEG sweep changing the level. [hypothesis, not established] antto measured about 8704 samples at 44.1 kHz (197 ms) for the short decay by one criterion and 341 ms by another (KVR start=75, 90); 197 ms agrees with the 10 % time, 341 ms with roughly the 1 % time (4.6 τ = 331 ms). [primary measurement, criteria not stated]

Decay knob law: VR6 is a 1 MΩ A (audio) taper pot. With the usual 10 to 15 % of resistance at half rotation, mid-knob gives τ ≈ 170 to 220 ms. Model the knob as τ(x) = 1 µF · (68 kΩ + 1 MΩ · taperA(x)) + 4 ms with taperA(x) = (b^x − 1)/(b − 1), b ≈ 30 to 80. Pot tolerance is ±20 %, so maximum τ on real units spans about 0.87 to 1.27 s. [derived; taper constant is an assumption]

### 3.3 Behaviours

- **Retrigger**: every rising gate edge recharges C62 to the same peak, whatever was left. There is no accumulation in the MEG itself. [primary topology]
- **Slid or tied notes**: the gate never falls, there is no edge through C54, and the MEG is not retriggered. It keeps decaying from wherever it was. [primary topology; JS section 4.3]
- **Gate off**: no effect. There is no release stage. The capacitor keeps discharging at the same rate. [primary topology]
- **Accent latch changes mid-decay**: the discharge resistance switches instantly. An accented note that slides into a non-accented one continues with the Decay-knob rate; a non-accented note that slides into an accented one continues at the accent τ (72 to 87 ms) from its current level. [derived]
- **Accent persists through rests and after stop**: the 4013 is only clocked when a note is latched. JS measured this on the latch output: his Fig. 15 is captioned "First accent is followed by a rest. Second accent is followed by a tie and rest", and the text says "For ties, rests, and 2 accented notes after one another, the accent ... simply stays active over the whole duration and is turned off at the beginning of the next unaccented note". DFM: a machine stopped on an accented note stays in the accented state ("start it again and stop on an unaccented note"). [primary, logic-analyzer measurement]
- The MEG trigger is not delayed by the gate network in section 6.2. The filter envelope starts 0.4 to 5 ms before the VCA opens. [derived]

## 4. Env Mod

### 4.1 Circuit [primary, SN p.5 and p.8 Fig. 2; X0X mod notes confirm R61 and R63]

- VR5 (50 kΩ, A taper): pin 3 = MEG output, pin 1 -> R61 (10 k) -> VQ9 bus, wiper -> R63 (220 k) -> Q10 base.
- Q10 base also receives R72 (100 k) from the accent sweep network and has R71 (2.2 k) to the VQ9 bus.
- Q10 is a PNP follower whose emitter current is set by the Cutoff pot through R73 (100 k) + TM3 (470 k trimmer). Q11 is the NPN that sinks the ladder tail current, emitter on the VQ9 bus.

So I_tail ∝ I_cutoff-pot · exp((V_Q10base − VQ9)/V_T). The Cutoff knob enters linearly as a current (through a log pot); everything summed at Q10's base enters exponentially. antto states the same form: "Fc = (Vcutoff) * exp(Venvmod + Vaccent + TM3)" (KVR t=562991). [primary topology, secondary confirmation]

### 4.2 Scaling [derived]

One octave is V_T·ln 2 = 17.9 mV at 300 K at Q10's base. The base node is a divider of each input resistor against R71 ∥ R63 ∥ R72 = 2.13 kΩ:

- Env Mod path: 2.13 k / 220 k = 9.69 mV per volt, **K_env = 0.54 octave per volt** of (wiper voltage − VQ9).
- Accent path: 2.13 k / 100 k = 21.3 mV per volt, **K_acc = 1.19 octave per volt** of (resonance-gang wiper voltage − VQ9).

There is no temperature compensation on these. Depth in octaves falls about 0.33 % per kelvin.

Wiper voltage with the pot at resistance fraction a (0 at pin 1, 1 at pin 3):

    V_w = VQ9 + g · (V_MEG − VQ9),   g = (10 k + a · 50 k) / 60 k,   g from 1/6 to 1

    octaves_env(t) = K_env · g · (V_MEG(t) − 3.3 V)

With V_MEG from 10 V down to 0 V:

| Env Mod | g | Peak above nominal | Rest below nominal | Total sweep |
|---|---|---|---|---|
| min | 0.167 | +0.60 oct | −0.30 oct | 0.90 oct |
| max | 1.0 | +3.62 oct | −1.79 oct | 5.41 oct |

"Nominal" is the cutoff the knob would give with the summing node at VQ9, which the circuit only passes through when the MEG crosses 3.3 V. Three consequences:

1. Env Mod cannot be turned to zero. R61 leaves one sixth of the envelope. X0X: replacing R61 with a jumper is the "turn it all the way down" mod. [primary]
2. Turning Env Mod from min to max lowers the resting cutoff by **1.49 octaves** while raising the peak by 3.0 octaves. This is Roland's "gimmick" (SN p.8, Fig. 3): the envelope starts from a point VQ9 below the knob setting so that the sweep stays in the audible range. [primary description, derived numbers]
3. The split is fixed: 67 % of the sweep above nominal, 33 % below (3.3 V / 10 V). antto's figure is 31.37 % (KVR t=562991), the service notes say "about 1/3" by drawing. [primary]

Cross-check against measurement: Open303's `calculateEnvModScalerAndOffset` holds constants fitted to a real unit. Its sweep is 0.74 oct (Env Mod 0 %) to 4.51 oct (100 %) at low cutoff and 0.86 to 5.06 oct at high cutoff, with an offset of 0.29 to 0.34 of the envelope height. Ratio max/min ≈ 6.0, offset ≈ 1/3. The schematic gives ratio 6.0, offset 0.33, and absolute depth 0.90 to 5.41 oct, 4 to 22 % higher than the fit (0.90 vs 0.74 and 0.86; 5.41 vs 4.51 and 5.06). The structure agrees closely; the absolute scale should be taken from the measurement. [primary measurement via O303 source vs derived]

One loading term the divider above leaves out: the Env Mod wiper drives R63 from a source resistance of (R61 + a·50 k)·(1 − a)·50 k / 60 k, which is 8.3 kΩ at minimum, up to 15 kΩ around 40 % of the track, and 0 at maximum. That puts up to 15 kΩ in series with R63's 220 kΩ, so depth drops by about 3.6 % at minimum Env Mod and up to 6 % mid-track, and does not change at maximum. The max:min ratio becomes about 6.2. It is small, but it goes the same way as the measured fit. [derived]

Knob law: 50 kΩ A taper, so half rotation gives a ≈ 0.10 to 0.15, g ≈ 0.25 to 0.29. Most of the depth arrives in the last third of the knob. [derived]

## 5. Accent

One latched bit does four things: shorts the Decay pot (section 3), switches the MEG onto the Accent pot, and from the Accent pot's wiper feeds both the VCA (section 6.3) and the accent sweep network into the filter. This section is the sweep network.

### 5.1 Circuit [primary, SN p.5; values confirmed in BOM; prose agrees with RW-U except the pot value]

    MEG out --[IC12 pins 1/2, control 13 = ACCENT]--> VR7 pin 3
    VR7: Accent pot, 50 kΩ B (linear), pin 1 to ground, wiper = V_acc
    V_acc --> R119 47 k --> (VCA branch, section 6.3)
    V_acc --> D24 --> R46 47 k --> node A = VR4b pin 1 (anticlockwise end)
    VR4b: second gang of the Resonance pot, 50 kΩ B, node A to node B
    node B = VR4b pin 3 (clockwise end) --> C13 1 µF / 50 V electrolytic --> ground
    VR4b wiper (node W) --> R72 100 k --> Q10 base (R71 2.2 k to VQ9 = 3.3 V)

RW-U describes "a 100k pot" and "100/147 of the MEG voltage". The schematic, the parts list ("K162T00W-50kB x 2, RESONANCE"), the BOM, and Whittle's own later manual ("The Resonance pot is a dual 50k pot, with a linear taper") all say 50 k. Use 50 k.

X0X confirms the roles: "R119 controls the amplitude gain of the accent, and R46 controls the resonance gain", and puts the Devil Fish sweep-speed mods on C13.

### 5.2 Three facts that set the behaviour [derived]

**The network rests at 3.3 V.** The only DC path from C13 is through the pot and R72 into the Q10 base node, which sits at VQ9. C13 idles at 3.3 V and contributes zero cutoff offset at rest. All sweep voltages below are relative to VQ9.

**There is a threshold.** D24 conducts only when V_acc exceeds the node A voltage by a diode drop, so the wiper of the Accent pot has to rise above about 3.8 V. With a 10 V MEG peak that needs the Accent knob above 38 % unloaded, about 45 % with the pot's own source resistance and the VCA branch loading it. Below that, accent changes loudness and decay time and nothing else. At full Accent only the top of the MEG gets through: D24 stops conducting when the MEG has decayed to roughly 4 to 5 V, about 50 to 65 ms after the trigger.

**Resonance sets where the filter taps the network.** Let r be the resonance pot position (0 = anticlockwise).

- r = 0: wiper at node A. The filter sees the diode-fed divider directly: an instantaneous step that follows the MEG down. C13 still charges through the full 50 k track and adds a slow tail.
- r = 1: wiper at node B. The filter sees only C13, which charges through R46 + 50 k = 97 k and drains through R72 = 102 k. Charging τ with the diode on is C13·(97 k ∥ 102 k) = **50 ms**; with the diode off the drain τ is **102 ms**.
- In between, the wiper sees a mix of the two.

Discharge time constant at any r, diode off: τ_dis = C13 · ((1 − r)·50 k + 102 k), from **152 ms (r = 0) to 102 ms (r = 1)**. During discharge the filter sees the fraction 102 / (102 + 50·(1 − r)) of the capacitor's offset.

C13 can only charge while ACCENT is high, but whatever is left on it raises the cutoff of every following note, accented or not (mystran makes the same observation from the schematic, KVR t=629426, and reports hearing it: "takes several notes to discharge", t=262829 start=1095).

### 5.3 Single accent: shapes [derived, Accent knob at max, K_acc = 1.19 oct/V]

Cutoff offset in octaves from the sweep network alone, time measured from the gate edge:

| t (ms) | r = 0 | r = 0.5 | r = 1 |
|---|---|---|---|
| 0.2 | 3.04 | 1.60 | 0.01 |
| 5 | 2.84 | 1.61 | 0.34 |
| 10 | 2.62 | 1.60 | 0.61 |
| 20 | 2.20 | 1.53 | 0.98 |
| 30 | 1.81 | 1.40 | 1.16 |
| 40 | 1.44 | 1.24 | **1.22** |
| 60 | 0.81 | 0.88 | 1.08 |
| 80 | 0.67 | 0.75 | 0.89 |
| 120 | 0.52 | 0.54 | 0.60 |
| 200 | 0.30 | 0.29 | 0.27 |
| 300 | 0.16 | 0.13 | 0.10 |
| 400 | 0.08 | 0.06 | 0.04 |

| Resonance | Peak | Time of peak | Character |
|---|---|---|---|
| 0 | 3.05 oct | 0 ms | Spike that tracks the MEG for 50 ms, then a 152 ms tail from about 0.8 oct |
| 0.25 | 2.31 oct | 0 ms | |
| 0.5 | 1.61 oct | 5 ms | Flat top for 20 ms |
| 0.75 | 1.26 oct | 27 ms | |
| 1.0 | 1.22 oct | 40 ms | Whittle's "wow": 50 % at 10 ms, peak at 40 ms, back to 50 % at 119 ms |

In volts at r = 1 the C13 node goes from 3.3 V to about 4.33 V. The x0x wiki's "may peak at 6v" is an illustrative figure in a paragraph about relative levels, not a measurement. [lore]

Peak depth against the Accent knob, single accent:

| Accent knob | r = 1 | r = 0.5 | r = 0 |
|---|---|---|---|
| 0.4 and below | 0 | 0 | 0 |
| 0.5 | 0.05 | 0.15 | 0.28 |
| 0.6 | 0.17 | 0.35 | 0.64 |
| 0.7 | 0.34 | 0.58 | 1.05 |
| 0.8 | 0.56 | 0.84 | 1.55 |
| 0.9 | 0.84 | 1.17 | 2.17 |
| 1.0 | 1.22 | 1.61 | 3.05 |

The response is strongly convex because the pot output is linear and the threshold is fixed.

### 5.4 Consecutive accents: accumulation [derived, Accent knob at max]

Peak sweep offset per step for six accented 16ths followed by four plain ones, 125 BPM (120 ms step):

| Step | 1 (A) | 2 (A) | 3 (A) | 4 (A) | 5 (A) | 6 (A) | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| r = 1 | 1.22 | 1.51 | 1.56 | 1.57 | 1.57 | 1.57 | 0.73 | 0.23 | 0.07 | 0.02 |
| r = 0.75 | 1.26 | 1.60 | 1.68 | 1.70 | 1.71 | 1.71 | 0.74 | 0.26 | 0.09 | 0.03 |
| r = 0.5 | 1.61 | 2.03 | 2.14 | 2.17 | 2.18 | 2.18 | 0.74 | 0.29 | 0.11 | 0.04 |
| r = 0 | 3.05 | 3.36 | 3.46 | 3.49 | 3.50 | 3.50 | 0.77 | 0.35 | 0.16 | 0.07 |

At r = 1 the offset left on C13 at the start of the 2nd, 3rd and 4th accents is 0.60, 0.71 and 0.73 oct. The peak moves earlier, from 40 ms to 33 ms.

Tempo dependence at r = 1:

| BPM | 1st | 2nd | 3rd | 6th | Floor before 6th |
|---|---|---|---|---|---|
| 100 | 1.22 | 1.43 | 1.46 | 1.46 | 0.52 |
| 120 | 1.22 | 1.49 | 1.54 | 1.55 | 0.70 |
| 130 | 1.22 | 1.52 | 1.58 | 1.59 | 0.78 |
| 140 | 1.22 | 1.55 | 1.62 | 1.64 | 0.86 |
| 150 | 1.22 | 1.57 | 1.66 | 1.68 | 0.94 |
| 170 | 1.22 | 1.62 | 1.73 | 1.76 | 1.09 |

Reading the tables:

- The second accent is 0.2 to 0.4 octave above the first (2.5 to 5 semitones at the resonant peak, 100 to 170 BPM). The third adds another 0.03 to 0.11. After that it is flat. The accumulation is a two-to-three-note effect, larger at faster tempi.
- The floor between accents rises more than the peaks do: at 140 BPM the filter never falls below +0.86 oct during a run. The "squeal that climbs" is the floor and peak rising together while the MEG's own sweep, retriggered at full height each note, rides on top.
- Accent on every other step (A N A N) at 125 BPM, r = 1: peaks 1.22, 1.30, 1.31. The plain notes in between start at +0.60 to +0.65 oct and fall to about +0.2 oct by the next accent. Accumulation needs adjacent accents.
- At lower Accent settings the ratios are similar and the absolute values small: knob 0.75, r = 1 gives 0.44, 0.55, 0.58, 0.58.
- After a run, the following plain notes are brighter by 0.73, 0.23, 0.07 oct (r = 1, 125 BPM).

Whittle's ASCII sketch in RW-U draws each peak roughly 1.5 times the last. The circuit values do not support that much growth at stock; it describes the direction, and the Devil Fish "Slow" sweep mode (larger C) exaggerates it. X0X reports a 100 µF C13 taking "~10 seconds to reach its peak". [lore for the sketch, derived for the numbers]

### 5.5 Unit variation

C13 is a 1 µF aluminium electrolytic, typically −10/+50 % when new and drifting with age. R46, R72 and the pot are 5 to 20 % parts. VQ9 depends on one V_EB. The threshold knob position (about 45 %) moves with MEG peak height and VQ9. Expect ±25 % on every number in 5.3 and 5.4 between units.

## 6. VEG and VCA

### 6.1 VEG circuit [primary, SN p.5; values confirmed in BOM]

- C42 (1 µF tantalum) is discharged at every MEG trigger by Q32 through R132 + R130 (100 Ω each). The trigger reaches Q32 through D36, R134 (22 k) and C41 (0.1 µF).
- C42 then recharges toward +12 V through R123 (1.5 MΩ): τ = **1.5 s**.
- Q31 (PNP) has its base on C42, its emitter fed from the gate voltage through D35, R133 (2.2 k) and R131 (220 k), and its collector into the BA662A control pin (IC15 pin 1).

Control current: I_VEG = (V_gate − V_D35 − V_EB(Q31) − V_C42) / 222 kΩ. With V_gate = 11.9 V and C42 just discharged this is **about 47 µA**. [derived]

Normalized to its starting value:

    g_VEG(t) = max(0, 1.118 · exp(−t / 1.5 s) − 0.118),  t from the last trigger

| t | 60 ms | 120 ms | 250 ms | 500 ms | 1 s | 2 s | 3.37 s |
|---|---|---|---|---|---|---|---|
| g_VEG | 0.956 | 0.914 | 0.828 | 0.683 | 0.456 | 0.177 | 0 |
| dB | −0.4 | −0.8 | −1.6 | −3.3 | −6.8 | −15 | off |

An exponential with τ = 1.5 s, shifted down so that it reaches exactly zero at 3.4 s. DFM: "The TB-303's VEG decay was fixed at ~ 3 to 4 seconds". X0X: "R123 sets the vca decay. The stock value is 1.5M and gives a vca decay time of about 3 seconds". Robin Schmidt's fit of a real unit's slow component is τ = 1230 ms (KVR start=60; Open303 `ampEnv.setDecay(1230.0)`); the formula above, fitted as a pure exponential over its first second, gives 1.27 s. [primary measurements agree with derived]

Sustain is zero. A chain of slides longer than 3.4 s goes silent on unaccented notes, because nothing retriggers C42.

Second-order: C41 has no discharge path except Q32's base, so Q32 keeps conducting weakly for tens of ms after the trigger and holds C42 near zero. The VEG is therefore flat for roughly the first 50 to 100 ms before the 1.5 s decay starts. The difference from the formula is under 0.5 dB. Ignore it. [derived]

### 6.2 Gate shaping: turn-on delay, attack, release [primary topology, derived timings, confirmed by DFM and antto]

The gate that feeds the VCA is not the CPU line. Q37 inverts PI2, and Q36 (PNP) re-creates a 12 V gate. Q36's base has R142 (10 k) and **C72 (10 µF)** to the +12 V rail and R143 (10 k) to Q37's collector.

Turn-on: C72 has to charge to about 0.55 V through R143 before Q36 conducts. From empty that takes 5.4 ms. After gate-off C72 discharges through R142 with τ = 100 ms, so if the previous note ended recently the delay is shorter:

    d_on ≈ 5.4 ms · (1 − exp(−(t_off − 17 ms) / 100 ms)),  floor about 0.3 ms

where t_off is the time the CPU gate was low before this note.

| Situation | t_off | d_on (simulated) |
|---|---|---|
| First note after a long pause | long | 5.4 ms |
| 16ths at 40 BPM | 186 ms | 4.5 ms |
| 16ths at 125 BPM | 59 ms | 1.9 ms |
| 16ths at 300 BPM | 24 ms | 0.4 ms |

DFM: "the start of the audible note was between 1 and 5 ms (typically 4 ms) after the start of the gate signal". antto: "the VCA envelope may take a surprisingly long time to charge up before it opens, iirc up to 5ms" (KVR t=262829 start=1110). [primary measurements]

Attack: once Q36 starts conducting its collector rises exponentially and saturates. Simulated 10 to 90 % rise is **0.36 ms**. antto measured the fade-in from audio as "20-35 samples long (at 44100Hz)", 0.45 to 0.8 ms (KVR t=258816). DFM says "a 3 ms attack time". The two measurements conflict; the circuit supports antto. Use 0.5 ms as the default and expose it. [derived, primary vs primary]

Release: when the CPU gate falls, C72 keeps Q36 saturated while it discharges through the transistor's own base-emitter junction and R142, then the collector voltage collapses.

| | Simulated (h_FE 120 to 400) | DFM measurement |
|---|---|---|
| Hold at full level after CPU gate-off | 7.8 to 11.9 ms | "8 ms of normal volume" |
| Fall, 90 to 10 % | 5.8 to 6.1 ms | "8 ms of linear decay" |
| Silent after | 16 to 20 ms | "about 16 ms" |

The fall is the top of an exponential (τ about 4.5 ms) cut off by the two-diode threshold in the VCA path, which is why it looks linear on a scope.

Net effect on a plain 16th at 125 BPM: CPU gate 61.4 ms, sound from about +2 ms to about +69 ms at full level, silent by +77 to +81 ms. Audible length is about 62 % of the step.

### 6.3 Accent into the VCA [primary topology, derived numbers]

    V_acc --> R119 47 k --+--> R120 22 k --> D27 --> BA662 pin 1
                          |
                         C36 0.033 µF to ground

The series diode and the control pin's own junction make a threshold of about 1.2 V at the Accent wiper (pin 1 assumed at 0.7 V; the BA662 has no public datasheet figure for this). Above it, I_acc ≈ (V_acc − 1.2 V) / 69 kΩ. C36 with 47 k gives a 1.55 ms lag before the diode conducts and about 0.5 ms after; the 10 to 90 % rise of I_acc is about 1.2 ms. RW-U: "Via an RC network of a 47k and a 0.033uF to soften the attack a little".

Simulated I_acc, Accent knob at max: 60 µA at 0.5 ms, 121 µA at 2 to 3 ms, 109 µA at 10 ms, 93 µA at 20 ms, 66 µA at 40 ms, 46 µA at 60 ms, 21 µA at 100 ms, 4 µA at 150 ms. The decay looks like τ ≈ 60 ms because of the threshold. That matches the "tau2 = 58 ms" Robin Schmidt fitted to a sample of a real unit, but the thread does not say whether that note was accented (section 3.2), so the agreement supports this model without proving it.

The OTA's gain is proportional to total control current, so the level boost is (I_VEG + I_acc)/I_VEG:

| Accent knob | Peak I_acc | Peak boost |
|---|---|---|
| 0.13 and below | 0 | 0 dB |
| 0.3 | 22 µA | +3.3 dB |
| 0.5 | 44 µA | +5.7 dB |
| 0.75 | 74 µA | +8.1 dB |
| 0.9 | 99 µA | +9.8 dB |
| 1.0 | 122 µA | +11.0 dB |

The boost is a decaying bump: at full Accent it has fallen from +11 dB to +5.7 dB by 60 ms. Schmidt's fitted weight for the fast component was 0.76 of the slow one at an unstated Accent setting, which corresponds to a knob position near 0.45 in this model. The absolute scale is the least certain number in this document; calibrate it by ear or by measurement and keep the shape. [derived; primary measurement with unknown knob position]

Three behaviours follow from the topology:

1. **Accented notes speak earlier.** The accent current does not pass through the delayed gate. The VCA opens within about 1 ms of the CPU gate on an accented note and 2 to 5 ms later on a plain one.
2. **Accented notes have a tail.** After gate-off I_VEG stops within 16 ms but I_acc continues until the MEG falls below the threshold. At full Accent and 125 BPM the tail is still 46 µA (the level of a plain note) at gate-off and reaches zero about 150 to 160 ms after the note started. antto: "ACCENT actually adds a tail on the VCA on accented notes" (KVR t=258816). RW-S: after gate-off "the volume drops to nothing very quickly, unless it is an accented note, in which case the Main EG drives the VCA as well". Open303 approximates this with `accentAmpRelease = 50` ms.
3. The VCA accent path is independent of the Resonance knob.

### 6.4 BA662A and the signal path [primary topology, derived levels]

- Signal input (pin 3) is fed from two places: the filter output through R121 (220 k) + C21 (0.01 µF), and the **Resonance pot's first-gang wiper** through R122 (100 k) + C20 (0.01 µF). R124 (2.2 k) to the bias node completes a divider. The second path adds up to 2.2 times the first as resonance rises, a level compensation of up to **+10 dB at full resonance**. The trace was followed on a 150 dpi scan; confirm it against the filter research document.
- Divider ratios: 2.2 k / 222 k = 0.0099 for the direct path, 2.2 k / 102 k = 0.0215 for the resonance path.
- First-order high-pass corners formed by C21 and C20: 72 Hz (direct) and 156 Hz (resonance path). DFM reports the stock unit "32 Hz was down by 5 dB"; a single 72 Hz pole gives −7.8 dB at 32 Hz. Open303 puts a 44.5 Hz one-pole high-pass **before** its filter (`highpass1`) and a 24.2 Hz one-pole after it (`highpass2`), plus a 150 Hz high-pass in the feedback path. For a small signal, the two outer high-passes give −4.7 dB and −2.0 dB at 32 Hz, −6.7 dB together, wherever they sit in the chain. So the three sources fall within 3 dB of each other at 32 Hz: −5 dB measured by Whittle, −6.7 dB fitted by Open303, −7.8 dB from the schematic. A ±10 % tolerance on C21 and the in-phase resonance path could account for part of the gap. Where the corners actually sit (before the filter, which changes how hard the filter is driven, or after it) is for the filter research document. It is flagged here because the 72 Hz corner is physically at the VCA input.
- Absolute level, the one number the service notes give: the VCF adjustment (SN p.4, "Check point: TP6", Cutoff centre, Resonance full clockwise, Env Mod/Decay/Accent full anticlockwise, adjust TM3 for a 2 ms ± 0.5 ms ring period) draws the TP6 waveform as ringing between about 5.2 V and 5.8 V. That is roughly 0.6 V peak-to-peak of resonant ringing at the filter output. Through the 0.0099 and 0.0215 dividers this gives about 3 to 10 mV peak at the OTA input, which agrees with the "10 mV" figure used below. [primary drawing, approximate]
- OTA law: I_out = I_ABC · tanh(V_diff / 2V_T). Gain is linear in control current. With the load at pin 6 of (22 k + 47 k) ∥ 220 k = 52.5 kΩ and I_ABC = 47 µA, the gain from filter output to VCA output is about 0.47 (−6.5 dB) on a plain note and up to 1.7 (+4.6 dB) at the peak of a full accent.
- Nonlinearity: the differential pair sees about 1 % of the filter output. 10 mV peak there gives V_diff/2V_T = 0.19, a third harmonic near −50 dB. At high resonance the compensation path roughly triples the drive and the tanh compresses audibly. The amount depends on the filter's absolute output level, which is the filter document's number.
- The output of the VCA goes through C38 (1 µF) to the Volume pot.

### 6.5 Clicks

Sources of the note-start click, all [derived] unless noted:

- The VCA opens in 0.4 to 0.8 ms on a free-running oscillator (no phase reset anywhere in the circuit), so the onset catches the waveform at an arbitrary point.
- On accented notes the control current steps by up to 3.6 times in about 1 ms.
- OTA control feedthrough (offset times the control step) is AC-coupled by C38 and produces a thump that is present even with the filter closed. Unit-dependent, not documented for the BA662. [lore]
- Open303 smooths its amp envelope with a 200 Hz two-pole low-pass (rise about 1.8 ms), which is slower than hardware.

## 7. Slide

### 7.1 Circuit [primary, SN p.5; DFM "Slide Slew Times" describes the same thing]

The DAC's 100 kΩ output resistance feeds IC11b (AN6562, follower). Two 4066 sections and C35 (0.22 µF) sit around it, driven in antiphase by Q29/Q30 from the slide line:

- Slide off: C35 is connected to the op-amp **output**. It tracks the pitch CV and does nothing.
- Slide on: C35 is connected to the op-amp **input**, across the DAC. The CV becomes a one-pole lag with τ = 100 kΩ · 0.22 µF = **22 ms**.

Because C35 follows the output while slide is off, a slide always starts from the pitch actually sounding.

### 7.2 Numbers

| Quantity | Value | Tag |
|---|---|---|
| Time constant | 22 ms | [derived from primary values]; antto models it as a 7.23 Hz one-pole (22.0 ms), KVR start=1080; Gordonjcp "22ms time constant", start=1110 |
| Domain | Linear in the 1 V/oct CV, so exponential approach in semitones | [primary topology] |
| Interval dependence | None. Constant time, any interval. | [primary topology] |
| 90 % / 95 % / 99 % settled | 51 ms / 66 ms / 101 ms | [derived] |
| "Slide time" as quoted | 60 ms (DFM, "approximate audible time"), 105 ms at 99 % (KVR start=1080: the value found when an x0xb0x was matched by ear to Xhip's 99 % glide; the summarizer attributes it to antto, the 99 % convention to aciddose) | [primary measurement], both consistent with τ = 22 ms. One KVR post (aciddose, start=1110) computes "2pi * 200e3 * 220e-9 = ~276 ms". That uses the wrong resistance and a spurious 2π; Gordonjcp corrects it in the same thread to 100 kΩ · 0.22 µF = 22 ms. |
| Residual after one 16th at 125 BPM | exp(−120/22) = 0.4 %: an octave slide is 5 cents short at the next boundary | [derived] |
| Residual at 300 BPM (50 ms step) | 10 %: an octave slide is 124 cents short | [derived] |

Open303 slews frequency in Hz with τ = 0.2 · 60 ms = 12 ms. Both the domain and the constant differ from hardware.

Unit variation: 0.1 % ladder resistors, a 5 to 10 % film capacitor. Slide time is the most consistent timing in the instrument.

### 7.3 When a slide happens [primary: JS sections 3, 4.3, 6; RW-S; X0X firmware]

The slide flag is stored on the note being slid **from**. Two things follow from one flag:

1. At the point where that note's gate would have gone low, it stays high and runs into the next note. The next note gets no gate edge: no MEG retrigger, no VEG reset.
2. At the start of the next note the slide line is raised (which is also the latch edge that loads the new pitch and accent) and held high for that entire note, including any tied steps and following rests. It drops at the start of the next note that is not slid into.

So the glide occupies the destination note, starting exactly at its step boundary. Nothing moves before the boundary. RW-S was written to kill the rumour that the sequencer anticipates.

Details of the shared latch line (JS):

- Every non-slid note starts with a **44 µs** pulse on the slide line to latch pitch and accent. The lag circuit is engaged for those 44 µs, which moves the CV by 0.2 % of the interval. Inaudible.
- Inside a multi-note slide the line is pulled low for **8.75 µs** to make the next latch edge. C35 is on the op-amp output for that instant and is pulled toward the old target (the DAC still holds it). How far it moves depends on the AN6562's output resistance and current limit, which were not measured: from about 2 % to roughly a third of the remaining slide residual. The residual is 0.4 % of the interval at 125 BPM, so this is inaudible at normal tempi and at most a few cents near 300 BPM. [derived, op-amp drive unverified] A chain of slides is therefore continuous: each new target is approached from wherever the CV is.
- Pitch on a normal note changes at the latch, within the same interrupt pass as the gate edge. The x0xb0x firmware does latch, then about 10 µs, then gate. Since the VCA opens 0.4 to 5 ms later, a non-slid pitch is always settled before sound.

## 8. Gate and timing

All figures in this section are [primary] from JS unless marked.

| Quantity | Value |
|---|---|
| Clock | 24 ppqn. One step = 6 clocks in normal mode. |
| Triplet mode | One step = 8 clocks, gate 5 on and 3 off |
| Tempo range | 40 to 300 BPM (SN p.1 specification): step 375 ms to 50 ms. The top end is a trimmer (TM1, TP1). JS's unit reached 308 BPM (48.7 ms step). |
| CPU interrupt | 1.8 ms, free-running, not locked to the tempo clock. It is set by trimmer TM2 to "1.8 ms ± 0.2 ms" (SN p.4 adjustment, TP2), so it varies from unit to unit, and so does the tempo where the beating disappears |
| Gate-on latency after clock rising edge | 0.8 to 3.3 ms, mean 2.06 ms, σ 0.74 ms |
| Gate-on after the interrupt that sees the clock | 0.917 to 1.062 ms |
| Gate-off after its interrupt | 2.3 to 2.4 ms |
| Normal note gate length | 3 clocks plus the difference of the two latencies |
| Gate on-time, 102.3 ms step (147 BPM) | 52.35 to 52.67 ms (read off JS Fig. 8): 51.3 % |
| Gate on-time, 48.7 ms step (308 BPM) | about 25.7 ms, occasionally 23.2 ms (JS Fig. 7): 52.8 % |
| Duty cycle range reported | 49.96 % to 55.8 %; 50.25 to 50.5 % at the slowest tempo, 55 to 56 % at the fastest |

Working rule: **CPU gate length = T_step / 2 + 1.35 ms**. (The mean latencies give the same thing: gate-off is 3.51 ms after its clock edge on average, gate-on 2.06 ms, a difference of 1.45 ms.) The variation comes from the 1.8 ms interrupt beating against the tempo clock, and it is **not** a symmetric ±1 ms scatter. In JS Figs. 7 to 10 most gate lengths sit within about 0.3 ms of the working value. A minority are about 2.3 to 2.5 ms shorter, which is one interrupt slip (Fig. 7: 25.7 ms typical, occasional 23.2 ms; Fig. 10: 49.8 ms off-time typical, one 47.5 ms). Onset jitter against the ideal grid is larger: 0.8 to 3.3 ms after the clock edge, σ 0.74 ms. JS's text range of duty cycles (49.96 to 55.8 %, "55 to 56 %" at the fastest tempo) is wider than his Fig. 7 shows (52.8 %, at most 53.8 %); the figures are taken as the better evidence. With a 1.8 ms interrupt, the beating vanishes at 119.90 BPM (278 interrupts per beat, JS eq. 1). On a unit trimmed to 1.6 or 2.0 ms that tempo moves.

RW-S gives 3.5 clocks on, 2.5 off (58 %). JS measured the CPU pin with a logic analyzer, with statistics, eighteen years later and with RW-S as a cited reference; RW-S itself now carries a note that it may be wrong. JS is believed. The 58 % figure is close to what the **audible** gate looks like once the analog release in section 6.2 is added, which may be what was observed. The seed report's "75 to 80 %" comes from a third-hand reading of RW-S and is not supported.

Note types:

- **Plain note**: latch (44 µs pulse), gate on, gate off half a step plus 1.35 ms later.
- **Note with slide flag, followed by a note**: gate stays on through the boundary. The chain's gate ends half a step plus 1.35 ms into the last note of the chain. antto: gated length "is either 0.5 or 1.5 (slided) or 2.5 (two slides)" steps (KVR start=45).
- **Tie**: gate stays on through tied steps and ends half a step into the last one. No latch, no pitch change. The slide line reads high during ties and following rests with no effect on pitch.
- **Rest**: nothing is latched, gate stays low, the accent flip-flop keeps its last value.
- **Accent**: the raw CPU accent line is high for 2 of the 3 gate-on clocks and has stray pulses; none of that matters because IC13 latches it. The latched accent lasts a whole step and through any ties and rests until the next latched note.

The mean 2 ms gate-on latency is a constant offset against an external clock and has no audible effect inside the instrument. The onset jitter (σ 0.74 ms) and the occasional one-interrupt-short gate are audible only as a faint unevenness in accentuation at fast tempi, per JS ("a slight beating in the note accentuation can be heard").

Pattern end and loop: the CPU logic is per step. No source describes special handling at the loop point, and x0xb0x firmware treats the wrap like any other step boundary, so a slide flag on the last step ties into the first step of the next pass. [secondary]

## 9. Output stage [primary, SN p.5; values confirmed in BOM]

    BA662 buffer out (pin 8) --> C38 1 µF --> VR8 Volume 50 kΩ A --> wiper
    wiper --> R155 47 k --> C59 1 µF --> Q34 emitter (summing node)
    Mix In --> R156 100 k --> C58 1 µF --> same node
    Q34 (NPN, base biased from 5.333 V via R157, C52) + Q33 (PNP), feedback R153 100 k, collector load R160 10 k
    --> C56 1 µF --> R161 1 k --> OUTPUT jack (R165 100 k to ground)
    --> R159 33 k --> C51 2.2 µF --> IC14 LA4140 headphone amp --> 2 × 10 Ω --> phones

| Quantity | Value | Tag |
|---|---|---|
| Mixer gain, synth | −100 k / 47 k = −2.13 (+6.6 dB), inverting | [derived] |
| Mixer gain, Mix In | −1.0; SN spec: "MIX In: Impedance 100 kΩ, Gain: Unity" | [primary] |
| Output impedance | SN spec says 10 kΩ; the series part is R161 = 1 k | [primary] |
| Polarity | The mixer is an inverting stage (schematic). Whether the BA662 inverts depends on which of pins 2 and 3 is the + input, and that cannot be read off the scan. DFM ("Idiosyncrasies") says the main output is opposite in polarity to the Devil Fish's own "Filter Audio Out", which is taken through Devil Fish circuitry, so it does not settle the stock filter-to-output polarity. | [derived for the mixer; net polarity unverified] |
| Coupling high-passes | C38 into the pot: 3.2 to 6.6 Hz depending on Volume. C59·R155: 3.4 Hz. C56 into the load: 1.6 Hz (no load, R165 only) to 15.8 Hz (10 k load). | [derived] |
| Clipping | "With high levels of signal coming out of the VCA and with the Volume pot set at or near maximum, some clipping can occur in the output amplifier. This typically only happens on the loudest (for instance the accented) notes" (DFM) | [primary observation] |
| Bass loss | "32 Hz was down by 5 dB" on a stock unit (DFM). Open303's fitted high-passes give −6.7 dB and the 72 Hz VCA-input pole gives −7.8 dB (section 6.4) | [primary measurement, method not stated] |
| Headphone | LA4140 power amp, same signal, mono to both ears | [primary] |
| Noise floor, absolute level | Not documented in any source read. The only absolute level found is the filter-output ringing of about 0.6 V p-p at TP6 in the SN VCF adjustment (section 6.4). The SN specification lists "Output: Impedance 10 kΩ" and no output level. A web search on 2026-10-02 found no measured line-out level or noise floor either. | |

Open303 represents the whole post-filter chain as a 24.2 Hz high-pass, a 14 Hz all-pass and a 7.5 Hz notch, fitted to recordings. Those are the right order of magnitude for three cascaded 1 µF couplings plus the load.

## 10. Interactions worth exploiting

Each of these falls out of the circuit and can be put in front of the user as pattern or knob behaviour. [derived unless marked]

1. **Accent ignores the Decay knob.** With Decay long, plain notes sweep slowly (τ up to 1.07 s) and accented notes blip (τ 72 to 87 ms). The contrast is larger with Decay up.
2. **Accent after a slide depends on the Decay knob.** A slid-to note is not retriggered, so its accent works on whatever MEG is left. Simulated at 125 BPM, r = 1, Accent max, one plain 16th sliding into an accented one:

   | Decay knob | MEG at the boundary | VCA accent current | Filter accent peak |
   |---|---|---|---|
   | min | 1.9 V | 11 µA (+2.0 dB) | none |
   | mid (218 k) | 5.9 V | 65 µA (+7.9 dB) | 0.29 oct |
   | max | 9.0 V | 107 µA (+10.8 dB) | 0.97 oct |

   With Decay high, sliding into an accent gives a swell with no attack click and then a fast filter close, because the accent also switches the remaining decay to the accent τ (72 to 87 ms).
3. **Sliding out of an accent** hands the rest of the MEG back to the Decay knob and drops the VCA boost within a millisecond.
4. **Resonance reshapes the accent.** Low resonance: a 3-octave click that is mostly gone in 60 ms. High resonance: a 1.2-octave swell peaking at 40 ms. The second is the acid "wow"; the first is a percussive ping. Mid settings give a flat-topped 1.6-octave pulse.
5. **Runs of accents climb**, by 0.3 to 0.4 octave on the second note and a little more on the third, more at higher tempo. The filter floor between accents rises too.
6. **Notes after an accent are brighter.** At r = 1 and 125 BPM, the next three plain 16ths carry +0.6 to +0.73, +0.23 and +0.07 octaves.
7. **Env Mod near minimum with Accent high** isolates the accent sweep: plain notes move 0.9 octave, accented notes add up to 3. Whittle's instruction for hearing the sweep modes is exactly this, with the filter near self-oscillation (DFM).
8. **Accent knob below about 45 %** gives a loudness-and-snap accent with no filter sweep. Above it the sweep depth grows steeply.
9. **Env Mod lowers the base cutoff.** Raising Env Mod by itself darkens the resting tone by up to 1.5 octaves. Players compensate with Cutoff; the two knobs are coupled by design.
10. **Accented notes are longer and earlier**: about 1 to 4 ms earlier onset and an 80 to 100 ms amplitude tail after gate-off at full Accent.
11. **Accent across rests.** A rest after an accented note keeps the accent switches closed: the MEG finishes its fast decay into both the VCA tail and C13.
12. **Long ties fade.** The VEG has no sustain. A held note is 6.8 dB down after 1 s and silent at 3.4 s unless accented notes within the chain keep feeding the VCA from the MEG, which is itself not retriggered.
13. **Fast tempi shorten slides audibly.** Above about 200 BPM a slide no longer reaches pitch within a step.

## 11. Reference model

State in volts, times in seconds. dt is one control tick; audio rate is fine and nothing here is stiff below 0.5 ms.

    constants
      VQ9 = 3.3      V_MEG_PK = 10.0     K_ENV = 0.54 oct/V     K_ACC = 1.19 oct/V
      C13 = 1e-6     R46 = 47e3     RP = 50e3     R72 = 102.1e3     V_D24 = 0.45

    on note start (step boundary + latency):
      accentLatched = note.accent
      if previous note had slide flag:      # tie + glide, no retrigger
          slideOn = true
      else:
          slideOn = false; cv = target      # instant pitch
          vmeg = V_MEG_PK                   # optional 0.1 ms rise
          tVEG = 0
          cpuGate = high;  schedule cpuGate low at T_step/2 + 1.35 ms
                           (cancel if this note has the slide flag and the next step is a note)

    every tick:
      # MEG
      R    = accentLatched ? 68e3 : 68e3 + 1e6 * taperA(decayKnob)
      vmeg *= exp(-dt / (R * 1e-6 + 0.004))

      # Env Mod
      g       = (10e3 + 50e3 * taperA(envModKnob)) / 60e3
      oct_env = K_ENV * g * (vmeg - VQ9)

      # Accent sweep network (u = V_C13 - VQ9, w = wiper - VQ9)
      Ra = res * RP;  Rb = max((1 - res) * RP, 1e3)
      Rs = R46 + accKnob * (1 - accKnob) * 50e3 + Ra
      vacc = accentLatched ? accKnob * vmeg / (1 + accKnob * (1 - accKnob) * 50e3 / 69e3) : 0   # wiper, loaded by the VCA branch
      s  = accentLatched ? vacc - V_D24 - VQ9 : -inf
      w_off = u * R72 / (R72 + Rb)
      if s > w_off:
          w  = (s / Rs + u / Rb) / (1 / Rs + 1 / Rb + 1 / R72)
      else:
          w  = w_off
      u += (w - u) / (Rb * C13) * dt
      oct_acc = K_ACC * w

      fc = fc_nominal(cutoffKnob) * 2 ^ (oct_env + oct_acc)

      # VCA
      vN     = 12 - 11.85 * exp(-tVEG / 1.5);   tVEG += dt
      i_veg  = max(0, vGateAnalog - 1.15 - vN) / 222.2e3
      i_acc  = lp1(max(0, vacc - 1.2) / 69e3, tau = 1 ms)
      gain   = (i_veg + i_acc) / 47.7e-6        # 1.0 = plain note at onset

      # Slide
      if slideOn: cv += (target - cv) * (1 - exp(-dt / 0.022))   # cv in volts/octave or semitones
      else:       cv = target

    vGateAnalog (from cpuGate):
      rise:  delay d_on = 5.4 ms * (1 - exp(-(t_off - 17 ms) / 100 ms)), then 0 -> 11.9 V in about 0.5 ms
      fall:  hold 8 ms, then 11.9 V * exp(-t / 4.5 ms)   (i_veg reaches 0 about 8 ms later)

`fc_nominal` is the cutoff with the summing node at VQ9 and belongs to the filter research document. The clamp Rb ≥ 1 kΩ keeps the r = 1 case from becoming stiff and changes nothing audible.

This reduced model was run against the full simulation (Shockley diodes, C36 node, diode-stack MEG). It reproduces the tables in 5.3 and 5.4 within 2 % at full Accent, and within 0.05 octave and 4 µA at Accent 0.4 to 0.75. [derived]

Scaling choice: K_ENV and K_ACC above are the schematic values. The measured Env Mod depth in Open303 is 4 to 22 % lower (section 4.2). Multiply both by a single "CV scale" trim (default about 0.88) so that envelope and accent stay in their hardware ratio.

## Measurable acceptance targets

Procedure for all: render at 96 kHz with the internal sequencer, drive off, and read the model's control signals directly where possible (cutoff in octaves, VCA gain, pitch CV). Audio-based checks use the resonant peak frequency at high resonance as the cutoff probe.

| # | Target | Value | Tolerance | Source |
|---|---|---|---|---|
| 1 | MEG time to 10 %, Decay min | 171 to 200 ms | ±15 % | SN p.5 (200 ms), derived (171 ms) |
| 2 | MEG time to 10 %, Decay max | 2.4 to 2.5 s | ±20 % (pot tolerance) | SN p.5, derived |
| 3 | MEG τ on any accented note, Decay knob anywhere | 72 to 87 ms (10 % point 170 to 200 ms) | within band | derived 72 ms; SN p.5 and antto (197 ms to 10 %) imply about 85 ms |
| 4 | MEG attack | under 0.5 ms to 90 % | upper bound | derived, antto |
| 5 | MEG on a slid-to note | no retrigger; continuous through the boundary | exact | SN topology, JS 4.3 |
| 6 | Env Mod total sweep, knob min / max | 0.74 to 0.90 oct / 4.5 to 5.4 oct | within that band | O303 measured constants, derived |
| 7 | Env Mod max:min depth ratio | 6.0 | ±5 % | SN (R61, VR5) |
| 8 | Fraction of sweep below the knob's nominal cutoff | 0.33 | ±0.04 | SN p.8, antto 31.37 %, O303 0.29 to 0.34 |
| 9 | Rest cutoff shift, Env Mod min to max | −1.25 to −1.5 oct | within band | derived, scaled by row 6 |
| 10 | Accent sweep, single, Accent max, res max: peak and time | 1.0 to 1.25 oct at 40 ms | ±25 % depth, ±8 ms | derived |
| 11 | Same, 50 % points | rise at 10 ms, fall at 119 ms | ±20 % | derived |
| 12 | Accent sweep, single, Accent max, res min | 2.6 to 3.05 oct at t = 0, under 1 oct by 60 ms | ±25 % | derived |
| 13 | Six accented 16ths at 125 BPM, res max: peaks | ratios 1 : 1.24 : 1.28 : 1.29 (flat after the 4th) | ±0.05 on each ratio | derived |
| 14 | Same at 140 BPM | 1 : 1.27 : 1.33 : 1.34 | ±0.05 | derived |
| 15 | Residual on the three plain 16ths after that run, 125 BPM, res max | 0.73, 0.23, 0.07 oct | ±30 % | derived |
| 16 | C13 discharge τ, res max / res min | 102 ms / 152 ms | ±20 % | SN values |
| 17 | Accent knob threshold for any filter sweep | 0.40 to 0.48 of travel | within band | derived |
| 18 | VEG level at 1 s into a held note, relative to onset | −6.8 dB | ±1 dB | SN values; Schmidt τ = 1230 ms gives −7.1 dB |
| 19 | VEG reaches silence on a held, unaccented note | 3.0 to 4.0 s | within band | DFM, X0X, derived 3.4 s |
| 20 | VCA accent peak boost, Accent max | +11 dB | ±3 dB (see open question 2) | derived |
| 21 | VCA accent bump decay | τ 58 to 65 ms | ±15 % | Schmidt fit, derived |
| 22 | Accented note amplitude tail after gate-off, Accent max | reaches zero 150 to 160 ms after note start | ±20 ms | derived |
| 23 | Plain note turn-on delay after CPU gate | 5.4 ms after a long pause; 1.9 ms in 16ths at 125 BPM | ±1 ms | derived; DFM 1 to 5 ms, antto up to 5 ms |
| 24 | VCA rise time, plain note | 0.4 to 0.8 ms | within band | antto, derived |
| 25 | Plain note release | full level for 8 ms after CPU gate-off, silent by 16 to 20 ms | ±3 ms | DFM, derived |
| 26 | Slide τ | 22 ms; 90 % at 51 ms; 99 % at 101 ms | ±5 % | SN values, antto 7.23 Hz |
| 27 | Slide law | exponential in semitones, identical time for 1 and 24 semitones | exact | SN topology |
| 28 | Slide start | at the destination step boundary, never earlier | under 0.1 ms early | JS, RW-S |
| 29 | CPU gate length, plain note | T_step/2 + 1.35 ms | ±0.3 ms on most notes; in jitter mode, an occasional note about 2.4 ms short and onset scatter σ ≈ 0.7 ms | JS text and Figs. 7 to 10 |
| 30 | Gate length, n slid notes | (n − 0.5) · T_step + 1.35 ms from the first onset | same | JS, antto |
| 31 | Mixer stage polarity | mixer inverts (net filter-to-output polarity unverified, section 9) | exact for the mixer | SN topology |

## Parameters worth exposing as trimmers

"Stock" is the value above. Ranges cover unit tolerance and the common mods (Devil Fish, x0xb0x). Group them on an advanced or "under the hood" page; none belongs on the front panel except where Phoscyon-style parity puts it there.

| Trimmer | Stock | Sensible range | Why |
|---|---|---|---|
| Slide time (τ) | 22 ms | 5 to 130 ms | Devil Fish: 60 to 360 ms audible, 2 to 300 ms from CV, which is τ ≈ 22 to 130 ms |
| Accent decay (MEG τ on accented notes) | 72 ms (parts); 85 ms matches Roland's 200 ms 10 % spec | 10 ms to 1.3 s | Devil Fish Accent Decay pot: "30 ms and 3 seconds" (DFM, definition not stated; τ ≈ 13 ms to 1.3 s if those are 10 % times) |
| Normal decay range (min / max τ) | 70 ms / 1.07 s | min 10 to 100 ms, max 0.8 to 1.5 s | Pot and capacitor tolerance; Devil Fish Normal Decay |
| VEG decay (τ) | 1.5 s | 7 ms to 1.5 s, plus "infinite" | Devil Fish: 16 ms to 3 s and sustain. X0X: R123 swap. |
| VCA attack (rise) | 0.5 ms | 0.3 to 30 ms | antto 0.45 to 0.8 ms vs DFM 3 ms; Devil Fish Soft Attack 0.3 to 30 ms |
| Gate turn-on delay (max d_on) | 5.4 ms | 0 to 6 ms | Unit-dependent (C72, Q36 gain); 0 gives the Devil Fish "starts within 0.5 ms" |
| Gate release (hold / fall) | 8 ms / 8 ms | 0 to 12 ms each | Depends on Q36 h_FE |
| Gate length | 50 % + 1.35 ms | 10 to 100 % | Not adjustable on hardware; standard clone control |
| Gate jitter (CPU interrupt emulation) | off; when on, a 1.8 ms interrupt grid | interrupt period 1.6 to 2.0 ms (SN TM2 tolerance) | JS: 1.8 ms interrupt beating. Emulate it by quantising edges to the grid rather than adding random noise. |
| Accent sweep capacitor (C13) | 1 µF | 0.33 to 4.7 µF, plus a 100 µF "slow" extreme | Electrolytic tolerance and ageing; Devil Fish Sweep Speed fast / normal / slow |
| Accent sweep depth (K_acc, or R46) | 1.19 oct/V | 0 (off) to 2× | X0X "Increase Accent Range" (lower R46); Devil Fish sweep on/off |
| Accent sweep threshold (VQ9 + diode) | 3.75 V | 2.5 to 4.5 V | Moves the knob position where the sweep starts; sensitive to MEG peak and VQ9 |
| Accent VCA amount (R119 + R120) | 69 kΩ, about +11 dB at max | +6 to +14 dB at max | Least certain derived number; X0X lowers R119 for more |
| Accent VCA threshold | 1.2 V | 0.6 to 1.8 V | BA662 pin 1 voltage not documented |
| Env Mod minimum (R61) | 10 kΩ, g_min = 1/6 | 0 to 1/6 | Devil Fish and X0X: jumper R61 for true zero |
| Env Mod maximum (R63) | 220 kΩ | 1× to 3× depth | Devil Fish triples it (100 k in parallel) |
| CV scale (octaves per volt at the summing node) | 0.88 × schematic | 0.75 to 1.05 | Reconciles derived depth with the measured Open303 mapping; also the temperature term |
| Env offset fraction (VQ9 / MEG peak) | 0.33 | 0.28 to 0.36 | O303 measured 0.29 to 0.34 |
| VCA thump (control feedthrough) | small | 0 to audible | Unit-dependent, undocumented |
| Resonance level compensation (R122 path) | +10 dB at res max | 0 to +10 dB | Confirm against the filter research document before exposing |

## Open questions

1. **No stock-unit scope capture of the accent node.** Sections 5.3 and 5.4 are simulation. They rest on five resistors, one capacitor, VQ9 and the MEG peak, all [primary], so the shapes are trustworthy; the absolute octaves carry the ±25 % of the parts plus the 4 to 22 % by which the derived Env Mod depth exceeds the measured one. Handle with the CV-scale trim, and validate by recording the resonant peak of a hardware unit (or a trusted circuit clone) playing A A A A N N N N at 125 and 140 BPM with resonance at max.
2. **Absolute VCA accent amount.** +11 dB at full Accent assumes the BA662 control pin sits at 0.7 V and the gate is 11.9 V. Schmidt's measured fast-to-slow ratio of 0.76 has no knob position attached. Expose as a trim, default to the derived value, and re-fit against a recording of the same pattern with Accent at min, noon and max.
3. **VCA attack: 0.5 ms or 3 ms.** antto's audio measurement and the circuit say under 1 ms; Whittle's manual says 3 ms. Default 0.5 ms, trimmer to 30 ms.
4. **Slide flag on a note followed by a rest.** JS and RW-S only describe slides into notes. x0xb0x firmware holds the gate through the rest's first half. Offer both behaviours; default to "no slide, normal gate length" (the seed rule), and check against a hardware recording when one is available.
5. **Slide across the pattern loop and across pattern changes.** Assumed to behave like any step boundary. Unverified on hardware.
6. **Does the CPU strobe the latch on a rest?** Resolved in the verification pass: no. JS's Fig. 15 is a logic-analyzer capture of the latched accent with an accent followed by a rest, and an accent followed by a tie and a rest. The text states that the latched accent stays on through ties and rests until the next unaccented note. This also agrees with JS Fig. 17 (slide line held high through rests, so there is no rising latch edge) and with DFM's stopped-on-accent observation. Model the hold.
7. **Post-VCA and pre-VCA high-pass corners.** Schematic gives 72 Hz and 156 Hz at the VCA input. At 32 Hz, Whittle measured −5 dB overall, Open303's fitted 44.5 Hz pre-filter and 24.2 Hz post-filter high-passes give −6.7 dB, and the 72 Hz pole alone gives −7.8 dB, so the three agree within 3 dB. What remains open is where the corners sit relative to the filter's nonlinearity, plus a measured magnitude response. Resolve in the filter research document.
8. **Resonance level compensation path (R122 from the resonance wiper).** Traced on a low-resolution scan. If the filter document's trace differs, the +10 dB figure is wrong.
9. **A-taper constants** for Decay, Env Mod, Cutoff and Volume. The schematic gives the taper letter only. Fit to knob-position recordings or expose a taper trim.
10. **MEG tail below 5 %.** Whether it clips to zero or creeps for a second depends on diode and V_BE matching. Model a clean exponential to zero.
11. **Triplet mode gate (5 of 8 clocks)** comes from one sentence and one figure in JS. Low stakes; implement as stated.
12. **Noise floor and nominal output level** are undocumented. Add an optional noise source with a level trim defaulting to off, and normalise output so a plain note at Volume max peaks near −12 dBFS.
13. **BA662 distortion amount** depends on the filter's output level in volts. One anchor exists: the SN VCF adjustment drawing shows about 0.6 V p-p of resonant ringing at TP6 (Resonance max, Cutoff centre, Env Mod and Accent min). The filter document should confirm that TP6 is the node feeding R121, and give levels at other settings, before the tanh drive is fixed.

## Implications for the build

1. Run one voice-state object per instrument with these persistent states: `vmeg`, `vC13` (as `u`), `vC42` timer, `vC72` (or the d_on formula), pitch CV, the accent latch, the slide switch. None of them resets on note-on except `vmeg` and the VEG timer, and those only on a real gate edge. No state resets on transport stop except by explicit "panic".
2. Implement the MEG as a single exponential with τ = (68 kΩ + R_pot)·1 µF + 4 ms, peak 10 V, re-armed to full on every non-slid note. The Decay parameter maps to R_pot through an audio taper. Display the knob in ms-to-10 % (200 ms to 2.5 s) if a readout is wanted, and compute with τ.
3. On any note whose accent bit is set, force R_pot to zero for as long as the accent latch is set, including ties and rests. Do not implement accent decay as a separate envelope.
4. Implement Env Mod as `oct = K·g·(vmeg − 3.3)`, g from 1/6 to 1. The base-cutoff drop with Env Mod is part of the instrument; do not "fix" it. Offer the Devil Fish options (true zero, 3× range) as trims.
5. Implement the accent sweep as the two-node network in section 11, with resonance position as the tap. Do not use an injection-plus-leak integrator and do not scale depth up with resonance. The seed project's `AccentIntegrator` (0.6 injected per onset, 180 ms leak, ×(1+res)) has the wrong shape in all three respects.
6. Keep the diode and the 3.3 V rest level in that network. The knob threshold near 45 % and the fact that only the top of the MEG charges C13 both come from them.
7. Feed the VCA from two summed control currents: the gated VEG and the thresholded, 1 ms-smoothed accent current. The accent current must bypass the gate so accented notes start earlier and ring past gate-off.
8. Remove any MEG-to-VCA term on unaccented notes. If A/B tests against recordings suggest the plain-note amplitude has a fast component, look for it in the filter, where it physically is.
9. Model the analog gate as its own stage: delay d_on that depends on the preceding off-time, 0.5 ms rise, 8 ms hold, about 8 ms fall. These four numbers are most of the difference between a 303 note and a rectangular-gated one.
10. Slide is a one-pole lag with τ = 22 ms on the pitch in semitones (or volts per octave), engaged for the whole destination note and bypassed otherwise. When bypassed, the lag state must track the current pitch so the next slide starts from the sounding note. Never scale slide time by interval or tempo.
11. Sequencer rules: slide flag lives on the source step; it extends that step's gate into the next note and turns the lag on for the next note. The next note updates pitch target and accent latch and retriggers nothing. Ties hold the gate with no latch. Rests latch nothing.
12. CPU gate length is half a step plus 1.35 ms. Provide an optional "CPU jitter" mode that quantises gate edges to a free-running 1.8 ms grid (trimmable 1.6 to 2.0 ms) with the measured latencies (on: +1.0 ms, off: +2.35 ms after the tick). Check the mode against JS Figs. 7 and 8: most gates should land within 0.3 ms of the nominal length, with occasional one-tick slips. Do not add Gaussian noise instead.
13. In MIDI or host-driven mode, map overlapping notes to slide (no retrigger, lag on) and velocity above a threshold to accent, and apply the same latch semantics: accent state changes only at note-on.
14. Do not reset oscillator phase at note-on. The onset click depends on it.
15. Put the VCA's tanh on the signal input and keep the gain linear in control current. Take the drive level from the filter research document.
16. The mixer inverts and adds 6.6 dB; three 1 µF couplings follow. Implement as two or three one-pole high-passes between 2 and 15 Hz, with the output-load one adjustable, and add an optional soft clip for the "output amp clips on accents at full volume" behaviour.
17. Ship the trimmers in the table above with stock defaults, and ship two presets of them: "Stock 1982" (all defaults) and "Devil Fish ranges" (extended limits). Unit-to-unit variation (C13 ±30 %, Decay pot ±20 %, VQ9, gate transistor gain) can be a single "unit" seed that perturbs them.
18. Build the acceptance tests in the targets table as automated checks on the control signals before any listening. Rows 3, 5, 10, 13, 16, 18, 26, 29 and 30 are cheap, deterministic, and catch the failures that make clones sound wrong.

## Verification log

Adversarial fact-check, 2026-10-02. Sources were opened directly: the archive.org service-notes PDF (pages 4 and 5 cropped and zoomed from the 150 dpi scan, plus its OCR text), the RE-303 BOM v1.2 PDF, Julian Schmidt's paper (text and Figs. 7 to 17), the Devil Fish manual text, Whittle's 303-unique and 303-slide pages, the Wayback copy of the x0xb0x envelopemods page (2019-10-17 snapshot), x0xb0x firmware `synth.c`, and Open303 at commit 313bf0d. The KVR threads were read through a summarizing fetcher, so their quotes are confirmed in substance, not checked verbatim. The accent sweep network was re-simulated independently: Shockley D24, pot source resistance and VCA-branch loading included, MEG τ 72 ms, 20 to 50 µs steps.

| # | Claim | Verdict | Source checked |
|---|---|---|---|
| 1 | MEG parts: C62 1 µF tantalum, R138 68 k, R152 100 Ω, R150 22 Ω, C54 0.047 µF, R148 10 k, VR6 1 MΩ (A), R117 22 k | confirmed | BOM lines 88 to 98; SN p.5 ("VR6 1M(A)") |
| 2 | Roland's 200 ms and 2.5 s are times to the 10 % point | confirmed | SN p.5 drawing: "DECAY VR MAX T=2.5 sec. MIN T=200ms", curve with a 10 % marker |
| 3 | Accent decay τ 68 to 72 ms (target ±10 %) | corrected to 72 to 87 ms | Max τ (1.07 s) matches Roland's 2.5 s exactly. Min τ from parts gives a 166 to 171 ms 10 % time against Roland's 200 ms and antto's 197 ms (KVR start=75). |
| 4 | Accent 4066 (IC12 pins 3/4) shorts the Decay pot | confirmed | SN p.5; X0X "Variable Accent Decay" mod (pot between IC12 pin 4 and D28) |
| 5 | Diode stack is D28, D29, D26 | confirmed, with a note | The scan prints "D27" for the middle diode; the BOM groups D26, D28, D29 |
| 6 | Env Mod network: VR5 50 kΩ A, R61 10 k, R63 220 k, R71 2.2 k, R72 100 k; K_env 0.54, K_acc 1.19 oct/V | confirmed | BOM; SN p.5. Recomputed: 2.2 k ∥ 220 k ∥ 100 k = 2.13 k, V_T·ln2 = 17.9 mV |
| 7 | Schematic depth is "7 to 20 %" above Open303's fit | corrected to 4 to 22 % | Open303 `calculateEnvModScalerAndOffset` constants: 0.737/4.511 and 0.864/5.059 oct |
| 8 | Env Mod offset fraction ≈ 1/3, antto 31.37 %, Roland "gimmick" | confirmed | SN p.8 OCR ("there is a gimmick in the TB-303 VCF circuit. The Q9 has a key role"); KVR t=562991; Open303 oC 0.294, oF 0.048 |
| 9 | VQ9 ≈ 3.3 V from R64/R65 dividing 5.333 V | confirmed | BOM (R64 and R65 both 10 k, so 2.67 V plus V_EB); DFM "resting voltage ... approximately 3.3 volts" |
| 10 | Accent sweep topology: D24, R46 47 k into VR4b pin 1, C13 1 µF on pin 3, wiper via R72 to Q10 base; VR4 is 50 kΩ B dual | confirmed | SN p.5 zoomed; BOM "50K B LIN DUAL"; DFM "dual 50k pot, with a linear taper" |
| 11 | Sweep time constants 50 / 102 / 152 ms | confirmed (arithmetic) | 1 µF·(97 k ∥ 102 k) = 49.7 ms; 1 µF·102 k; 1 µF·152 k |
| 12 | Single-accent shape: r = 1 peak 1.22 oct at 40 ms; r = 0 peak 3.05 oct at 0 ms; r = 0.5 1.61 oct at 5 ms | confirmed | Independent re-simulation: 1.24 at 40.4 ms; 3.09 at 0; 1.63 at 5.4 ms |
| 13 | Accumulation ratios at r = 1: 1 : 1.24 : 1.28 : 1.29 (125 BPM), 1 : 1.27 : 1.33 : 1.34 (140 BPM); afterglow 0.73, 0.23, 0.07 | confirmed | Re-simulation: 1 : 1.234 : 1.282 : 1.290 and 1 : 1.266 : 1.323 : 1.339; afterglow 0.75, 0.23, 0.07 |
| 14 | Accent knob threshold for filter sweep about 45 %; depth-vs-knob table | confirmed within 0.05 oct | Re-simulation with loading: knob 0.5 gives 0.05 / 0.18 / 0.33 oct (r = 1 / 0.5 / 0), 0.7 gives 0.35 / 0.59 / 1.12 |
| 15 | A N A N plain notes "sit at +0.60 to +0.64 oct" | corrected | They start there and decay to about +0.2 oct within the step (re-simulation) |
| 16 | VEG: C42 1 µF, R123 1.5 M, R131 220 k, R133 2.2 k; τ 1.5 s; silence at about 3.4 s; −6.8 dB at 1 s | confirmed | BOM; SN p.5; X0X "stock value is 1.5M and gives a vca decay time of about 3 seconds"; DFM "~ 3 to 4 seconds"; Open303 `ampEnv.setDecay(1230.0)`; arithmetic re-done |
| 17 | Schmidt's 58 ms fast component is the accent path | downgraded to hypothesis | KVR start=60: fit is to "one of autodafe's samples", accent state not stated |
| 18 | Gate network: C72 10 µF across R142 10 k, R143 10 k; turn-on delay up to about 5.4 ms; off-recovery τ 100 ms | confirmed | BOM; SN p.5. Recomputed: τ_on = 5 k·10 µF = 50 ms to 0.55 to 0.6 V gives 4.8 to 5.3 ms; DFM "between 1 and 5 ms (typically 4 ms)" |
| 19 | Release 8 ms hold plus 8 ms fall; DFM attack 3 ms vs antto 20 to 35 samples | confirmed (quotes) | DFM text; KVR t=258816 ("20-35sample long VCA 'fade-in'", "ACCENT actually adds a tail on the VCA") |
| 20 | VCA accent: R119 47 k, R120 22 k, C36 0.033 µF, D27; +11 dB at full Accent | partly confirmed | Parts and topology confirmed (BOM, SN p.5, RW-U "47k and a 0.033uF"). Arithmetic re-done: (9.7 − 1.2) V / 69 k ≈ 123 µA against 47 µA gives +11.1 dB. The absolute value is still unverified (pin-1 voltage). |
| 21 | VCA-input HP 72 / 156 Hz; "Open303 has no post-filter HP above 24 Hz ... far lower" | corrected | Open303 also has `highpass1` at 44.486 Hz before the filter. Combined −6.7 dB at 32 Hz, against −7.8 (schematic) and −5 (DFM) |
| 22 | Slide: C35 0.22 µF between op-amp input and output via two 4066 sections, DAC Thevenin 100 k, τ 22 ms | confirmed | SN p.5 zoomed; BOM (C35 0.22 µF film ±10 %; ladder R74 to R90 all 200 k 0.1 %, so R = 100 k); DFM "resistance of the DAC ... 100K"; KVR Gordonjcp 22 ms, antto 7.23 Hz |
| 23 | Slide flag on source step, slide line high for the whole destination note and through ties and rests, 44 µs and 8.75 µs latch pulses | confirmed | JS sections 3, 4.3, 6, Figs. 16 to 19; RW-S |
| 24 | 8.75 µs pulse moves C35 "about 2 %" | downgraded to unverified | Depends on AN6562 output drive; bounded by the slide residual, inaudible either way |
| 25 | CPU gate = T/2 + 1.35 ms; on-latency 0.8 to 3.3 ms (σ 0.74, mean 2.06); off 2.3 to 2.4 ms after its tick | confirmed | JS text; Fig. 7 (25.7 ms at a 48.7 ms step), Fig. 8 (52.38 to 52.67 ms at 102.3 ms) |
| 26 | Gate jitter "roughly ±1 ms" | corrected | JS Figs. 7 to 10: most gates within 0.3 ms, occasional about 2.4 ms short slips; text range 49.96 to 55.8 % is wider than the figures |
| 27 | Interrupt 1.8 ms; tempo 40 to 300 BPM | confirmed, tolerance added | SN p.4 adjustment "INT CLOCK ... TP2 ... 1.8ms ± 0.2ms" (TM2); SN p.1 "TEMPO Control: 40 to 300" |
| 28 | Triplet mode 8 clocks, 5 on and 3 off | confirmed as stated by the single source | JS 4.2 and Fig. 11 |
| 29 | Accent held through rests (open question 6) | upgraded to [primary]; question closed | JS section 5 text and Fig. 15 caption |
| 30 | Accent latch polarity | added | SN p.5: Q35 NPN inverter; X0X `synth.c` "accent is active low"; JS Fig. 14 caption |
| 31 | Output stage: R155 47 k, R153 100 k (gain −2.13), R156 100 k, R161 1 k, R165 100 k, C56/C58/C59 1 µF, R159 33 k, C51 2.2 µF, LA4140 | confirmed | BOM lines 133 to 145; SN p.5. HP corners recomputed (3.4 Hz, 1.6 Hz, 15.8 Hz) |
| 32 | Main output inverted relative to the filter (DFM) | downgraded | The DFM statement compares against the Devil Fish's added Filter Out. Only the mixer inversion is established. |
| 33 | Output level / noise floor | gap partly filled | SN VCF adjustment drawing: about 0.6 V p-p ringing at TP6 (5.2 to 5.8 V). There is still no line-out level or noise data (web search 2026-10-02). |
| 34 | x0xb0x holds the gate into a rest after a slide flag | confirmed | `synth.c` `note_on` (rest: no latch, no gate strike) and `note_off(slide)` |
| 35 | X0X quotes: R61 jumper, R63 ∥ 100 k for 3×, "R119 controls the amplitude gain ... R46 controls the resonance gain", 100 µF C13 "~10 seconds", "may peak at 6v" | confirmed (quotes) | Wayback envelopemods 20191017040546 |

Still unverified after this pass: absolute VCA accent dB (BA662 pin-1 voltage), absolute octaves per volt (CV-scale trim), all accent curves against hardware (no scope capture exists in any source reached), VCA attack 0.5 ms against 3 ms, slide-into-rest and loop-boundary behaviour on the real CPU, the R122 resonance-wiper path, pot taper constants, and the noise floor.
