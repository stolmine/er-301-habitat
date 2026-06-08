# Canals — Audio-rate parameter modulation

Plan to bring Canals' parameter modulation up to audio-rate
fidelity. Currently the implementation samples parameters
once per frame and gates SVF coefficient recompute on
above-threshold change — both fatal to audio-rate FM and
the self-patching scenarios the user wants to explore.

## Problem statement

Current per-sample loop in `Canals::process()` reads V/Oct
ONCE at index 0 per frame, and recomputes SVF coefficients
only when params change beyond a threshold:

```cpp
float v = voct[0];  // ← single sample per frame

if (fundamental != s.prevFundamental || span != s.prevSpan ||
    quality != s.prevQuality || mode != s.prevMode ||
    (v > s.prevVoct + 0.001f || v < s.prevVoct - 0.001f))
{
  // Recompute SVF coefs only on parameter change
  ...
}
```

Consequences:

- **Upper modulation bandwidth ≈ framerate/2.** At 64-sample
  frames / 48 kHz: ~375 Hz max modulation rate before
  Nyquist artifacts dominate. Anything above is aliased
  garbage.
- **Audio-rate FM impossible.** Sending an audio signal
  into V/Oct produces stair-stepped modulation — sounds
  like an aggressive sample-and-hold artifact.
- **Self-patching destroys character.** Patching LOW → V/Oct
  would be musically interesting (2-op FM territory), but
  with control-rate sampling the destination only ever sees
  the first sample of LOW's buffer per frame.
- **Slow parameter sweeps work.** That's why the unit
  feels OK with knob turns and slow CV — they're below
  the 375 Hz limit. Audio-rate use cases all break.

## Convention (ER-301 LadderFilter)

The canonical pattern in ER-301's built-in filters
(reference: `mods/core/objects/filters/LadderFilter.cpp`)
is a **two-pass per-sample architecture**:

**Pass 1 — Per-sample coefficient bake:**
```cpp
for (int i = 0; i < FRAMELENGTH; i += 4) {
  // Vectorized: read V/Oct, derive cutoff, compute filter coefs
  // Write to scratch buffers (P, K, R in LadderFilter)
}
```

**Pass 2 — Per-sample audio processing:**
```cpp
for (int i = 0; i < FRAMELENGTH; i++) {
  // Read pre-computed coefs from scratch buffers
  // Apply to filter state, write to output buffer
}
```

Key principles:

- **Always recompute** — no change detection.
- **Two passes** — coef bake separated from audio processing.
  Lets the coef pass be vectorized (NEON or similar) while
  the audio pass stays scalar.
- **Scratch buffers from `AudioThread::getFrame()`** —
  rented from the audio thread's pool; released at end of
  process(). No per-call allocation.

LadderFilter has 3 derived coefs per sample (P, K, R). Canals
has 6 SVFs × 3 coefs each (g, r, h) — significantly heavier
per-sample compute, but the same architecture applies.

## Architecture for Canals

### Top-level changes

- Drop the `if (params changed)` gating entirely.
- Drop the `prevFundamental`, `prevSpan`, `prevQuality`,
  `prevVoct`, `prevMode` state from the Internal struct.
  No longer needed.
- Read `voct[i]`, `mFundamental.value()`, etc. per sample
  (or every 4 samples — see "Sub-block compromise" below).
- Compute SVF coefficients per sample.
- Apply SVF processing per sample using the just-computed
  coefficients.

The current code does pass 1 (coef bake) at the frame
boundary, then pass 2 (audio processing) per sample. The
restructure folds pass 1 INSIDE the per-sample loop OR
makes it a separate scratch-buffer pass per Iframe LadderFilter.

### Decision: monolithic per-sample loop or two-pass scratch?

**Monolithic per-sample loop** (simpler, less memory):
```cpp
for (int i = 0; i < FRAMELENGTH; i++) {
  // Read voct[i], derive cutoffs, set all 6 SVF coefs
  // Run 6 SVFs through current sample
  // Write outputs
}
```

