# Open303 port - plan of record

Status: **in progress** (2026-08-10). Supersedes `planning/open303-port-scoping.md`

> **2026-08-10 revisions (user direction + source verification).** Three changes
> to this document, recorded rather than silently applied:
> 1. **Package is `biome`, not `catchall` -> `spreadsheet`.** User call. The
>    prototype-then-promote step is dropped; it lands in biome directly, next to
>    Varishape Voice (the closest existing template: mono voice, V/oct + gate).
> 2. **Phases 1 and 2 are done together, not sequentially.** User chose the
>    am335x-shippable target up front, so the optimization work is not deferred
>    behind a naive-port measurement.
> 3. **Two optimization items did not survive contact with the source** - B3 is
>    withdrawn (wrong premise) and C's approx-coeffs item is a no-op (already the
>    shipped behavior). Both are struck through in place below with the evidence.
>    The am335x target still holds: B2 alone clears the L2 wall.
>
> Scope discipline for this pass: the DSP work is the full Phase 1 + 2 set, but
> the unit surface stays at the v1 control list and the Devil Fish aux controls
> stay deferred.

(2026-04-30 surface assessment) on two points: the wavetable tables are NOT
cache-friendly on am335x, and we should go `float`-first, not double-internal.
The scoping doc's file inventory, audio-path walk, and phased shape still hold;
read it for the per-file LOC table.

Source: `~/repos/Open303` (github RobinSchmidt/Open303). License: **MIT**,
`Copyright (c) 2009 Robin Schmidt` (`License.txt` verified). Attribution stays
in every vendored file. Ledger item: `open303-port`; part of the `port-mit-direct`
backlog (Open303 = spreadsheet flagship alongside Ngoma).

## Goal

An am335x-shippable monophonic acid-bassline voice, shipped in **`biome`**
(2026-08-10 user call; the earlier catchall-prototype-then-promote-to-spreadsheet
route is dropped). User-facing name/description stays generic (no Roland / 303 /
TB wording, per the no-third-party-branding convention); internal `rosic_*`
filenames keep their names for attribution.

## Port boundary

Keep the voice, drop the sequencer. `rosic::Open303::getSample()` branches on
`sequencer.getSequencerMode() != OFF`; run it OFF and the whole internal 303
sequencer falls away. Drive `triggerNote / slideToNote / releaseNote` from
ER-301 inlets instead.

- **Keep** (audio path + envelopes + wavetable + init FFT): `rosic_Open303`,
  `rosic_BlendOscillator`, `rosic_MipMappedWaveTable`, `rosic_TeeBeeFilter`,
  `rosic_AnalogEnvelope`, `rosic_DecayEnvelope`, `rosic_LeakyIntegrator`,
  `rosic_OnePoleFilter`, `rosic_BiquadFilter`, `rosic_EllipticQuarterBandFilter`,
  the `Global*` / `rosic_RealFunctions` / `rosic_NumberManipulations` /
  `rosic_FunctionTemplates` math, and (construction-time only) the FFT trio
  `rosic_FourierTransformerRadix2` / `rosic_Complex` / `fft4g.c`. The FFT can be
  swapped for the `pffft` spreadsheet already ships; cheap to defer.
- **Drop**: `rosic_AcidSequencer`, `rosic_AcidPattern`, `rosic_MidiNoteEvent`,
  the `std::list` note tracking (replace with a 3-4 entry fixed ring), the VST
  shell, `Build/`, `Notes/`.

## The hot-path reality (why the optimization work is required, not optional)

Measured against the actual code (`rosic_Open303.h:317-410`,
`rosic_MipMappedWaveTable.h`):

- **The oscillator lives inside the 4x oversampling loop** and blends TWO
  wavetables, so each output sample costs **8 mip-table gathers**.
- **Tables are `double[12][2052] x 2` ~= 385 KB** - every mip level is a full
  2048 samples regardless of how few harmonics it holds (the source even
  comments "room for optimization here"). 385 KB blows the am335x 256 KB L2, so
  those 8 gathers/sample thrash cache. This is the same L2 wall that made
  anamnesis CM4-only.
- **Per-sample `pow(2.0, ...)`** on the env-modulated cutoff
  (`rosic_Open303.h:374`) - a libm call every output sample.
