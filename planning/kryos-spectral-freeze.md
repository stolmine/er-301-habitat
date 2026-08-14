# Kryos: rebuild as a phase-vocoder spectral freeze

Status: **BUILT** (2026-08-13), spreadsheet-independent package kryos 1.0.0.5.
Steps 0-6 done; step 7 (hardware CPU + listening pass) open.
Harness: tools/kryos-test/run.sh, 7 functional checks, all passing.

Step 0 detail: Offline prototype built and measured:
`tools/kryos-proto/freeze.py`, listening set in /tmp/kryos/. Steps 1-7 open. User goal: ape the Disting EX Spectral Freeze
algorithm, **single voice** (the EX's four-voice structure is explicitly not
wanted). Source read: disting EX user manual 1.26, chapter 24, pp. 171-176.

Provenance note only. The unit ships as **Kryos**, already a generic name; no
third-party product name may appear in any user-facing string, per the
index-vs-identity rule. This document is internal.

## 1. What the current unit actually is, and why it misses

`mods/kryos/Kryos.cpp` is 279 lines and **contains no FFT at all** despite the
"spectral freeze" label. It is 12 biquad bandpass filters at fixed log-spaced
centres, an envelope follower per band, and on freeze it holds each band's
envelope and resynthesizes with one sine per band. So it is a 12-partial
static additive resynthesizer.

The manual names this exact failure mode in its own words:

> The simplest spectral freeze would take just one FFT 'frame' and use that to
> continuously synthesize audio. This generates a completely static sound,
> which, while sometimes a useful effect, doesn't give you the sort of textural
> sustain that is often wanted.

The current engine is strictly worse than that description: 12 partials rather
than a full spectrum, and no history at all. **The engine gets replaced, not
tuned.** Keep the unit name, the Lua shell, and the package.

**Attribution must change with it.** `assets/toc.lua` credits
"Émilie Gillet / ER-301 port by stolmine" and `Kryos.cpp:2` says "Spectral
analysis inspired by Mutable Instruments Clouds, code by Émilie Gillet, MIT
License". Once the biquad bank is gone none of that is true any more, and
leaving it would misattribute work to Emilie Gillet that is not theirs. Rewrite
both to describe what the unit actually is.

## 2. What the EX algorithm is