**Two-pass with scratch buffers** (matches LadderFilter,
NEON-friendlier):
```cpp
float *g_low = AudioThread::getFrame();
float *g_ctr = AudioThread::getFrame();
// ... 12 more scratch buffers for r, h per SVF

for (int i = 0; i < FRAMELENGTH; i++) {
  // Compute and store all coefs to scratch buffers
}
for (int i = 0; i < FRAMELENGTH; i++) {
  // Read coefs from scratch, run SVFs
}

AudioThread::releaseFrame(...);  // x 18 buffers
```

For v1 (scalar): **monolithic per-sample loop**. Smaller
code, no scratch buffer management overhead, only one cache
sweep through the frame.

For v2 (NEON, future): **two-pass with scratch** — required
for NEON vectorization across the coef bake. 18 scratch
buffers feels like a lot but each is `FRAMELENGTH * sizeof(float)`
= 256 bytes (64-sample frames). 18 × 256 = 4.6 KB total —
well within L1 dcache.

### Sub-block compromise (consideration)

Per-sample coef bake is the most expensive part. Practical
real-world FM modulation is bandlimited (typical use is
0-5 kHz modulation rate; nobody FMs a filter cutoff at
20 kHz). We could recompute coefs every 4 samples (sub-block
of 4) — gives 6 kHz modulation Nyquist at 48 kHz, plenty
for musical FM, and saves 75% of the coef bake cost.

But this re-introduces a form of staircase artifact at
modulation rates above 6 kHz. The LadderFilter does true
per-sample (no sub-block); we should match the convention
for v1 and only sub-block if CPU shows hot.

**Decision: per-sample coef bake in v1.** Match LadderFilter.
Revisit sub-block only if profiling shows necessity.

### Per-sample coefficient pipeline

For each sample `i`:

```cpp
// 1. Read inputs at this sample
float v = voct[i];                    // V/Oct CV per-sample
float fund = mFundamental.value();    // params block-rate; constant per frame
float span = ...;
float quality = ...;

// 2. Convert to cutoffs in Hz
float totalSemis = v * 120.0f + fund;
float freqHz = 261.63f * SemitonesToRatio(totalSemis);
// ... clamp, derive lowHz/highHz from span ...
float lowF = clampNorm(lowHz);
float ctrF = clampNorm(freqHz);
float highF = clampNorm(highHz);

// 3. Derive frequency-compensated damping per resonant stage
float dampLowF = damping * (ctrF / max(lowF, 0.001f));
float dampHighF = damping * (ctrF / max(highF, 0.001f));
dampLowF = clamp(dampLowF, ..., damping * 4.0f);  // runaway guard
dampHighF = ...

// 4. setFreq on all 6 SVFs (computes g, r, h internally)
s.low1.setFreq(lowF, dampLowF);
s.low2.setFreq(lowF, kButterDamp);
s.ctr1.setFreq(lowF or ctrF, dampLowF or damping);  // depends on mode
s.ctr2.setFreq(highF or ctrF, kButterDamp);
s.hi1.setFreq(highF, dampHighF);
s.hi2.setFreq(highF, kButterDamp);

// 5. Process audio through SVFs
auto lo1 = s.low1.process(in[i]);
auto lo2 = s.low2.process(lo1.lp);
lowOut[i] = (lo2.lp + antiRes * lo1.hp) * kLowGain;
// ... CENTRE, HIGH ...

// 6. Fader morph + rail-clip
float mix = vL*wL + vC*wC + vH*wH;
out[i] = SistersSvf::fastTanh(mix);
```

Per-frame still:
- Read params (fund, span, quality, mode, output, drive)
- Compute knob-derived constants (Q→damping curve, fader
  weights, antiRes scalar)
- Apply input denormal flush

### What stays block-rate

