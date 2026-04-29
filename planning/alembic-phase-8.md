# Alembic Phase 8 — Non-Audio Dims + Sample-Pointer Excitation

Status: **planning**, post-`.175` Ngoma-removed release, 2026-04-28.

Companion to `planning/alchemy-voice.md` (full design doc) and the
`project_alembic_codex.md` memory (current phase table).

## Release path this plan sits inside

After `2.5.5.175` (Ngoma stable, xform removed), the spreadsheet release
path is:

1. **Phase 8a** — Order 2 (scan-neighbor gradients → detune offsets)
2. **Phase 8b** — Order 3 (topology metrics → reagent flag)
3. **Phase 8c** — meta-mapping (sample-level features bias the table)
4. **Phase 8d** — sample-pointer excitation (audio-path)
5. **Phase 8e** — sample-pointer depth control (scan ply shift sub)
6. Sample-swap retraining bug fix (gate `analyzeSample` on
   `Sample::isFullyLoaded()` or defer to a re-checking task)
7. Wavetable stacked-frames viz (reagent-scan companion to sphere)
8. **Phase 6** serialization (preset shape now final)
9. Spreadsheet release with Ngoma + Alembic

**Phase 7 (per-region user-bias sub-params) is deferred.** Ferment
plus the existing 8 plies is the v1 control surface; user-bias fades
can land later as a 1.x refinement without schema break.

## Why this order

- 8a / 8b / 8c are pure training-time. They write into the existing
  preset row layout, no schema change. Cheap, additive, listen-tested
  one at a time per "earns its complexity."
- 8d is audio-path. It introduces a new modulation source the matrix
  can route. Likely extends the source pool (currently 0..3 PMM ops +
  4..6 filter LP/BP/HP), so will require a schema bump for the lane
  src indices. Lock down BEFORE serialization.
- 8e adds one ParameterAdapter (sample-pointer depth). Schema impact:
  one more adapter Bias to round-trip. Trivial.
- Sample-swap fix is independent of feature work but has to ship
  before serialization is meaningful — without it, quicksave-restore-
  then-swap-sample is broken UX.
- Wavetable viz can land any time after 8a-c; no DSP coupling.
- Phase 6 last because the persisted shape must be final on first
  ship; any later schema bump is a forced migration path.

---

## 8a — Order 2: scan-neighbor gradients → detune offsets

**What**: per node, look at its 2-3 nearest neighbors in scan order,
compute the feature-space gradient magnitude, fold into the trained
detunes for ops B/C/D.

**Why**: currently detunes are pure-feature-driven per-node — they
ignore the surrounding context in the trained map. Order 2 says
"isolated picks should drift further; clustered picks should stay
coherent with their neighbors."

**Implementation sketch**:
- After `analyzeSample`'s preset population pass, run a second pass
  over the time-sorted picks.
- For each node `i`, compute `gradient[i] = (||features[i] - features[i-1]||
  + ||features[i] - features[i+1]||) * 0.5` (Euclidean over the 7-dim
  normalized space). Endpoints handled by single-sided gradient.
- Map `gradient[i]` ∈ [0,1] (already normalized features) to a detune
  scalar `1 + gradient[i] * 0.3` (cap at +30%) applied to row[9..11]
  (op B/C/D detunes; op A always stays at its trained value to anchor
  pitch).
- Multiplicative on top of existing detune, so disabling Order 2 is
  literally `gradient *= 0` and doesn't reshape the trained values.

**Files**: `AlembicVoice.cpp` (new helper after preset population),
no header change, no Lua change.

**Cost**: O(N × 7-dim distance) at training time = 64 × ~7 flops × 2
neighbors = ~900 flops, negligible. Zero runtime cost.

**Risk**: low. If musical result is bad, scalar coefficient (0.3)
is a single dial.

**Listen test**: scan slowly across a percussive vs sustained sample;
gradient zones should produce wider stereo / detune feel.

**Version**: `2.5.5.176`

---

## 8b — Order 3: topology metrics → reagent flag

**What**: compute per-node eccentricity (distance from cluster
centroid in feature space) and sparsity (mean distance to K-nearest
neighbors). Fold both into the wavetable blend amount (row[28]) so
"edge" nodes get more shaper engagement and "center" nodes stay clean.

**Why**: currently row[28] = (1-runLen) × (entropy + flatness) × 0.5,
which is pure feature-driven. Order 3 spatializes it across the
trained map — the shaper isn't just "high-entropy nodes," it's also
"isolated nodes."

**Implementation sketch**:
- After Order 2, compute `centroid` = mean of features across all 64
  nodes.
