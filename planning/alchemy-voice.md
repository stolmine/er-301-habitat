# Alchemy Voice: Self-Contained SOM Synthesis

Status: **planning**. No code yet. Placeholder unit name.

Package: **spreadsheet** (custom plies, overview viz, shift-toggle controls).

## Intent

A voice that isn't cribbed from classic analog topologies or reference synths. Thus far the habitat library has ported, re-implemented, or riffed on well-documented DSP (SVF drum cores, comb resonators, state-variable filters, classic phase-distortion). This one starts from a different premise: the voice is inseparable from its own state, and the state is inseparable from the data it was trained on.

**Themes:** alchemy, metallurgy, chemistry, the occult, metamorphosis, transmutation, cybernetics. The voice is a _vessel_ that transmutes loaded material into continuously varying synthesis states. Parameters are rites, not knobs.

**Name candidates** (float, not decided): Crucible, Alembic, Athanor, Calcine, Sublimate, Chrysopoeia, Mercurius, Ouroboros, Solve. I like **Alembic** — the vessel of transmutation.

## Departure From Current SOM Unit

The existing `mods/catchall/Som` (742 LOC + 271 LOC Voronoi viz) has the right _bones_ but the wrong _stance_:

| Current SOM                                                    | Alembic                                                                            |
|----------------------------------------------------------------|------------------------------------------------------------------------------------|
| Trains from live audio input                                   | Loads a user sample, trains **offline** once, frozen                               |
| Exposes Plasticity / LearningRate / NeighborhoodRadius to user | Training is a **black box** — no user tuning                                       |
| Training-param sweep rarely produces musical results           | Training is deterministic per sample; the sample _is_ the parameter                |
| Features = audio-domain only (RMS, brightness)                 | Features are **multimodal** — audio + binary-domain + spectral + entropic          |
| Filter-pair + mux is stable but tame                           | Mux gains a **scanning mixer** that crossfades routings along the node path        |
| Voronoi viz: per-pixel seed search, causes encoder capture     | New viz: cheap, asynchronous, never blocks the UI thread                           |

The effect of all this: a voice where the user's creative act is _choosing material_, not tuning learning hyperparameters.

## Training Model

### Offline sample-derived nodes

User drops a sample onto a slot (same flow as custom-sample units in the library). On commit:

1. **Coarse pass:** scan the sample at a low frame rate (~20 Hz hop, ~50 ms stride) extracting only the cheap subset of the feature vector.
2. **Divergence sampling:** pick 64 maximally-different moments in the sample (see Difference-Based Sampling below).
3. **Full-feature pass:** extract the complete multi-modal feature vector at those 64 picked moments.
4. Run k-means (few iterations) to tighten Layer 0 vectors and establish scan-chain ordering.
5. Run the Layer 1 derivation stack to produce the 64 per-node presets.
6. Serialize base vectors, presets, and scan-chain ordering into unit state.

Runtime cost: **zero**. Training happens once on commit. No plasticity loop in the audio thread. This retires `mPlasticity`, `mLearningRate`, `mNeighborhoodRadius`, `mDecay` — gone from the UI entirely.

### Difference-Based Sampling (the critical piece)

Uniform sampling over tonally-stable material collapses: 64 frames from a 10-second sustained source cluster into 4–5 real regions with 15× redundancy, and k-means converges on 64 nearly-identical centroids. Average nodes in, average synth in.

Instead we sample for **maximal divergence**. Two-stage pick:

1. **Change filtering.** On the coarse-pass feature stream, compute per-frame L2 change magnitude `||f[t] - f[t-1]||`. Suppress runs below a threshold — long self-similar regions don't deserve 64 samples, they deserve a representative handful.
2. **Farthest-point sampling.** From the surviving candidate frames, iteratively pick 64 frames such that each new pick is the one maximally distant (in coarse feature space) from all previously picked. This is stronger than top-K-by-change — change magnitude picks onsets but misses distinct steady states; farthest-point guarantees a diverse _set_.

The hybrid does three useful things:

- **Removes the sample-length cap.** Longer samples become _better_ training material because there's more divergence to mine. 60-second samples produce more distinct nodes than 10-second samples, not fewer.
- **Skips self-similar regions.** Sustained notes, silence, tape loops — all compressed to their representative moments, not over-sampled.
- **Preserves temporal variety.** Even from a short source, the 64 nodes span the widest feature region the sample can offer.

