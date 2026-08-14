# Design note: Assay - audio analysis to CV, on the multi-out framework

Status: design note / not started. Ledger item `assay-audio-listener`.

User picked this out of four proposals on 2026-08-14 as the one they liked most.

Named **Assay**: the analysis of an ore to determine what it is made of. Sits
with Breccia, Sediment, Sill and Strata.

Package: **biome**, with the CV utilities and next to Spectral Follower, which is
the closest existing thing.

## What it is

One unit that listens to audio and emits five control signals:

| sub-out | label | what it is |
|---|---|---|
| 1 (primary) | `env` | amplitude envelope |
| 2 | `pitch` | detected fundamental as V/oct |
| 3 | `cent` | spectral centroid - "brightness" |
| 4 | `flat` | spectral flatness - tonal vs noisy |
| 5 | `trig` | transient trigger |

Sill turns voltages into gates. Assay turns *audio* into voltages. Between them
a patch can respond to what is actually sounding rather than only to what was
deliberately patched.

Nothing in the collection or the firmware does this. `SpectralFollower`
(`mods/biome/SpectralFollower.h`) is a single band-limited envelope: one biquad
BPF, one envelope, four parameters. The firmware's `Envelope Follower` is one
broadband envelope. There is **no pitch detector anywhere in the tree** - I
grepped for autocorrelation, YIN, and pitch-detection naming across `mods/` and
the firmware and found nothing.

## Why it is a multi-out unit and why it is affordable

It passes the author guide's "derivable at destination" test easily: you cannot
reconstruct spectral flatness from an envelope downstream.

The cost argument is the interesting part. **One STFT feeds pitch, centroid and
flatness.** Given a magnitude spectrum, centroid is `sum(f*mag)/sum(mag)` and
flatness is a geometric-over-arithmetic mean ratio - both are single reductions
over bins, i.e. the guide's Category A "free byproducts." Only the transform is
expensive. Envelope and transient run entirely in the time domain and never touch
it.

So the gating rule from the guide matters more here than in any other unit
proposed so far: **if none of `pitch`/`cent`/`flat` is connected, skip the FFT
entirely.** That is a textbook Category C gate, and it means a patch using Assay
only as an envelope follower pays almost nothing.

`pffft` is already vendored (spreadsheet, mi, biome) and an STFT already runs on
hardware in Spectral Freeze, so the transform is proven rather than new.

Sub-out labels, all within the 6-char limit: `{"env", "pitch", "cent", "flat",
"trig"}`. Five is comfortable for the M6 cycler. **Primary is `env`** because it
is the most universally useful default and because it is the one thing vanilla
firmware can reach.

## The latency trap

This needs stating loudly because it will otherwise be discovered as a bug:

**The five outputs do not share a latency.** `env` and `trig` are time-domain and
effectively instantaneous. `pitch`, `cent` and `flat` are STFT-derived and lag by
at least a hop, plausibly 256-1024 samples. Using `trig` to sample-and-hold
`pitch` will therefore latch the pitch of the *previous* note unless the trigger
is delayed to match.

Two honest options: document the skew and let the user compensate, or delay
`env`/`trig` to align with the spectral outputs. Delaying is friendlier but makes
the unit useless as a fast transient detector. **Recommend: do not delay, document
the skew, and expose a Delay sub-param on the trigger so the user can align it
when they want to.**

## Pitch detection

The hard part, and the one that decides whether the unit is any good.

- **Zero-crossing** is cheap and falls apart on anything with harmonics. No.
- **Autocorrelation / YIN** is the accurate choice but wants its own buffer and
  its own cost, and does not share work with the other outputs.
- **FFT-based, with harmonic product spectrum** reuses the transform that
  centroid and flatness already need. Peak bin plus parabolic interpolation for
  sub-bin resolution; HPS to suppress the octave errors that are the classic
  failure mode of spectral pitch detection.

Take the FFT route, because sharing the transform is the whole cost argument for
the unit.

**Two behaviours that decide whether it feels good in a patch:**

1. **Silence must not produce garbage.** Below an input gate, hold the last valid
   pitch rather than emitting whatever the noise floor peaks at. A pitch output
   that thrashes between notes is worse than one that freezes.
2. **Low confidence should hold, not lie.** When the spectrum is too flat or too
   noisy for a fundamental to mean anything, hold. Rather than spend a sixth
   sub-out on a confidence signal, fold confidence into the hold behaviour - and
   note that `flat` is already a serviceable confidence proxy the user can patch.

## Controls

| control | notes |
|---|---|
| **Attack** / **Release** | envelope ballistics |
| **Sens** | transient detection threshold |
| **Range** | pitch search range, constrains octave errors |
| **Smooth** | slew on the CV outputs, shared |
| **Gate** | input threshold below which pitch and spectral outputs hold |

Sub-params: trigger **Delay** (see the latency trap), FFT **Size** if it proves
worth exposing.

## Cautions

- **Vanilla**: sub-outs 3+ are invisible on stock firmware, so `cent`, `flat` and
  `trig` are stolmine-only. `env` works everywhere and `pitch` resolves on a
  stereo chain. This is the third stolmine-first unit proposed in two days
  (`diptych-mid-side`, `sill-window-comparator`, this) - the accumulation is
  worth a deliberate decision about biome's vanilla posture rather than drifting
  into it.
- **Package trig bug**: `pffft` builds its own twiddle tables at setup rather
  than calling `sinf`/`cosf` per sample, and Spectral Freeze already ships it on
  hardware, so this is settled - but audit the objdump anyway, because the bug is
  silent on emu.
- **`log` for flatness**: geometric mean needs a log per bin. Use `log2_poly`
  from `util/neon_math.h`, not libm.
- **`feedback_runtime_branched_dsp_dispatch`**: the FFT-connected gate is a
  block-rate decision, never a per-sample branch.
- **CV output convention**: pitch must be V/oct in whatever scaling the firmware
  actually uses. Pin it against the core `V/oct to Hertz` unit rather than
  assuming - the same caution as `sill-window-comparator`.

## Phases

1. **Time-domain half.** Envelope with Attack/Release, transient detector with
   Sens. Two sub-outs, no FFT, works on vanilla. Useful on its own and a real
   shipping increment.
2. **STFT + centroid and flatness.** Add the transform behind the
   `isConnected()` gate; prove the gate saves what it should by measuring CPU
   with and without those outputs patched.
3. **Pitch.** HPS with parabolic interpolation, hold-on-silence and
   hold-on-low-confidence. Test against a swept sine, a square, a detuned pad and
   a drum loop - the last two are where octave errors appear.
4. **Trigger delay sub-param** for aligning against the spectral outputs.
5. **Hardware.** A8 CPU in both gated and ungated states, multi-out picker
   checklist from the guide, serialization round-trip.

## Related

- `spectral-sort-unit`, `units-spectral-processing` and the soothe-style
  resonance suppressor all want a per-bin spectral analysis stage. If more than
  one of them is built, the STFT front end should become a shared atom rather
  than being written three times.
- Sujet's `STFTSpectral.h` already computes a `localRef[k]` ±8-bin spectral
  boxcar and a prominence ratio against it. That is a more sophisticated
  descriptor than anything Assay needs, and it is the obvious donor if a
  "tonality" or "prominence" output is ever wanted.
