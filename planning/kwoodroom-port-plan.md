# kWoodRoom — port plan

Status: **Phase 0 + Phase 1 SHIPPED, validated on hardware 2026-06-04 (house 0.1.0.4)**. Per user: "it works and it sounds absolutely wonderful. CPU 29% idle in stereo. we'll come back for optimization later." Phases 4+ (NEON optimization, float-conversion of 3×3 trellis) deferred until a CPU regression bites.

**Locked 2026-05-31**: unit name = **kWoodRoom** (faithful AW port → keep upstream name), package = **new `house` package**. Renaming policy revisited 2026-06-03: AW faithful ports keep their AW names; recombined / original work gets habitat-native names. See `feedback_no_third_party_branding`.

## Result baseline (for later optimization comparison)

- **CPU**: 29% stereo at default params on Cortex-A8 (single instance)
- **Memory**: ~113 KB / instance (double-precision state arrays, per the original port plan)
- **Sound**: "absolutely wonderful" — user audition confirms the AW character ported faithfully
- **Hardware-validated**: first insert, first audio, no hang, no glitch
- **CPU headroom available**: per Phase 4 plan, NEON Householder reduction (`3*hX - total` identity) should drop the 6×6 portion ~4× → target ~12-15% stereo. Defer until needed.

## Goal

Land a low-CPU "small wooden room" algorithmic reverb as the
first unit in a new dedicated reverb package, ported from
Airwindows kWoodRoom. Distinct aesthetic from Network in the
spreadsheet package (heavy, glitch / spatial) — kWoodRoom is the
natural-room, drum-booth, glue-and-color end of the spectrum.

Selected over Galactic (the LOW-risk reference) because kWoodRoom
is the most aesthetically differentiated candidate in the
Airwindows shortlist; if it ports cleanly it directly fills the
"small wooden room" bucket that nothing else in the package
currently covers. Galactic and Galactic2 are queued as follow-on
units in the same `house` package once kWoodRoom ships — see
`planning/airwindows-reverb-research.md`.

The package name `house` plays on "housing structures" (rooms,
spaces, cathedrals, etc.) and stays habitat-themed. kWoodRoom,
Galactic-derived units, and any future room-sims naturally live
here.

## Source

- Repo: <https://github.com/airwindows/airwindows>
- Path: `Plugins/MacVST/kWoodRoom/source/{kWoodRoom.h, kWoodRoom.cpp, kWoodRoomProc.cpp}`
- Sources cached at `/tmp/kwoodroom/` from research pass.
- License: MIT (Airwindows is Chris Johnson, no per-function audit).
- AW faithful port → ships under upstream name `kWoodRoom`. The
  `feedback_no_third_party_branding` rule applies to recombined /
  original work that just borrows AW mechanics; faithful ports
  preserve the original name as attribution.

## Topology (verified from source)

Per side, the signal chain is:

```
input
  → Bezier-undersample accumulator (bez[])
  → 3×3 early-reflection trellis (3 stages, 9 delay lines)
  → 6×6 nested-Householder reverb trellis (6 stages, 36 delay lines)
  → Bezier-undersample filter stage (bezF[])
  → 1-pole IIR smoother
  → + earlyReflection * earlyLoudness
  → wet/dry mix
  → output
```

Cross-feedback between L and R inside the 6×6: L's stage-6 output
becomes R's input feedback; R's stage-6 output becomes L's
feedback. Serial chain, not parallel — kills the "L+R in two NEON
quads" idea. The win has to come from vectorizing across the 6
delay lines within one side's layer (see NEON strategy below).

The 3×3 is per-side independent. Its 9 delay lengths are chosen
from a 36-entry `early[]` table indexed `start..start+8` where
`start = (int)(positionParam * 27)` — 27 discrete starting
positions, so param E (Position) is steppy on automation.

