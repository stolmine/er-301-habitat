# Design note: NEON FDN reverb with spectral flavor

Status: design note / not started. Ledger item `fdn-reverb`.

Goal: a new reverb that is (a) more performant than Fabula and the native-object
FDNs, by being a custom C++ atom that NEON-vectorizes the whole tank, and (b)
more interesting than Sujet, by pairing a dense time-domain FDN core with a
spectral flavor layer. FDN Householder is the one reverb topology that is
genuinely NEON-friendly on Cortex-A8 (see [[project_fabula_am335x]] NEON verdict).

## Reference: the yrn1 / Erbe FDN (the one the user enjoys)

github.com/yrn1/er-301-custom-units, `src/mods/fdelay/assets/{FDN,SFDN}.lua`
(Jeroen Baekelandt, BSD), based on **Tom Erbe (UCSD), "Reverb Topologies and
Design"** (tre.ucsd.edu ... reverbtopo.pdf). Architecture:
- **4 delay lines**, delay times from reflection ratios (refl 0.365994 / 0.573487
  / 0.775216 / 1.0), scaled by one Delay control; max ~13.6 s.
- **Hadamard feedback matrix** built from `app.Sum` butterflies (the dif/sum +
  negative/positive network) - a 4x4 +/-1 mix.
- **Per-line 3-band EQ** (eqHigh/eqMid/eqLow) in each line = the "filter" in
  Filter Delays = frequency-dependent decay (the spectral flavor, done natively).
- **Modulation**: FDN modulates the 4 delay times (DopplerDelay, depths 0.13/
  0.17/0.19/0.23) for chorusing; SFDN is static (plain Delay), cheaper.
- Stereo in -> HPF -> dry/wet crossfade; built ENTIRELY from native ER-301
  objects wired in Lua (no custom atom).
- **CPU: FDN ~20% stereo, SFDN ~11-12%.**

The key takeaway: the design is proven and pleasant, and its 20% is spent almost
entirely on per-object overhead of a Lua-wired graph (every Sum, EQ, delay is a
separate scheduled object). A single custom atom that does the same math in one
tight NEON loop should cost a fraction of that. That is our opening.

## Lessons from Sujet (zaum STFTSpectral)

Sujet is a phase vocoder (N=1024, hop=256, 4x OLA) whose "reverb" is per-bin
magnitude smearing (Blur = symmetric cross-time IIR, Bloom = asymmetric
slow-rise/fast-fall). It underwhelmed for a structural reason: **frequency-domain
magnitude-smearing as the reverb ENGINE is washy** - no recirculation, so no
eigentones, no density buildup, no developing tail; plus phase-vocoder haze and
transient smear. BUT two Sujet ideas are genuinely good and worth keeping as
FLAVOR: (1) **Space** - structured, temporally-coherent, frequency-weighted
(BQI) anti-symmetric per-bin phase for stereo width without metallic coloration,
prominence-weighted (partials frontal, noise diffuse); (2) **Bloom** shimmer/swell.

Lesson: strong time-domain core for density/resonance/performance; spectral
processing as a flavor layer, never the engine.

## Core architecture (custom C++ atom, NEON)

- **N delay lines** (start N=8; 4 like Erbe is the floor). Delay lengths mutually
  prime / from reflection ratios; FIXED (or only BLOCK-modulated) so the reads
  are contiguous at a moving write head - NOT the per-sample gather reads that
  killed NEON on Fabula. This is the crux of the NEON win.
- **Householder feedback matrix**: `y_i = x_i - (2/N) * sum(x)`. One horizontal
  reduce, broadcast, one multiply-subtract across the vector (~2 NEON ops for
  N=8). Alternative: **Hadamard / FWHT** (log2(N) butterfly stages, zero
  multiplies) - what yrn1/Erbe use. Both vectorize; Householder is a single dense
  reflection (arguably smoother density), Hadamard is the classic. Try both.
- **Per-line damping filters (the FIRST spectral element)**: a low-shelf +
  high-shelf (Jot absorption) or a small few-band EQ per line, stored SoA across
  the N lines -> a NEON one-pole/SVF bank ([[feedback_neon_soa_svf_bank]]). Gives
  frequency-dependent decay (bright/dark tail, "spectral RT60") - exactly yrn1's
  per-line 3-band EQ, but vectorized. This alone makes it characterful.
- **Input diffuser**: 3-4 series allpasses (Dattorro-style) so the early density
  isn't sparse/metallic before the tank fills. Sample rate, cheap.