- **Parameter values** (`mFundamental.value()` etc.): these
  are knob/CV-adapter values that update at the audio block
  boundary anyway. Reading them per-sample wouldn't add
  bandwidth — they're already control-rate. Cheap to leave
  outside the inner loop.
- **Q-knob → damping curve**: depends only on quality
  (block-rate). Compute once per frame, use the result in
  the per-sample compensation math.
- **Fader weights (wL, wC, wH)**: depend only on outputPos
  (block-rate).
- **Mode (XOVER vs FORMANT)**: block-rate option.

### What moves to per-sample

- **`v = voct[i]`** — the V/Oct buffer is audio-rate.
- **`freqHz`, `lowF`, `ctrF`, `highF`** — derived from v
  per sample.
- **Frequency-compensated damping per stage** — derived
  from per-sample cutoffs.
- **`setFreq` calls on all 6 SVFs** — required for per-
  sample audio-rate mod.

## CPU projection

Per-sample work, mono:

| Step | Cycles |
|---|---|
| Read v, derive totalSemis | 2 |
| `SemitonesToRatio(totalSemis)` (stmlib LUT) | ~15 |
| Compute lowHz, highHz from span | 4 |
| `clampNorm` x3 | 3 |
| Freq-comp damping calc x2 | 6 |
| `setFreq` x6 SVFs (π·f, x³ Taylor, 1/(1+rg+g²)) | ~150 |
| SVF process x6 (per-sample state update) | ~150 |
| Anti-res additions x3 | 9 |
| Per-block gain x2 | 4 |
| Fader morph + tanh + write Out | ~25 |
| Per-block buffer writes | 6 |
| **Total per sample** | **~375 cycles** |

At 48 kHz mono on Cortex-A8 (~720 MHz):
`375 × 48000 / 720M = 2.5% CPU`. Acceptable. Comparable to:
- Filterbank ~10% stereo (8 bands)
- Pecto ~6% stereo
- Filament ~4% stereo

Stereo would double to ~5%, still reasonable.

Compared to the current change-detection-gated version:
- Current: ~50 cycles/sample (only the SVF process pass)
- New: ~375 cycles/sample
- **7.5× CPU increase** — but enables audio-rate FM.

If CPU becomes a problem, sub-block-of-4 amortizes the coef
bake to ~94 cycles/sample average, total ~160 cycles/sample
≈ 1.1% CPU. Reserved for v1.5 if needed.

## NEON considerations (deferred to Phase 4+)

NEON vectorization could bake the 6 SVFs' coefficients in
parallel. Layout: pad to 8-wide, leaving 2 lanes unused
(simpler than 6-wide custom). One frame produces 8 lanes of
(g, r, h) coefficients per sample.

Two-pass architecture would be required:

- **Pass 1 (NEON)**: read voct + bake 8-wide coefficient
  buffers
- **Pass 2 (scalar)**: read coefs, run scalar SVFs

Reasons to defer:
- v1 scalar is correct + maintainable
- NEON on Cortex-A8 has well-known hint-surface traps
  (`feedback_neon_intrinsics_drumvoice`,
  `feedback_neon_hint_surfaces`)
- Profile-driven optimization is preferred over speculative
- Even if NEON saves 50% in Pass 1, that's only ~12% total
  cpu reduction (since Pass 2 is unchanged)
- The "right" optimization may be sub-block coef bake, not
  NEON

Plan: ship scalar in v1. Profile on hardware. If hot, try
sub-block before NEON.

## Implementation phases

**Phase 1 — Scalar per-sample refactor** (~30-45 min)

1. Remove `if (params changed)` block in `Canals::process()`
2. Remove `prev*` state from Internal struct (no longer needed)
3. Move per-sample work into the existing per-sample loop:
   - Read `voct[i]` (not `voct[0]`)
   - Compute `totalSemis`, `freqHz`, `lowF`, `ctrF`, `highF`
     per-sample
   - Apply freq-compensated damping per-sample
   - Call `setFreq()` on each SVF per-sample
