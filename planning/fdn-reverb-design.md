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

## Phasing

1. **Core POC**: N=8 Householder FDN, fixed prime delays, per-line low+high-shelf
   damping, 3-4 input allpasses. Get it dense, smooth, and NEON-clean. A/B the
   tail vs Fabula/yrn1; confirm CPU well under 20% stereo.
2. **Voicing**: delay tunings (Erbe ratios as a start), matrix choice
   (Householder vs Hadamard), decay/damping curves, optional block-rate delay mod.
3. **Spectral flavor**: pick option 2 (band-split) or option 3 (Sujet STFT send)
   once the core sings. Option 3 recycles Sujet.

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
