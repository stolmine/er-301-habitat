# Sujet Fiction Ops — Research Note

Status: **research complete, ready for implementation passes.**
Context: `planning/sujet-design.md` §5 "Reach" ops, sub-phase 0.2.0.7+.
Prior research: `planning/sujet-blur-research.md`, `planning/sujet-bloom-research.md`.

These three ops are the "сюжет" layer — the spectral impossibilities that distinguish Sujet
from a believable reverb. All operate on the per-bin magnitude arrays after the
magAcc → blurState → bloomState chain and before synthesis. They add "fictional" behaviour
no physical room can produce.

---

## Op 1: Spectral Warp (inharmonic frequency remap)

### What it is and prior art

Spectral warp remaps the frequency axis of the accumulated magnitude spectrum so that
what were harmonic partials become inharmonic — bell-like, metallic, alien. The primary
prior art is Mutable Instruments Clouds `WarpMagnitudes` (read below), CDP STRETCH
SPECTRUM, and Michael Norris SoundMagic Spectral Stretching.

**Clouds `WarpMagnitudes` — the definitive open-source reference:**
(`eurorack/clouds/dsp/pvoc/frame_transformation.cc`, lines 271–298)

Clouds maintains a table of 6 polynomial coefficient sets (`kWarpPolynomials[6][4]`):
```c
const float kWarpPolynomials[6][4] = {
  { 10.5882f, -14.8824f, 5.29412f, 0.0f },
  { -7.3333f, +9.0, -1.79167f, 0.125f },
  { 0.0f, 0.0f, 1.0f, 0.0f },          // identity: f→f
  { 0.0f, 0.5f, 0.5f, 0.0f },
  { -7.3333f, +9.5f, -2.416667f, 0.25f },
  { -7.3333f, +9.5f, -2.416667f, 0.25f },
};
```
The `amount` param (0..1) is scaled to 0..4 and crossfaded between adjacent rows.
For each output bin i, the normalised frequency `f = i / size` (0..1) is evaluated
through the cubic polynomial `wf = (d + f*(c + f*(b + a*f))) * size`, and the
source magnitude is linearly interpolated at position `wf`. Row 2 (amount=0.5) is the
identity. Below 0.5 the mapping compresses towards DC (pitch shift down). Above 0.5
the mapping stretches towards Nyquist (inharmonic / bell-like as partials are pushed
apart non-uniformly).

The key perceptual point: `wf` is a NONLINEAR function of `f` — a polynomial, not a
linear scale. This means different frequency regions stretch at different rates, which
is what creates the inharmonic bell quality (the Risset effect — when harmonic ratios
are uniformly stretched by an exponent > 1, they become non-integer, like metallic
bells and gongs).