Outer Bezier `bez[bez_cycle]` is the load-bearing "derez"
mechanism: every input sample bumps `bez_cycle`; when it crosses
1.0, a "reverb sample" fires and the full 6×6 trellis advances.
Between fires, output is a quadratic Bezier interpolation across
the three most recent reverb outputs, then IIR-smoothed.

## State arrays (per channel)

| Group | Lines | Element type | Bytes / side | Notes |
|---|---|---|---|---|
| 3×3 trellis `a3{A..I}{L,R}` | 9 | double | ~47.5 KB | max sizes 499..832+5 |
| 6×6 trellis `a6{A..Y, ZA..ZK}{L,R}` | 36 | double | ~9.2 KB | many lines tiny (d6S=0, d6Y=3, d6T=8) |
| Counters `c3*, c6*` | 90 ints | int | ~360 B | |
| Feedback floats | 14 | double | 112 B | |
| `bez[11]`, `bezF[11]` | 22 | double | 176 B | |
| `fpdL/R` | 2 | uint32_t | 8 B | dither RNG (drop on port) |
| Params A..F | 6 | float | 24 B | |

**Total ≈ 113 KB / instance** with the original `double`-based
arrays. Two cheap optimizations get us down to ~66 KB:

- Drop the 3×3 `a3*` arrays from `double` to `float` (Householder
  is sum-preserving so accumulation error stays bounded). Saves
  ~24 KB / instance. The 6×6 stays `double` until profiling
  proves otherwise — it's the heart of the algorithm and the
  computation is short-tailed enough that the precision matters.
- Optionally also `float` the 6×6 (saves ~5 KB, marginal). Defer
  to listening-test after first port.

## Public parameters

| Plugin name | Range | Default | Mapping (ER-301 ply) | Effect |
|---|---|---|---|---|
| A "Regen" | 0..1 (φ-curved internally) | 0.5 | `Regen` | 6×6 feedback scale; `reg6n = (1 - (1-A)^φ) * 1.302e-3` per tap. Max effective ≈ 7.8e-3 across 6 taps. |
| B "Derez" | 0..1 | 0.5 | `Time` (or `Rate`) | Outer Bezier undersample rate. Includes `/overallscale` so it's SR-aware. 0.5 = full-rate; lower = stepped, higher = smooth. |
| C "Filter" | 0..1 | 0.25 | `Tone` | Inner Bezier+IIR rate inside reverb cycle. |
| D "EarlyRF" | 0..1 | 0.5 | `Reflect` | Early-reflection re-add gain; `earlyLoudness = D²`. |
| E "Positin" | 0..1 (steps to 27) | 0.75 | `Position` | 3×3 delay-length window. 27 discrete positions. |
| F "Dry/Wet" | 0..1 | 0.5 | `Mix` | Output dry/wet. |

All 6 are continuous biases on the ER-301 side via the standard
GainBias / ParameterAdapter pattern. Position (E) is internally
quantized to 27 steps so a stepped-integer DialMap on its readout
is appropriate.

## Per-sample work estimate

Two regimes:

**Every input sample (~always-on):**
- 35–50 FLOPs (Bezier reconstruction, IIR smoother, wet/dry)
- Drop the per-sample `pow(2, expon+62)` dither — ER-301 doesn't
  need it on the internal float bus. Saves the `frexpf + pow`.

**Every reverb-cycle hit (fires once per `1/derez` input samples,
i.e. once per sample at B=0.5, once per 64 samples at B=0):**
- ~600 FLOPs original (54 for the 3×3, ~500 for the 6×6)
- ~90 counter-advance branches
- ~90 delay-line reads — all fixed-tap (NEON-friendly)

At default rate (B=0.5), cycle fires every sample → ~640
FLOPs/sample total before NEON optimization. With Householder
NEON reduction (see strategy), the 6×6 portion drops ~6× to
~80 FLOPs, total ~200 FLOPs/sample. At 48 kHz mono that's
~10 MFLOPs/sec — comfortably ~3-5% CPU on Cortex-A8.