- **Decay**: one loop gain scalar (bounded < 1), optionally folded into the
  per-line filters. Modulation optional (block-rate delay-length wobble to break
  eigentones without per-sample gathers).

Everything - delay reads (contiguous), matrix (reduce/butterfly), per-line
filters (SoA bank) - vectorizes. That is the performance ceiling Fabula could not
reach and the native-object FDNs pay object-overhead for.

## The spectral element - where to intervene (ranked)

1. **Per-line loop filters = native FDN spectral control.** Frequency-dependent
   decay. In-loop, sample-rate, NEON. Start here (yrn1 proves it works).
2. **Band-split / parallel small FDNs per band.** Crossover the input into 2-4
   bands, an FDN per band with independent size/decay -> explicit per-band RT60
   as a control surface. More CPU but each FDN is small; the richest STRUCTURAL
   spectral element.
3. **Spectral send/overlay = Sujet's good parts, latency-safe.** DO NOT put the
   STFT in the recirculation - its ~1024-sample frame latency would force the
   loop to frame-rate and destroy echo density (half of why Sujet is washy).
   Instead take a SEND off the FDN into an STFT block doing Sujet's Space-width +
   Bloom-shimmer (or a spectral freeze), and blend the return at the output or
   feed it into the FDN INPUT at low level. Reuses zaum/STFTSpectral as the
   overlay on a strong core.

## Performance target

Beat the native-object FDN's ~20% stereo by a wide margin (single-atom NEON), and
be denser/more resonant than Sujet. Verify: fixed-length delay reads compile to
contiguous NEON loads (no gather), the matrix is a reduce/butterfly not a scalar
loop, and the per-line filters are an SoA bank (objdump the .o). am335x rails:
every graphic virtual inline, file-level no-tree-vectorize, class-member work
buffers (no stack NEON :64 trap), whole-.o hint scan.

## Phased build-out (detailed roadmap)

The Phase-1 scaffold is a *correct* FDN but a minimal one: it recirculates and
decays, but almost all of the character work that separates a lush, believable
reverb from a clean-but-ringy delay net is still ahead. What follows is the full
scope, ordered by how much each item changes the SOUND.

### Phase 1 - core POC  [DONE - spreadsheet 2.8.3.5, unit "Plenum", atom FDNTank.h]

8-line Householder FDN (`f = d - 0.25*sum`), 4-stage Schroeder input diffuser,
per-line one-pole HF damping, DC-blocked mono feed, decorrelated +/-1 stereo
output taps (one shared tank -> true stereo), equal-power dry/wet. Scalar,
am335x-safe (zero q-reg / alignment hints). Controls: Size/Decay/Damp/Mix.

Its minimality, and where each gap is addressed:
- Decay is frequency-flat AND length-uniform (one HF filter; one shared loop
  gain across lines of different length) -> booms in the bass, RT60 uncontrolled
  -> **Phase 2a/2b**.
- No modulation -> fixed eigenfrequencies -> metallic ring on sustained/long
  tails -> **Phase 2d**.