- **4x oversampling** through a 12th-order elliptic decimator - fixed per-sample
  cost.

## Optimization plan

### A. Structural: pull the oscillator OUT of the oversampling loop (biggest win, hits L2 + CPU)

The oscillator is already band-limited by its mip tables, so it does not need
oversampling for its own aliasing - the 4x exists for the filter nonlinearity
(resonance + `shape()` + tanh drive). Compute one oscillator sample per output
sample and linear-interpolate it up into the filter's 4x grid. Gathers/output
drop 8 -> 2 (removes ~75% of the memory-bound oscillator work and its CPU); the
filter keeps its anti-alias benefit. Intermod difference is inaudible on an acid
bass.

### B. Table footprint (stackable, ~385 KB -> ~30 KB)

1. **`float` not `double`** -> ~197 KB. Mandatory; also the only path to NEON and
   the reason to reverse the scoping doc's "double-internal" call.
2. **Shrinking mip pyramid.** High-octave levels hold few harmonics and need a
   fraction of 2048 samples. A pyramid (2048, 2048, 1024, 512, 256, ...) sums to
   ~2-3x the base table instead of 12x -> ~30 KB/waveform. Needs per-level length
   + phase-scale in the table read. Biggest pure-memory win.
3. ~~**Derive the square from one saw table.**~~ **WITHDRAWN 2026-08-10 - the
   premise was wrong, verified against the source.** This item claimed the 303
   square is a phase-shifted-saw *difference*. It is not.
   `fillWithSquare303()` (`rosic_MipMappedWaveTable.cpp:245-268`) builds a saw,
   applies `-tanh(tanhShaperFactor*saw + tanhShaperOffset)` with
   `tanhShaperFactor = dB2amp(36.9) ~= 70` and `offset = 4.37`, then circular-
   shifts by `squarePhaseShift = 180` degrees. `set303SquarePhaseShift` sets that
   circular shift of the ALREADY-shaped table; it is not a saw-minus-saw
   construction. The mip levels are band-limited by FFT AFTER the tanh, so they
   are not reachable from the saw's band-limited levels - applying the tanh to a
   band-limited saw at runtime re-introduces exactly the aliasing the pyramid
   exists to prevent. No cheap substitute; the second table stays.
4. **Optional int16 tables + fixed-point interp** -> another x0.5; quality cost
   negligible on a gritty voice, Cortex-A8 int loads are cheap. Reserve for
   headroom.

Stacking 1-2 lands the table set at ~33 KB, permanently hot in L2. Dropping B3
costs nothing: B2 alone clears the L2 wall with room to spare (see the sizing
below), so B3 was never load-bearing.

**B2 sizing, derived from the actual band-limiting.** `generateMipMap()` zeroes
packed-spectrum indices `>= tableLength/2^t`, i.e. bins `>= 1024/2^t`, so level
`t` holds harmonics below `1024/2^t`: level 0 is full-band (1023 harmonics,
needs the full 2048 samples), level 1 holds 511, level 2 holds 255, and so on.
Lengths therefore go 2048, 1024, 512, 256, ..., 2 - a sum of ~4094 samples plus
4 guard samples per level, ~16.6 KB per waveform in float and **~33 KB for the
pair**, against 385 KB today. Note the top levels are nearly empty (level 11
holds no harmonics at all).

### C. Oversampling / CPU

- After A, the 4x body is only filter + elliptic decimator + declicker.
- **Tiers:** am335x = **2x**, CM4 = **4x**. 2x halves the remaining OS cost.
- **The elliptic decimator is RETIRED on both tiers (2026-08-10), not just at 2x.**
  The plan originally swapped it only for the 2x tier. Source inspection says it
  cannot stay on either tier once the audio path is float:
  `EllipticQuarterBandFilter::getSample` (`rosic_EllipticQuarterBandFilter.h:53-99`)
  is a **12th-order Direct Form II IIR** with feedback coefficients ranging to
  `a06 = 308.16` against `b00 = 1.37e-4`. A direct-form recursion of that order
  and coefficient spread is ill-conditioned in single precision - it is already
  marginal in double, and in float it risks audible noise or outright
  instability. Keeping it in double instead would put a 12th-order double-precision
  IIR on the per-oversampled-sample path, which is precisely what
  `feedback_cortex_a8_no_double_in_hot_loops` forbids (no DP NEON on A8; doubles
  fall back to scalar VFPv3 at 3-4x).
  **Replacement:** a **polyphase halfband FIR** decimator, `O3Halfband`. Stable
  in float by construction (FIR, no recursion), every other tap is exactly zero,
  and it cascades - ONE stage for the 2x tier, TWO stages (4x->2x->1x) for the 4x
  tier. Uniform code on both tiers, no double anywhere on the audio path.
  `rosic_EllipticQuarterBandFilter.*` stays vendored for provenance but is out of
  the build.
