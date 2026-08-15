# Design note: Nacre - quadrature width, image rotation, frequency shift

Status: design note / not started. Ledger item `nacre-quadrature-width`.

User request 2026-08-14, after the stereo-width survey. One unit built on a
Hilbert/quadrature pair, because that single primitive unlocks three effects that
would otherwise be three units.

Named **Nacre**: mother-of-pearl gets its colour from phase interference between
thin layers, not from pigment. That is exactly what this unit does, and it sits
with Breccia, Sediment and Window Comparator.

Package: **spreadsheet**, with the effects. Biome is the alternative since the
frequency-shifter face is a utility, but the unit as a whole is a creative
stereo effect, not a utility.

## Correcting the record first

The gap review of 2026-08-13 called stereo/imaging "a total blank." That was
wrong, and `diptych-mid-side.md` repeats it. There is no user-facing width
*unit*, but there is real width DSP in the tree:

- **Sujet's Space** (`mods/zaum/atoms/STFTSpectral.h`) is a sophisticated
  spectral decorrelator: a fixed-seed white-phase table smoothed by 6 boxcar
  passes so adjacent bins get *correlated* offsets (the anti-metallic trick), BQI
  frequency weighting peaking at 600 Hz, applied anti-symmetrically to L/R, and
  weighted by spectral prominence so tonal peaks stay frontal while noise bins
  spread. Magnitude untouched.
- **`er-301/mods/core/objects/Spread.{h,cpp}`** is a widener object that exists
  in the firmware and is never exposed as a unit.
- **Filament** (`mods/house/atoms/Filament.h`) already does asymmetric allpass
  decorrelation (Fibonacci delays 5/13 on L against 8/21 on R).
- Fabula's APFTank, Galactic, Network and Anamnesis all have structural or
  geometric width.

So the gap is reachability, not technique. Nacre is deliberately a technique the
tree does *not* already have.

## The primitive

An IIR Hilbert transformer pair: two parallel chains of first-order allpass
sections with a one-sample delay on one branch, whose outputs differ by 90
degrees across the audio band. Published coefficient sets exist; 8-12 sections
total gives usable accuracy. Call the outputs I and Q.

Three things fall out of it:

1. **Quadrature width.** Q is decorrelated from I at every frequency with no comb
   notches - unlike a Haas delay or a complementary comb, which achieve
   decorrelation by putting notches somewhere.
2. **Single-sideband frequency shift.** `out = I*cos(2*pi*f*t) - Q*sin(2*pi*f*t)`
   shifts the whole spectrum by f Hz, inharmonically. This is the frequency
   shifter currently buried inside `biome-utility-dsp-units`.
3. **Image rotation.** The same shift at a fraction of a Hz, applied
   anti-symmetrically to L and R, makes the stereo image rotate continuously.

Building the pair once and exposing all three is the whole argument for this
being one unit rather than three.

## The structural idea: inject width into S only

Work in mid/side. M passes through untouched. All width is injected into the
side signal:

```
M = (L+R)*0.5            S = (L-R)*0.5
S' = S + width * H(M)         // H = the quadrature (Q) branch
out L = M + S'           out R = M - S'
```

The consequence is worth stating plainly: **the mono sum is
`L + R = 2M`, exactly, at every width setting.** Anything added to S vanishes
when the channels sum. So Nacre is mathematically mono-compatible by
construction, not by careful tuning - the same trick that makes the Lauridsen
comb mono-safe, applied to a quadrature source instead of a delay.

It also handles both cases with one path. A mono source has `S = 0`, so
`S' = width*H(M)` synthesises width from nothing. A stereo source keeps its own
S and gets quadrature energy added on top. No mode switch.

## Controls

| control | notes |
|---|---|
| **Width** | 0..1, CV. Quadrature injection into S. 0 must be bit-identical bypass. |
| **Shift** | bipolar, CV. Frequency offset. |
| **Range** | option: Drift (±2 Hz) / Shift (±500 Hz) / Wide (±5 kHz) |
| **Mode** | option: Rotate (anti-symmetric, L +f / R -f) / Shift (common, both +f) |
| **Bass** | mono-below crossover frequency |
| **Mix** | linear, see below |

Shift and Range together are one idea at two scales: at sub-Hz it is image
rotation, at hundreds of Hz it is a frequency shifter. Mode decides whether the
shift is anti-symmetric (rotation) or common (classic SSB shifting). One
mechanism, two well-known musical behaviours, no redundant DSP.

