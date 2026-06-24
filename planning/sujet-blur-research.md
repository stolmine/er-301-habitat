# Sujet Blur — Research & Redesign Note

Status: **research complete, ready for implementation pass**.
Cross-reference: `planning/sujet-design.md` §5 (Blur op) and §10 (sub-phase 0.2.0.5).

---

## Key finding: the two axes

**Cross-FREQUENCY blur** (what the current 3-tap Sujet `blurMag` does): convolves the
magnitude spectrum _within a single frame_ across adjacent bins. A 3-tap kernel on 513
bins spreads energy ±1 bin = ±46.9 Hz at N=1024/48 kHz. This is inaudible on most
material because spectral energy is already smeared across several bins by the analysis
window (a Hann window has a main lobe of ~4 bins). You need a kernel width of roughly
one critical-band (≈ 12–24 bins at 1 kHz, widening at high frequencies) before
cross-bin blur becomes perceptible. The CDP `BLUR AVRG` opcode performs exactly this —
averaging across N adjacent frequency channels — and notes that N must be "odd and
≤ half the analysis channel count." Even with a large N, cross-bin blur only broadens
spectral lines; it does not melt transients or create a cloud because it operates on one
frame at a time.

**Cross-TIME blur** (what the current Sujet `blurMag` does NOT do): averages each bin's
magnitude over a window of past frames. This is the axis that all canonical spectral
blur tools use for the "melting into a cloud" effect:

- Csound `pvsblur`: "Average the amp/freq time functions of each analysis channel for a
  specified time (truncated to number of frames)" — [csound.com/manual/opcodes/pvsblur](https://csound.com/manual/opcodes/pvsblur/).
