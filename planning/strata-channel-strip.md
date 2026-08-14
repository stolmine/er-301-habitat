# Design note: Strata - a channel strip on the house atom bin

Status: design note / not started. Ledger item `strata-channel-strip`.

User request 2026-08-14: a channel strip built on sub-controls - top level is
bypass gates per section, parameters underneath - with nonlinearity throughout,
sourced from Airwindows.

Named **Strata**: layers laid down in order, which is what a strip is.

Package: **house**. The atoms are already there, and this is the composition
consumer that `house-atom-library` and `house-harness-patterns` were staged for.

## The argument for building it

`house-atom-library` is ledgered as "component-only by default (no Lua unit/toc)
**until standalone value exists**." Strata is that standalone value.

More concretely: **Capacitor2, Point and Distance2 are already vendored, already
NEON/A8-tuned, and currently ship to nobody.** All three are gated behind Filament
and Carriage, which are parked (built into the `.so`, absent from `toc.lua`), and
`house-suppress-customs-optimize-ports` directs that the house originals stay
suppressed. That is three good atoms of idle capital. Recomposing them into a
strip is cheaper than any new port, and it is the only route by which that work
reaches a user.

Of the ten strip-relevant atoms, **six are already in `mods/house/atoms/`**:
Console0Channel, Console0Buss, Capacitor2, Spiral, ChromeOxide, Distance2 - plus
Point, ChainMix and AllpassMono as usable components.

Naming trap for anyone reading the tree: `mods/house/atoms/RotCoat.h`,
`Carriage.h`, `Filament.h` and `Lacquer.h` are **habitat originals** whose names
coincide with, or evoke, Airwindows plugins. They are not ports. Do not go
looking for `airwindows/plugins/.../RotCoat` expecting a match.

## Sections

Seven, in this order. The order follows CStrip's own stated reasoning - its
lowpass sits last, after "all processing like the compressor that might produce
hash."

| # | Section | Built from | Headline param |
|---|---|---|---|
| 1 | **Gate** | Pop3's gate | Threshold |
| 2 | **Filter** | Capacitor2 (HP + LP) | Cutoff |
| 3 | **EQ** | SmoothEQ3 or BiquadStack | Tilt / Mid |
| 4 | **Comp** | Pop3's compressor | Compress |
| 5 | **Drive** | Channel9 (Density/Spiral + slew) | Drive |
| 6 | **Punch** | Point + Distance2 | Transient |
| 7 | **Out** | ClipOnly2 / ClipSoftly | Level |

Each section's bypass is a **true bypass, not unity gain** - CStrip does exactly
this with its `engageX` flags, and it means an unused section costs nothing.
Resolve the engage flags at block rate; a whole-section skip is a block-level
branch, which is fine, but nothing may switch per-sample inside a section
(`feedback_runtime_branched_dsp_dispatch`).

### Why these parts

**Pop3** is the standout of the whole survey: 148 lines of file, **22 lines of
actual algorithm**, 8 params, ~32 bytes of state, no arrays, and only two
*conditional* sines. It is the ConsoleX dynamics section, and habitat currently
has **no compressor or gate atom at all**. Its design is unusually portable: the
gain multiplier lives in the linear domain (over threshold it leaky-integrates
toward `compThresh/|x|`, an infinite-ratio target; under it, one-poles back to
1.0), ratio is a plain linear crossfade rather than a dB slope, and the gate is
phase-based with the `sin` skipped entirely once phase passes π/2. The gate
triggers off the *uncompressed* signal, independent of the compressor.

Note it has **no makeup gain** - ConsoleX supplies that externally. Strata's Out
section covers it.

**Channel9** is the nonlinearity the request asks for, and it is remarkably
cheap: 171 lines, three controls, no buffers. The saturation is a crossfade
across two curves - `sin(x*1.5708)` (old Density, fat) and
`sin(x*1.2533*|x*1.2533|)/|x*1.2533|` (Spiral, clean) - dry→Spiral over 0-100%,
Spiral→Density over 100-200%. Its highpass carries a Capacitor2-style dielectric
coefficient so the corner rides instantaneous level and deepens with Drive. Its
slew clipper is Slew3-style: not a raw first difference but a 3-tap golden-ratio
predictor limiting *acceleration*, so steady HF passes.

And the console models are **literally three numbers each**:

| Model | iirAmount (HP) | threshold (slew) | cutoff (Hz) |
|---|---|---|---|
| Neve | 0.005832 | 0.33362176 | 28811 |
| API | 0.004096 | 0.59969536 | 27216 |
| SSL | 0.004913 | 0.84934656 | 23011 |
| Teac | 0.009216 | 0.149 | 18544 |
| Mackie | 0.011449 | 0.092 | 19748 |