- For each node `i`:
  - `eccentricity[i] = ||features[i] - centroid||` (then min/max
    normalize across nodes so values are 0..1)
  - `sparsity[i] = mean(||features[i] - features[neighbor]||) for
    K=4 nearest neighbors` (normalize same way)
- Bias the existing row[28]: `row[28][i] *= (1.0 + eccentricity[i] *
  0.4 + sparsity[i] * 0.3)`. Cap at 1.0 in the consumer (already
  CLAMPed).
- Multiplicative on top.

**Files**: `AlembicVoice.cpp` (extend the same helper as 8a). Header /
Lua unchanged.

**Cost**: O(N²) at training time for sparsity (64×63 distance pairs)
= ~30k flops. One-shot, training-time only, negligible.

**Listen test**: the wavetable shaper should engage more strongly at
"weird" picks (claps, scratches, isolated transients in a dense
sample) and back off in dense melodic clusters.

**Version**: `2.5.5.177`

---

## 8c — Meta-mapping: sample-level features bias the table

**What**: compute global features over the entire sample (not per
pick) and use them as biases applied to the trained map. Five
sub-mappings, each independently gateable per "earns its complexity."

**Sub-mappings**:

1. **Per-sample feature variance** → bounds per-node chaosScore range.
   - Compute std-dev of each of the 7 features across the 64 picks.
   - Mean of those 7 std-devs = `featureVariance` ∈ [0,1] roughly.
   - Use as a multiplier on the chaosScore range when picking topology
     (row[12..27] matrix selection). High variance → wider topology
     spread; low variance → narrower.

2. **Mean entropy** → biases topology distribution.
   - Mean of feature[4] (entropy) across 64 picks.
   - Currently topology is picked via `chaosScore` against fixed
     thresholds. Bias the threshold midpoint by `meanEntropy * 0.2`
     so high-mean-entropy samples (noise/grit) skew toward topo
     masks 5/6 (cross-mod heavy), low-entropy samples stay in 1/2
     (harmonic).

3. **Sample length** → adjusts coarse hop.
   - Currently coarse hop is fixed at ~20 Hz. For samples shorter
     than 1s, this gives < 20 picks before farthest-point, which is
     thin. Adapt: `hop = clamp(sampleLength / 64, minHop, maxHop)`.
   - Doesn't change the preset row, just how picks are chosen.

4. **Stereo correlation** → drives detune asymmetry.
   - Compute Pearson correlation between L and R channels across
     the whole sample.
   - Low correlation (wide stereo) → asymmetric detunes: row[10]
     (op C detune) gets a + bias, row[11] (op D detune) gets a -
     bias of equal magnitude. High correlation (mono) → symmetric.
   - For mono-loaded samples: skip this mapping entirely.

5. **DC offset / waveform asymmetry** → drives matrix-path bias
   direction.
   - Compute mean amplitude (signed). Non-zero = DC offset or
     asymmetric waveform.
   - Positive DC → bias diagonal matrix entries up (more self-fb).
   - Negative DC → bias off-diagonal up (more cross-mod).
   - Magnitude scaled small (≤ 0.1) so it nudges, doesn't dominate.

**Implementation sketch**:
- New `extractSampleLevelFeatures()` helper called once at
  `analyzeSample` start, populates a 5-element `mSampleFeatures`
  struct.
- Each of 1–5 wired into the relevant existing helper:
  - 1, 2 into topology selection
  - 3 into the coarse-pass loop hop
  - 4, 5 into the preset-row population pass

**Sequencing**: ship 1, 2, 3 first — they're additive over the
existing preset row. Add 4 and 5 if they earn complexity.

**Files**: `AlembicVoice.cpp` (extend feature-extraction +
preset-population helpers). Header gets one struct field for
`mSampleFeatures` (~5 floats). No Lua change.

**Cost**: training-time only. Pearson correlation = O(N) over the
sample. Tiny.

**Versions**: `2.5.5.178` (1+2+3), `2.5.5.179` (4+5 if wanted)

---

## 8d — Sample-pointer excitation (audio-path)

**What**: at runtime, the voice reads the trained sample at the
current scan-node's stored timestamp (windowed) and feeds it as a
new modulation source into the routing matrix.

**Why**: bridges sample-trained synthesis with sample-as-audio.
Scanning across nodes traces a path through the source as well as
through trained presets. Adds a thread of original texture beyond
what features capture.

**Implementation sketch**:

- **Per-node sample position**: after farthest-point selection in
  `extractCoarseFeatures`, store `mPresetTimestamp[64]` = pick
  timestamp in samples (already implicit in the time-sorted picks).
  New 64-int member array on the C++ class.