Cost stays bounded: coarse pass is ~20 Hz × a cheap feature subset, so a 60-second sample costs ~2.4× a 10-second one at the coarse stage, but the full-feature pass is fixed at exactly 64 frames regardless of source length.

### Rationale

### Rationale

The current unit's training params give the user three ways to misuse a SOM and one way to get something interesting. Removing them reduces the search space to the one dimension the user actually cares about: what material does the voice speak in. Loading a different sample is a more meaningful gesture than nudging a learning rate.

## Multi-Modal Feature Extraction

Currently the SOM sees the sample only through an audio-domain lens. To make nodes meaningfully orthogonal, extract features from several _representations_ of the same material:

### Audio-domain (familiar)

- RMS energy
- Spectral centroid (brightness)
- Spectral flux (onset-ness)
- Zero-crossing rate

### Binary-domain (novel, user's suggestion)

Treat the raw sample bytes as data, not audio. This gives dimensions uncorrelated with what the frame _sounds_ like:

- **Byte histogram entropy** — how "random" is the bit pattern of this frame?
- **Run-length distribution** — long runs of identical bytes vs. high-churn regions
- **Compressibility proxy** — LZ77-style match count in a small window; low-entropy frames stand near low-entropy frames on the map
- **Bitplane variance** — per-bitplane popcount variance across the frame's samples; picks up dithering artifacts, bit-depth history, encoder watermarks

Two frames that sound similar can have wildly different binary fingerprints (one from a 16-bit studio source, one from a degraded MP3 decode). This produces map geography that _tracks provenance_, not just timbre.

### Statistical / entropic

- Sample-value kurtosis and skew (captures distortion history)
- Autocorrelation peak lag (captures pitchedness without committing to a pitch)

### Why this matters for the voice

Currently the SOM's 6 dimensions map directly to voice params. With orthogonal feature dimensions, walking the map produces walks through _genuinely different_ parameter regions, not smoothly correlated ones. Nodes trained on "hissy + pitched" end up in a different region than "clean + pitched," even if their audio-only features were close.

## Mapping Model

Features from Layer 0 don't touch synth parameters directly. They pass through a three-layer model: a base feature vector, a multi-order derivation stack that expands it into a preset, and a set of group-wise fades that decide how much each region of the voice is governed by training vs. user bias. The layers are distinct because they answer different questions — _what was heard_, _how to read it_, and _how much to let it rule each part of the voice_.

### Layer 0 — Node base vector

The raw clustered feature vector from offline training: ~12 dims spanning audio-domain (RMS, brightness, flux, ZCR), binary-domain (byte entropy, run-length, compressibility, bitplane variance), and statistical (kurtosis, skew, autocorr lag). This is the SOM's native coordinate system. Nodes are _positions_ in this space; the derivation stack is how positions become sounds.

### Layer 1 — Multi-order derivation stack

Offline, at training-commit time, we expand the base vector through successive orders. Each order is a distillation step — higher orders read relationships the lower orders can't see. All outputs are cached per-node so runtime cost is zero.

| Order | Domain                      | Reads                          | Produces (examples)                                                                     |
|-------|-----------------------------|--------------------------------|-----------------------------------------------------------------------------------------|
| 0     | Direct                      | single feature                 | brightness → ratio slot; entropy → diagonal feedback; pitchedness → ratio lock strength |
| 1     | Products / contrasts        | pairs of features              | `brightness × entropy` → cross-mod density; `pitched / noise` → integer-vs-irrational  |
| 2     | Gradients along the map     | this node vs. K scan neighbors | high Δfeature across neighborhood → detune offsets (unsettled); low → none              |
| 3     | Topology metrics            | whole-map structure            | eccentricity / richness-distance → reagent flag, matrix sparsity pattern                |

Each of the 55 slots in the per-node preset is fed by exactly one order's output. The split:

- **Direct (O0):** pitched-vs-noisy → ratios; RMS → output levels. The user's expectation that pitch tracks pitched material is preserved.
- **Contrast (O1):** matrix off-diagonal values (cross-mod density). Contrasts between features are what make nodes feel _different_ from each other.
- **Gradient (O2):** detune offsets. Nodes in stable neighborhoods are coherent; nodes at feature boundaries shimmer.
- **Topology (O3):** reagent flag, matrix sparsity (which paths exist at all vs. are zero). Topology is what tells us a node is _unusual_ — and unusual nodes deserve unusual waveshapers / connectivity.