A five-entry lookup table is the entire model distinction. That maps onto an
`OptionControl` perfectly - **but see the 48 kHz problem below, which partly
breaks it.**

**EQ**: habitat has no EQ atom at all. Two candidates, both with **zero
transcendentals in the sample loop**. *SmoothEQ3* is the cheapest steep EQ in the
Airwindows repo - 3 params, 2 biquads + 2 one-poles per channel, ~34
flops/sample/ch, ~254 bytes of state, Q fixed at exactly 1.0 so the coefficient
math drops the `K/Q` divides entirely. *BiquadStack* is the SSL-parametric kernel
Chris wrote while "trying to work out what was so special about SSL channel
strips" - a Butterworth Q ladder used as bandpasses rather than for rolloff,
giving "a little region of intensity" with phase-interference "moats" at the
edges rather than a narrowing spike. Its nonlinearity is beautifully cheap:
`dis = fabs(a0 * (1.0 + out*nonlin))` clamped to 1.0 - b0 modulated by
instantaneous level, saturating by clamping, no tanh anywhere.

Start with SmoothEQ3 for cost; BiquadStack is the upgrade if the strip wants a
sweepable parametric rather than fixed bands.

**ClipOnly2** is free: zero params, **zero transcendentals, zero divisions**,
~15 lines. Chris marks the block in-source as liftable. Unclipped samples pass
bit-identical; all the work is synthesizing a soft ramp in and out of an
otherwise pure hard clip. `hardness = 0.7390851332151606` is the fixed point of
`x == cos(x)`. **ClipSoftly** is its counterpart and touches every sample, "making
them bigger, fatter, tubier"; it drags the output toward the *previous* sample as
input goes over unity, which is what kills clip glare. Ship both as an Out-section
option. Use ClipOnly2, not ClipOnly - v1 has no sample-rate compensation.

**Not taken**: CStrip and CStrip2 whole. CStrip is ~700 lines of one-copy DSP and
CStrip2 ~562, and both bundle decisions we would rather make ourselves. Chris's
own claim about CStrip checks out in source - it is literal ButterComp, four
independent compressors per channel via pos/neg split and A/B `flip`, and the
pedia notes the interleaving that the original ButterComp *didn't* actually use
"the one in CStrip does." Worth reading, not worth porting whole when Pop3 gives
a comparable job in 22 lines. If the CStrip gate is ever wanted specifically, lift
from **Dynamics**, which is that gate standalone in far less source.

## UI: the SectionGate control

The request was "top level all gates, parameters on shift." The shape is right;
the two gestures should swap, because the house already assigns them.

Habitat's established idiom for *section → its parameters* is the **Enter-key
expansion view**. Impasto: `lo = {"lo","loThreshold","loRatio","loAttack",
"loRelease"}`. Parfait does the same with eight controls per band across 34
controls and 8 views. Breccia, Pecto and Vitrail all follow it. Meanwhile **shift
is already taken** by a different pattern - the param-mode sub-display toggle in
`DensityControl` and `MixControl`, where shift swaps the sub-graphic for a row of
three readouts.

So:

- **Enter** on a section opens its parameter board. The idiom users know.
- **Shift-press** toggles that section's bypass. One gesture, instant, no
  collision.
- The **encoder** on each top-level control drives that section's headline
  parameter, so the top row is playable rather than being pure switchgear.

The deliverable here is a reusable `SectionGate` ViewControl - extends GainBias,
draws the section name and an on/off indicator, binds shift to the engage
parameter. Nothing in the tree does this yet, and once it exists any future
multi-section unit gets it for free. That is worth as much as the strip.

Views follow Breccia's **shared-visualizer** pattern: every expansion view
includes one common display control, so entering any section shows that display
plus the section's params. For a strip the display should carry gain reduction
and level; Impasto already has GR visualization to borrow from.

## Portability - what to fix while porting

The survey turned up a set of issues that apply to everything on this list:

- **Read the double copy.** Every `Proc.cpp` contains the algorithm twice, and
  Airwindows has already commented the dither out of the double version.
- **Delete every dither block.** It is VST float-buss conditioning, pointless on
  a modular buss, and it accounts for the *only* per-sample `pow` and `frexpf` in
  nearly every one of these plugins. This is the same free win already recorded
  in `aw-batch2-ports` and `gesso-glue-compressor`.
- **Everything is `double`.** Cortex-A8 NEON is single-precision only, so doubles
  fall to the non-pipelined scalar VFPv3 unit. This is the highest-value change,
  and it is the pass habitat already ran on its own atoms (`c918309`: Console0
  Channel/Buss and ChromeOxide, f64 ops 60→16 / 63→16 / 368→89, tone-identical to
  1 LSB; `709c38a` for Lacquer). Float is safe **except** for the lowest-frequency
  biquads - compute coefficients in double at setup, run the recursions in float.