4. Build both arches, install linux, smoke-test that basic
   filter behavior still works (no audio-rate input — just
   verify control-rate works)
5. Audition with audio at V/Oct (sine sweep, then audio
   signal) — should hear smooth FM, no stair-step

**Phase 2 — Self-patching audition** (~15 min)

User-driven testing on hardware. Specifically:
- Canals LOW out → Canals V/Oct in (self-FM)
- Canals CENTRE out → Canals V/Oct in (stronger self-FM)
- Other modulation sources at audio rate
- Compare character against hardware Three Sisters' 2-op FM

If character matches expectation, Phase 3.

**Phase 3 — Tune freq-comp damping in audio-rate context** (~15 min)

Audio-rate FM might affect the freq-compensated damping
balance — at fast cutoff sweeps, the SVF dynamics change.
Verify per-block amplitude balance still holds. Tune if
needed (e.g., smoother f_ref/f computation, lower clamp
ceiling).

**Phase 4 — NEON (deferred, conditional)**

Only if Phase 1's CPU profile shows >5% on Cortex-A8 in
typical patches. Restructure to two-pass with scratch
buffers, vectorize coef bake. Per the NEON memories — be
aware of hint surface traps on Cortex-A8.

## Open questions

1. **FM input as a separate inlet?** Currently V/Oct is the
   only CV input. We could add a dedicated `FM` inlet with
   its own depth knob (per the original redesign plan).
   v1: skip — audio-rate V/Oct gives most of the use cases.
   Revisit if user wants index-style FM control.

2. **Should Span and Q be audio-rate too?** Currently both
   are ParameterAdapters with `block-rate` CV input. Could
   move to audio-rate sampling. v1: leave block-rate — V/Oct
   is the primary FM destination on hardware. Audio-rate Q
   sweeps are possible but exotic; defer.

3. **Should we add coefficient smoothing?** At very high
   audio-rate FM rates (~5+ kHz), the cutoff sweep produces
   aliasing in the cutoff signal itself. A 1-sample low-pass
   on V/Oct (`voctSmoothed = 0.5 * voct[i] + 0.5 * voct[i-1]`)
   would help. v1: skip — see if it's a real problem first.

4. **NEON Hint Surface**: per `feedback_neon_hint_surfaces`,
   stack-allocated NEON arrays in the inner loop are
   dangerous on Cortex-A8 with `-O3 -ffast-math`. The current
   `mod.mk` for spreadsheet already has `-fno-tree-vectorize`
   on am335x which mitigates this. v1 scalar is safe.

## Testing checklist (Phase 1)

- [ ] Linux build clean
- [ ] am335x build clean, Canals.o 0 NEON hints
- [ ] Insert on chain → basic filter audio works (control-rate
      V/Oct via knob)
- [ ] Sine sweep on V/Oct input → smooth pitch sweep (no
      stair-step at high modulation rates)
- [ ] Audio signal on V/Oct → recognizable FM
- [ ] Self-patch LOW → V/Oct → 2-op FM character
- [ ] Hardware audition by user — does it match Three Sisters
      audio-rate FM character?
- [ ] CPU usage on hardware ≤ 5% mono (profile via stolmine
      load view)

## Related memories

- `feedback_cortex_a8_no_double_in_hot_loops` — keep all
  per-sample math single-precision float (we already do)
- `feedback_disable_tree_vectorize_am335x` — already set
  globally in `mod.mk`
- `feedback_neon_intrinsics_drumvoice` — relevant for Phase 4
- `feedback_neon_hint_surfaces` — same

## Identity in one sentence

Move all SVF coefficient computation from frame-rate
(change-gated) to per-sample (always-recompute), reading
V/Oct per-sample so audio-rate FM works and the user's
self-patching scenarios sound musical.