A **phase vocoder** (stated in the manual's own footnote 78). The parts that
matter, with the four-voice machinery stripped:

| mechanism | manual detail |
|---|---|
| capture | a **history window of FFT frames**, not one frame |
| Depth | 1-128 frames, the extent of the movement window; 1 disables motion |
| Rate | 0-100, 0 stopped, 100 natural speed, **highly non-linear for control at slow speeds** |
| Movement | Forwards / Backwards / Alternating / Random walk / Random skip |
| loop wrap | **interpolated, not crossfaded** - it is resynthesis, not a looper |
| Offset | 0-1023, direct playback position back in time; motion is *added* to Offset |
| Etherization | 0-127, default 127. Lowering it "progressively drops out components of the sound which are judged to be 'transient'. The result tends towards a small number of pure tones which represent the strongest harmonics of the material captured." |
| Attack / Release | 0-127 envelope on the voice |
| Pitch | coarse -48..+24 ST, fine +/-100 cents. Manual concedes quality "is not the last word" |
| Freeze gate | high freezes and plays, low releases |
| Saturation | soft saturation at the mix output |

Dropped as out of scope: the four voices, Freeze target and Auto, per-voice
gain/pan/shift, MIDI note mapping, and the built-in delay and reverb (habitat
already ships Fabula, Petrichor and Network for that).

**Etherization is the signature.** Everything else here is a competent phase
vocoder; the transient-rejection control is the thing that gives the algorithm
its identity, and it should be treated as the headline feature rather than a
trim.

## 3. Feasibility: better than expected

The machinery already exists in-tree.

- `Sediment.h` runs a working am335x STFT: **kFFT 1024, kHop 256 (4x overlap),
  pffft, COLA overlap-add**, estimated 2-4% CPU. That is the exact scaffolding
  a freeze needs and it is already hardware-shaped.
- `pffft.c/.h` already ships in **spreadsheet, mi, catchall and scope**.
- A freeze is **cheaper than Sediment's sort**: while frozen there is no
  analysis FFT at all, only synthesis.

**The trig problem, and the escape.** A textbook phase vocoder does a polar to
rectangular conversion per bin per hop, which at 512 bins and 187.5 hops/sec is
~192k sin/cos per second. At the measured am335x cost of 300-500 ns for libm
sin that is not affordable by an order of magnitude.

Escape: for a *freeze* the phase advance per bin is **constant**, so precompute
one complex rotation per bin at construction and advance by a single complex
multiply (4 mul, 2 add) per bin per hop. No trig in the loop at all, and the
layout is SoA so it is NEON-friendly. Build the rotation table with double
`sin`/`cos` at construction, per the package-trig-LUT rule.

**Memory is the real constraint, and it forces a scope cut.** Storing the EX's
full 128 frames of complex spectra would be 128 x 512 x 2 x 4 B = **512 KB**,
against a 256 KB L2. Even magnitude-only at 128 frames is 256 KB.

Proposal: **magnitude-only history, 32 frames** = 32 x 512 x 4 B = **64 KB**,
which is about 170 ms of history at 48k. Magnitude-only is not a compromise
here, it is the mechanism: regenerating phase from a per-bin rotation is what
makes the wrap "interpolated, not crossfaded" rather than a splice. Depth then
maps 1-32 instead of 1-128. Revisit upward only if 64 KB measures comfortable.

## 4. Controls, single voice

| control | maps to | notes |
|---|---|---|
| Freeze | gate inlet | high freezes and plays, low releases. Threshold `> 0.5f`, per the gate-threshold convention |
| Depth | 1-32 frames | 1 disables motion |
| Rate | 0-1 | non-linear, resolution at the slow end where it matters |
| Movement | Option, 5 values | Forwards / Backwards / Alternating / Random walk / Random skip |
| Offset | 0-1 | position back in time, motion adds to it |
| Ether | 0-1, default 1 | transient rejection. Default = no rejection, matching the EX |
| Attack / Release | 0-1 | envelope |
| Pitch | semitones, CV | V/Oct-style |
| Mix | 0-1 | dry/freeze, **equal-power** (`feedback_equal_power_drywet_crossfade`) |

Mod gain defaults to 0 on every control.

## 5. Etherization, concretely

The manual gives behaviour, not method. A bin is "transient" if its magnitude
fluctuates across the captured frames; it is a steady harmonic if it persists.
So per bin, over the history window, compute a steadiness score - the ratio of
the temporal mean magnitude to its variation, or equivalently mean over
(mean + k*stddev). Sort or threshold on that score and attenuate the bins that
fall below, with the parameter sweeping the threshold from "keep everything"
(127 / 1.0) down to "only the few strongest steady partials".

This runs **once per freeze**, over 32 frames x 512 bins, not per sample. That
is 16k operations at a gesture, which is free.

The manual's phrasing is the acceptance test: as the control is lowered the
result should tend "towards a small number of pure tones which represent the
strongest harmonics".

## 6. Build order

0. **Offline prototype in Python against a WAV.** Etherization and the five
   Movement modes are SOUND questions, settle them offline before any build
   cycle. This was right for Sediment and for Breccia.
1. Lift Sediment's STFT scaffolding (pffft setup, COLA, aligned buffers).
2. Freeze capture into the 32-frame magnitude history, plus the per-bin
   rotation table.
3. Attack/Release envelope, Mix, gate handling. First audible milestone.
4. Movement, Rate, Depth, Offset.
5. Etherization.
6. Pitch shift.
7. Hardware CPU, then the listening pass.

## 6a. Step 0 results - what the prototype settled

Four decisions, all measured, all of which change the C++.

**1. Store the true instantaneous frequency at capture. Do not use bin-centre
rotation.** Both were built and compared on the same freeze:

| phase model | spectral match | envelope wobble |
|---|---|---|
| bin-centre nominal advance (stores nothing) | 0.775 | **0.615** |
| true inst. freq at the freeze instant (+2 KB once) | 0.724 | **0.172** |

Bin-centre scores slightly better on spectral shape and is 3.6x worse where it
counts. Adjacent bins of one partial rotate at slightly wrong relative rates and
beat against each other - the classic phasey-freeze warble. The cost is one
extra float per bin ONCE (2 KB), not per frame. `02_BAD_binphase.wav` in the
listening set is the bad one, kept deliberately for comparison.

**2. Etherization must score steadiness WEIGHTED BY LEVEL.** The first attempt
ranked on steadiness alone and was completely inert - 24 bins and 4 partials at
every setting from 1.0 down to 0.03. Cause, measured: 492 of 513 bins are noise
floor and score inside a band 0.039 wide, so a quantile threshold spends its
entire travel re-sorting inaudible bins and never reaches the loud ones. The
reference says "the STRONGEST harmonics" and that word is load-bearing. Score
is `steadiness * magnitude`, thresholded by keeping the top N with N swept
geometrically. Result:

| Ether | bins kept | partials out | harmonic share | noise floor |
|---|---|---|---|---|
| 1.00 | 513 | 56 | 0.23 | -30.5 dB |
| 0.60 | 106 | 15 | 0.44 | -83.9 dB |
| 0.30 | 23 | 6 | 0.68 | -157.2 dB |
| 0.05 | 7 | 2 | 0.76 | -156.5 dB |
| 0.00 | 4 | 1 | 0.74 | -159.9 dB |

56 partials down to 1 while harmonic share more than triples. That is the
manual's sentence, measured.

**3. All five Movement modes are alive and distinct.** Centroid drift 532-593
against 279 for a static freeze, with different decorrelation signatures.

**4. Pitch: remap magnitude AND rotation together, and silence out-of-range
bins.** Tracking specific partials rather than the global spectral maximum,
worst mean error is 0.50 ST across -12 to +19 ST, which is fine for a feature
the reference itself concedes is not its strong suit.

**A test-source caveat worth carrying.** The first source was too clean - 5
harmonics and short transients, so after a 170 ms window there was almost
nothing transient left to reject and Etherization had no work to do. A
sustained broadband bed had to be added before the test meant anything. Same
lesson as the Kaleidoscope crossfade test: when measuring a rejection, the
material must actually contain the thing being rejected.

**A wrong call, recorded.** The -12 ST case first read as a +29 ST error and was
called a real DSP bug. It was not - the estimator takes the global maximum, and
after downshifting a different partial becomes loudest. Matched partials come
out at exactly 0.5. The out-of-range guard added while chasing it is correct on
its own terms (a clamp would hold the edge bin instead of going silent) but it
fixed nothing measurable.

## 7. Open

- Does 32 frames of history give enough movement to be interesting, or does
  Depth need to reach further and pay for it with 16-bit magnitude storage?
- Pitch shift by bin remap versus resampling the resynthesis; the EX itself
  concedes the quality here, so cheap is acceptable.
- Whether Saturation is worth a control or should just always be on.

## 8. Related

- `kryos-load-hang` - the reported hardware hang did **not** reproduce
  (2026-08-13). Emulator clean via `er-301/tests/emu/94-kryos-insert-smoke.test`,
  and the static pass ruled out vectorization, NEON hints, stale objects,
  unbounded loops and allocation size.
- `spectral-sort-unit` (Sediment, parked) - source of the STFT scaffolding.
- Blocks the multiband spectral-freeze idea (per-band freeze gates on the
  crossover engine) noted in the original Kryos ledger entry.

## Sources

- disting EX user manual 1.26, chapter 24 "Spectral Freeze", pp. 171-176:
  https://www.expert-sleepers.co.uk/downloads/manuals/disting_EX_user_manual_1.26.pdf
- https://www.expert-sleepers.co.uk/distingEXfirmwareupdates.html


## 9. Build notes (1.0.0.4)

Engine replaced wholesale. `Kryos.h/.cpp` are now an STFT phase vocoder;
`pffft.c/.h/_stubs.c` copied into the package; the biquad bank is gone.

**Two things the C++ does that the prototype did not have to.**

Rotation is stored as a **unit complex pair, not an angle**, so advancing the
phase is one complex multiply per bin per hop with no trig in the loop at all -
and it is derived as `cur * conj(prev)` normalized, which needs no atan2 either.
A Newton step (`1.5 - 0.5*|z|^2`) pulls the accumulator back onto the unit
circle each hop; without it, thousands of multiplies of not-quite-unit values
drift to zero or blow up. The angle IS kept, but only so pitch shifting can
rescale it (angle scales linearly with the ratio, a unit complex does not), and
the complex form is rebuilt from the sine LUT when the control moves.

Ether sorts the per-bin scores **once at capture** so the live control is an
O(1) threshold lookup at block rate rather than a selection pass per hop.

## 10. Three bugs the harness caught

`tools/kryos-test` compiles the real `Kryos.cpp` with shipping flags into
0x3B-poisoned storage and asserts behaviour. It earned its keep immediately.

**1. The freeze collapsed to exact silence when the input stopped.** Analysis
kept writing into the magnitude history while frozen, so the capture was
overwritten within kHist hops (170 ms) and the voice played zeros. Measured as
rms 0.0728 while the input ran, then precisely 0.0000 with it cut - the one
thing a freeze must never do. Fix: the FFT still runs every hop (so the next
capture is warm) but the history ring is only written while NOT frozen.

**2. `-fno-tree-vectorize` was missing for linux.** GCC vectorized the
double-`sin` LUT loop into libmvec (`_ZGVbN2v_sin`), which a package .so does
not link against, so the module failed to load with "undefined symbol" the
moment a unit was inserted. Same flag as the am335x NEON rule, different
failure. spreadsheet already had it; kryos only had the am335x arm.

**3. kryos had no header-dependency tracking at all** (no `-MMD -MP`), so
editing a `.h` did not rebuild the `.o` that included it. Added. Related trap
hit during the same session: editing `mod.mk` does not invalidate existing
objects either, so a CFLAGS change needs the object dir removed by hand - and
NOT via `make kryos-clean`, which deletes the whole `testing/<arch>` tree
rather than just this package.

## 11. A test that lied

The Movement check first reported all five modes identical, then one identical
pair. Neither was a DSP fault. At Rate 0.5 the position advances 0.045
frames/hop, so crossing 31 frames takes ~1400 blocks; in a 400-block window
Alternating never reaches a boundary and is CORRECTLY identical to Forwards.
The stimulus was under-powered, not the code wrong. Same shape as the
Etherization test needing a broadband bed before it measured anything: when a
test says the code is broken, check that the stimulus actually exercises the
thing being measured.


## 12. Energy preservation (1.0.0.5) - and where the "bloom" comes from

Reported: level drops noticeably when Ether is lowered and when pitch is
shifted. Measured on material with a broadband bed (where real energy lives,
unlike a clean test tone):

| control | before | after |
|---|---|---|
| Ether 1.0 -> 0.05 | **-4.1 dB** | **-1.8 dB** |
| Shift -24 ST | **-7.2 dB** | -1.1 dB |
| Shift +24 ST | **+4.2 dB** | -0.3 dB |
| Shift range swing | **11.4 dB** | **2.2 dB** |

Shift was the worse offender and it was ASYMMETRIC, which points straight at the
cause: a downshift reads `src = k/rho` from the quieter top of the spectrum and
drops whatever falls past the last bin, while an upshift stretches loud
low-frequency content across more bins. Neither operation preserves energy, so
both controls were secretly level controls.

Fix: accumulate the frame's energy BEFORE gating and shifting and again AFTER,
then scale by `sqrt(eRef/eOut)`. Capped at 8x (at extreme settings eOut can be a
rounding error and an uncapped ratio would detonate) and one-pole smoothed over
~30 ms, since both controls are swept live and a step would zipper.

**This is also the bloom.** Holding total energy while discarding all but a few
bins necessarily makes the survivors much louder - they expand to fill the space
the transients vacated instead of just leaving a hole. The behaviour that was
wanted falls out of the normalization; it did not need a separate mechanism.

Residual -1.8 dB at the Ether extreme is not an energy error but a density one:
two partials at the same total energy as a full spectrum are still perceptually
thinner. Partial count over the sweep is unchanged (11 / 11 / 7 / 2), so the
gate still does its job.