- Csound `pvsmooth`: first-order IIR lowpass per bin, time-varying coefficient — equivalent
  to an infinite causal temporal average — [csounds.com/manual/html/pvsmooth.html](http://www.csounds.com/manual/html/pvsmooth.html).
- CDP `BLUR BLUR`: "time-averages spectral data by linearly interpolating between start
  and end windows of a blurring span" — [composersdesktop.com/docs/html/cblur.htm](https://www.composersdesktop.com/docs/html/cblur.htm).
- Trevor Wishart (quoted in the Max/MSP forum): "average the spectral data in each
  frequency-band channel over N time-windows (spectral blurring) thus reducing the
  amount of detail" — the definition he uses in *Audible Design*.

Cross-time blur is perceptually dominant because it affects attack shape, sustain
texture, and decay character simultaneously. A transient spreads into a held smear. A
sustain turns into a running average that sounds increasingly like a diffuse cloud. At
long blur times (many hops) it converges toward Freeze.

**Conclusion:** the Blur control should be primarily cross-TIME. The current cross-bin 3-tap
kernel is a secondary, optional ingredient for additional spectral-line widening, but
it alone will never sound vivid.

---

## Prior-art algorithm inventory

### Csound pvsblur — box filter over N frames (canonical)

Per-bin, per-hop: maintain a ring buffer of the last `ceil(blurtime / hoptime)` frames
of magnitude (and optionally frequency). Output for hop `u`, bin `k` is the unweighted
mean of the ring:

```
blurMag[u][k] = (1/N_frames) * sum_{i=0}^{N_frames-1} magAcc[u-i][k]
```

Memory cost: `N_frames × 513 × sizeof(float)` per channel. At blurtime = 0.5 s,
hop = 256/48000 ≈ 5.33 ms → N_frames ≈ 94. That is 94 × 513 × 4 ≈ 193 KB per channel
— acceptable on CM4 (1 MB L2).

Side-effect noted in the docs: the stream is delayed by `blurtime` (the ring buffer
introduces lag). In Sujet, the existing 26.7 ms STFT latency absorbs small blur times;
present total latency in the Predelay readout.

Source: [csound.com/manual/opcodes/pvsblur](https://csound.com/manual/opcodes/pvsblur/)

### Csound pvsmooth — per-bin IIR (equivalent, cheaper, no ring buffer)

One-pole IIR per bin, applied frame-by-frame:

```
smoothMag[u][k] = (1 - alpha) * magAcc[u][k]  +  alpha * smoothMag[u-1][k]
```

`alpha ∈ [0, 1)` is the smoothing coefficient. `alpha = 0` → no smoothing (output = input).
`alpha → 1` → very long time constant → output converges to an exponential moving
average of all past magnitudes. The pvsmooth param `kacf` ("amplitude cutoff frequency")
is in units of fraction of half-frame-rate; the relationship to alpha is the standard
one-pole formula: `alpha = exp(-2π × kacf × frame_rate)`.

Perceptual equivalence with pvsblur: for an exponential window vs a box window, the
subjective blur character is similar at the same effective time constant, but the IIR
tail decays softly rather than cutting off. The pvsmooth docs note this gives "no
increase in computational cost when higher amounts of blurring are desired" — the IIR
state is only 513 floats per channel regardless of blur depth.

State cost: 513 × sizeof(float) × 2 ch = 4 KB. Effectively free.

Source: [csound.com/manual/opcodes/pvsmooth](https://csound.com/manual/opcodes/pvsblur/)
(separate page; linked from Csound 7 manual index)

### CDP BLUR BLUR — linear interpolation over time span

Interpolates linearly between the magnitude frame at `t_start` and the magnitude frame
at `t_end = t_start + blurspan` for each intervening frame. This softens transitions
without averaging — it "blurs detail in the time dimension" by replacing each frame with
a blend of the boundary frames. Less of a "cloud" effect, more of a "wash" between
events. Not the algorithm we want for Sujet's cloud effect.

Source: [composersdesktop.com/docs/html/cblur.htm](https://www.composersdesktop.com/docs/html/cblur.htm)

### CDP BLUR AVRG — cross-frequency channel average

Averages amplitude across N adjacent frequency channels per frame (cross-bin, not
cross-time). "The energy (amplitude) in adjacent channels is averaged out over
Average-span adjacent channels, without affecting the frequencies." Described as useful
to "enrich the timbre and make its texture rougher." This is the closest analogue to
the current Sujet 3-tap kernel — confirmed weak in isolation.

Source: [composersdesktop.com/docs/html/cblur.htm](https://www.composersdesktop.com/docs/html/cblur.htm)

### GRM Tools / IRCAM SuperVP — time-averaging with smooth parameter

GRM Tools Evolution includes a blur/freeze class effect noted in the user community as
producing "a succession of freezes" rather than a true blur at extreme settings —
consistent with a box-filter approach. IRCAM AudioSculpt's SuperVP engine uses a
frequency-smoothing filter of selectable order (default 3) applied within frames, which
is cross-bin. The ASAP plugin suite (IRCAM's current product) is based on SuperVP.

No detailed open algorithm description found. The community characterisation of GRM's
blur as "successive freezes" suggests a long-window box average with a delay side-effect.

Source: [kvraudio.com spectral blur thread](https://www.kvraudio.com/forum/viewtopic.php?t=476926),
[inagrm.com GRM Tools ST User Guide](https://inagrm.com/sites/default/files/download/doc/GRM_Tools_ST_Eng.pdf) (PDF, image-only, not parseable)

---

## Why the 3-tap cross-bin kernel is weak

1. **Kernel width vs auditory filter width.** The Hann analysis window already spreads
   each sinusoid across ~4 bins (the window's main lobe). A 3-tap kernel (±1 bin) adds
   almost nothing beyond what the window already does. A cross-bin kernel only becomes
   perceptually meaningful at widths approaching one critical band. At N=1024/48 kHz, a
   critical band near 1 kHz spans ~100 Hz / 46.9 Hz per bin ≈ 2 bins; near 4 kHz it
   spans ~500 Hz / 46.9 Hz ≈ 10 bins; near 8 kHz, ~900 Hz / 46.9 Hz ≈ 19 bins. So a
   useful cross-bin kernel needs σ ≈ 5–15 bins (Gaussian), frequency-dependent, to reach
   perceptual width. Source: [wikipedia.org/wiki/Critical_band](https://en.wikipedia.org/wiki/Critical_band),
   [dsprelated.com ERB](https://www.dsprelated.com/freebooks/sasp/Equivalent_Rectangular_Bandwidth.html).

2. **It does not affect time structure.** A cross-bin kernel applied within a frame
   cannot smear transients across time. Each frame is processed independently. The
   "melting transients into a cloud" effect requires accumulation across frames.

3. **Phase is unchanged.** Without phase modification accompanying the magnitude
   broadening, the cross-bin smear produces tonal noise (resynthesizing the same phases
   with blended magnitudes sounds like slight comb-filtering). The existing Diffuse (V)
   control handles phase randomization independently, but cross-bin blur without matched
   phase intervention is auditorily minimal.

---

## What makes blur vivid: principles

1. **Cross-time averaging over many hops.** At hop = 256 smp / 48 kHz ≈ 5.33 ms, you
   need at least 10–20 frames (50–100 ms) to audibly "freeze" attacks into sustain. A
   box over 50 frames (≈ 267 ms) gives dramatic cloud effects.

2. **IIR exponential average is the cheap path.** A one-pole IIR per bin with `alpha`
   close to 1 achieves effectively the same perceptual result as a long box filter.
   Control: `alpha = exp(-1 / tau_frames)` where `tau_frames = blur_time / hop_time` is
   the target time constant in hops. At `alpha = 0.95`, tau_frames ≈ 19 hops ≈ 102 ms.
   At `alpha = 0.995`, tau_frames ≈ 199 hops ≈ 1.06 s.

3. **Phase interaction.** The existing Diffuse (V) control already handles phase
   randomization. Magnitude blur on its own (IIR smoothing of `magAcc`) changes the
   _envelope_ of each bin's decay, making onsets "seep" forward into later frames. This
   is perceptually independent of and additive with V: blur widens the temporal envelope;
   V broadens the spectral line into noise. Using both together gives the fullest cloud.

4. **Frequency-dependent time constant (optional, advanced).** Matching the blur time
   constant to auditory filter width as a function of frequency (shorter tau at low
   frequencies where critical bands are narrow; longer tau at high frequencies) gives a
   perceptually balanced smear. This is a secondary refinement, not needed for v1.

5. **Combining both axes.** Using a wide cross-bin kernel (Gaussian, σ ≈ 10–20 bins) in
   addition to cross-time IIR adds spectral halo/bloom around partials — a "wings"
   effect around peaks. This is the CDP BLUR AVRG character layered on top of the
   temporal smear. But cross-bin alone is insufficient.

---

## Recommended Blur redesign for Sujet

### Mechanism: per-bin IIR temporal smoother on `magAcc`

Replace the current 3-tap cross-bin kernel with a per-bin one-pole IIR smoother applied
to `magAcc[k]` each hop. This is the pvsmooth model — the cheapest, most effective path.

### State cost

One additional array `blurState[k]` per channel: 513 × 4 bytes × 2 ch = ~4 KB.
Total additional state per unit instance: ~4 KB. Negligible.

### Update equations (per hop, per bin k, after accumulation)

```
// Blur parameter: blur ∈ [0, 1]
// Map to alpha (smoothing coefficient):
//   blur = 0   → alpha = 0  (no smoothing, output = magAcc)
//   blur = 0.5 → alpha ≈ 0.95 (tau ≈ 19 hops ≈ 100 ms)
//   blur = 1.0 → alpha ≈ 0.999 (tau ≈ 1000 hops ≈ 5.3 s, near-freeze)
//
// Recommended mapping (exponential):
//   alpha = blur^2 * 0.999      // quadratic gives better feel in lower range
// OR:
//   alpha = 1.0 - exp(-6.0 * blur)   // maps 0→0, 0.5→0.95, 1→~0.9975
// The second form feels more even. Recompute per-block (it's just one float).

// Per-hop, per-bin:
blurState[k] = alpha * blurState[k] + (1.0f - alpha) * magAcc[k];
// Use blurState[k] in place of magAcc[k] for phase recombination:
Y(u,k) = blurState[k] * exp(j * phi_out(u,k));
```

`magAcc[k]` itself is NOT modified — the IIR runs in parallel, reading `magAcc` and
writing to `blurState`. This keeps Freeze working correctly: when `g(k) → 1`, `magAcc[k]`
holds the frozen magnitude; `blurState[k]` smoothly converges toward it.

### Param mapping for 0..1 Blur control

| Blur value | alpha  | tau (hops) | tau (ms at hop=5.33ms) | Character |
|------------|--------|------------|------------------------|-----------|
| 0.0        | 0.000  | 1          | ~5 ms                  | No blur (direct) |
| 0.2        | 0.330  | 1.5        | ~8 ms                  | Very subtle |
| 0.4        | 0.632  | 2.7        | ~14 ms                 | Light smear |
| 0.6        | 0.777  | 4.5        | ~24 ms                 | Noticeable |
| 0.7        | 0.900  | 10         | ~53 ms                 | Attack softening |
| 0.8        | 0.950  | 20         | ~107 ms                | Cloud building |
| 0.9        | 0.980  | 50         | ~267 ms                | Strong cloud |
| 0.95       | 0.992  | 125        | ~666 ms                | Near-freeze blur |
| 1.0        | 0.9975 | 400        | ~2.1 s                 | Approaches Freeze |

Recommended mapping expression:

```cpp
// blur ∈ [0, 1], recomputed per-block
float alpha = 1.0f - expf(-6.0f * blur);
// This gives: blur=0→alpha=0, blur=0.5→0.9502, blur=0.8→0.9933, blur=1→0.9975
```

### Interaction with existing controls

- **Diffuse (V):** Independent. Blur smooths the magnitude envelope over time; V
  randomizes the phase per-hop. Used together they compound: blur widens the attack
  into a cloud, V diffuses that cloud into noise. The combination of high Blur + moderate
  V is the "lush spectral reverb" target.
- **Freeze:** Freeze blends `g(k) → 1`, which causes `magAcc[k]` to converge to a fixed
  value. With Blur also active, `blurState[k]` converges slowly toward that frozen value
  rather than snapping. This is musically useful (slow "creep into freeze"). No conflict.
- **Decay:** Blur operates on `magAcc[k]` _after_ the decay step. If Decay is short,
  bins decay fast and `magAcc[k]` collapses quickly — Blur then smears the exponential
  tail. If Decay is long, both Blur and the SMD accumulation contribute sustained energy.
  No conflict; they are multiplicative in the magnitude domain.

### Optional secondary enhancement: wide Gaussian cross-bin pass

After the IIR time-smoother, optionally apply a Gaussian cross-bin convolution to
`blurState[k]` for the spectral-halo / "wings" effect:

```
// σ in bins; a σ=8 kernel at N=1024 covers ~375 Hz at 1 kHz — near one critical band
// Implemented as 2–3 passes of a 3-tap box filter (σ_box ≈ width/sqrt(3)):
// 3 passes of 3-tap → σ ≈ sqrt(3) ≈ 1.7 bins (minimal, still weak)
// To get σ=8 efficiently: use a 5-pass symmetric IIR approach or explicit Gaussian table
// DEFER to v1.x — the time-IIR alone is the primary vivid effect
```

For v1, defer the cross-bin Gaussian. It adds CPU and implementation complexity for a
secondary effect. The time-IIR alone is the redesign.

---

## Prioritized recommendation

**Phase 0.2.0.5 implementation order:**

1. **First: replace the 3-tap cross-bin kernel with the per-bin IIR time smoother.** Add
   `blurState[k]` array (513 floats per channel). Each hop: compute `alpha` from Blur
   param via `alpha = 1 - exp(-6 * blur)`, update `blurState[k] = alpha * blurState[k] +
   (1 - alpha) * magAcc[k]`, use `blurState[k]` for resynthesis. Reset `blurState[k]`
   to 0 on unit reset/init. This is ~10 lines of new code, ~4 KB new state.

2. **Then: tune the alpha mapping by ear.** The `exp(-6 * blur)` formula is a starting
   point. The exponent (6.0) controls how much of the param range is "subtle" vs "vivid."
   A larger exponent (e.g. 9.0) compresses the vivid range to the top of the knob; a
   smaller one (e.g. 4.0) brings drama on at lower Blur values. Calibrate against the
   reference target: at Blur = 0.7, a drum hit should noticeably smear into a 50–100 ms
   sustain; at Blur = 0.9, a plucked note should melt into a diffuse cloud.

3. **Optional (v1.x): add a wide cross-bin Gaussian pass** if the spectral "wings" halo
   effect is desired. Recommended σ = 8–16 bins (frequency-dependent via Bark scale would
   be ideal but is complex). Can be driven by a separate "Spread" param or folded into
   Blur at high values.

---

## Sources

- Csound pvsblur documentation: https://csound.com/manual/opcodes/pvsblur/
- Csound pvsmooth (old manual): http://www.csounds.com/manual/html/pvsmooth.html
- CDP BLUR function reference: https://www.composersdesktop.com/docs/html/cblur.htm
- CDP spectral overview: https://www.composersdesktop.com/docs/guide/cdpspect.html
- Wishart quote (Max forum): https://cycling74.com/forums/spectral-blurring-in-msp-wpvoc
- KVR spectral blur thread: https://www.kvraudio.com/forum/viewtopic.php?t=476926
- Vickers SMD paper: https://www.sfxmachine.com/docs/FDReverbSpectralMagDecay.pdf
- Critical band: https://en.wikipedia.org/wiki/Critical_band
- ERB scale (JOS): https://www.dsprelated.com/freebooks/sasp/Equivalent_Rectangular_Bandwidth.html
- Exponential averager cutoff (Rick Lyons): https://www.dsprelated.com/showarticle/182.php
- Phase vocoder done right: https://arxiv.org/pdf/2202.07382
