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

2. **Should Span and Q be audio-rate too?** ~~Currently both
   are ParameterAdapters with `block-rate` CV input. v1: leave
   block-rate.~~ **RESOLVED 2026-06-22 → YES, audio-rate.** This
   deferral is the source of the "flappy" character the user
   reports when modulating Span/Quality. See **Phase 5** below.

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

---

# Phase 5 — Span + Quality (+ Fundamental) audio-rate via Inlet promotion

Added 2026-06-22. Resolves Open Q #2. Brings the remaining tone
controls up to the same audio-rate fidelity V/Oct already has, so
Canals "takes audio modulation at every input" like the original
Three Sisters instead of sounding flappy under Span/Quality mod.

## Root cause of the flap (evidence)

The lesson is borrowed verbatim from the native ER-301 Sine Osc
(`er-301/mods/core/objects/oscillators/SineOscillator.{h,cpp}`):
**a modulatable target is an `Inlet` (audio buffer summed/read
per-sample), never a `Parameter` (block-rate scalar).** In the
sine osc every modulatable input — `mFundamental`, `mVoltPerOctave`,
`mPhase`, `mFeedback` — is an `Inlet`; the only `Parameter` is
internal phase state. That per-sample buffer read at line 84 is why
its PM sidebands are exact.

Canals already gets this right for ONE input and wrong for the rest,
in the same file:

| Input | C++ decl (`Canals.h`) | Lua wiring (`Canals.lua`) | Read rate | Result |
|---|---|---|---|---|
| V/Oct | `Inlet mVOct` (h:26) | `connect(tune,"Out",op,"V/Oct")` (lua:112) | `voct[i]` per-sample, 2× OS (cpp:350) | audio-rate ✓ |
| Span | `Parameter mSpan` (h:32) | `ParameterAdapter`+`tie` (lua:122-124) | `mSpan.value()` once/block (cpp:165) | block-rate → flap |
| Quality | `Parameter mQuality` (h:33) | `ParameterAdapter`+`tie` (lua:128-130) | `mQuality.value()` once/block (cpp:166) | block-rate → flap |
| Fundamental | `Parameter mFundamental` (h:31) | `ParameterAdapter`+`tie` (lua:116-118) | `mFundamental.value()` once/block (cpp:164) | block-rate (knob — OK, but see below) |

`ParameterAdapter` + `tie()` **is** the block-rate bridge: it samples
the CV and exposes a single scalar the DSP reads once per 64-sample
frame. The derived constants that depend on Span/Quality — `damping`
(cpp:170-191), `antiRes` (cpp:193), `spanMult`/`invSpanMult`
(cpp:195-197) — are likewise computed once per block, so resonance and
bandwidth literally cannot move within a frame. On a high-Q resonant
filter that 750 Hz stair-step reads as a "flap." `connect`→`Inlet`→
per-sample read is the only fix.

## Scope decision

- **DECISION 2026-06-22: Span + Quality only this pass.** Fundamental
  stays a block-rate `Parameter` knob — V/Oct already provides audio-rate
  freq FM, so Fundamental promotion is deferred to a trivial follow-up.
  The Fundamental notes below are retained for that follow-up. In this
  pass `innerStep` keeps `fundamental` as a block-rate closed-over local
  and takes only `span`/`quality` per-sample.
- **Core (must-do): Span + Quality → Inlet.** This is the flap fix.
- **Companion (DEFERRED): Fundamental → Inlet.** Same
  pattern; gives a second audio-rate freq-CV input alongside V/Oct.
  Note the scaling asymmetry: V/Oct is `v*120` semitones (≈±10 oct
  for a ±1 signal — the strong/coarse FM input), whereas a GainBias
  Fundamental adds in raw semitone units (≈±1 semitone for a ±1
  signal at gain 1 — a fine/vernier FM input). Complementary, not
  redundant. Include it unless CPU forces a trim.
- **Stays block-rate: Output (fader morph), Mode (option).** Not part
  of the "freq/quality/span" modulation surface; audio-rate output
  morphing is a separate future idea.

## CPU — correcting the original projection

The original doc feared a 7.5× CPU jump. That fear was about going
from change-gated to per-sample, which **already shipped** (V/Oct is
per-sample at 2× OS). The expensive per-output-sample work is already
paid: `innerStep` runs twice (2× OS), each pass doing 6×`setFreq`
(~20 cyc each incl. a divide) + 6×`process` (each `pseudoSaturate`
~45 cyc + SVF math) ≈ ~1000 cyc/output-sample today.