- **Fast `exp2` polynomial** to replace the per-sample `pow(2.0, ...)` cutoff
  (`rosic_Open303.h:374`). This is the only per-sample libm call in the voice and
  the whole of C's CPU win outside the tier change.
- ~~Use the existing `calculateCoefficientsApprox4()` rather than the exact
  tan/exp coeff path.~~ **NO-OP 2026-08-10 - already the shipped behavior.**
  `TeeBeeFilter::setCutoff` calls `calculateCoefficientsApprox4()` directly
  (`rosic_TeeBeeFilter.h:162`); `calculateCoefficientsExact()` is never reached
  in this configuration and its `tan`/`exp`/`sinCos` are dead code. The
  per-sample coefficient path is already libm-free: in `TB_303` mode it is a
  rational for `b0` plus a 6th-order polynomial for `k`. No work, no win. (The
  one `exp` in `setResonance` is param-change-rate, not per-sample.)
- **No NEON for a single serial mono voice** - the lever is float-scalar + fewer
  ops. (NEON would only pay off in a paraphonic SoA fork.)

## Measured, 2026-08-10 (x86 bench, offline harness)

A/B against the pristine upstream engine built from `~/repos/Open303`, same
params, same note, 48 kHz, 1 s render.

**Fidelity.** Raw correlation reads low (0.61 / 0.70) purely because the
halfband cascade has a fixed group delay the elliptic IIR did not - 22 output
samples at 4x, 14 at 2x. Aligned for that lag:

| tier | aligned corr vs upstream | spectrum, bins within 60 dB of peak |
|---|---|---|
| 4x | 0.9997 | median -0.32 dB, 95th pct 1.70 dB, max 2.59 dB |
| 2x | 0.9995 | median -0.32 dB, 95th pct 2.06 dB, max 3.04 dB |

First 8 harmonics match upstream within 0.1 dB. Fundamental identical.

**Footprint - the point of the exercise.**

| | upstream | port |
|---|---|---|
| `sizeof(MipMappedWaveTable)` | 213,472 B | 33,296 B |
| `sizeof(Open303)` | 431,800 B | 67,952 B |

The whole voice now fits in a quarter of the am335x 256 KB L2 instead of
overflowing it by 175 KB.

**CPU (x86, ns/sample).** Upstream 171.5, port 4x 194.2, port 2x 120.4.
Read this honestly: **the 4x tier is SLOWER than upstream on x86.** The 63-tap
halfband cascade is ~51 MACs per output against the elliptic's ~12, and x86
models neither thing that motivated the swap - it has fast doubles and enough
cache to hide 385 KB of tables. The shipping am335x tier is 2x, which is 1.4x
faster than upstream even on the arch that flatters upstream most.
- OPEN, if CM4 profiling ever asks for it: the 4x cascade's FIRST stage can be
  much shorter than 63 taps. In a multistage decimator only the final stage
  needs full stopband depth, because stage 1's near-Nyquist fold lands in a
  region stage 2 removes. Not done - it only affects the non-shipping tier.
- OWED: am335x hardware CPU%. Per feedback_f64_count_poor_cpu_proxy the x86
  numbers understate the win, since upstream is double throughout.

## 2026-08-11 defect fixes + hot-spot pass (biome 2.2.3.3)

Two audible defects reported from the device, both root-caused and fixed, plus
the optimization sweep that fell out of the same review.