**CDP STRETCH SPECTRUM:** warps the frequency axis "beginning at 1 at the frq_divide
(no stretch) and gradually increasing/decreasing" up to maxstretch at the top bin,
controlled by an exponent curve. Creates inharmonicity because "the frequency ratios
between the partials of the original sound are not preserved." Trevor Wishart used it
in *Vox-5* to make a vocal spectrum into an "inharmonic (bell-like) spectrum."
([composersdesktop.com/docs/html/cstretch.htm](https://www.composersdesktop.com/docs/html/cstretch.htm))

**Michael Norris Spectral Stretching:** "analyses the frequency content of a sound and
then stretches those partials further from or closer to a particular centre frequency."
Separate stretch amounts for above and below a pivot frequency.
([michaelnorris.info/soundmagic/effects/SpectralStretching.html](https://www.michaelnorris.info/soundmagic/effects/SpectralStretching.html))

**Inharmonicity / stretched partials:** Academic basis is Risset's bell synthesis
(stretching the harmonic series by an exponent α > 1 so partial n has frequency
f_n = n^α × f_1 rather than n × f_1). At α = 1 the spectrum is harmonic; at α > 1
the upper partials are stretched apart, producing the metallic bell / percussion quality.
([en.wikipedia.org/wiki/Inharmonicity](https://en.wikipedia.org/wiki/Inharmonicity),
[ccrma.stanford.edu/~malcolm/correlograms/text/58 Tones And Tuning With Stretched Partials.html](https://ccrma.stanford.edu/~malcolm/correlograms/text/58%20Tones%20And%20Tuning%20With%20Stretched%20Partials.html))

**Clouds `SIZE` param:** The Modwiggler thread confirms the Clouds SIZE parameter drives
WarpMagnitudes and is described as "like a 1-knob GRM Warp" that over its travel "does
spectral shifting, but also spectral reversal" — the Clouds polynomial table includes
reversal. ([modwiggler.com/forum/viewtopic.php?t=146233](https://www.modwiggler.com/forum/viewtopic.php?t=146233))

### Recommended SMD mechanism

Apply after bloomState, before synth. Use a power-law (exponent) warp: simpler than
Clouds' polynomial table, more mathematically transparent, and produces the Risset
inharmonic character cleanly.

```
// Warp ∈ [0, 1]; bipolar design around 0.5 (identity):
//   Warp < 0.5 → compress (harmonic partials cluster toward DC, voice-like)
//   Warp = 0.5 → identity (warpedBin[i] = i, no remap)
//   Warp > 0.5 → stretch (partials pushed apart, inharmonic, bell-like)
//
// Power-law: warpedBin[i] = (i/kNyq)^alpha * kNyq
// where alpha is derived from Warp:
//   alpha = exp(k_warp_range * (Warp - 0.5))
//   k_warp_range = 2.0f gives alpha range [e^-1, e^1] ≈ [0.37, 2.72]
//   Warp=0 → alpha≈0.37 (strong compression toward LF)
//   Warp=0.5 → alpha=1.0 (identity)
//   Warp=1.0 → alpha≈2.72 (strong stretch, extreme inharmonicity)

// Per hop, before synth pass:
// 1. Copy bloomState → warpScratch[i] (temporary read source)
// 2. For each output bin i = 1..kNyq-1:
//      float norm = (float)i / kNyq;
//      float wf   = powf(norm, alpha) * kNyq;   // warped read position
//      // Linear interpolation from warpScratch:
//      int   wi   = (int)wf;
//      float frac = wf - wi;
//      float m    = warpScratch[wi] * (1-frac) + warpScratch[min(wi+1, kNyq)] * frac;
//      bloomState[i] = m;   // overwrite in place (read from warpScratch, write to bloomState)
// 3. Synth reads (potentially re-warped) bloomState as usual.
```

Or, to follow Clouds more closely, adapt its `kWarpPolynomials` table directly — it is
vendored in the repo and already proven musical.

**State needed:** One scratch array `float mWarpScratch[kStftBins]` — 513 × 4 = 2 KB.
One pre-computed `float mWarpAlpha` per block. `powf` called once per block.
The inner loop is `kNyq-1` iterations of one `powf`-free interpolation: O(N/2) floats.

**Phase handling:** The warp remaps magnitudes but the existing `mPhaseScratch[k]`
(stored from the analysis pass) is indexed by output bin k, not warped bin. After a
large warp, `mPhaseScratch[k]` holds the phase of the ORIGINAL bin at position k, not
the source bin that magnitude was drawn from. This creates a phase mismatch.
Two options: (a) ignore — at high Diffuse (V>0) the phase is randomized anyway, so
the mismatch becomes noise, which is fine and expected for a "fiction" op. (b) for
Warp-without-Diffuse, accept the tonal artifacts (it will sound metallic/aliased, which
is the заумь character). For v1, option (a) is safe and sufficient. A more coherent
approach would compute the phase from the warped source bin index too; defer this.

**Param scaling:**
- Warp = 0.5 → identity, bit-identical to no warp.
- Warp = 0.7 → alpha ≈ 1.49 → subtle inharmonic stretch, bell texture on sustained chords.
- Warp = 1.0 → alpha ≈ 2.72 → extreme stretch, all partials pushed toward Nyquist,
  metallic crash / alien bell.
- Warp = 0.0 → alpha ≈ 0.37 → all partials compressed toward DC, vocoder mud.

**Interaction with other ops:**
- Blur: blurState feeds warpScratch → warping acts on the temporally-smoothed spectrum.
  This is correct and musical (Blur+Warp = inharmonic smear cloud).
- Freeze: at Freeze=1, magAcc is frozen, blurState and bloomState converge to frozen
  value, Warp then remaps the frozen magnitudes. "Bell-frozen" — a drone frozen into an
  inharmonic chord. Spectacular effect.
- Governor: Warp is a magnitude REMAP (redistribution), not energy addition. Total
  magnitude is conserved (interpolation does not add energy). No runaway risk. Safe.
- CPU: O(N/2) = 512 multiply-adds + 512 linear interpolations per hop per channel.
  Cheap. `powf` called once per block.

**Established/exotic:** Clouds' exact polynomial table is production-proven. Power-law
stretch is well-documented in Risset/CCRMA literature. **Well-established.** Lowest
implementation risk of the three.

---

## Op 2: Scramble (stochastic bin permutation / spectral chaos)

### What it is and prior art

Scramble rearranges which bins' magnitudes get synthesized at which frequencies —
a stochastic permutation in the magnitude domain — producing chaos ranging from subtle
timbral roughening to complete spectral disorder.

**Clouds `AddGlitch` — exact source:**
(`eurorack/clouds/dsp/pvoc/frame_transformation.cc`, lines 173–227)
Four algorithms chosen randomly each glitch trigger:

- **Case 0 "Spectral hold-and-blow":** Walks bins LF→HF. Every 1/16 chance (random & 15 == 0),
  captures the current bin magnitude as `held`. Otherwise writes `held` to the output.
  The held value also accumulates: `held = held * 1.01f` (slow exponential blow-up on
  frozen value). Creates linear magnitude "trails" across frequency.

- **Case 1 "Shift up with aliasing":** Picks a random float factor 1..2×. Reads the
  source array at a phase-accumulating index (source += factor, wrapping at size).
  Maps the output spectrum from sub-sampled indices of the source — a stretched, aliased
  read that warps and aliases the spectrum. Quick destructive pitch shift.

- **Case 2 "Kill largest + boost second":** `max_element` → zero; second `max_element` →
  × 8. Removes the dominant partial and blows up the next. Deconstructive.

- **Case 3 "Nasty high-pass":** Every 1/16 chance per bin, attenuates that bin by
  `i/16` — a random, frequency-weighted attenuation pass. Makes the spectrum sparser
  at low frequencies, effectively randomizing a high-pass with tilt.

These are per-frame, one-shot glitch algorithms. Clouds applies them only when the gate
is high and picks a NEW algorithm on the next non-glitch frame.

**SuperCollider PV_BinScramble:**
"Randomizes the order of the bins." Parameters: `wipe` (0..1, proportion of bins
scrambled), `width` (0..1, maximum relative distance of scramble), `trigger`
(re-randomize). Mechanism: constrained random permutation — each bin can be swapped
only within a window of `width × size` bins from its original position. At low width,
only local shuffles; at wide width, global permutation.
([doc.sccode.org/Classes/PV_BinScramble.html](https://doc.sccode.org/Classes/PV_BinScramble.html))

### Recommended SMD mechanism: stochastic band swap

A controllable "Scramble" axis from subtle (occasional band swaps) to chaotic (full
Clouds-style glitch), driven by the existing per-channel PRNG.

The unifying structure is a **stochastic band-swap**: divide the spectrum into bands
of width W bins; on each hop, with probability P, each band is swapped with a randomly
chosen other band. P and W are both controlled by the single Scramble param.

```
// Scramble ∈ [0, 1]
// Band width W: narrows at low Scramble (many small bands = fine-grained),
//   widens at high Scramble (few large bands = coarse rearrangement):
//   W = max(1, (int)(kNyq * Scramble * 0.3))   // max width ≈ 30% of spectrum
// Swap probability P per band per hop:
//   P = Scramble * Scramble   // quadratic: subtle at low values, active above 0.5
//
// Per hop:
// 1. Read bloomState → scratchBuf (copy; preserve original for reads)
// 2. For each band start b = 0, W, 2W, ...:
//      xorshift → u01 (float 0..1)
//      if u01 < P:
//        xorshift → target band t (random in [0, kNyq], aligned to W)
//        swap scratchBuf[b..b+W-1] ↔ scratchBuf[t..t+W-1]
// 3. bloomState[k] = scratchBuf[k]  // overwrite for synth
```

At Scramble = 0: P ≈ 0, no swaps — identity, bit-identical to no Scramble.
At Scramble = 0.3: P ≈ 0.09, W ≈ 9 bins — occasional small-band swaps → roughness.
At Scramble = 0.7: P ≈ 0.49, W ≈ 21 bins — frequent medium swaps → spectral chaos.
At Scramble = 1.0: P = 1.0, W ≈ 30 bins — every band swapped every hop → full disorder.

For the extreme glitch character at Scramble > 0.9, optionally incorporate one of the
Clouds `AddGlitch` algorithms (hold-and-blow is particularly musical for building
static-freeze textures). The simplest approach: at Scramble > 0.85, run the hold-and-
blow logic (case 0) once per hop over the entire spectrum. The probability of triggering
the held state is controlled by `(Scramble - 0.85) / 0.15`.

**State needed:** One scratch array `float mScrambleScratch[kStftBins]` — 2 KB.
No persistent hop-to-hop state (permutation is drawn fresh each hop from the PRNG).
PRNG is already per-channel.

**Phase handling:** Band swaps move magnitudes to wrong-frequency output bins.
`mPhaseScratch[k]` still holds the analysis-frame phase at bin k, not the source bin.
Same mitigation as Warp: at Diffuse > 0, phase randomization covers the mismatch.
At Diffuse = 0 the scramble will produce metallic tonal artifacts — which is exactly
the Clouds-glitch character. This is correct and expected for заумь territory.

**Interaction with other ops:**
- Blur: blurState is the source for scratchBuf. Scramble on a blurred spectrum produces
  shuffled smear-bands — a cloud with no tonal identity. Very alien.
- Freeze: at Freeze=1, bloomState is frozen. Scramble then endlessly re-permutes the
  same frozen spectrum every hop → evolving timbral mutation from a static input.
  The most musically interesting combination.
- Governor: Band swaps conserve total energy (no new energy is added — magnitudes are
  just moved, not scaled). No runaway. SAFE. If hold-and-blow's `held * 1.01f` is used
  at extreme Scramble, the governor (Spiral) catches any build-up.
- CPU: O(N/2) per hop for the copy, plus O(N/(2W)) swap decisions. At W=10, that is
  ~26 bands × 1 random draw each = trivial. The Clouds hold-and-blow loop is also O(N/2).
  Total: well within budget.

**Established/exotic:** Band swap is straightforward; Clouds algorithms are
production-proven; SuperCollider PV_BinScramble is the SC standard approach. Some risk
in the transition from subtle to chaotic feeling discontinuous — param mapping needs
tuning. **Moderately exotic; risk is in perceptual tuning, not stability.**

---

## Op 3: Spray (noise-skirt magnitude injection)

### What it is and prior art

Spray adds a controlled noise "skirt" around each spectral partial by injecting
amplitude-scaled noise into each bin's magnitude. This is the STFT/SMD approximation
of the Loris/SMS bandwidth-enhanced additive synthesis model — in the original Loris
model, each sinusoidal partial has a "bandwidth" parameter (0 = pure sine, 1 = pure
noise) implemented via frequency-modulating the oscillator with band-limited noise.

**Loris bandwidth-enhanced model (Fitz & Haken, CERL, 1995):**
"narrowband (small modulation index) frequency modulation with a filtered noise
modulator...the effect is to reshape the spectrum of the oscillator, by adding
approximately a scaled copy of the noise modulator's spectrum centered at the carrier
frequency." The result is "an approximately bell-shaped spectrum centered at the
carrier frequency" — a noise skirt whose width is proportional to the modulation index.
Amplitude, frequency, AND bandwidth are the three degrees of freedom per partial.
([cerlsoundgroup.org/Loris/ICMC95/BandwidthOscillators.html](https://www.cerlsoundgroup.org/Loris/ICMC95/BandwidthOscillators.html))

**SMS sinusoidal+noise model (Serra & Smith, 1990):**
The residual (noise) is synthesized by "applying the residual-spectrum-envelope (a
time-varying FIR filter) to white noise" with random phases exp[jφ(ωₖ)], φ uniform
in [-π,π]. The key insight: in the STFT domain, ADDING a noise contribution to each
bin's magnitude (with random phase) IS the per-bin noise injection without requiring
explicit sinusoidal peak tracking.
([dsprelated.com/freebooks/sasp/Sines_Noise_Modeling.html](https://www.dsprelated.com/freebooks/sasp/Sines_Noise_Modeling.html))

**Difference from existing Diffuse (V):**
Diffuse randomizes the PHASE of each bin per hop — it does NOT change magnitude. This
broadens each spectral line into narrowband noise (the Vickers SMD diffuseness axis)
but does not add new energy. Spray adds MAGNITUDE noise — it injects additional energy
into bins, creating a noise floor/halo proportional to bin magnitude. They are distinct:
- Diffuse = phase jitter per line → line broadens into narrowband noise, no extra energy.
- Spray = magnitude noise injection → actual noise energy added around each line.
Combined (Spray + Diffuse): adds breathy noise halo (Spray) AND randomizes the spectral
line positions (Diffuse) — the SMS sinusoidal+residual aesthetic.

### Recommended SMD mechanism: per-bin magnitude noise injection

```
// Spray ∈ [0, 1]
// Injection coefficient: epsilon = Spray * kSprayMax
//   kSprayMax tuning: 0.5 gives full noise floor equal to signal; start at 0.3.
//   epsilon = 0 → no injection → identity.
//
// Per hop, after bloomState is updated, before synth:
// Option A: per-bin amplitude-proportional injection (Loris model)
//   for k = 1..kNyq-1:
//     xorshift → n01 (float uniform [0,1) from upper bits of PRNG)
//     // inject noise proportional to bin magnitude:
//     bloomState[k] += epsilon * bloomState[k] * n01
//     // clamp to magAcc range: bloomState[k] = min(bloomState[k], kMagClamp)
//
// Option B: neighborhood spread (SMS residual model)
//   for k = 1..kNyq-1:
//     xorshift → n_signed (float [-1,1))
//     // spread to neighbors (width 1 bin each side):
//     float dE = epsilon * bloomState[k] * fabsf(n_signed)
//     bloomState[k]   -= dE                           // remove from center bin
//     bloomState[k-1] += dE * 0.5f * (n_signed > 0)  // give to left neighbor
//     bloomState[k+1] += dE * 0.5f * (n_signed < 0)  // give to right neighbor
//     // energy-conserving noise spread

// For v1: Option A is simpler and musically sufficient.
// Option B preserves energy (spectral energy spreads without addition).
```

Option A adds magnitude noise proportional to each bin's current level → breathy noise
halo whose density scales with the existing reverb tail. At low Decay (quiet tail), the
noise is quiet; at long Decay (dense tail), the noise is denser. This tracks well.

Option B conserves energy and spreads it to neighbors — closer to the SMS model, but
noisier at the bin level and requires boundary handling. Defer to v1.x.

**Phase handling:** The existing Diffuse (V) path randomizes phase per bin anyway.
At V > 0, the magnitude noise from Spray combines with random phases → genuine noise
texture. At V = 0, the added magnitude lands on the deterministic `mPhaseScratch[k]`
phase → adds spectral noise without phase diffusion, a subtly different texture (colored
noise tracks the spectral envelope). Both are valid. Spray and Diffuse are independent
controls and their combined space is rich.

**Param scaling:**
| Spray | epsilon | Character |
|-------|---------|-----------|
| 0.0   | 0.000   | Off, identity |
| 0.2   | 0.060   | Barely audible noise lift |
| 0.5   | 0.150   | Breathy, audible noise halo |
| 0.8   | 0.240   | Strong noise injection, noisy-spectral texture |
| 1.0   | 0.300   | Maximum: nearly equal noise and signal |

kSprayMax = 0.3 gives a 0 dB noise-to-signal ratio at Spray=1. Reduce kSprayMax if
that is too aggressive (0.2 for a subtler ceiling).

**State needed:** None (uses existing PRNG; no persistent per-bin state). Zero bytes.

**Energy/runaway concern:** Option A ADDS energy to bloomState. If Decay is long and
Spray is high, the noise injection feeds INTO the SMD accumulator's reverb tail on the
next hop (because bloomState modifies the output of this hop, and the next hop's magAcc
is driven by the new input — the injection does NOT feed back into magAcc directly).
The Spiral governor on the wet output caps any runaway. However, repeated Spray+Freeze
could accumulate noise unboundedly in bloomState. Mitigation: cap bloomState to
kMagClamp after injection (same cap already applied to magAcc). OR apply Spray AFTER
the governor (i.e. in the synth domain, add noise to the final magnitude used for
cosf/sinf, not to bloomState). This latter approach is the safest: Spray is then
purely a synthesis-stage coloration, not an accumulator input.

**Safest implementation:** Apply Spray INSIDE the synth pass, not to bloomState:
```
// In synth pass for complex bin k:
float sprayNoise = epsilon * bloomState[k] * n01;   // positive, proportional
float m = bloomState[k] + sprayNoise;               // total magnitude for this bin
ifft_in[k]        = m * cosf(phi);
ifft_in[kNyq + k] = m * sinf(phi);
// bloomState[k] is NEVER modified → no feedback into accumulator → unconditionally safe.
```
This eliminates all runaway risk: Spray is a synthesis modifier only, not a magnitude
accumulator modifier.

**Interaction with other ops:**
- Diffuse (V): orthogonal. Spray adds magnitude noise; V randomizes phase. Together:
  fully stochastic synthesis — noise magnitude at random phase = proper broadband noise.
  The SMS S+N model rendered in real time.
- Blur: blurState feeds bloomState; Spray sits after. Blurred+Sprayed = smeared signal
  with noise halo. Beautiful for textures.
- Bloom: bloomState is the source; Spray is applied to it in synth. Bloom+Spray = slow-
  onset swell with breathy noise.
- Freeze: at Freeze=1, bloomState is frozen signal. Spray adds live noise AROUND the
  frozen spectrum each hop. The frozen partials breathe. Extremely musical.
- CPU: 512 PRNG draws + 512 multiplies per channel per hop. No `sinf`/`cosf` beyond what
  the synth pass already does. Very cheap.

**Established/exotic:** Directly grounded in Loris/SMS model (well-established academic
foundation). The specific SMD-domain per-bin injection is novel but trivially derived
from first principles. **Well-established in model; novel in SMD context. Low risk.**

---

## Implementation Order

### Recommended build order: Spray → Warp → Scramble

**1. Spray first (lowest risk, highest payoff, zero persistent state)**
- Zero new state bytes. Uses existing PRNG. Trivial insertion into synth pass.
- Perceptually complementary to existing Diffuse (V) — a new orthogonal texture axis.
- Freeze+Spray is immediately musical. Works with all existing ops.
- No phase coordination needed. No interpolation tables. No complex param mapping.
- Risk: tune `kSprayMax`. That is the entire risk surface.
- Implementation cost: ~15 lines of code in the synth pass of `smdProcess`.

**2. Warp second (medium complexity, distinctly fictional effect, proven algorithm)**
- Requires one scratch buffer (2 KB) and 512 linear interpolations per hop.
- Can directly adapt Clouds' `kWarpPolynomials` table (already vendored in the repo).
  Or start with simpler power-law exponent (single `powf` per block, no table needed).
- Phase mismatch at high Warp is absorbed by Diffuse — no new phase logic needed for v1.
- Freeze+Warp = inharmonic bell drone. The single most "impossible room" sound in Sujet.
- Risk: interpolation boundary handling (k=0 and k=kNyq), and whether the polynomial
  table or power-law sounds more musical. Low technical risk; medium tuning effort.
- Implementation cost: ~30 lines including scratch buffer management.

**3. Scramble last (most complex param mapping, stability considerations)**
- Requires one scratch buffer (2 KB) and a band-swap loop.
- Param mapping from Scramble → (W, P) needs ear-tuning to avoid feeling like a
  random noise generator at moderate values.
- The Clouds hold-and-blow algorithm (case 0) is the recommended extreme-value behavior.
- Freeze+Scramble is the most chaotic use — endlessly mutating frozen spectrum.
- Risk: medium. The band-swap is safe (energy conserved); the hold-and-blow needs
  the Spiral governor active. Main risk is the subtle→chaotic transition feeling
  abrupt or unmusical without careful param curve shaping.
- Implementation cost: ~50 lines (band-swap loop + Clouds case 0 fallback).

---

## Sources

- Clouds `frame_transformation.cc` (WarpMagnitudes, AddGlitch):
  `eurorack/clouds/dsp/pvoc/frame_transformation.cc` (in-repo)
- Clouds Spectral Madness demo / SIZE param described:
  https://www.modwiggler.com/forum/viewtopic.php?t=146233
- Clouds spectral mode parameter descriptions:
  https://synthmodes.com/modules/clouds_stock/
- CDP STRETCH SPECTRUM (inharmonic frequency axis warp):
  https://www.composersdesktop.com/docs/html/cstretch.htm
- Michael Norris Spectral Stretching (centre-pivot spectral stretch):
  https://www.michaelnorris.info/soundmagic/effects/SpectralStretching.html
- Inharmonicity / stretched partials / Risset bell:
  https://en.wikipedia.org/wiki/Inharmonicity
- CCRMA stretched partials (Terhardt 1974, Slaymaker 1970):
  https://ccrma.stanford.edu/~malcolm/correlograms/text/58%20Tones%20And%20Tuning%20With%20Stretched%20Partials.html
- Loris bandwidth-enhanced oscillator (Fitz & Haken ICMC 1995):
  https://www.cerlsoundgroup.org/Loris/ICMC95/BandwidthOscillators.html
- Loris / reassigned bandwidth-enhanced additive model:
  https://www.cerlsoundgroup.org/Loris/
- SMS sinusoidal+noise model (Serra & Smith), STFT domain:
  https://www.dsprelated.com/freebooks/sasp/Sines_Noise_Modeling.html
- SuperCollider PV_BinScramble (constrained random bin permutation):
  https://doc.sccode.org/Classes/PV_BinScramble.html
- JOS Spectral Audio Signal Processing (STFT applications):
  https://www.dsprelated.com/freebooks/sasp/Applications_STFT.html
- Risset bell synthesis (additive inharmonic partials):
  http://msp.ucsd.edu/techniques/v0.11/book-html/node71.html