Span/Quality promotion adds, per internal step: one extra
`SemitonesToRatio` LUT call for `spanMult` (~15 cyc) + the Quality→
`damping` piecewise curve (~10 cyc). ×2 OS ≈ +50 cyc/output-sample on
top of ~1000 → **~+5%**, not 7.5×. Fundamental adds nothing measurable
(it's already inside `totalSemis`). Net: roughly **~4% mono / ~8%
stereo**, a small bump. Profile on hardware; sub-block-of-2 remains the
reserved escape hatch.

## C++ changes — `Canals.h`

Replace the three `Parameter` decls with `Inlet`s (keep display names
so the Lua `connect(...,"Span")` etc. resolve):

```cpp
// REMOVE:
//   od::Parameter mFundamental{"Fundamental", 0.0f};
//   od::Parameter mSpan{"Span", 0.25f};
//   od::Parameter mQuality{"Quality", 0.0f};
// ADD (group with the other inlets, above the outlets is fine):
od::Inlet mFundamental{"Fundamental"};
od::Inlet mSpan{"Span"};
od::Inlet mQuality{"Quality"};
```

`mOutput` and `mMode` stay `Parameter`. No serialization change: the
persisted value lives in the Lua `GainBias`/`ParameterAdapter` object's
Bias (framework-serialized as part of the unit graph), not in the C++
Parameter — which was only ever a `tie` mirror. Inlets hold no state.

## C++ changes — `Canals.cpp`

1. **Constructor (cpp:63-93):** swap the three `addParameter` calls for
   `addInput`:
   ```cpp
   // was: addParameter(mFundamental); addParameter(mSpan); addParameter(mQuality);
   addInput(mFundamental);
   addInput(mSpan);
   addInput(mQuality);
   ```
   Keep `addParameter(mOutput); addParameter(mMode);`. Inlet/outlet
   ordering doesn't matter for Lua name lookup, but add the three new
   inlets near `mVOct` for readability.

2. **Grab the buffers (cpp:138-146 block):**
   ```cpp
   float *fundBuf = mFundamental.buffer();
   float *spanBuf = mSpan.buffer();
   float *qualBuf = mQuality.buffer();
   ```

3. **Delete the block-rate samples + derived constants** (cpp:164-197):
   remove `fundamental = mFundamental.value()`, `span = ...`,
   `quality = ...`, and the whole `damping`/`antiRes`/`spanMult`/
   `invSpanMult` derivation. They move into `innerStep`. Keep
   `outputPos`, `mode`, the fader weights `wL/wC/wH`, `kButterDamp`,
   `kLowGain/kHighGain`, `kInvSR_OS` — those stay block-rate.

4. **Extend `innerStep` signature** to take per-sample fund/span/quality
   and compute the derived constants inside:
   ```cpp
   auto innerStep = [&](float v, float fund, float span, float quality,
                        float xL, float xC, float xH,
                        float &vL, float &vC, float &vH)
   {
     // Quality → damping (piecewise, lifted verbatim from cpp:170-191)
     float damping;
     if (quality < 0.0f)            damping = 1.0f / 0.7071f;
     else if (quality < 0.9f)     { float t = quality*(1.0f/0.9f);
                                    float qMag = 0.7071f + t*t*t*49.3f;
                                    damping = 1.0f / qMag; }
     else                         { float t = (quality-0.9f)*10.0f;
                                    damping = 0.02f*(1.0f-t) + (-0.15f)*t; }
     float antiRes  = (quality < 0.0f) ? -quality : 0.0f;
     float spanMult = stmlib::SemitonesToRatio(span * 48.0f);
     float invSpanMult = 1.0f / spanMult;

     float totalSemis = v * 120.0f + fund;   // was: + fundamental
     // ... rest of innerStep UNCHANGED (freqHz, lowHz/highHz, lowF/
     //     ctrF/highF clamps, ratio damping, the mode 0/1 SVF chains,
     //     vL/vC/vH writes) ...
   };
   ```
   Clamp the per-sample values at read (mirror the old CLAMPs):
   `span` to [0,1], `quality` to [-1,1] — do this where they're read
   in the loop (step 6), not inside innerStep, so the midpoint interp
   sees clamped endpoints.

5. **2× OS carry state** — add to `Internal` and `Init()`:
   ```cpp
   float lastFund;     // init 0.0f
   float lastSpan;     // init 0.25f
   float lastQuality;  // init 0.0f
   ```
   And locals before the loop, beside `prevV`:
   ```cpp
   float prevFund = s.lastFund;
   float prevSpan = s.lastSpan;
   float prevQual = s.lastQuality;
   ```

6. **Per-output-sample loop (cpp:322-396):** read + clamp the three new
   buffers, interpolate midpoints exactly like V/Oct, pass to both
   inner steps:
   ```cpp
   float fundCurr = fundBuf[i];
   float spanCurr = CLAMP(0.0f, 1.0f, spanBuf[i]);
   float qualCurr = CLAMP(-1.0f, 1.0f, qualBuf[i]);

   float fundMid = (prevFund + fundCurr) * 0.5f;
   float spanMid = (prevSpan + spanCurr) * 0.5f;
   float qualMid = (prevQual + qualCurr) * 0.5f;

   innerStep(vMid,  fundMid,  spanMid,  qualMid,  xMidL, xMidC, xMidH,  vL_a,vC_a,vH_a);
   innerStep(vCurr, fundCurr, spanCurr, qualCurr, xCurrL,xCurrC,xCurrH, vL_b,vC_b,vH_b);
   // ... decimate / NaN-clamp / write unchanged ...
   prevFund = fundCurr; prevSpan = spanCurr; prevQual = qualCurr;  // beside prevV = vCurr
   ```

7. **Frame-end carry (cpp:398-403):**
   ```cpp
   s.lastFund     = prevFund;
   s.lastSpan     = prevSpan;
   s.lastQuality  = prevQual;
   ```

`stmlib/dsp/units.h` is already included (cpp:10) so `SemitonesToRatio`
is available inside innerStep.

## Lua changes — `Canals.lua`

For each of fundamental / span / quality, swap `ParameterAdapter`+`tie`
for `GainBias`+`connect`+`MinMax`, mirroring the existing `lowIn` block
(lua:84-90). Span example:

```lua
-- Span (audio-rate: GainBias Out -> Inlet, per-sample)
local span = self:addObject("span", app.GainBias())
span:hardSet("Gain", 1.0)
span:hardSet("Bias", 0.25)
local spanRange = self:addObject("spanRange", app.MinMax())
connect(span, "Out", spanRange, "In")
connect(span, "Out", op, "Span")          -- was: tie(op,"Span",span,"Out")
self:addMonoBranch("span", span, "In", span, "Out")
```

Fundamental: `Bias 0.0`, range obj `fundamentalRange`, `connect(...,"Fundamental")`.
Quality: `Bias 0.0`, range obj `qualityRange`, `connect(...,"Quality")`.

In `onLoadViews` (lua:235-264) point each view's `range` at the new
MinMax object; the `gainbias` field already points at the renamed
object, and `biasMap`/`biasPrecision`/`initialBias` stay as-is. The
`GainBias` ViewControl drives `app.GainBias` natively (it's the intended
pairing — more correct than the prior ParameterAdapter pairing).

`Output` (ModeSelector) and `Mode` keep `ParameterAdapter`+`tie` — they
stay block-rate.

## Risks / watch-items

- **CV scaling feel.** GainBias adds CV in the control's native units
  (span 0..1, quality -1..1). A full-scale audio signal sweeps the
  whole range at gain 1 — expected. The user can trim with the gain.
  Confirm in audition it isn't too hot/cold; adjust default gain if so.
- **Self-osc boot.** Quality at/above the top-decile self-osc edge must
  still boot from the denormal seed. Per-sample quality doesn't change
  the seed path (cpp:344-348) — verify high-Q self-osc still sings.
- **am335x NEON.** No new stack NEON arrays; `-fno-tree-vectorize` is
  set for am335x (mod.mk:89-91). `innerStep` stays scalar. Run the
  objdump `vld1...[:64]` hint check on `Canals.o` per the build script
  (mod.mk:107 `neon_hint_check`).
- **SWIG.** Editing `Canals.h` (Parameter→Inlet) changes class layout;
  `SWIG_HEADER_DEPS` (mod.mk:39) force-regens the wrapper — the
  `feedback_swig_header_dep` heap-corruption trap is auto-handled. Do a
  clean-ish rebuild to be safe.
- **Freq-comp damping under fast mod.** Now that damping is per-sample
  AND span is per-sample, the `ratioLow/ratioHigh` compensation
  (cpp:271-276) tracks live. Re-audition per-block amplitude balance
  (hardware targets LOW 0.68 / CTR 1.12 / HIGH 0.65) under fast
  Span/Quality sweeps; this is the Phase 3 tuning carried forward.

## Build / version

- Bump `PKGVERSION 2.8.1 → 2.8.1.1` in `mods/spreadsheet/mod.mk`
  (4th-digit dev iteration per `feedback_dev_digit_during_iteration`;
  required for the device to re-extract per `feedback_package_version_bump`).
- Build BOTH arches (`feedback_always_build_both_arches`):
  `make spreadsheet ARCH=linux` and `make spreadsheet ARCH=am335x`.
- Auto-install linux to emu (`feedback_linux_build_auto_install`):
  `cp testing/linux/spreadsheet*.pkg ~/.od/rear/` (confirm exact path).
- Verify `Canals.h` edit actually landed before building
  (`feedback_verify_edit_landed_for_silent_failures`) — Parameter→Inlet
  is exactly the kind of layout change where a silent stale build bites.

## Test checklist

- [ ] linux + am335x build clean; `Canals.o` 0 NEON hints
- [ ] Insert on chain → control-rate Span/Quality knobs still behave
- [ ] Slow LFO → Span: smooth, no zipper/step
- [ ] Audio-rate signal → Span: smooth bandwidth FM, no flap
- [ ] Audio-rate signal → Quality: smooth resonance FM, no flap
- [ ] High-Q self-osc still boots + sings (denormal seed path intact)
- [ ] Per-block amplitude balance unchanged at rest (LOW/CTR/HIGH)
- [ ] Fundamental-CV (if included): fine FM, complementary to V/Oct
- [ ] Quicksave/reload round-trips Span/Quality/Fundamental bias values
- [ ] Hardware audition: matches Three Sisters "takes mod everywhere"

## One-sentence identity

Promote Span, Quality, and Fundamental from block-rate `Parameter`s to
audio-rate `Inlet`s (GainBias→connect, per-sample read, derived coeffs
computed inside the 2×-OS innerStep) so Canals stops flapping and takes
audio modulation at every input like the original Three Sisters.

---

## Phase 5b — residual "quantized/floppy" audit vs native Sine Osc (2026-06-22)

After 5 shipped (audio-rate Span/Quality), user reported it still sounds
"a bit quantized or floppy" and asked if we're pulling punches vs the
Sine Osc. Honest accounting:

**Fixed:**
1. Block-rate Span/Quality → per-sample Inlet (the flap). [Phase 5]
2. **Truncated `SemitonesToRatio` LUT → interpolated.** stmlib's
   `SemitonesToRatio` (units.h:41) indexes the fractional table with a
   *truncating* cast (`static_cast<int32_t>(frac*256)`) — 256 hard steps
   per semitone, no interp. Under audio-rate FM the cutoff/spanMult walks
   that staircase → audible quantization. The native Sine Osc derives
   freq via smooth polynomial `simd_exp` (SineOscillator.cpp:59), no
   table. Fixed with `semisToRatioSmooth()` (Canals.cpp) — linearly
   interpolates stmlib's low table, keeps exact node values (preserves
   hardware self-osc freq calibration), index-clamped vs OOB. Shipped
   2.8.1.2.