**Bass earns its place twice.** The Hilbert approximation degrades near DC, so
quadrature width at the very bottom is unreliable anyway; a mono-below crossover
turns that limitation into the feature everyone actually wants (bass mono). It
also absorbs the multiband-width idea from the survey without spawning a fourth
unit.

## Cautions

- **The am335x package-trig bug.** The SSB modulator needs sine and cosine, and
  `docs/multi-output-units-author-guide.md` documents that package-side
  `sinf`/`cosf` miscompute on am335x at the package/firmware call boundary -
  silently and geometrically. Do not call libm. Use a **coupled-form (magic
  circle) quadrature oscillator**, which is two multiply-accumulates per sample,
  needs no libm at all, and is cheaper than a LUT lookup. The LUT pattern
  (`mods/spreadsheet/FilterResponseGraphic.h`, `kLutCos`/`kLutSin`) is the
  fallback if amplitude drift in the recursion turns out to matter - it will need
  periodic renormalisation either way.
- **State the usable band.** The Hilbert pair holds 90 degrees only over a
  designed range, degrading at both DC and Nyquist. Pick the coefficient set
  deliberately, document the band, and do not pretend the corners work.
- **This is an internal-stereo unit** (Pattern B in `planning/larets-stereo.md`),
  not two mono instances. M/S needs both channels inside one object. Getting this
  wrong is `biome-discont-mix-left-only` again.
- **Stereo-only.** `channelCount = 2` in toc.lua plus the `onLoadGraph` guard,
  same as Mid Side and `SpreadDelayUnit`.
- **`drywet-crossfade-audit`**: the wet path is *correlated* with the dry (it is
  built from the same M), so linear crossfade is correct here and equal-power
  would be wrong. Note it so the audit does not "fix" it later.
- **`feedback_runtime_branched_dsp_dispatch`**: Range and Mode are options,
  resolved to coefficients at block rate, never switched inside the sample loop.
- **Overlaps `biome-utility-dsp-units`**, which lists a frequency shifter among
  its four utilities. Nacre's Shift face covers it. Not marked as superseding,
  because that item also covers the simple allpass, dome filter and
  bitcrush/downsample - but when Nacre ships, strike the frequency shifter from
  that item's scope rather than building it twice.
- **`scope-goniometer` is the honest prerequisite.** Width and mono compatibility
  cannot be tuned by ear alone. Nacre is testable without it - the mono-sum
  identity below is a numerical test - but a user cannot *use* it well without a
  correlation display.

## Acceptance tests

Two of them are numerical, which is unusual for a width effect and is the payoff
of the M/S structure:

1. **Width = 0 and Shift = 0 is bit-identical to input.**
2. **`L + R == 2M` exactly, at every Width setting**, with Shift at 0. If this
   fails, width is leaking into mid and the structure is wrong.
3. Shift in Rotate mode at 0.1 Hz produces a visibly rotating goniometer trace
   with no change in mono sum level.
4. Shift in Shift mode reproduces classic inharmonic frequency-shifter behaviour
   against a known-good reference.

## Phases

1. **Hilbert pair.** Coefficient set chosen, band documented, I/Q phase error
   measured offline across the band before any unit exists.
2. **Width in M/S.** Tests 1 and 2 above are the gate. Nothing else proceeds
   until the mono-sum identity holds exactly.
3. **Quadrature oscillator, no libm.** Coupled form with renormalisation, then
   Shift, Mode and Range. Tests 3 and 4.
4. **Bass crossover.**
5. **Mix, stereo guard, serialization of both options.**
6. **Hardware.** am335x CPU, and specifically an audit that no `sinf`/`cosf`
   reached the package binary - the trig bug is silent on emu and only appears on
   hardware.

## Not ledgered, from the same survey

Two other techniques worth having, deliberately left out of this unit:

- **Lauridsen complementary comb** - `L = M + d(M)`, `R = M - d(M)`, sum exactly
  `2M`. The cheapest honest mono-to-stereo widener, one delay line. Different
  character from quadrature (comb coloration per channel rather than diffuse
  phase), so it is a genuinely different unit, not a variant.
- **Velvet-noise decorrelation FIR** - two short sparse ±1 sequences per channel.
  Already ranked and deferred as "exotic" in
  `planning/spatial-glitch-impl/02-field.md`.