Stereo cost is **the same** as mono, because the cross-feedback
chain means one cycle processes both L AND R in series. Excellent
stereo CPU profile.

## NEON strategy (the headline win)

The 6×6 Householder reflection per output is

```
out[X] = 2*h[X] - sum(others) = 2*h[X] - (total - h[X]) = 3*h[X] - total
```

where `total = h[A] + h[B] + h[C] + h[D] + h[E] + h[F]`. This
reduces the original 7-op per-line work (`2*hX - hA - hB - hC -
hD - hE - hF` with the X term skipped, ~6 adds + 1 mul) to a
single multiply-subtract per line, plus one shared sum per layer.

Per-layer cost goes from `6 lines × 7 ops = 42 ops` to
`1 sum (~6 adds) + 6 fmsub (~12 ops) ≈ 18 ops` — a 2.3× per-layer
win, ~4× across the full 6×6.

Implementation: pad 6 → 8 lines (2 zero lines) to fit two NEON
quads. Compute `total = sum across both quads`, broadcast,
multiply each line by 3.0f, subtract `total`. The padded zero
lines stay zero (sum unchanged, output zero).

Per `feedback_neon_voice_bus_template` / `feedback_neon_soa_svf_bank`,
the 6-wide → 8-wide pad + AoS → SoA reorganization is the
standard habitat NEON pattern. Apply here.

Counter-wrap branches (90 per cycle) are the next bottleneck.
Powers-of-two delay sizes would let us use bitmask wrapping but
the actual sizes aren't power-of-two. Keep the branchy form
(`c++; c -= (c > d) ? (d+1) : 0;`) but ensure the six counters
in a layer all advance identically (the existing code already
does this).

## CloudSeed-trap audit (verdict: clean)

Verified against source:

- **No `if (firstFrame)` guards.** First sample's behavior is
  controlled by deliberate `bez[bez_cycle] = 1.0` and
  `bezF[bez_cycle] = 1.0` init (lines 86-87 of kWoodRoom.h) which
  forces the first reverb cycle to fire so output is non-zero.
  This is load-bearing — preserve in the port.
- **No allocations or resizes after constructor.** All state is
  fixed-size class members initialized in the ctor body with
  explicit loops.
- **No init-order dependencies.** ctor body sets every counter to
  1 (not 0) — the read formula returns `arr[0]` initially, safe.
- **No host-API or threading dependencies.** `processReplacing`
  reads `getSampleRate()` per-block but only feeds it into the
  derez calculation; nothing else SR-dependent.
- **No `std::vector` resizes.** All arrays are fixed-size C arrays.

The structural patterns that flagged CloudSeed (ModulatedAllpass
chains, dynamic state vectors, per-sample multi-way switches) are
all absent here.

## NEON / Cortex-A8 audit

- **No modulated delay reads.** Every read in the source uses
  `arr[c - ((c > d) ? d+1 : 0)]` which, given the post-increment
  wrap, reduces to `arr[c]` with `c ∈ [0, d]`. Single fixed index,
  monotonically advancing. NEON-friendly.
- **All state arrays are class members** (heap-allocated) — safe
  from the stack-local `vld1 :64` trap documented in
  `feedback_neon_intrinsics_drumvoice`.
- **No runtime-branched DSP dispatch.** All six lines in a layer
  do identical work each cycle (the existing source is already in
  this shape — preserve it). The `c > d` branch is a counter wrap,
  not a topology dispatch.
- **Package-side `pow()` for `reg6n` recompute** at block-rate —
  cheap, fine.
- **am335x already enforces `-fno-tree-vectorize`** package-wide
  per `feedback_disable_tree_vectorize_am335x`, so auto-vec
  alignment traps are pre-empted at the build-flag level. We use
  explicit NEON intrinsics for the Householder reduction.