3. Span/Quality GainBias Gain default 1.0 → 0.0 (CV opt-in). [2.8.1.2]

**Still pulled (candidates if residual floppiness persists), ranked:**
- **Crude [½,½] 2-tap decimator** (cpp ~line 392). -3 dB at 12 kHz; leaves
  internal-rate imaging/aliasing from the in-loop pseudoSaturate + self-
  osc. Sine Osc has no decimator (exact at base rate). A halfband FIR
  decimator is the next concrete lever for "grit/rough." Moderate cost.
- **Linear-interp upsampling** of V/Oct/span/quality to the 2× midpoint —
  mild LP, slightly softens fast mod. Minor.
- **2-term Taylor `tan()`** for SVF g (`g=pif(1+pif²/3)`) — <1% in band,
  diverges toward Nyquist → high cutoffs slightly warped. Minor.

**Intrinsic (NOT a punch — physics of resonant-filter FM):** a 6-pole
resonant SVF cascade has state/memory, so fast cutoff/Q modulation
interacts with the resonant ringing (can't track instantaneously) and
the freq-compensated damping moves with cutoff → some amplitude wobble.
The memoryless exact PM of a sine will always be cleaner. The analog
Three Sisters has the same memory but truly continuous coefficients;
we approach but can't perfectly match analog continuity. Some residual
"floppy" under aggressive modulation is the filter being a filter.

### Phase 5c — Span slew (pop fix), 2.8.1.3

After 5b, user: "sounds quite good… span now produces pops as it moves
along its travel." Mechanism (confirmed against od source): `GainBias`
ramps a knob change over only one block (~1.3 ms, `FrameOfLinearRamp`),
and the old `ParameterAdapter` actually `hardSet` its value (no ramp).
So the delta isn't smoothing — it's that Span's knob has **4-octave
exponential leverage** (`span*48` semitones → both band cutoffs via
`semisToRatioSmooth`), so each encoder detent lands as an abrupt cutoff
jump that pops the resonant bands. Now audible because 5b removed the
quantization that masked it. Quality doesn't pop (maps to damping
additively, no leverage).