**Defect 1 - extremely quiet.** The Level control mapped 0..1 to -60..0 dB, so
the shipped default bias of 0.5 sat at -30 dB. Measured at the unit's actual
default biases: peak 0.033, RMS 0.006. User independently confirmed by finding
that Level 1.0 (0 dB under that map) produced a normal waveform. Level is now a
LINEAR output gain 0..1, passed as 20*log10(lv) so `ampScaler == lv` exactly,
matching the biome voice convention. Measured after: peak 0.525, RMS 0.077.
- NOTE, not a defect but worth knowing: the voice can exceed full scale when
  resonance is up. At Level 0.5 / Resonance 0.7 the peak measures 1.055. That is
  the instrument being loud, not a bug, but it means Level 1.0 with resonance
  will clip into the DAC and wants attenuation downstream.

**Defect 2 - Slide killed the note.** On a gate rising edge with Slide high the
unit called `slideToNote()`, which by design never touches the amp envelope:
upstream only ever called it mid-legato with a note already sounding. With a
modular gate that falls between steps the envelope had already released, so the
slid note was silent (measured peak 0.0000). Slide now selects GLIDE vs JUMP of
pitch, and a note sounds on every rising edge: `triggerNote()` gained a `glide`
argument that skips the `pitchSlewLimiter.setState()` force-set. A glide from
silence degenerates to a jump. True legato (gate held across a pitch change)
still avoids a re-attack through the tracking branch. Measured across a 30 ms
gate gap, 55 -> 110 Hz: glide tracks 143/158/172/186/196/206/212/215 Hz,
monotonic; jump lands at 110 Hz immediately.

**Hot-spot pass.** Four findings from an ARM-codegen review, all applied:
1. `setNoteNumber()` ran EVERY sample on the no-edge branch, and it costs a
   double-precision libm `exp()` via `pitchToFreq` - the exact per-sample
   libm-in-double this port removed from `getSample()`. Now guarded by a
   note-change check.
2. `TeeBeeFilter::calculateCoefficientsApprox4()`'s live TB_303 branch used bare
   double literals, promoting the whole chain: 31 f64 ops per sample including a
   `vdiv.f64`. Constants are f-suffixed.
3. That same function computed a 12th-order and an 8th-order polynomial per
   sample and then DISCARDED them in TB_303 mode (the branch overwrote b0/k/g,
   and TB_303's `getSample` never reads a1). TB_303 now returns early.
4. Nine `double` members read on the per-sample path (`ampScaler`, `oscFreq`,
   `cutoff`, `envScaler`, `envOffset`, `accentGain`, `pitchWheelFactor`, `n1`,
   `n2`) retyped to `o3Float`. Setters still compute in double; the narrowing
   happens once at set time.

Also fixed, a latent UB: `O3Halfband::mPos` was a signed `int` advanced twice
per output sample and never reset, overflowing after a couple of hours of
continuous audio. Signed overflow is UB that GCC at -O3 is entitled to exploit,
which is the class of bug that burned anamnesis (`field::hash01`). Now unsigned,
where wraparound is defined and the `& kMask` indexing stays correct.

**Verification.** A/B against the pre-optimization build: correlation 0.999998,
difference -53 dBr (4x) / -55.8 dBr (2x) - float-precision shift on the
coefficient path, inaudible. x86 bench: **2x tier 120.4 -> 91.6 ns/sample (24%
faster), 4x 194.2 -> 155.9**, so the 4x tier now beats upstream's 171.5 as well.
The am335x win should be larger than x86 shows, since x86 models neither VFPLite
double cost nor conversion traffic; hardware CPU% still owed.

**Confirmed NOT a problem (investigated, no action).** The `approx4` comment
warns the polynomial is valid only to normalized radian cutoff pi/4, and the 2x
tier reaches wc = 1.309 at 20 kHz. That comment belongs to the pa/pr
polynomials, which are dead in TB_303 mode. An impulse-response Goertzel probe
of the live mystran/kunn fit at 96k vs 192k internal rates puts the resonant
peak at 19,930 Hz for a commanded 20,000 Hz - 0.35% flat, about 6 cents -
monotone across the range, resonance height within ~1.7 dB of the 4x tier, no
instability at resonance 100.

**NEON: no-go confirmed** (was estimated, now evidenced). The sample budget is
dominated by serial feedback recursions with a one-sample dependency: the ladder
twice per output, plus feedback highpass, three one-poles, two biquads, three
leaky integrators. No cross-sample parallelism for a mono voice. The only
SIMD-able kernel is the halfband's 17 MACs (under 10% of sample cost, with a
ring-buffer seam), and the oscillator is gather-bound with no NEON gather on A8.

