# Peaks (`peaks`) -- v1.0.0

Ports of Émilie Gillet's *Peaks* (Mutable Instruments) and Tim Churches' *Dead
Man's Catch* alternative firmware for Peaks (both MIT licensed). Fourteen mono
generators: four 808-flavoured drum voices, clocked LFOs and sequencers, a
number station, and a bytebeat engine. All units are mono and generator-only:
they have no audio input and a single `Out1`. All parameter dials are plain
`GainBias` controls (each has a CV input branch plus a gain), and none of the
units has a sub-display or a menu.

Package author string: "Émilie Gillet & Tim Churches / ER-301 port by stolmine".

A note on ranges: internally every parameter is a 16-bit integer. Unipolar
dials map `[0,1]` → `0…65535`; bipolar dials map `[-1,1]` → `0…65535` and the
DSP re-centres them. Output is `int16 / 32768`, i.e. roughly ±1.0.

---

## Tap LFO

<mnemonic: TL> · Category: Peaks

A clock-synced LFO. It listens to the Clock input, predicts the incoming tempo
(including irregular taps) and runs a wavetable oscillator locked to it. Five
waveshapes with a continuous secondary parameter that morphs each one, plus a
phase offset so you can place the wave anywhere relative to the clock.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Clock | Gate | gate mode | n/a | Tempo input. Each rising edge feeds the period predictor. |
| Reset | Gate | trigger mode | n/a | Resets the oscillator phase to the Phase offset. |
| Rate | GainBias | `[0,1]` | 0.50 | **Sets the output level, not the rate**; see verification notes. 0 = silence, 1 = full scale. |
| Shape | GainBias | `[0,1]`, 5 steps | 0.50 | Waveshape: `0-0.2` Sine · `0.2-0.4` Triangle · `0.4-0.6` Square · `0.6-0.8` Steps · `0.8-1.0` Noise. |
| Parameter | GainBias | `[-1,1]` | 0.00 | Secondary shape control: sine/triangle skew (ramp-up ↔ ramp-down), square duty cycle, number of steps, noise slew. |
| Phase | GainBias | `[-1,1]` | 0.00 | Phase the oscillator jumps to on Reset. |

**I/O**: Mono out (±1.0 scaled by Rate). Clock in (gate), Reset in (trigger).
No V/Oct.

---

## Bass Drum

<mnemonic: BD> · Category: Peaks

The Peaks 808-style kick: a resonant bandpass "resonator" struck by a
pulse-shaped excitation, with a separate transient click. Punch stiffens the
attack, Tone opens the click's brightness, Decay lengthens the body ring.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate | gate mode | n/a | Triggers the drum on the rising edge. |
| Pitch | GainBias | `[-1,1]` | 0.00 | Resonator frequency, roughly ±1 octave around the 808 kick pitch. |
| Punch | GainBias | `[0,1]` | 0.50 | Strength/steepness of the initial excitation pulse. |
| Tone | GainBias | `[0,1]` | 0.50 | Lowpass on the transient click: dull to bright. |
| Decay | GainBias | `[0,1]` | 0.50 | Resonator Q, i.e. how long the body rings. |

**I/O**: Mono out. Gate in. No V/Oct.

---

## Snare Drum

<mnemonic: SD> · Category: Peaks

Two tuned resonators (body and rim) mixed against a filtered noise burst.
Tone balances the two resonators against each other; Snappy sets how much noise
rides on top.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate | gate mode | n/a | Triggers the drum on the rising edge. |
| Frequency | GainBias | `[0,1]` | 0.50 | Pitch of both resonators. 0.5 is the centre/808 pitch; the DSP treats this as a bipolar offset. |
| Tone | GainBias | `[0,1]` | 0.50 | Balance between the low body resonator and the higher rim resonator. |
| Snappy | GainBias | `[0,1]` | 0.50 | Amount of noise (snare wires) in the mix. |
| Decay | GainBias | `[0,1]` | 0.50 | Length of the body and noise envelopes. |

**I/O**: Mono out. Gate in. No V/Oct.

---

## High Hat

<mnemonic: HH> · Category: Peaks

The Peaks hi-hat: a metallic noise source (six detuned square oscillators) run
through a bandpass and a fast decay envelope. This port exposes no parameter
dials: the DSP's frequency, decay, frequency-randomness and decay-randomness
all sit fixed at their mid values, so every hit varies slightly in pitch and
length. Trigger it; there is nothing to adjust.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate | gate mode | n/a | Triggers the hat on the rising edge. |