Fix: one-pole LP slew on the [0,1] Span value BEFORE the exponential
(`s.spanSlew += (target - s.spanSlew) * kSpanSlew`), `kSpanSlewMs = 5`
(corner ~32 Hz). Replaces Span's 2× OS midpoint interp (slew is already
smooth). Lightly bandlimits Span CV — span audio-rate FM is exotic, knob
smoothness wins. `kSpanSlewMs` is a one-line tunable. Quality keeps its
per-sample midpoint interp (no slew).

### Phase 5d — soft-knee the clamps (the real seam fix), 2.8.1.5

User after 5c: slew made it "even more poppy… more seams it is audibly
crossing," and asked how the builtins handle it. Decisive finding from
`er-301/mods/core/objects/filters/LadderFilter.cpp` (+ Stereo variants):
native ladder filters derive cutoff with smooth `simd_exp` (not a LUT),
apply it per-sample with NO slew, and have **no in-band hard clamps** —
the only clamp is the cutoff rail `[minNormF, 0.9999]` (≈ DC / Nyquist),
which normal playing never reaches.

Canals, by contrast, had a CASCADE of hard clamps — `freqHz`/`lowHz`/
`highHz` to [20,20000], `lowF`/`ctrF`/`highF` to [0.001,0.499], and the
damping ratio to `>4→4`. Because Span spreads the bands ±4 octaves, a
normal Span sweep drives the LOW band into the ~96 Hz floor, the HIGH
band into the 20 kHz ceiling, and the ratios into 4 — and each hard
`if(x>lim)x=lim` is a C1 derivative break = a seam. The 5c slew made it
WORSE because a slow continuous sweep crosses each seam audibly instead
of blowing past. (Verified the 5b interpolated LUT is continuous across
semitone boundaries — NOT the seam source.)