- Mono-summed input, no predelay, fixed diffusion, no wet tone -> **Phase 2d**.
- Scalar DSP -> **Phase 4** (NEON).
- No spectral-flavor layer (the concept's differentiator) -> **Phase 3**.

### Phase 2 - voicing: the jump from "delay net" to "reverb"  [IN PROGRESS]

**2a. RT60-based per-line gain (replaces the uniform loop gain).**
A single `g` across lines of different length means short lines decay faster in
wall-clock time -> the tail's spectral/temporal balance drifts and RT60 isn't a
real control. Fix: per line, `g_i = 10^(-3 * T_i / RT60)` where `T_i = L_i / SR`
(seconds). Equivalently `g_i = exp(-6.907755 * T_i / RT60)`. Now every line
reaches -60 dB at the same wall-clock time -> a uniform, tuned tail. The
Householder matrix stays purely lossless; ALL decay comes from the per-line
loss filters. `g_i` computed at BLOCK rate (8 expf/block; block-rate scalar expf
is am335x-safe - Network.h uses one). Clamp `g_i < 0.9995`.

**2b. Frequency-dependent loss filter per line (Jot absorptive filter).**
The per-line loss becomes a first-order shelving filter with three regimes:
- **Mid** gain `g_i` (from 2a).
- **High**: a one-pole LP folds HF damping into the loss:
  `lp_i = (1-hf)*d_i + hf*lp_i; base = g_i * lp_i` -> DC gain `g_i`, HF gain
  `g_i*(1-hf)/(1+hf)`. `hf` from the Damp control (0..~0.7).
- **Low**: a low-shelf toward a SEPARATE bass gain `g_bass_i` computed from a
  bass RT60 = `RT60 * bassRatio`. `r_i = g_bass_i / g_i`;
  `bassLp_i = (1-bA)*base + bA*bassLp_i; out = base + (r_i - 1)*bassLp_i`.
  At DC -> `g_bass_i` (bass rings longer/shorter), at HF -> unchanged. `bA` is a
  fixed ~300 Hz one-pole corner; `bassRatio` from a new **Bass** control
  (`2^((bass-0.5)*2)` ~ 0.25x..4x, default 0.5 = neutral). Stable by
  construction because `g_bass_i` is itself an RT60 gain clamped < 0.9995.
This is the design note's ranked-#1 spectral element ("spectral RT60"), and it
is exactly yrn1/Erbe's per-line 3-band EQ done as a compact shelving pair.

**2c. Perceptual Decay -> RT60 curve.** Map Decay 0..1 log-spaced to
RT60 ~0.2..30 s (`0.2 * 150^decay`), so the knob's travel is musical instead of
crammed near max (the Phase-1 linear `g = decay*0.97` put all the action at the
top).

  [2a-2c are the current work; they add one control -> Size/Decay/Bass/Damp/Mix.]

**2d. The rest of voicing (next, after the decay pass auditions):**
- **Delay-length modulation + fractional reads.** Slow, decorrelated per-line
  delay wobble to break the fixed eigenfrequencies (de-metalize the tail) and to
  smooth Size sweeps (currently block-rate integer length recompute -> zipper).
  Needs fractional (linear/allpass-interpolated) reads; currently integer taps.
- **Diffusion control** (allpass g and/or stage count; fixed at 0.5 now).
- **Predelay** line (gap before the tail).
- **True-stereo input** injection (currently mono-summed -> loses input width).
- **Wet tone**: HPF (keep rumble out; Fabula has one) + LPF / global tilt.
- **Matrix A/B**: Householder vs Hadamard/FWHT (density/coloration differ - the
  note wants both prototyped).
- Optional in-loop soft-saturator / Spiral governor ([[feedback_spiral_feedback_governor]])
  for glue + safely higher feedback (currently only a +/-16 blow-up guard).

### Phase 3 - spectral flavor layer: the coupled filterbank vocoder [SPEC'd]

Chosen design (interviewed 2026-07-16). Of the three ranked options, we take a
variant of #3 built on a **6-8 band SVF channel vocoder** rather than an STFT
(am335x-affordable, low-latency, NEON-friendly, reuses Tomograph/SoA-SVF; the
full STFT is deferred to a CM4-class "quality" variant). It is **COUPLED**, not
layered: the processed spectrum refeeds the tank input at low level and the
time-domain FDN re-densifies it -> spectral as a *modulator* of a strong time
engine, never the washy engine Sujet was.

**Signal flow (coupling):**
```
in -> diffuser -> [ FDN tank ] -> wet out
                     ^      |
                     |      v  (mono tap of the tank field)
                     +-- 6-8 band SVF vocoder <-+
                          (refeed at level that rises with the macro)
```

**The one macro ("Spectral", provisional name) - a continuous morph through 5
stations, freeze at the top.** The knob is BOTH a refeed-amount ramp (0 = send
off = the plain reverb) AND a character journey; adjacent stations
CONTINUOUSLY crossfade (no flat zones) in the envelope/gain domain:
```
0.00  clean            (send off, refeed 0)
0.25  contour / tilt   (spectral coloration of the refed energy)
0.50  gate / sparkle   (drop quiet bands -> sparse, crystalline)
0.75  bloom / smear    (asymmetric slow per-band envelopes -> swell)
1.00  freeze           (held magnitudes -> resynth pad, refeed near ceiling)
```

**The engine - a 6-8 band SVF channel vocoder:**
- *Analysis:* 6-8 SVF bandpasses (SoA, NEON later) -> per band a band signal
  `b_k` and an envelope `env_k` (rectify + one-pole).
- *Per-band station transforms* (all operate on env / band gain, so they
  crossfade cleanly):
  - **Contour:** `gain_k = tilt(f_k)` - spectral tilt across the bands.
  - **Gate:** `gain_k = softgate(env_k, threshold)` - drop quiet bands.
  - **Bloom:** `gain_k` driven by an ASYMMETRIC-smeared env (slow attack/
    release) - swelling.
  - **Freeze:** latch `env_k` (hold magnitudes) and RESYNTHESIZE
    `excitation_k * heldEnv_k`. (User chose the resynth freeze over an
    FDN-unity self-freeze: purer/glassier hold, and it keeps the FDN loop gain
    fixed so the coupling cannot run away - freeze is bounded resynth energy,
    not a unity-gain tank.)
- *Resynthesis:* crossfade "live band signal * gain" (natural; low/mid knob)
  -> "excitation * held magnitude" (freeze; top). Excitation = band-limited
  noise and/or band-center oscillators (voicing choice - noise = airy pad,
  sines = glassy tonal pad). The refed signal need not sound natural alone; the
  FDN re-densifies it.
- *Sum bands -> soft-limited refeed* into the tank input.

**Controls (macro + hidden depth):**
- Top level: **Spectral** macro (0..1) - the 6th control (Size/Decay/Bass/Damp/
  Mix + Spectral).
- Expansion / hidden subs:
  - **Refeed ceiling / intensity** - how hard the coupling drives at max (how
    close freeze gets to infinite, overall spectral presence).
  - **Spectral tilt / voice** - bright<->dark of the refed spectrum, whole
    journey.
  - **Spectral focus** - rolls the 6-8 band CENTER FREQUENCIES around
    (compress toward low / spread wide / shift up) -> a movable spectral lens,
    not a fixed grid. The spectral operations then act on wherever the focus
    sits.

**Stability / CPU:** resynth-freeze keeps the tank loop gain as Decay sets it;
the refeed only adds bounded, soft-limited energy -> no runaway. Bypass the
whole vocoder when the macro is 0. 6-8 SVF bands + envelopes + resynth is
am335x-affordable; NEON the SVF bank later (SoA, [[feedback_neon_soa_svf_bank]]).

**Build order:**
- **P3.1** - 6-8 band SVF analysis + coupling plumbing (mono tank tap, soft-
  limited refeed), macro = amount + contour/tilt only. Get the loop stable and
  audible; confirm the coupling densifies rather than rings.
- **P3.2** - gate + bloom envelope transforms + the continuous-morph crossfade.
- **P3.3** - resynth freeze (excitation + held magnitudes) at the top; voice the
  excitation.
- **P3.4** - hidden subs (refeed ceiling, tilt/voice, spectral focus).
- **P3.5** - NEON the SVF bank + resynth; measure CPU.

Deferred to a later/CM4 variant: the full STFT (Sujet reuse) as a heavier
"quality" spectral mode; band-split parallel FDNs (demoted - mostly "EQ'd
decay", which the per-line shelves already approximate, for far more CPU);
shimmer (time-domain in-loop pitch shift) as a separate future intervention.

### Phase 4 - performance (NEON)

The whole reason for a custom atom. Vectorize the matrix (reduce + broadcast-
subtract), the per-line loss-filter bank (SoA one-poles + shelves ->
[[feedback_neon_soa_svf_bank]]), and the output dot-products. Contiguous
fixed-delay reads make this vectorize where Fabula's gathers could not. Then
actually measure CPU vs the native-object FDN ~20% stereo target.

### Phase 5 - UI / UX

Custom overview + a decay/spectral viz; expansion views; readouts in real units
(RT60 in seconds, Size in ms); an xform/randomize "re-roll a room"
(Pecto/Fabula pattern); serialize any added internal state; final habitat name
(Plenum is provisional).

## Gotchas / risks

- FDN tails sound thin/metallic before the delay lengths, matrix, and input
  diffusion are tuned - that is the craft; budget voicing time.
- Loop gain must stay < 1 with margin (per-line filters lower it further); confirm
  decay, not drone, at max size.
- Householder vs Hadamard changes density and coloration - prototype both early.
- Generic functional name; the Erbe paper + yrn1 are design references only
  ([[feedback_no_third_party_branding]]).

## References

- Tom Erbe, "Reverb Topologies and Design" (UCSD).
- yrn1/er-301-custom-units src/mods/fdelay (FDN.lua / SFDN.lua, Baekelandt, BSD).
- Jot / Schroeder-Moorer FDN literature; Householder feedback matrices.
- zaum/atoms/STFTSpectral.h (Sujet) - the spectral overlay source for phase 3.