**I/O**: Mono out. Gate in. No V/Oct, no parameter CV.

---

## FM Drum

<mnemonic: FM> · Category: Peaks

A two-operator FM percussion voice with a noise component, the "digital" drum
of the Peaks set. Sweeps from tight FM toms through wooden clicks to metallic
noise hits depending on Frequency, FM index and Noise.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate | gate mode | n/a | Triggers the drum on the rising edge. |
| Frequency | GainBias | `[0,1]` | 0.50 | Carrier pitch. Low settings also add an auxiliary pitch-envelope sweep. |
| FM | GainBias | `[0,1]` | 0.50 | FM index (modulation depth), scaled ×3 internally. |
| Decay | GainBias | `[0,1]` | 0.50 | Amplitude envelope length. |
| Noise | GainBias | `[0,1]` | 0.50 | Blend of filtered noise into the FM tone. |

**I/O**: Mono out. Gate in. No V/Oct.

---

## Bouncing Ball

<mnemonic: BB> · Category: Peaks

A physics envelope: a trigger drops a ball from a height and it bounces,
losing energy each time it hits the floor. The output is the ball's height.
Useful for accelerating retrigger ramps and natural-feeling decays.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate | gate mode | n/a | Drops the ball (rising edge resets position and velocity). |
| Gravity | GainBias | `[0,1]` | 0.50 | Downward acceleration: how fast the bounces happen. |
| Bounce | GainBias | `[0,1]` | 0.50 | Elasticity. Low = energy lost fast (few bounces), high = long bounce trains. |
| Amplitude | GainBias | `[0,1]` | 0.50 | Starting height, i.e. the peak of the first bounce. |
| Velocity | GainBias | `[-1,1]` | 0.00 | Initial velocity. Negative throws the ball down, positive throws it up. |