## Implementation phases

### Phase A — Scaffold the `house` package

The smoke-test harness needs somewhere to live. Stand up the new
package first.

1. `mods/house/` directory tree:
   - `mods/house/mod.mk` — copy from `mods/scope/mod.mk` (simplest
     existing template, no submodule deps), set `PKGNAME=house`,
     start `PKGVERSION=0.1.0.1` (4th-digit dev iteration toward
     eventual `0.1.0` first release).
   - `mods/house/assets/init.lua` — boilerplate `Library` subclass,
     identical shape to `mods/scope/assets/init.lua`.
   - `mods/house/assets/toc.lua` — `units = { }` initially empty;
     fill in kWoodRoom entry in Phase 2.
   - `mods/house/house.cpp.swig` — `%module house_libhouse` skeleton
     mirroring `mods/scope/scope.cpp.swig`, no `%include` lines yet.
2. Top-level `Makefile`: append `house` to `PROJECTS = mi kryos
   peaks scope spreadsheet biome catchall porcelain` → `... house`.
3. Sanity build: `make house ARCH=am335x` and `make house` should
   both produce `house-0.1.0.1.pkg` with no Lua units inside,
   just the boilerplate `init.lua` + `toc.lua` + empty
   `libhouse.so`.
4. README.md package table: add a `**house**` row mentioning
   kWoodRoom as the first unit (or defer until kWoodRoom ships).
5. `clean-sd-packages.sh` and `install-packages.sh` auto-discover
   packages from `testing/<arch>/*.pkg`, so no script edits needed.
6. Per `feedback_release_asset_defaults`, the new `house` package
   should be added to the default upload set once the first unit
   ships (will need a memory bump at release time).

### Phase 0 — Hardware smoke-test harness (do this FIRST)

Per the CloudSeed lesson, we don't want to find out about a
first-frame hang after building a full unit wrapper. Build a
**minimal** harness inside the freshly-scaffolded house package:

1. Stand up a bare `house::KWoodRoomDSP` C++ class in
   `mods/house/KWoodRoomDSP.h` (header-only, scratch — easier to
   iterate than splitting now) with the kWoodRoom constructor +
   `process(float *inL, float *inR, float *outL, float *outR,
   int frameLen)` method.
2. Create a minimal `house::Smoketest` `od::Object` subclass with
   a single `In`/`Out` and a `process()` that:
   - On first call: constructs a `KWoodRoomDSP` on the heap, runs
     it for 100 blocks with random noise input, checks output is
     non-zero and finite at each step, logs pass/fail via
     `od::logInfo`, then deallocates.
   - On subsequent calls: passthrough.
3. Register Smoketest in `toc.lua` + `house.cpp.swig`.
4. Build, install, insert on hardware, check log.

**Hard gate**: do not advance to Phase 1 until the smoke test
returns clean on hardware. Budget: half a session. If it hangs,
bisect the topology (3×3 alone, then add 6×6 layers one at a
time) — same approach we used on Larets stereo's per-channel
state isolation.

### Phase 1 — Bare DSP class compiles + first-output

- Promote `KWoodRoomDSP.h` from the Phase-0 scratch into the
  canonical `mods/house/KWoodRoomDSP.{h,cpp}` split if the file's
  grown unwieldy; otherwise leave header-only.
- Drop VST host dependencies (`audioeffectx.h`, `<set>`, `<string>`).
- Convert `processReplacing` body to a clean `process()` that
  takes float buffers + frame length.
- Convert 3×3 arrays from `double` → `float`. Keep 6×6 as `double`
  for now.
- Drop the per-sample dither.
- Default all 6 params to their plugin defaults so we can sanity-
  check first sound without UI work.
- Build for am335x, install, run smoke test from Phase 0,
  confirm "reverb-shaped" output.

### Phase 2 — Minimal Lua wrapper