- **Runtime read**:
  - At each block, K-blend the timestamps of the active K
    scan-neighbors → `currentPos`.
  - Read sample at `currentPos` with linear interp (sample is
    `int16_t` already in `mSamplePool` — same as Pecto's `bufRead`
    pattern).
  - Apply a windowed envelope: ±50ms (fixed) Hann taper centered on
    each pick's timestamp. Multiple picks blended → multiple
    overlapping windows.
  - Output is one `float` per block-rate sample.
- **Routing**: extend `mLaneSrc` source pool from 0..9 to 0..10. Source
  10 = sample-pointer-excitation output. Existing matrix routing in
  `applyRouting` picks it up as another candidate src for any
  destination.
- **Sample-pool fallback**: if `mpSample == nullptr` or `mSamplePool`
  is empty, output zero. Don't crash.
- **Feature-driven win shaping**: window width scales with
  `1 - feature[5]` (runLen) — short bursts for transient-heavy nodes,
  longer windows for stationary nodes. Per-node, baked into preset
  table at training time as `mPresetWinWidth[64]`.

**Pitch coherence**: native rate read for v1 (read at sample's native
SR, no pitch tracking). Voice already has its own f0 carrier; the
excitation is a *texture* injection, not a tracked osc. Revisit if
the result feels detached.

**Files**: `AlembicVoice.h` adds `mPresetTimestamp[64]` and
`mPresetWinWidth[64]`. `AlembicVoice.cpp` adds:
- `extractCoarseFeatures` writes timestamps + win widths
- `process` adds sample-read + window logic per block
- `applyRouting` extended for source 10
- per memory `feedback_neon_intrinsics_drumvoice`: any new array
  touched by NEON must be class-member, not stack-local.

**Schema impact**: preset row stays 49 floats. New per-instance state:
2 × 64 = 128 floats added (`mPresetTimestamp` as float for unified
storage, `mPresetWinWidth` as float). Phase 6 serialization needs to
include these.

**Cost (runtime)**: 1 sample read + linear interp + window mult per
output sample × FRAMELENGTH = ~5 flops/sample. Negligible.

**Risk**: medium. Audio-path. Need to verify on hardware that the
new matrix source doesn't reshape codegen badly (lessons from Ngoma
re-applied: smaller patch, listen test before stacking, objdump check
on `applyRouting`).

**Version**: `2.5.5.180`

---

## 8e — Sample-pointer depth control

**What**: depth fader exposed in the scan ply's shift sub-display.
User decision: this lives alongside the existing K (path window)
sub on the scan ply.

**Implementation sketch**:
- New `mSamplePointerDepth` `od::Parameter` on `AlembicVoice` (0..1,
  default 0.5).
- New ParameterAdapter `samplePointerDepth` in `AlembicVoice.lua`,
  tied to that Parameter.
- `AlembicScanControl.lua` (the paramMode shift-toggle control)
  extended: shift sub-display now exposes BOTH K and
  samplePointerDepth. Per memory `feedback_parammode_convention`,
  this means a multi-sub paramMode setup with paramModeDefaultSub
  pinned. Needs the `setSubCursorController` discipline from
  memory `feedback_subcursor_inheritance`.
- The depth scales source 10's output amplitude before it enters
  the routing matrix. depth=0 = matrix sees zero from source 10
  regardless of trained lane attens; depth=1 = full trained
  contribution.

**Files**: `AlembicVoice.h` adds `mSamplePointerDepth`,
`AlembicVoice.cpp` adds Parameter wiring, `AlembicVoice.lua` adds
adapter + tie + branch + sub-control, `AlembicScanControl.lua`
extended for the multi-sub.

**Schema impact**: one ParameterAdapter Bias to round-trip in
serialize/deserialize.

**Risk**: small but non-zero. Multi-sub paramMode has known bugs
(see Decision 8 PINNED in `todo.md` shift-handling section). Confirm
the new sub plays nicely with existing K sub before shipping.

**Version**: `2.5.5.181`

---

## Sample-swap retraining fix

Independent of Phase 8 but blocks meaningful Phase 6.

**Symptom**: direct sample swap without detach doesn't retrain
reliably. Detach-then-attach via menu works.

**Hypothesis**: `analyzeSample` race against `Sample::Pool` async
load. The new sample's `Sample` object exists but `data()` may
return zero or partial buffer when `analyzeSample` reads it.

**Fix attempts** (in order):
1. Gate `analyzeSample` on `mpSample->isFullyLoaded()` (if that API
   exists in current SDK). Bail if not loaded; defer to a callback
   or polling task.