- **Bounded-range sines are polynomial-replaceable.** ClipSoftly, Channel9,
  CStrip's EQ and Pop3 all call `sin`/`cos` on explicitly clamped [0, π/2] or
  [−π/2, π/2] domains, and `mods/house/atoms/Spiral.h` already has
  `spiralFastSaturate` - 5th-order Taylor, ~20× faster, 0.45% error. Note that
  per-sample double `sin()` is *not* itself a correctness problem on am335x -
  `aw-batch2-ports` established that Galactic already ships four per sample on
  hardware, and the package-trig bug is `sinf`/`cosf`-specific. So this is a CPU
  decision, not a correctness one.
- **Denormals.** Airwindows guards only the *input* (`if (fabs(x)<1.18e-23)`);
  internal IIR and envelope state is unprotected almost everywhere. Set FZ in
  FPSCR globally. Pop3 specifically: `popGate *= (1-gateRelease)` decays toward
  denormals in silence and the `<0.0` clamp does not catch positive denormals -
  add `if (popGate < 1e-20) popGate = 0.0;`.
- `pow(x,2)` → `x*x`, and gate the setup blocks on parameter change - the whole
  SmoothEQ/ConsoleX family recomputes coefficients every block regardless.

### The 48 kHz problem, which is specific and matters

Airwindows is calibrated at 44.1 kHz; the ER-301 runs 48. Several coefficients do
not scale:

- **Channel9's ultrasonic biquads are gated on `if (biquad[0] < 0.49999)`.** At
  48 kHz, Neve (0.600) and API (0.567) fall outside and get **no biquad at all**;
  only SSL, Teac and Mackie filter. At 44.1 kHz only Teac and Mackie do. So
  shipping the five-model table naively means the model distinction partly
  collapses to just `iirAmount` and `threshold`, and the defining ultrasonic
  character of the two most-wanted models silently vanishes. Either re-derive the
  cutoffs for 48 kHz or oversample the Drive section. **Decide this before the
  model list is exposed as an OptionControl**, or the labels will be lying.
- CStrip's lowpass coefficient ignores sample rate entirely
  (`iirAmountC = kHz*0.0188 + 0.7`), landing ~9% high at 48 kHz.
- CStrip's gate release is a fixed per-sample decrement, unscaled.
- CStrip2's LowCap/HiCap are raw normalised coefficients with no frequency
  mapping.
- CStrip's `divisor /= compscale` can push the compressor coefficient above 1.0
  at high rates. Mild at 48 kHz (compscale ≈ 1.088) but clamp it.

Only the first genuinely bites Strata as specified, but the pattern is the point:
**check every constant for sample-rate assumptions before trusting it.**

## Relationship to the other dynamics items

`gesso-glue-compressor` is a dedicated glue bus compressor on a Pressure6 engine.
Strata's Comp is Pop3 - a different, cheaper, strip-internal compressor with a
gate attached. They are not redundant: one is a bus tool with an SSL surface, the
other is a channel-strip stage. Build Gesso first if only one gets built, since
it fills the standalone-compressor gap that Strata does not.

`aw-batch2-ports` is the plan of record for Airwindows porting mechanics; its
cross-cutting findings apply here and should not be re-derived.

## Phases

1. **SectionGate control.** Build and prove the reusable ViewControl against a
   throwaway two-section unit before the strip exists. Enter opens params,
   shift toggles bypass, true bypass verified as CPU savings.
2. **Skeleton + Out.** Seven sections wired, all bypassed, ClipOnly2 on the end.
   Null test: everything bypassed is bit-identical to input.
3. **Pop3.** Comp and Gate sections, with the denormal fix. Habitat's first
   dynamics atom.
4. **Drive.** Channel9, after the 48 kHz cutoff question is settled and the
   model table re-derived.
5. **Filter + EQ.** Capacitor2 (its HP side needs restoring - the habitat port
   kept LP only) and SmoothEQ3.
6. **Punch.** Point and Distance2, un-parked from Carriage.
7. **Hardware.** A8 CPU with all sections active and with all bypassed, the
   difference being the real test of the bypass design; serialization round-trip
   of seven engage flags plus every section's params.

## Attribution

MIT throughout, credit requested. Pop3, Channel9, SmoothEQ3/BiquadStack,
ClipOnly2, ClipSoftly and Capacitor2 all get named in the unit description,
package README and release note per `readme-airwindows-attribution`. The console
model names are Chris's own labels for voicings, not emulation claims, and should
be described that way.