- New unit: `kWoodRoom` in `mods/house/`.
- C++ `od::Object` subclass `house::KWoodRoom : public od::Object`
  in `mods/house/KWoodRoom.{h,cpp}`. The DSP itself lives in the
  `KWoodRoomDSP` helper from Phase 1; kWoodRoom's `process()` is a
  thin shim.
- 2 inlets (`In L`, `In R`), 2 outlets (`Out L`, `Out R`), no
  parameters yet — DSP runs at defaults.
- Lua wrapper with standard channel-count branching pattern
  (`if channelCount > 1 then connect R`). For a stereo-only
  reverb, mono insert just sums L to internal L+R input and the
  internal cross-feedback handles the rest.
- toc.lua entry. Verify it inserts cleanly and produces audible
  reverb on hardware.

### Phase 3 — Full parameter surface

- Add 6 `od::Parameter` members (Regen, Derez, Tone, Reflect,
  Position, Mix) matching the plugin's A..F.
- Wire through `od::ParameterAdapter` per habitat convention
  (cf. Pecto, Petrichor for pattern reference).
- Lua: 6 GainBias-style ply controls with appropriate maps
  (Position needs an integer-stepped DialMap for the 27 discrete
  positions; others are 0..1 linear).
- Verify each param does what's expected on hardware. Listen-test
  the parameter space.

### Phase 4 — NEON optimization (Householder reduction)

- Refactor the 6×6 per-layer work to use the `3*hX - total`
  identity.
- Pad 6 lines → 8 (SoA, 2 NEON quads).
- Use `vmlsq_n_f32` for the multiply-subtract.
- Verify clean output (RMS A/B against pre-NEON build on the
  linux emu).
- Run `tools/check-neon-hints.sh` on the spreadsheet swig wrapper
  + the unit .o.
- CPU profile: expect ~3-5% per instance.

### Phase 5 — Serialization

- All 6 params via `ParameterAdapter` Bias round-trip per the
  `feedback_serialize_deserialize_pattern` template.
- No options (yet — Position quantization is internal, not a
  user-facing toggle), so no `enableSerialization` work on the
  C++ side beyond what GainBias auto-does.

### Phase 6 — Stereo + cross-feedback verification

The cross-feedback IS the stereo mechanism — there's no separate
stereo / mono switch. On a mono chain, only Out1 is wired (L);
the R-channel computation still runs and contributes to L's
feedback. CPU is the same.