**I/O**: Mono out (unipolar 0…1, the ball's height). Gate in.

---

## Mini Sequencer

<mnemonic: MS> · Category: Peaks · Unit title on screen: **Mini Seq**

A four-step CV sequencer. Each clock edge advances one step and the output
holds that step's value. The four step dials are bipolar, so it emits a
±0.625-ish control voltage suitable for modulation or (with a scaler) pitch.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Clock | Gate | gate mode | n/a | Advances to the next step on each rising edge. |
| Reset | Gate | trigger mode | n/a | Arms a reset: the sequence jumps to step 1 on the *next* clock edge. |
| Step 1 | GainBias | `[-1,1]` | 0.00 | Value held on step 1. |
| Step 2 | GainBias | `[-1,1]` | 0.00 | Value held on step 2. |
| Step 3 | GainBias | `[-1,1]` | 0.00 | Value held on step 3. |
| Step 4 | GainBias | `[-1,1]` | 0.00 | Value held on step 4. |

**I/O**: Mono out, stepped CV scaled to about ±0.625. Clock in (gate), Reset
in (trigger).

---

## Number Station

<mnemonic: NS> · Category: Peaks

A shortwave numbers-station simulator: a synthetic voice reading digits,
buried in radio noise, drift, ringmod interference and distortion. A gate
starts a transmission. It works as an atmospheric noise source rather than a
tuned instrument.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate | gate mode | n/a | Starts/holds a transmission. |
| Tone | GainBias | `[0,1]` | 0.50 | Formant/pitch-shift of the voice, from low and gruff to chipmunk. |
| Probability | GainBias | `[0,1]` | 0.50 | Transition probability of the digit sequence: how often it moves to a new digit. |
| Noise | GainBias | `[0,1]` | 0.50 | Level of the radio noise bed. |
| Distortion | GainBias | `[0,1]` | 0.50 | Waveshaping / transmission grit on the voice. |

**I/O**: Mono out. Gate in.

---

## Randomised Envelope

<mnemonic: RE> · Category: Dead Man's Catch · Unit title on screen: **Rand Envelope**

Tim Churches' stochastic AD envelope: a quartic attack into an exponential
decay, where each trigger re-rolls the peak level and the decay time. Set both
randomness dials to zero and it is a plain AD; open them and every hit breathes
differently. Randomisation only ever subtracts, so the dials pull the peak
and decay *down* from the values you set.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate | gate mode | n/a | Fires the envelope on the rising edge. |
| Attack | GainBias | `[0,1]` | 0.50 | Attack time (quartic curve). |
| Decay | GainBias | `[0,1]` | 0.50 | Base decay time (exponential curve). |
| Amp Rand | GainBias | `[0,1]` | 0.50 | Random reduction applied to the peak level on each trigger. 0 = fixed peak. |
| Decay Rand | GainBias | `[0,1]` | 0.50 | Random reduction applied to the decay time on each trigger. 0 = fixed decay. |

**I/O**: Mono out (unipolar envelope). Gate in.

---

## Mod Sequencer

<mnemonic: MQ> · Category: Dead Man's Catch

An eight-step sequencer driven by four dials: steps 1-4 are the dial values,
steps 5-8 are their inversions. That gives a symmetric, self-balancing
eight-step modulation shape from half the controls: a palindrome-flavoured
LFO for CV duty.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Clock | Gate | gate mode | n/a | Advances one step per rising edge (8 steps). |
| Reset | Gate | trigger mode | n/a | Arms a reset: jumps to step 1 on the next clock edge. |
| Step 1 | GainBias | `[-1,1]` | 0.00 | Step 1 value; step 5 is its inverse. |
| Step 2 | GainBias | `[-1,1]` | 0.00 | Step 2 value; step 6 is its inverse. |
| Step 3 | GainBias | `[-1,1]` | 0.00 | Step 3 value; step 7 is its inverse. |
| Step 4 | GainBias | `[-1,1]` | 0.00 | Step 4 value; step 8 is its inverse. |

**I/O**: Mono out, full-scale stepped CV (±1.0). Clock in (gate), Reset in
(trigger).

---

## FM LFO

<mnemonic: FL> · Category: Dead Man's Catch

A free-running LFO whose rate is frequency-modulated by a second, internal LFO.
Slow wobbles, lurching rubato modulation, and at extreme settings audio-rate
chirps. The Shape dial steps through seven fixed shape+parameter presets.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Reset | Gate | trigger mode | n/a | Resets both the main and the modulator phase. |
| Rate | GainBias | `[0,1]` | 0.50 | Base LFO frequency (exponential map, very slow → audio rate). |
| Shape | GainBias | `[0,1]`, 7 presets | 0.50 | Waveshape preset, in sevenths: 1 Sine · 2 Triangle · 3 Triangle (fully skewed → ramp) · 4 Square · 5 Steps · 6 Noise (slewed) · 7 Noise (sample-and-hold). |
| FM Amount | GainBias | `[-1,1]` | 0.00 | **Actually the modulator's rate**; see verification notes. |
| FM Rate | GainBias | `[-1,1]` | 0.00 | **Actually the modulation depth**, and it is V-shaped: 0.00 (centre) = no FM, and depth increases toward both −1 and +1; the sign selects the modulator's waveshape variant. |

**I/O**: Mono out. Reset in (trigger). No clock input; this LFO is
free-running.

---

## WSM LFO

<mnemonic: WL> · Category: Dead Man's Catch

A free-running LFO whose *waveshape* is modulated by a second internal LFO,
rather than its pitch. The shape parameter is continuously swept (sine folding,
overdrive, triangle skew), giving a wave that morphs as it cycles. Timbral
movement without any pitch drift.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Reset | Gate | trigger mode | n/a | Resets both the main and the modulator phase. |
| Rate | GainBias | `[0,1]` | 0.50 | LFO frequency (exponential map). |
| Shape | GainBias | `[0,1]`, 7 presets | 0.50 | Waveshape preset, in sevenths. As shipped the seven slots resolve to: 1 Folded sine · 2 Folded power sine · 3 Folded power sine · 4 Overdriven sine · 5 Triangle · 6 Square · 7 Square. (The DSP defines a sixth shape, Noise, but no preset slot reaches it; see verification notes.) |
| WSM Amount | GainBias | `[-1,1]` | 0.00 | **Actually the waveshape-modulator's rate**; see verification notes. |
| WSM Rate | GainBias | `[-1,1]` | 0.00 | **Actually the waveshape-modulation depth**, V-shaped: 0.00 (centre) = static waveshape, depth rises toward both −1 and +1. |

**I/O**: Mono out. Reset in (trigger). No clock input.

---

## PLO

<mnemonic: PL> · Category: Dead Man's Catch

A phase-locked oscillator: it tracks the Clock input's period and oscillates at
a continuously variable multiple or division of it, from /16 up to ×16. Unlike
Tap LFO this is intended to run at audio rate, so it is a clock-locked tone
source. Waveshape modulation is built in.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Clock | Gate | gate mode | n/a | Reference clock. Its period sets the base frequency (with tempo prediction). |
| Reset | Gate | trigger mode | n/a | Resets the oscillator phase to zero. |
| Pitch | GainBias | `[0,1]` | 0.50 | Clock multiplier, continuous and piecewise-log: 0.0 = /16, 0.5 = ×1, 1.0 = ×16. |
| Shape | GainBias | `[0,1]`, 7 presets | 0.50 | Waveshape preset, in sevenths, using the same seven slots as WSM LFO: 1 Folded sine · 2 Folded power sine · 3 Folded power sine · 4 Overdriven sine · 5 Triangle · 6 Square · 7 Square. |
| WSM Rate | GainBias | `[0,1]` | 0.50 | Rate of the internal waveshape-modulation oscillator. |
| WSM Depth | GainBias | `[0,1]` | 0.50 | Depth of waveshape modulation. At 0 the shape is static and the preset's own shape parameter is used. |

**I/O**: Mono out. Clock in (gate), Reset in (trigger). No V/Oct; pitch comes
from the clock.

---

## ByteBeats

<mnemonic: BB> · Category: Dead Man's Catch

Tim Churches' bytebeat engine: eight one-line integer expressions in `t`,
rendered directly as 8-bit audio. Chiptune arpeggios, glitch drones and
generative noise from arithmetic. The gate resets `t` to zero, so triggering it
restarts the pattern from the top. Output is downsampled by 4 for the classic
crunchy texture.

The eight equations, in order:

| # | Equation | Source / character |
|---|---|---|
| 1 | `((t*3 & t>>10) \| (t*p0 & t>>10) \| (t*10 & (t>>8)*p1 & 128)) & 0xFF` | royal-paw.com; atmospheric, hopeful |
| 2 | `((t*p0 & t>>4) \| (t*5 & t>>7) \| (t*p1 & t>>10)) & 0xFF` | stephth |
| 3 | `(((t>>p0) & t) * (t>>p1)) & 0xFF` | r/bytebeat "cool equations" |
| 4 | `((((t>>p0 \| t) \| t>>p0)*10 & (5*t \| t>>10)) \| (t ^ t%p1)) & 0xFF` | xifeng.weebly.com, second listing |
| 5 | `t * (((t>>p1) ^ ((t>>p1)-1) ^ 1) % p0)` | BitWiz transplant, Equation Composer *Ptah* bank; runs at double rate |
| 6 | arpeggiation: `p = (t/(1236+p0) % 128) & ((t>>(p1>>5))*p1)`, `q = (t/(t/((500*p1)%5)+1)) % p`, out `= (t>>q>>(p1>>5)) + (t/(t>>((p1>>5)&12))>>p)` | Equation Composer *Khepri* bank |
| 7 | `sample ^ (t>>(p1>>4)) >> ((t/6988*t%(p0+1)) + (t<<t/(p1*4)))` | "The Smoker", Equation Composer *Khepri* bank |
| 8 | `((t & p0) - (t % p1)) ^ (t>>7)` | BitWiz; warping overtone echo drone, double rate |

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate | gate mode | n/a | Rising edge resets `t` and the phase to 0, restarting the pattern. |
| Formula | GainBias | `[0,1]` | 0.50 | **Actually the clock rate / pitch** of the bytebeat counter; see verification notes. Higher = faster (`bytepitch = (65535−v)>>11`, minimum 1). |
| Param 1 | GainBias | `[0,1]` | 0.50 | The equation's `p0` term. Meaning varies per equation (multiplier, shift amount or modulus). |
| Param 2 | GainBias | `[0,1]` | 0.50 | The equation's `p1` term. Same caveat. |
| Speed | GainBias | `[0,1]`, 8 steps | 0.50 | **Actually the equation selector**; see verification notes. Divides `[0,1]` into eight equal bands selecting equations 1-8 above; the default 0.50 selects equation 5. |

**I/O**: Mono out, 8-bit stepped, downsampled ×4. Gate in.

<!-- VERIFICATION NOTES

Sources read: mods/peaks/assets/*.lua (all 14 + toc.lua), mods/peaks/PeaksUnit.h,
mods/peaks/PeaksUnit.cpp, mods/peaks/mod.mk (PKGVERSION 1.0.0), and the DSP under
mods/peaks/peaks/ (lfo.h/.cc, bytebeats.h/.cc, mini_sequencer.h, bouncing_ball.h,
multistage_envelope.h/.cc, number_station.h, drums/*.h).

Control-name / behaviour mismatches found in code (Lua label vs what the DSP
parameter slot actually does). All are in the unit Lua, not the DSP:

1. Tap LFO "Rate" (Param1): Lfo::Configure with sync_==true and
   CONTROL_MODE_FULL routes parameter[0] to set_level(), not set_rate().
   set_rate() is never called in sync mode, so rate_ stays at its Init() value
   of 0; the clock multiplier shift computes to (0-32768)>>12 = -8, i.e. a
   fixed base_inc>>8 division. So the dial is an output level control and the
   clock ratio is fixed. (lfo.h Configure; lfo.cc:77-106, Init at lfo.cc:49.)

2. FM LFO "FM Amount" (Param3) → set_fm_rate(), "FM Rate" (Param4) →
   set_fm_depth(). The two labels are swapped relative to the DSP.
   (lfo.h FmLfo::Configure.)

3. WSM LFO "WSM Amount" (Param3) → set_wsm_rate(), "WSM Rate" (Param4) →
   set_wsm_depth(). Same swap. (lfo.h WsmLfo::Configure.)
   PLO's equivalents (Param3 → wsm_rate, Param4 → wsm_depth) are correct.

4. ByteBeats "Formula" (Param1) → set_frequency(), and "Speed" (Param4) →
   set_p2(), which is what selects the equation (equation_index_ = p2_ >> 13).
   So the first and last dials are swapped in meaning. Param2/Param3 ("Param 1"
   / "Param 2") correctly feed p0_/p1_. (bytebeats.h Configure; bytebeats.cc:59-64.)

5. WSM LFO / PLO shape presets: lfo.cc defines wsmlfo_presets[7][2] (folded
   sine, folded power sine, overdriven sine, triangle, triangle-skewed, square,
   noise) but both WsmLfo::set_shape_parameter_preset (lfo.cc:434) and
   Plo::set_shape_parameter_preset (lfo.cc:626) index the *Lfo* `presets` table
   instead. Cast into WsmLfoShape the seven slots become folded sine, folded
   power sine, folded power sine, overdriven sine, triangle, square, square;
   the Noise shape is unreachable. wsmlfo_presets is dead code. This matches
   the upstream Dead Man's Catch source, so it may be inherited rather than
   introduced by the port; documented as-shipped behaviour above.

6. WSM LFO's preset parameter value is irrelevant: WsmLfo::Process overwrites
   parameter_ with wsm_delta_ every sample (lfo.cc:455). For PLO the preset
   parameter survives only when WSM Depth is 0.

Other findings:

- High Hat exposes only a Gate. peaks::HighHat::Configure takes four params
  (frequency, decay, frequency randomness, decay randomness) but the Lua never
  creates the ParameterAdapters, so all four sit at the od::Parameter default
  0.5f → 32768. That leaves the two randomness terms at ~50%, so hits
  vary in pitch and decay and this cannot be turned off.

- toc.lua titles vs on-screen unit titles differ for two units:
  toc "Mini Sequencer" → args.title "Mini Seq"; toc "Randomised Envelope" →
  args.title "Rand Envelope". Documented both.

- Mnemonic collision: Bouncing Ball and ByteBeats are both "BB".

- Snare Drum's frequency dial is declared unipolar [0,1] in Lua but the DSP
  subtracts 32768 from it, so 0.5 is the centre pitch and the usable range is
  bipolar. Same pattern on FM Drum's Frequency (unipolar and used unipolar
  there, so that one is fine).

- No unit in this package defines onLoadMenu/onShowMenu, a sub-display, or a
  collapsed-view control list (all `collapsed = {}`). No custom ViewControls.

Discrepancies vs README.md:

- README's Peaks table lists all 14 units and the categories match toc.lua.
  Descriptions are broadly accurate. Two are loose:
  * README's "4-step CV sequencer" for Mini Sequencer is right, but its
    "Extended step sequencer" for Mod Sequencer undersells/obscures that it is
    specifically 8 steps where the last four are inversions of the first four.
  * README's "Phase-locked oscillator" for PLO is accurate. It is also the
    only unit here intended for audio-rate use, and it takes its pitch from a
    clock, not V/Oct.
- README does not mention that High Hat has no parameter controls.
- README says these units "still need some testing for hardware parity", which
  is consistent with the label/DSP mismatches listed above.

Could not verify:

- Actual audible ranges (rate in Hz, decay in ms, etc.) come from lookup
  tables in resources.cc (lut_lfo_increments, lut_env_increments,
  lut_gravity) which I did not evaluate numerically. Ranges above are given in
  normalised dial terms only.
- Whether the port intends the label/DSP swaps in notes 1-4 as deliberate
  renames; nothing in RELEASE-2.8.0.md / RELEASE-2.8.1.md or planning/ addresses
  them. RELEASE-2.8.1.md states peaks was republished unchanged for v2.8.1.
-->