**Remaining, not done.** The 4x cascade's first halfband stage can be ~17 taps
instead of 63 (in a multistage decimator only the final stage needs full
stopband depth), saving ~24 MACs/output on the NON-shipping tier only. The 2x
tier's single stage IS the final stage, so its 63 taps are correctly sized and
there is nothing to shave. `idle` never re-arms (upstream parity), so the voice
burns full CPU while silent; re-arming on `ampEnv.endIsReached()` is an
average-CPU win with no change to the peak budget.

## Target tiers

| tier | build | oversampling | tables | precision | intent |
|---|---|---|---|---|---|
| am335x | `ARCH=am335x` | 2x, halfband | float pyramid, saw + square (~33 KB) | float | shippable hardware voice |
| CM4 / linux | `ARCH=linux` | 4x, elliptic | float pyramid | float | max fidelity |

Same source, tier switched at build (an `OS_FACTOR` + decimator selection).
`double` is not retained on either tier - the filter numerics are re-validated
by ear in the listening pass, not preserved by precision.

## ER-301 unit surface

- **Inlets:** V/oct -> pitch, Gate (rising edge, `> 0.5f` per the comparator
  convention), Accent gate, Slide gate.
- **Controls (v1):** Cutoff, Resonance, EnvMod, Decay, Accent, Waveform, Drive
  (tanh shaper), Tuning, Volume. ~6 plies (trig/pitch/cutoff/reso/envmod/decay/
  accent/level), matching Pecto/Ngoma.
- **Devil Fish aux** (AmpSustain, tanh offset, pre/feedback/post highpass,
  square phase shift): expanded-view aux controls, ship in v1.x once the core
  sound is dialed.
- **Wrap:** SWIG'd `od::Object` (the anamnesis / Ngoma pattern); vendored DSP
  under `mods/<pkg>/open303/` for namespace isolation.

## Build order

Revised 2026-08-10: phases 1 and 2 merge (am335x target chosen up front), and
phase 4's promotion step is dropped since it lands in biome directly.

- **Phase 1+2 - the voice, optimized, in biome.** Vendor the keep-set under
  `mods/biome/open303/` (attribution preserved in every file; `mod.mk` needs its
  flat `wildcard $(MOD_DIR)/*.cpp` extended to reach the subdirectory). Strip the
  sequencer branch from `getSample()`. Wire trig / V-oct / accent / slide inlets.
  Then, in the same pass: A (oscillator out of the oversampling loop), B1 (float
  audio path), B2 (shrinking mip pyramid), C (fast exp2, 2x am335x tier with a
  polyphase halfband). Build both arches; run all three lints.
  - **Precision split:** float is for the AUDIO PATH only. Table generation and
    the construction-time FFT stay in double - they run once at insert on the app
    stack, they are not hot, and keeping them exact preserves table fidelity.
    Store the resulting tables as float.
  - **Construction-time stack discipline:** table generation must not put large
    buffers on the stack. `generateMipMap`'s `spectrum` is already `static`, but
    `reverseTime` holds a `double tmpTable[2052]` (~16 KB) - unreached by the 303
    waveforms, but the vendored copy should use member/static scratch regardless.
    Per `feedback_draw_path_busy_stack`, insert-time paths are not guaranteed the
    32 KB app stack.
- **Phase 3 - listening + tuning:** hardware, driven by Excel/Ballot; verify the
  resonance sweep, accent, and slide against 303 reference recordings. If the
  sound is wrong, revisit the filter topology (`TeeBeeFilter` mode enum) before
  reaching for double.
- **Phase 4 - v1.x aux:** add the Devil Fish aux controls (AmpSustain, tanh
  drive/offset, pre/feedback/post highpass, square phase shift). Note these
  mutate the wavetables at param-change time, which is why the tables cannot be
  made `static`-shared until their interaction with the aux set is settled.

## Open decisions

- **Voice count:** monophonic (matches the 303, saves CPU) vs a paraphonic SoA
  fork (would justify NEON). Lean monophonic for v1.
- **FFT:** carry the rosic FFT trio (construction-only, cheap) vs substitute
  `pffft`. Lean carry-for-v1, swap later if the pyramid rework needs it.
- **int16 tables (B4):** only if 2x + pyramid still leaves L2 tight after Phase 2
  measurement.