- Verify: mono insert produces stable output (R-side state
  doesn't drift or starve since cross-feedback is symmetric).
- Stereo insert: confirm L and R outputs differ (the cross-feed
  ensures they're not identical even with identical input — the
  layer ordering is reversed between sides).

### Phase 7 — Viz (defer / minimal)

This is a utility reverb; not every unit needs an elaborate viz.
First-cut: bare standard ply layout, no custom viz. If a viz
makes sense, candidates:

- Single delay-time bar showing the 27-position lookup (E param)
- Decay-tail envelope-follower trace
- Or: nothing. Free up the screen.

Defer to user decision post-Phase 6.

### Phase 8 — Polish + ship

- Hardware smoke test: insert / delete / quicksave / re-insert /
  param walk / extreme-edge automation.
- Listen-test against reference Airwindows recordings if any.
- Update README package table with the `house` row + kWoodRoom
  description.
- Remove the Phase-0 Smoketest unit from toc.lua (keep the C++
  helper code dormant in the repo for the Galactic / Galactic2
  ports; not exposed via the unit picker).
- Bump house version from dev `0.1.0.x` → release `0.1.0`.
- Update `feedback_release_asset_defaults` to add `house` to the
  default upload set.
- Release notes entry in the next habitat release pass.

## Risk audit

| Risk | Mitigation |
|---|---|
| First-frame hang on Cortex-A8 (CloudSeed pattern) | Phase 0 smoke-test gate before any wrapper work |
| `pow()` per-sample dither hangs | Drop dither entirely (Phase 1) |
| `double`-heavy state causes register spill | Convert 3×3 to float immediately (Phase 1); profile 6×6 stays-double vs goes-float |
| NEON `:64` alignment traps from explicit Householder intrinsics | All state on heap (class members), per the safe pattern. Run `tools/check-neon-hints.sh` after Phase 4. |
| Counter-wrap branches kill speculative execution | Keep all 6 lines in a layer doing identical work each cycle; the source already does this |
| Param E (Position) is steppy on automation | Accept as-is for first ship; user feedback can drive a smoother later |
| 8% delay-line shorter at 48 kHz vs 44.1 kHz | Accept the slightly-tighter character |
| Insert crash from SWIG wrapper class-layout change | Use the all-inline-virtuals pattern for any custom Graphic (likely none needed for first ship); SWIG dep-tracking in mod.mk handles header changes |

## Locked decisions (2026-05-31)

1. **Unit name: kWoodRoom**.
2. **Package home: new `house` package** (not spreadsheet). Plays
   on "housing structures" — rooms, halls, cathedrals — and gives
   the eventual Galactic / Galactic2 / kCathedral / kGuitarHall
   ports a natural home together. Habitat-themed naming.
3. **toc category: "House"** — match the package name as the
   picker category, same convention as biome/spreadsheet/scope.

## Still open (need answer before Phase 7 / 8)

4. **Phase 7 viz**: skip entirely, or include a minimal
   delay-time / decay-tail indicator? Default I'll assume:
   **skip**. kWoodRoom is a utility reverb; the existing standard
   ply controls are sufficient.

5. **Post-kWoodRoom port queue** (revised 2026-06-02 after handoff
   integration — see
   `planning/airwindows-reverb-research.md` addendum):
   1. **WoodenBox** (replacing Galactic2 as next pick) — filed
      under AW's "Tone Color" not Reverb, smaller and cheaper
      than a k-verb, lowest-risk port in the catalog. Small
      wooden tone-shaper rather than a real space.
   2. **CreamCoat** — proves the divisor + Bezier mechanic in
      isolation; kWoodRoom already implements this pattern via
      kWoodRoom's outer Bezier, so CreamCoat is the canonical
      reference.
   3. **BrightAmbience3** — gated bright halo. **Use the "3",
      not the original BrightAmbience** (the original is
      genuinely CPU-hungry naive-prime-tap).
   4. **Galactic** — fourth, the lush option.
   5. **Verbity** — fifth, the feedforward-with-one-feedback
      spanner (slapback through infinite tail on one knob).
   6. Originals (XYZ engine, RotCoat) tracked in
      `planning/xyz-engine-design.md` and
      `planning/rotcoat-design.md`. RotCoat is the
      stronger pick for first original — structurally
      AM335x-friendly by construction.

## Related memories / docs

- `planning/airwindows-reverb-research.md` — the per-candidate
  ranking that led to this pick
- `feedback_no_third_party_branding` — habitat-native naming
- `feedback_neon_voice_bus_template` — the SoA NEON pattern for
  the Householder reduction
- `feedback_neon_soa_svf_bank` — sister NEON pattern
- `feedback_disable_tree_vectorize_am335x` — package-wide
  `-fno-tree-vectorize` already in place, pre-empts auto-vec
  alignment traps
- `feedback_no_out_of_line_virtuals` — if a custom Graphic is
  added in Phase 7
- `feedback_identical_means_identical` — the lesson from scope's
  warmup-counter bug; preserve `bez_cycle = 1.0` first-frame init
  literally, don't "improve" the source's control flow
- `feedback_stereo_pattern_selection` — cross-feedback makes this
  effectively internal-stereo (Pattern B) by construction; no
  Pattern A alternative exists
- `feedback_no_lazy_paths` — if a phase hits a wall, diagnose the
  mechanism (CloudSeed-style), don't strip features
