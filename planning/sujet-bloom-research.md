# Sujet Bloom — Research & Redesign Note

Status: **research complete, ready for implementation pass**.
Cross-reference: `planning/sujet-design.md` §5 (Bloom op) and §10 (sub-phase 0.2.0.5 / post-Blur).
Blur research: `planning/sujet-blur-research.md` (cross-time IIR already implemented in 0.2.0.9).

---

## Key finding: what "bloom" means in practice

The word "bloom" in reverb contexts has a specific, consistent meaning across all the prior art surveyed:
**a reverb tail that builds (rises) slowly before decaying** — the opposite of a sharp exponential onset.
The envelope looks like an attack swell: silence (or near-silence) right after the input event, building to peak density, then decaying. Perceptually: the sound "opens up" or "blooms" into the reverb, rather than immediately filling with reverb energy.

The term was coined by Keith Barr for the Alesis Midiverb II. He used allpass coefficients of 0.5 (chosen because they were cheap to compute with bit shifts), which caused very long attack times. Players complained. Barr re-framed the artifact as a feature and called it "Bloom" — a reverb with a "very slow build time, and an even slower decay." My Bloody Valentine (Kevin Shields) used it heavily. The envelope that blooms to a rich, highly diffuse reverb with a smooth decay. ([valhalladsp.com/2010/12/02/valhallashimmer-tips-and-tricks-bloom/](https://valhalladsp.com/2010/12/02/valhallashimmer-tips-and-tricks-bloom/))

Val Costello's description of the Valhalla Shimmer diffusion/bloom mechanism (from blog posts and the Shimmer Notes PDF): at diffusion 0.618 (Φ = 1/golden ratio), the attack and decay of the reverb tail are equal in length, and the envelope is a Gaussian (bell curve). This is the product of cascading many allpass delays in series: as allpass count increases, the reverb attack extends from near-instantaneous (exponential onset) toward a symmetrical bell. Above 0.618, the tail still "fades in" but decay > attack. At diffusion 0.5, the attack is very long relative to decay. The Midiverb's allpass coefficient of 0.5 produces the extreme case — dominant attack with slow onset. ([valhalladsp.com/2010/11/30/valhallashimmer-tips-and-tricks-diffusion/](https://valhalladsp.com/2010/11/30/valhallashimmer-tips-and-tricks-diffusion/), [valhalladsp.com/2010/12/01/valhallashimmer-tips-and-tricks-adjusting-the-reverb-envelope/](https://valhalladsp.com/2010/12/01/valhallashimmer-tips-and-tricks-adjusting-the-reverb-envelope/))

The Valhalla SuperMassive algorithms "Leo" and "Sagittarius" are named slow-attack reverb modes — very slow attack, super long decay, very high echo density — directly descended from the Bloom concept. ([valhalladsp.com/shop/reverb/valhalla-supermassive/](https://valhalladsp.com/shop/reverb/valhalla-supermassive/))

Eventide Blackhole "Gravity": the unique Gravity control reverses the arrow of time. Negative Gravity creates reverse-envelope type behavior (build-then-fade = backwards reverb). The envelope blooms forth. Described as producing "effects that bloom forth like spectral voices." ([soundonsound.com/reviews/eventide-blackhole](https://www.soundonsound.com/reviews/eventide-blackhole))

**Summary of the prior-art bloom envelope:** energy starts near zero, grows (swells) over a time window (the "attack"), peaks, then decays. The attack can be milliseconds (normal reverb) to multiple seconds (extreme bloom). The allpass-cascade mechanism produces it in the time domain; in the SMD frequency domain we need to replicate the envelope shape directly per bin.

---

## The four candidate mechanisms for SMD Bloom

### (a) Per-bin SLOW ATTACK asymmetric IIR envelope

The direct translation of Bloom into the SMD per-bin magnitude framework. Replace the symmetric IIR in Blur with an **asymmetric one-pole** that has independent attack and release (rise and fall) coefficients per bin:

```
if magAcc[k] > bloomState[k]:
    bloomState[k] = alpha_rise * bloomState[k] + (1 - alpha_rise) * magAcc[k]
else:
    bloomState[k] = alpha_fall * bloomState[k] + (1 - alpha_fall) * magAcc[k]
```

Set `alpha_fall` small (fast release, so the state tracks the decay of `magAcc` quickly) and `alpha_rise` large (slow attack, so rising energy builds slowly). Result: when a new note arrives, `magAcc[k]` jumps up but `bloomState[k]` rises slowly toward it — the onset blooms in. When the note ends, `magAcc[k]` decays (via SMD) and `bloomState[k]` follows it quickly.

This is the fundamental asymmetric-envelope technique: attack time much longer than release time. It is directly analogous to how the allpass-cascade produces the bloom envelope in Barr's Midiverb — only here we implement it explicitly in the magnitude domain rather than implicitly via allpass delay. The concept of separate attack/release in an IIR envelope follower is well-established in VCA/compressor design and has been applied to spectral magnitude tracking in noise reduction and dereverberation systems. ([image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11238882](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11238882) — asymmetric IIR for spectral separation: "when power increases (onset), α may be set to about 0.03, and when power decreases (offset), α may be set to about 0.25.")

**Perceptual result:** transient onset blooms in slowly (the magnitude swells) while the SMD decay tracks normally once the bloom state catches up. At high Bloom values the swell can take hundreds of milliseconds — a slow, evolving crescendo into the reverb tail.

**Distinctness from Blur:** Blur (the IIR already implemented) is a SYMMETRIC temporal smoother: `blurState = alpha * blurState + (1-alpha) * magAcc`. It smooths equally in rise and fall — a slow onset AND a slow trailing-off. It melts transients into an extended cloud in BOTH directions. Bloom is ASYMMETRIC: slow rise, fast fall. It specifically creates a swell/crescendo character on onsets while letting the decay track the SMD accumulator normally. They are mechanically distinct and perceptually complementary.

### (b) Frequency-staggered onset (per-bin onset delay as function of bin index)

Give each bin k a time offset τ(k) before it starts contributing to output. Higher bins respond later (or earlier) than lower bins, creating a frequency-dependent "opening" of the spectrum. Implemented as a per-bin counter or delayed magnitude gate.

Spectral delay literature confirms that applying independent delays to frequency bins produces distinctive spreading effects: "each spectral component has a dedicated recirculating delay line" ([econtact.ca/11_4/gibson_spectraldelay.html](https://econtact.ca/11_4/gibson_spectraldelay.html)). Commercial spectral delays (Ableton Spectral Time, B.S. Spectral Delay, Polyend Spectral Delay) apply per-bin delay times.

A linearly increasing delay by bin index τ(k) = k × Δτ produces a "sweeping" effect — the spectrum opens from LF to HF (or HF to LF) over time. Non-linear mappings (e.g. τ(k) proportional to log(k)) give a perceptually uniform spread across octaves.

**Implementation cost:** a ring buffer of past `magAcc` snapshots per bin, indexed by τ(k). Memory: if max stagger = S hops, need S × 513 floats per channel = same ring-buffer cost as pvsblur. At S=32 hops (170 ms), ~64 KB per channel.

**Perceptual result:** not a "bloom" swell but a spectral arpeggio or sweep. LF energy appears first; HF blooms in later (or vice versa). Musical but specifically chromatic / sweeping — closer to a filter sweep than a bloom crescendo.

**Verdict:** This produces a visually distinct and interesting effect (spectral arpeggiation / "opening") but is NOT the same as the Keith Barr bloom character (slow swell of the whole reverb energy). More appropriate as a separate mode or the "Spread" axis. It also has higher memory cost.

### (c) Reverse/inverse envelope — build-then-fade

Actual time reversal in the spectral domain: accumulate magnitudes over a window of frames, then play them back in reverse order. This is "reverse reverb" in its purest form — the energy rises (as if a reversed recording decays forward) then cuts off.

In the SMD context this would require buffering a window of N_rev frames of `magAcc`, reversing the sequence, and using the reversed values for synthesis. Latency = N_rev × hop_time. Memory = N_rev × 513 × 4 bytes per channel.

This is what Eventide Blackhole's negative Gravity does, and what is achieved in studio by recording, reversing the track, applying reverb, reversing again. ([soundonsound.com/reviews/eventide-blackhole](https://www.soundonsound.com/reviews/eventide-blackhole))

**Verdict:** Perceptually dramatic and recognizable as "reverse reverb" but computationally heavier (requires buffering of frames, look-ahead latency), and the effect is fundamentally time-lookahead (requires future frames to produce past output). Hard to implement in a causal real-time system without large added latency. Not the right mechanism for a real-time SMD unit — defer or mark exotic.

### (d) Gaussian-attack=decay symmetry (the Valhalla diffusion 0.618 model)

Use a symmetric bell-shaped envelope function applied to the magnitude accumulator: synthesize from a running average weighted by a Gaussian kernel centered at the current frame and spanning N_gauss past and future frames. The output peaks at the center, fades in symmetrically, and fades out symmetrically.

In real-time this requires a delay of N_gauss/2 hops (look-ahead). At N_gauss = 20 hops (107 ms center), the latency is 10 hops ≈ 53 ms additional beyond the STFT latency. Manageable.

**Verdict:** Creates the precise Gaussian reverb envelope Costello describes. But requires look-ahead buffering, adds latency, and produces a symmetric build/decay — neither the lopsided swell of Bloom nor a simple real-time mechanism. Interesting but complex. Mark as advanced/deferred.

---

## Recommendation: per-bin asymmetric IIR with slow attack / fast release

**Mechanism:** apply a per-bin one-pole IIR to `magAcc[k]` with separate `alpha_rise` and `alpha_fall`, where `alpha_rise >> alpha_fall` (slow rise, fast fall). Use `bloomState[k]` for synthesis instead of `blurState[k]` (or chain them: Blur→Bloom→synth if both active, see interaction below).

### State cost

One array `bloomState[k]` per channel: 513 × 4 bytes × 2 ch = ~4 KB. Same as Blur state.

### Update equations (per hop, per bin k, after the SMD accumulation step)

```cpp
// Bloom param: bloom ∈ [0, 1]
// Map to alpha_rise (slow attack coefficient) — identical formula to Blur but semantically
// the "slow" direction is rise (magAcc going up), not fall.
//
// alpha_rise controls how slowly the state catches up when magAcc[k] increases:
//   bloom=0 → alpha_rise=0 → no bloom (bloomState tracks magAcc instantly)
//   bloom=0.5 → alpha_rise≈0.950 (tau_rise≈19 hops≈100 ms attack)
//   bloom=0.8 → alpha_rise≈0.993 (tau_rise≈143 hops≈762 ms attack)
//   bloom=1.0 → alpha_rise≈0.998 (very long attack)
//
// alpha_fall controls how quickly state falls when magAcc[k] decreases:
//   alpha_fall should be small (fast) so the fall tracks the SMD decay closely.
//   A fixed alpha_fall = 0.0 means: when magAcc falls, bloomState snaps to magAcc instantly.
//   A small alpha_fall (e.g. 0.3) gives a touch of smoothing on falls.
//   Recommended: alpha_fall = 0.0 to start; makes Bloom a pure "slow attack" effect.

// Compute once per block:
float alpha_rise = 1.0f - expf(-6.0f * bloom);
const float alpha_fall = 0.0f;   // fast release: track magAcc immediately on descent

// Per-hop, per-bin:
if (magAcc[k] >= bloomState[k]) {
    // magAcc is rising or equal: apply slow attack
    bloomState[k] = alpha_rise * bloomState[k] + (1.0f - alpha_rise) * magAcc[k];
} else {
    // magAcc is falling: fast release (track immediately or with minimal smoothing)
    bloomState[k] = alpha_fall * bloomState[k] + (1.0f - alpha_fall) * magAcc[k];
}
```

Use `bloomState[k]` as the magnitude in the synth pass (the final output magnitude, replacing `blurState[k]` or `magAcc[k]` depending on whether Blur is also active).

### Param mapping

| Bloom | alpha_rise | tau_rise (hops) | tau_rise (ms) | Attack character |
|-------|-----------|----------------|---------------|-----------------|
| 0.0 | 0.000 | 1 | ~5 ms | Off (no swell) |
| 0.3 | 0.835 | 6 | ~32 ms | Soft onset rounding |
| 0.5 | 0.950 | 19 | ~102 ms | Noticeable swell |
| 0.7 | 0.986 | 71 | ~378 ms | Strong bloom |
| 0.9 | 0.998 | 500 | ~2.7 s | Very slow crescendo |

The exp(-6*bloom) mapping is the same formula as Blur's alpha — consistent control feel.

### Perceptual distinctness from Blur (critical)

| Axis | Blur | Bloom |
|------|------|-------|
| IIR type | Symmetric (same alpha rise and fall) | Asymmetric (slow rise, fast fall) |
| Effect on onset | Softens onset AND trailing edge — melts into a cloud | Delays the onset swell — energy builds slowly then releases at SMD speed |
| Effect on tail | Extends trailing edge (tail lingers) | Fall tracks magAcc promptly — tail is NOT extended beyond SMD decay |
| Character | "Dissolving / melting / cloud" | "Blooming / swelling / crescendo" |
| Extreme values | Approaches freeze-like smear of everything | Approach a very long pre-reverb build-in |
| Interaction | Can combine (Blur+Bloom): attack swells AND tail smears | Independent — both can be live simultaneously |

**Blur softens both ends of the reverb envelope symmetrically. Bloom specifically lengthens only the attack side (rise time), leaving the decay side unmodified.** A listener hears: Blur = the reverb "dissolves" in and out; Bloom = the reverb "opens" and swells from silence, then the normal SMD decay continues.

### Chaining Blur and Bloom

When both are active, apply Blur first (as already implemented: `blurState[k]`), then feed `blurState[k]` into the Bloom IIR:

```cpp
// After the existing Blur IIR update:
// blurState[k] = alpha_blur * blurState[k] + (1 - alpha_blur) * magAcc[k];

// Then Bloom IIR on top of blurState:
if (blurState[k] >= bloomState[k]) {
    bloomState[k] = alpha_rise * bloomState[k] + (1.0f - alpha_rise) * blurState[k];
} else {
    bloomState[k] = alpha_fall * bloomState[k] + (1.0f - alpha_fall) * blurState[k];
}
// Synth uses bloomState[k]
```

When Blur=0 and Bloom=0: `bloomState[k] = magAcc[k]` every hop — bit-identical to 0.2.0.7. When Blur>0 and Bloom=0: same as current 0.2.0.9. When Bloom>0 and Blur=0: pure asymmetric bloom swell. When both: blurred attack that also swells.

### Interaction with Freeze and Decay

- **Freeze:** magAcc[k] converges to a held value. The bloom IIR's "rise" path then slowly converges bloomState toward that held value (the reverb "blooms in" to a freeze hold — a beautiful effect). Fast fall means if Freeze is released, bloomState collapses quickly to the new (decaying) magAcc.
- **Decay:** Bloom operates on magAcc AFTER the SMD accumulation. Short Decay → magAcc collapses quickly when input stops; bloomState follows via fast-release. Long Decay → magAcc lingers; bloomState swells up to it and holds.
- **Diffuse (V):** Independent. Phase randomization runs on whatever magnitude is output by the Bloom/Blur chain. Bloom + V = a slow-attack diffuse cloud that blooms in and randomizes. Very musical.
- **Damp:** Also independent — Damp tilts per-bin g(k). bloomState[k] for HF bins (with smaller g(k)) will still see fast decay in magAcc on the fall side, so HF blooms in but decays fast — naturally darker bloom on the LF side.

### Optional frequency-dependent bloom depth

Scale `alpha_rise` by bin index to make HF bloom slower (or faster) than LF, producing a "spectral opening" sweep within the bloom swell. This is the frequency-staggered onset idea (mechanism b) implemented as a modulation of the bloom IIR coefficient rather than a separate delay buffer:

```cpp
// freq_stagger ∈ [0, 1], separate param or folded into Bloom depth
// alpha_rise_k = alpha_rise * (1.0f + freq_stagger * (float)k / kStftBins);
// clamped to [0, 0.9995)
```

This adds no memory cost and no extra state. Defer to v1.x; the baseline symmetric bloom (same alpha_rise for all bins) is already vivid and distinct.

---

## Optional / exotic deferred mechanisms

**Frequency-staggered per-bin delay buffer (mechanism b):** A ring buffer of past magnitude frames indexed by τ(k) = k × Δτ hops. Creates chromatic spectral arpeggio "opening." Memory: S × 513 × 4 bytes per channel per channel. Perceptually distinct from both Blur and Bloom. Reserve as a potential "Spread" or "Cascade" axis.

**Time-reversed bloom (mechanism c):** True reverse-reverb accumulation. Requires N_rev frames of look-ahead buffering and the same memory as pvsblur's ring. Adds ~N_rev × hop latency. Extremely dramatic; marked exotic.

**Gaussian-symmetry bloom (mechanism d):** Requires look-ahead framing (N_gauss/2 hops additional latency). Produces the Costello 0.618 bell-curve envelope. Marked advanced/deferred.

---

## Prioritized implementation plan (sub-phase 0.2.0.10)

1. **Add `bloomState[k]` array per channel** (513 × 4 bytes × 2 ch, memset to 0 in constructor).
2. **Compute `mBloomAlpha` once per block** in `process()`: `mBloomAlpha = 1.0f - expf(-6.0f * mBloomAmt)`. Same formula as Blur, same `expf` call pattern.
3. **Pass `blurState` and `alpha_rise` into `smdProcess`** (or a second helper `bloomProcess`). Feed the Bloom IIR from `blurState[k]` (chains correctly when Blur is also active, falls back to `magAcc[k]` when Blur=0).
4. **Synth pass uses `bloomState[k]`** as magnitude.
5. **`alpha_fall = 0.0f` constant to start** (snap release). Tune by ear: if the fast fall is jarring at high Decay settings, raise `alpha_fall` to 0.2–0.4.
6. **Bloom=0 bit-identical invariant:** alpha_rise=0 → bloomState[k] = blurState[k] or magAcc[k] every hop → same as 0.2.0.9 or 0.2.0.7. Verify.

---

## Sources

- Keith Barr / Midiverb II Bloom: https://valhalladsp.com/2010/12/02/valhallashimmer-tips-and-tricks-bloom/
- Valhalla diffusion envelope / Gaussian 0.618: https://valhalladsp.com/2010/11/30/valhallashimmer-tips-and-tricks-diffusion/
- Valhalla adjusting reverb envelope: https://valhalladsp.com/2010/12/01/valhallashimmer-tips-and-tricks-adjusting-the-reverb-envelope/
- Valhalla SuperMassive (Leo / Sagittarius slow attack): https://valhalladsp.com/shop/reverb/valhalla-supermassive/
- Eventide Blackhole Gravity (reverse envelope): https://www.soundonsound.com/reviews/eventide-blackhole
- Schroeder allpass cascade and Gaussian reverb: https://www.dsprelated.com/freebooks/pasp/Schroeder_Reverberators.html
- Valhalla Shimmer history (allpass cascade attack): https://valhalladsp.com/2010/11/23/valhallashimmer-a-bit-of-history/
- Asymmetric IIR per-bin (dry/ambient separation): https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11238882
- Spectral delay as compositional resource (Gibson): https://econtact.ca/11_4/gibson_spectraldelay.html
- FDN reverb slow attack forum: https://www.kvraudio.com/forum/viewtopic.php?t=547140
- Valhalla Shimmer controls: https://valhalladsp.com/2010/11/27/valhallashimmer-the-controls/
- Naming reverb algorithms (bloom type confirmed): https://valhalladsp.com/2014/01/18/naming-reverb-algorithms/