2. Post `analyzeSample` to a deferred task that re-checks readiness
   each tick and runs once it lands.
3. Add `loadDuration` parameter that lets users trigger manual
   retrain after the sample is fully loaded — last-resort UX
   workaround if 1+2 don't apply.

**Reproduce**: load sample A → commit. Without detach, load sample B.
Expected: scan flat. Workaround: detach → load B → attach.

**Version**: `2.5.5.182`

---

## Wavetable stacked-frames viz

**What**: render `mWavetableLUT[64][256]` as a stacked / cascading
waveform display. Active reagent-scan-position frames lit, neighbors
dimmed, time-sorted ordering visible at a glance.

**Why**: sphere viz = main scan; this would be the reagent-scan
companion. User should see WHERE they are in the wavetable space
the same way they see scan position on the sphere.

**Where it lives**: TBD. Options:
- Per-unit "wavetable inspector" mode — full-width 128×64 display.
- Sub-display when reagent ply is focused.
- Standalone graphic on the reagent control ply.

**Risk**: low. Pure render code, no DSP coupling. Per memory
`feedback_viz_encoder_capture_architectural`: tile granularity +
state cache + time slicing. Not raw CPU.

**Version**: `2.5.5.183`

---

## Phase 6 — Serialization (after 8e + sample-swap fix + viz)

Now persisted shape is final. Schema design:

- **Sample reference**: by Sample.Pool key + path. On deserialize,
  attempt re-resolve. If pool entry missing, leave voice with
  placeholder preset table and log warning.
- **Per-node trained state**:
  - `mPresetTable[64][49]` — 12.5 KB
  - `mLaneSrc[64][8]` + `mLaneDst[64][8]` — 1 KB
  - `mWavetableLUT[64][256]` — 64 KB
  - `mPresetTimestamp[64]` (Phase 8d) — 256 B
  - `mPresetWinWidth[64]` (Phase 8d) — 256 B
- **Per-instance globals**:
  - `mSampleFeatures` (Phase 8c) — 20 B
- **User-bias adapter biases**: every ParameterAdapter Bias on the
  Lua side (the standard Excel pattern).
- **Schema version**: bump to a new int. Migration path: missing
  fields default to centroid / zero / 1.0 as appropriate.

Storage size per instance: ~80 KB. Multiple instances per patch
viable but quicksave starts to feel heavy. Consider:
- Compress wavetable LUT (could be 8-bit per sample, 16 KB instead
  of 64).
- Re-derive on load if sample is in pool — only persist
  derivation-config + sample ref, not the full table.
- Trade: re-derive is slower on load but smaller save file.

Decision deferred until we measure actual quicksave size.

**Version**: `2.5.5.190+` (some headroom)

---

## Risks / open questions

- **Phase 8d's matrix source extension (0..10)**: lane src is
  `uint8_t`, plenty of headroom. But `applyRouting`'s per-source
  read pattern needs to add a branch for source 10. Per Ngoma
  lessons (`feedback_runtime_branched_dsp_dispatch`), differential
  dispatch is suspect. Use branchless arithmetic masking pattern
  here from the start, not a switch.
- **Phase 8e's multi-sub on AlembicScanControl**: Decision 8 visible
  highlight on the sub-display is a known pinned bug. Don't ship
  Phase 8e without verifying the sub indicator behaves correctly.
- **Phase 6 schema design**: serializing the wavetable LUT directly
  is the simplest, but largest. Re-derivation on load is more
  fragile (sample-pool absence). Decide post-prototype.
- **Working release name**: `[TBD]`. Spreadsheet package version
  bumps to whatever cuts after Phase 6 lands.

## Files this plan will touch

- `mods/spreadsheet/AlembicVoice.cpp` (8a, 8b, 8c, 8d, 8e, 6)
- `mods/spreadsheet/AlembicVoice.h` (8d adds 2×64 float arrays;
  8e adds Parameter; 6 — no header change)
- `mods/spreadsheet/assets/AlembicVoice.lua` (8e adds adapter +
  branch; 6 adds serialize/deserialize tables)
- `mods/spreadsheet/assets/AlembicScanControl.lua` (8e multi-sub)
- `mods/spreadsheet/assets/AlembicWavetableGraphic.{h,cpp}` (new,
  for viz step)
- `mods/spreadsheet/spreadsheet.cpp.swig` (probably no change —
  AlembicVoice public surface stays the same modulo Phase 8e
  Parameter, which auto-bridges via existing Parameter SWIG
  exposure)