**Ratio mean-centering.** After Layer 1 produces ratios per node, subtract the mean across all nodes and add back a user-controlled offset. Guarantees ratio _variation_ across nodes even when the source material was tonally uniform — if all frames had similar pitch, the raw ratios are similar, but the mean-centered deltas preserve whatever relative variation exists. Paired with the `Tune` ply (global f0), this gives the user predictable pitch control even from self-similar samples.

### Layer 2 — Group fades

Rather than one dry/wet between "trained" and "untrained," four independent group blends. Each runs 0 → 1: at 0 the user bias dominates; at 1 the Layer-1-derived preset is fully expressed.

```
pitch group      = { ratios[4], detunes[4] }          blend wPitch
structure group  = { matrix off-diagonal [12] }        blend wStructure
feedback group   = { matrix diagonal [4] }             blend wFeedback
dynamics group   = { output levels[4], reagent }       blend wDynamics
```

The 29 floats partition cleanly across the four groups: 8 + 12 + 4 + 5 = 29. No overlaps, no fall-through.

The scanning matrix mixer blends _between nodes_ first (K-weighted sum of presets from neighboring nodes along the scan path). Then group fades apply, blending that interpolated preset against user biases. Order matters: node interpolation preserves alchemical geography; group fades let the user choose _which regions_ of the voice obey that geography.

Ferment, Crucible, and Reagent globals from the previous revision still sit _on top_ of the group-blended output as post-scalars — they multiply the final matrix/diagonal/waveshaper mix. The separation: group fades decide _how much_ each region is trained-driven; globals shape _how much_ that region expresses at all.

### Why this abstraction level

- Too close to the synth (1:1 feature → param) produces predictable output — all features correlate and the training signal washes out.
- Too abstract (features → "mood" → params) loses the handle; loading a different sample feels the same.
- Middle ground: direct where continuity matters (pitch tracks pitched features), derived where character matters (structure and feedback live on contrasts and gradients, not raw values).

Each order is deterministic and offline-computable, so the runtime cost is zero and the output is reproducible from the sample alone.

## Voice Engine

### Pivot: phase-mod matrix core, not filter-pair FM

The coupled TPT SVF filter pair + binary divider from the som-stuber draft stays tame in practice — FM into self-oscillating SVFs is well-behaved, not chaotic. The broad-range reference is Joe's **Phase Mod Matrix** (`~/repos/Accents/Xxxxxx.lua`): 6 sine operators with a full 6×6 phase-modulation matrix (36 paths including self-feedback). Enormous state space, naturally edge-of-chaos because operator feedback cascades through the matrix.

**We go 4-op, not 6-op.** Reasons:

- **NEON alignment.** 4-wide `float32x4_t` is the native Cortex-A8 register. 4 phase accumulators, 4 ratios, 4 levels, 4-vector ops all line up with one SIMD lane. 6-wide means partial lanes, masking, or scalar fallback — worse throughput and worse code.
- **Matrix cost is quadratic.** 6×6 = 36 paths, 4×4 = 16 paths. Less than half the per-sample matrix work at effectively the same timbral palette — 4-op PM already spans DX7 TX81Z territory, and without DX's fixed algorithm restrictions we have access to every 4-op topology at once.
- **Crossfade bandwidth.** The scanning mixer writes ~(matrix + ratios + levels + detune) per block across K nodes. 16 matrix values vs 36 means K=4 crossfades become 64 vs 144 multiply-accumulates per block. Cheap either way but 4-op leaves more budget for the mapping layer and viz.

Alchemy's voice is **a 4-op phase-mod matrix whose state is painted by the SOM.**

### Per-node state

Each of the 64 nodes carries:

- **4 operator ratios** (multipliers against shared f0, range ~0–24)
- **4 output levels** (each op's contribution to the sum bus)
- **16 phase-mod matrix values** (flat `matrix[4][4]`; diagonal goes to self-feedback, off-diagonals go to cross-mod)
- **4 fine-tune offsets** (small detuning, drives beating and non-harmonic content)
- **1 reagent flag** (selects waveshaper family at the output: tanh / fold / wrap / bitcrush-by-mask)

Total: 4 + 4 + 16 + 4 + 1 = **29 floats per node**. 64 nodes × 29 = 1856 floats ≈ 7.4 KB. Fits easily in quicksave.

### How nodes are derived

See **Mapping Model** above. Each node's 55-float preset comes from running its base feature vector through the Layer-1 derivation stack (orders 0–3). Offline, deterministic, cached. The scan-chain ordering is preserved from clustering so walking the map traverses related presets smoothly.

### Scanning matrix mixer (the key move)

The previous filter-mux version crossfaded an 8-triple routing table. The new version crossfades the **entire phase-mod matrix plus ratios plus levels plus detunes** across K bracketing nodes:

```
at each audio block:
    find K nearest nodes along scan path (K = 3 or 4)
    compute equal-power weights w[k] summing to 1
    ratios[op]     = sum over k of w[k] * node[k].ratios[op]
    outLevels[op]  = sum over k of w[k] * node[k].outLevels[op]
    matrix[i][j]   = sum over k of w[k] * node[k].matrix[i][j]
    fineTune[op]   = sum over k of w[k] * node[k].fineTune[op]
    reagentWeights = accumulated w[k] per reagent family (soft crossfade)
```

The operators themselves run continuously; only their modulation topology morphs. This gives continuous _structural_ modulation — not just parameter sweeps. With 64 matrices × smooth K-weighted blends, the voice reads as a single vessel undergoing transmutation, not as 64 discrete timbres switching.

**Zipper noise:** per-block update is fine for matrix values (they change slowly as scan walks). Ratios should be one-pole smoothed sample-by-sample to avoid zipper on fast scans. Standard `ParameterAdapter` target pattern handles this automatically.

### Operator implementation

**Reference path (Lua graph):** For the initial port from Xxxxxx, use `libcore.SineOscillator` at the graph level — built-in `Fundamental`, `V/Oct`, `Phase`, `Feedback`, `Sync` inlets. The 4×4 matrix becomes 16 `GainBias` scalars, 12 off-diagonal `Multiply`s (op_out × matrix_scalar) summed via a 3-deep `Sum` cascade into each op's `Phase` input, and 4 self-loops op_out → `Feedback` via a scalar. This is the diff-able reference — a known-good shape we can listen against.

**Production path (native C++):** The Lua graph approach has high per-object overhead — 16 `GainBias` + 12 `Multiply` + 12 `Sum` = 40 `od::Object`s just for the matrix, each with its own `process()` call and per-buffer dispatch. For hardware viability the matrix and operators move into a single C++ unit (`Alembic.cpp` / `AlembicVoice.cpp`) that inlines the whole 4-op + 4×4 + output sum + waveshaper in a tight NEON loop. See **Optimization & Implementation** below.

At 64 nodes × 29 floats, the matrix state is written **as parameter targets** from C++ on the scan path update; in the native path, directly as floats into the inner loop's state struct (no adapter indirection needed).

### Self-contained excitation

No external audio input needed. Excitation is:

- Sample-bias at op inputs (read from the loaded sample at a node-dependent offset)
- The feedback loops themselves (the matrix self-oscillates)
- Optional: a slow internal LFO bussed into a selectable op's V/Oct or Phase

Audio input remains available as an **additional** phase-mod source into the matrix — patchable but not required.

### Signal Chain Sketch

```
Loaded sample ─► [offline training] ─► 64 per-node {ratios, levels, matrix, detune, reagent}
                                                │
Scan pos ─► path (K bracketing nodes) ─► scanning matrix mixer
                                                │ writes matrix + ratios + levels + detune
                                                ▼
         ┌────────────────── 4×4 phase-mod matrix ──────────────────┐
         │                                                           │
  op A ──┤   (each op_out routed into each op_in's Phase via         ├── level A
  op B ──┤    scalar-weighted matrix; diagonal goes to Feedback)     ├── level B
  op C ──┤                                                           ├── level C
  op D ──┤                                                           ├── level D
         └───────────────────────────────────────────────────────────┘
                                                │
                                           output sum
                                                │
                              per-node reagent waveshaper blend
                                                │
                                                ▼
                                             Output
```

Zero external dependency for excitation. Optional input remains as a patch point.

### What we retire from the filter-pair draft

- TPT SVF pair, binary dividers, staircase DACs — gone. They were clever but audibly tame.
- 8-triple mux routing tables — subsumed by the 4×4 matrix, which _is_ the routing.
- Anti-periodicity jitter — FM matrices don't lock the way dividers do; small detune per op handles this naturally.

## Optimization & Implementation

Setting this up from the start to be NEON-friendly is load-bearing — we can't graft SIMD on later without rewriting the inner loop. Architecture decisions below are chosen to make the eventual NEON port trivial.

### Native C++ voice unit

The 4-op matrix lives in a single C++ unit, not a Lua graph of 40 `od::Object`s. A Lua-graph PMM works as a reference but the per-object `process()` dispatch is fatal on hardware — each buffer tick touches 40 objects with their own state, buffer handoffs, and cache-unfriendly pointer chasing. The native unit inlines the entire voice:

```
struct AlembicVoice {
    float phase[4];          // per-op phase accumulator
    float prevOut[4];        // last-sample op outputs (for feedback)
    float ratio[4];          // live-blended from node presets
    float level[4];
    float matrix[16];        // row-major, matrix[dst*4 + src]
    float detune[4];
    int   reagent;
    // ... scan state, global scalars (Ferment, Crucible, Reagent tint)
};
```

Inner loop per sample, conceptually:

```c
// 1. Phase increment (4 ops, vectorizable)
float32x4_t vfreq = vmulq_f32(vratio, vload(f0Hz));     // 4 freqs at once
vphase = vaddq_f32(vphase, vmulq_f32(vfreq, vInvSR));

// 2. Matrix × prevOut  →  per-op phase offset (the chaotic part)
//    Each destination op sums contributions from all 4 sources including self.
//    4 rows × 4 vmla = 16 vmla.f32, then 4 horizontal sums → 4 phase offsets.
for (int dst = 0; dst < 4; dst++) {
    float32x4_t row = vload(&matrix[dst*4]);
    phaseOffset[dst] = horzSum(vmulq_f32(row, vprevOut));
}

// 3. Sine LUT lookup per op (4 scalar lookups — LUT gather; see memory below)
for (int op = 0; op < 4; op++) {
    out[op] = sinLUT(phase[op] + phaseOffset[op]);
}

// 4. Output sum with per-op level, then reagent waveshaper.
float mix = vector_dot(vload(out), vload(level));
float y   = reagentShape(mix, reagent);
```

Core cost per sample: **16 vmla + 4 horzSums + 4 sin-LUT reads + 4 adds + 1 dot + 1 shaper ≈ 60–80 cycles** on Cortex-A8. At 48 kHz that's ~3.5–4 MHz. Comfortable.

### NEON considerations specific to am335x

- **LUT sin, not `sinf()`.** Memory hit: package-shipped `sinf`/`cosf` on am335x miscomputes for certain graphics code paths. Doesn't matter for one-shot shaders, matters a _lot_ for 4×48000 lookups/sec. Use a 1024-entry `float` LUT with linear interpolation — matches the trig LUT pattern already validated by the package-trig-LUT memory.
- **Phase-to-LUT-index conversion** can be a single `vcvtq_u32_f32` + mask after we normalize to `[0, 1)`, not a `fmod`.
- **Avoid divisions** in the hot loop — precompute `invSR = 1.0 / 48000.0` at init.
- **Prefetch pattern.** Node-preset crossfade update (called once per block, not per sample) can follow the same 8-ahead prefetch discipline used in the delay gather (memory: _NEON 3-pass delay gather_). Preload next K node presets before the operator-loop frame.
- **SWIG wrapper hygiene.** If `AlembicVoice.h` is `%include`'d into the spreadsheet SWIG file, header edits must force-regenerate the wrapper. Memory: _SWIG wrapper header dep gap_ — stale wrappers' `sizeof` corrupts the heap and surfaces as crashes on `delete` / quicksave, not at load time. Add the force-clean rule to `mod.mk` from day one.

### Matrix-crossfade update (per audio block, not per sample)

Runs once on the block boundary, not in the sample loop:

```
find K=4 nearest nodes along scan path
compute equal-power weights w[k]
for i in 0..28:
    preset[i] = sum over k of w[k] * node[k].preset[i]
apply group fades:
    out.ratios    = lerp(userBias.ratios,    preset.ratios,    wPitch)
    out.detunes   = lerp(userBias.detunes,   preset.detunes,   wPitch)
    out.matOff    = lerp(userBias.matOff,    preset.matOff,    wStructure)
    out.matDiag   = lerp(userBias.matDiag,   preset.matDiag,   wFeedback)
    out.levels    = lerp(userBias.levels,    preset.levels,    wDynamics)
    out.reagent   = weighted-pick(userBias.reagent, preset.reagent, wDynamics)
apply globals:
    out.matOff  *= Ferment
    out.matDiag *= Crucible
    (Reagent global handled in the shaper-blend stage)
```

K × 29 + 29-lerp + handful of globals = ~180 mul-adds per block. At 128-sample blocks that's ~1.4 FLOPs/sample amortized, essentially free.

### Zipper noise at scan boundaries

Ratios and detunes feed directly into phase increments — any step in them clicks. Slew them with a one-pole smoother at the sample level (block-boundary target → per-sample interpolate). Matrix values can update per block without audible stepping because they're multiplied into feedback samples that are already smooth.

### Cache friendliness

- Node preset table: 64 × 29 × 4 bytes = 7424 bytes. Fits in ~1/4 of L1 cache. All 4 bracketing nodes are guaranteed cache-hot for scan moves ≤1 node/block.
- Scan chain (64 ints) fits in a single cache line × 2. Preload at init.

## Performance Targets

Back-of-envelope estimates. Not benchmarks — treat as "what we expect to see" and correct once real numbers come back from hardware. Only two axes we actually care about: commit-time wall-clock and runtime audio-thread CPU.

### Commit-time wall-clock

Sample analysis + training on commit. Dominant cost is feature extraction; k-means and derivation are each under 5 ms.

| Sample length | Coarse pass | Full-feature × 64 | Total wall-clock (realistic, audio engine running) |
|---------------|-------------|-------------------|----------------------------------------------------|
| 10 s          | ~10 ms      | ~45 ms            | **200–500 ms**                                     |
| 30 s          | ~30 ms      | ~45 ms (fixed)    | **300–800 ms**                                     |
| 60 s          | ~60 ms      | ~45 ms (fixed)    | **500–1200 ms**                                    |

Full-feature pass is fixed at exactly 64 frames regardless of source length — scaling is only the coarse pass, which is cheap. A progress modal is warranted; blocking the UI for up to ~1.2s on long samples is acceptable in line with how other sample-commit flows (slicing, trim) behave on the device.

### Runtime audio-thread CPU

Per-voice-instance, native C++ 4-op matrix at 48 kHz:

| Configuration                         | ER-301 CPU meter (estimated) |
|---------------------------------------|------------------------------|
| Mono voice                            | ~5–8%                        |
| Stereo (per-op L/R levels, cheaper)   | ~6–9%                        |
| Stereo (two full instances, detuned)  | ~11–15%                      |
| 4 stacked stereo instances            | ~30–40%                      |

Scanning-mixer update and viz don't meaningfully contribute — mixer runs per block, viz runs on a separate thread.

### Where we'll be wrong

- **Sine LUT gather** is scalar on A8 — estimate assumes ~35 cycles/4 lookups. If it's 50+ we push to ~180 cycles/sample and meter numbers go up ~50%.
- **Binary feature extraction cost** is the loosest estimate in the commit-time table. If a stream turns out expensive, we drop it from the set — the mapping model is designed to survive feature-set changes without rearchitecting.
- **ER-301 meter semantics** vary with chain configuration. A number on the meter is what matters, not raw MHz.

We won't run hard measurements against these; they're here so we notice when reality is off by 3× vs. off by 30%.

## Serialization

All sample-derived state persists with the unit so quicksave/quickload round-trips behave — the user should never lose their trained character to a reload.

### What we serialize

| Field                                    | Size      | Why                                                                |
|------------------------------------------|-----------|--------------------------------------------------------------------|
| Sample pool reference (slot name + hash) | ~32 bytes | Same pattern as existing sample-consuming units                    |
| Layer 0 base vectors — `float[64][12]`   | 3 KB      | Lets us re-derive presets if the Layer 1 stack changes between versions, without needing the sample |
| Layer 1 derived presets — `float[64][29]` | 7.4 KB    | The actual per-node synth state; primary cache                     |
| Scan-chain ordering — `int[64]`          | 256 B     | Derived from clustering; serialize to avoid re-running k-means     |
| Per-group user-bias values               | ~120 B    | wPitch-bias, wStructure-bias, wFeedback-bias, wDynamics-bias payloads |
| Global scalars                           | ~16 B     | Scan pos, K, Ferment, Crucible, Reagent, f0, etc. (standard adapter serialization covers this) |

**Total:** ~10.5 KB per unit instance. Well within reason for quicksave.

### Resilience to sample loss

If the sample pool entry is missing on quickload (sample deleted, SD swap), the preset cache still loads and the voice still sounds — the user just can't _re-train_ without re-loading the sample. This is the important property: **the voice doesn't break when the sample goes away**. Training is an input; the _output_ of training is the load-bearing state.

### Version compatibility

The base vectors serialize independently from the derived presets so a future version that changes the Layer 1 derivation can re-derive presets from cached base vectors without re-running clustering. The base vectors themselves are dimensionless feature numbers; their semantic interpretation lives in Layer 1 code. This decoupling is why Layer 0 is explicitly cached — it's the format-stable representation.

### Format

Flat binary blob, little-endian floats, version header. Writer/reader in C++ (not Lua) because Lua-side string buffer I/O is slow for 10 KB and fragile around SD fragmentation. Follow the `ConstantOffset`/adapter serialization pattern from memory (_Serialize/deserialize pattern_) for the adapter fields; add a custom block for the preset cache.

## Visualizer

### What's wrong with the current one

`SomSphereGraphic.h` does a per-pixel nearest-seed search over 64 nodes for every frame of a 128×64 viewport (with a half-frame toggle and a 64×64 cache). On hardware that's ~8K pixel ops/frame even when cached, and the cache invalidates whenever rotation moves. This is almost certainly why encoders capture and UI lag explodes — the draw call is blocking the event loop.

### Principles for the new viz

1. **Never allocate or iterate per-pixel per-frame.** Precompute once. The voronoi tessellation for a given rotation is _fixed data_ — bake it, don't recompute.
2. **Decouple visualization from input handling.** Draw can be throttled (every 2–4 frames, or on state change) without starving encoder events. The current implementation draws every frame regardless.
3. **No sqrtf, sinf, cosf in the draw hot path.** LUTs or precomputed projections. (Memory: package-shipped sinf/cosf on am335x miscomputes — always LUT.)

### Proposed viz

A **stratified alchemical chart** rather than a rotating sphere:

- 64 nodes laid out on a fixed 2D projection (not a rotating 3D sphere — that was the cycle hog). Layout: concentric rings or a Fibonacci disk so the topology still reads.
- Each node is a small glyph (4–6px). Glyph _shape_ encodes the node's reagent flag (which waveshaper family). Glyph _fill density_ encodes richness. Glyph _halo_ encodes how close the scan head is.
- Active path is drawn as a short tracer: the last N scan positions as a fading line of lit nodes.
- One live overlay: a thin spectrum bar or LFO trace along the top/bottom edge, rendered from voice output — keeps the display _alive_ without expensive compute.

Cost budget: ≤ 500 pixel ops per frame. That's at least 10× the current budget headroom.

**Fallback**: if the stratified chart feels static, the scan tracer plus slow global drift (rotating background tessellation texture, _not_ per-pixel recompute) can give it breathing motion without the Voronoi cost.

## UI / Plies

Tentative, in priority order. The point is to be _shorter_ than the current SOM unit's ply set because training params are gone:

1. **Scan** — primary knob. Walks node path. Sub: path window K (3/4) for the matrix mixer.
2. **Tune** — shared f0. Sub: V/Oct track, coarse/fine.
3. **Pitch / Struct** (shift-toggle per paramMode convention) — group fades wPitch and wStructure. Controls how much training paints ratios+detunes vs. matrix off-diagonal. Sub-display surfaces the user-bias value when the fade is below 1.
4. **FB / Dyn** (shift-toggle) — group fades wFeedback and wDynamics. Same pattern.
5. **Ferment** — global scalar on post-blended off-diagonal matrix. At 0 the matrix collapses to 6 independent sines; at 1 the blended state is fully expressed.
6. **Crucible** — global scalar on post-blended diagonal (self-feedback). Separate axis from Ferment.
7. **Reagent** — global tint over waveshaper family weights at the output sum.
8. **Level / Mix**.

Group fades (plies 3–4) and globals (plies 5–7) are distinct axes: fades decide how much each region of the voice is governed by the training; globals scale how much that region expresses at all. Together they let the user take any trained node from "clean additive sines at user-chosen ratios" all the way to "fully transmuted" without re-training. Fades can also be automated via the internal LFO — the slow drift of one regime into another.

Sample-load control: handled as a sample slot on the header ply (same pattern as existing sample-consuming units in the library).

## Open Questions

- **Sample length limits:** no hard cap needed under difference-based sampling — full-feature cost is fixed at 64 frames. Practical ceiling driven by wall-clock training time (progress modal acceptable up to ~2s). Likely comfortable with samples up to 60s+.
- **Windowing:** no longer a design question — difference-based sampling picks 64 moments adaptively. Frame size at the picked moments is TBD (probably 4096 samples = 85 ms).
- **Serialization size:** ~10.5 KB per instance (see Serialization section). Fine for quicksave. Sample itself lives in the sample pool with a slot-name reference; the trained state is independent.
- **Training on-device vs. host:** full offline SOM training might be too slow on am335x for long samples. Option: a fast one-shot approximation (random projection + k-means with ≤ 10 iterations) that we prove converges acceptably on representative material.
- **Binary-domain features on float audio:** the ER-301 has float samples, not int16. We can quantize to int16 for the binary feature pass, or hash float bit patterns directly. The latter gives us access to mantissa-level entropy which is _extremely_ on theme for "reading the hidden structure."
- **Stereo:** the matrix topology doesn't split naturally into L/R the way a filter pair did. Options: two matrix instances with detuned ratios (doubles cost), or split op levels into L/R pairs (cheaper; only `outLevels[op]` becomes per-channel), or run mono and let the user stack. Lean toward per-op L/R levels as the compromise.
- **CPU budget (native C++, 4-op):** ~60–80 cycles/sample on Cortex-A8 ≈ 3.5–4 MHz at 48 kHz. Block-boundary crossfade adds ~1.4 FLOPs/sample amortized. Well under budget — leaves headroom for the scanning-mixer richness, the viz, and multiple instances. See **Optimization & Implementation**.
- **How the viz communicates the scanning mixer:** visualizing crossfaded _routings_ (not just positions) is harder. Maybe a secondary ply shows active mod edges as animated lines between nodes, rendered only when that ply is focused.

## Build Order

1. **Port Xxxxxx as a 4-op Lua reference.** Into `mods/spreadsheet/` (or a dedicated pkg). Drop 2 operators, get the 4×4 matrix working with `libcore.SineOscillator` + GainBias graph. Pure Lua — no new DSP. This is the known-good listening reference, not the production voice.
2. **Native 4-op C++ voice unit.** `AlembicVoice.cpp/h` — 4 phase accumulators, sin LUT, 4×4 matrix inner loop, reagent waveshaper. SWIG-wrap, expose as `od::Object`. Prove it A/B-matches the Lua reference from step 1 on static presets. **This is the step most susceptible to hardware/emu divergence** — validate early, per the _Identical means IDENTICAL_ memory.
3. **Preset loader + crossfader.** C++ side: a 64-slot × 29-float preset table, K-weighted block-rate crossfade into the live voice state. Feed hand-authored presets (no training yet). Prove smooth scan between them.
4. **Strip training params** from a forked Som → Alembic skeleton. Freeze weights. Wire scan-node-path + K weights to feed the preset crossfader.
5. **New viz** (stratified chart, no per-pixel search, ≤500 pixel ops/frame). First thing the user interacts with; get it right before training complexity.
6. **Sample-slot wiring + difference-based sampling + offline feature extraction (audio-only).** Coarse-pass → farthest-point selection → 64-frame full feature extraction → Layer 0 + Layer 1 O0/O1 + mean-centered ratios. Smallest mapping that proves the pipeline end to end, with divergence sampling in place from day one (retrofitting uniform sampling → divergence sampling later would invalidate all cached state).
7. **Serialization.** Sample reference + Layer 0 vectors + preset cache + user biases. Quicksave/quickload round-trip must preserve trained character even if the sample pool entry is missing.
8. **Group fades.** wPitch, wStructure, wFeedback, wDynamics and their user-bias backing values.
9. **Ferment / Crucible / Reagent globals** (post-blend scalars).
10. **Order 2 + Order 3 derivations.** Compare audibly against the step-6 baseline; drop any order that doesn't earn its complexity.
11. **Multi-modal features.** Add binary-domain dims one at a time; iterative, spec deliberately left open.
12. **Sample-pointer excitation** (voice reads the trained sample as a node-offset modulator).
13. Polish, naming.

Each step is independently demoable and commit-sized. Steps 1–4 unblock everything else. Step 2 is the highest-risk technical bet — NEON-amenable C++ port must A/B-match the Lua reference on hardware under `-O3 -ffast-math`, or we're stuck. Step 7 (serialization) is scheduled early, before group fades, so all subsequent state lives in the serialized format from day one — no retrofitting.