Fix (user picked "soft-knee all the clamps"): C1 `softCeil`/`softFloor`
quadratic-ease helpers (identity until within `knee` of the limit, then
a quadratic meeting it with matched value+slope). Consolidated the
redundant Hz+normalized cascade into ONE `softClampF` per band into
[0.001, 0.2083] (= [96 Hz, 20 kHz], the exact old effective range),
matching the native ladder's single-rail posture. Damping ratios get
`softCeil(.,4,1)`. Slew reverted (5c undone). Self-osc calibration
unaffected at default (no clamp engaged mid-range; soft-knees only act
near the rails / extreme Span) — re-verify amplitudes at extreme Span.
Knees (`fKneeLo/fKneeHi`, ratio knee) are tunable constants.

### Phase 5e — seam-safe Span slew (control-step transients), 2.8.1.6

After 5d, user: "still popping, less so… pops at all Q levels, much more
apparent at higher cutoffs." Diagnosis: "all Q + worse at higher cutoff"
rules out resonance-sensitivity and points at control-step transients —
each Span knob detent is GainBias-ramped over only ~1.3 ms, and Span's
exponential mapping makes that a much bigger Hz jump at high cutoffs, so
the resonant bands click harder up top at any Q. Soft-knees (5d) removed
the rail-seam half; this is the other half.

Fix: re-introduced the one-pole Span slew (the 5c tool) — now seam-safe
because 5d made the clamps soft, so the slew no longer drags Span
through hard-clamp seams (the actual cause of 5c sounding worse).
kSpanSlewMs = 8 ms; slewing [0,1] = slewing log-cutoff = constant oct/s,
so it equalizes across the range (directly targets "worse at higher
cutoff"). Tunable. If a steady high-cutoff BUZZ (not movement-tied pops)
remains after this, that's 2× OS decimator aliasing → the deferred
halfband-FIR decimator is the next lever.
