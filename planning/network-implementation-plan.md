# Network — implementation plan (catchall spatial effect)

## Context

The user has a design brief (`planning/refs/spatial-effect-brief.pdf`) for a non-traditional reverb / "macro spatial simulation" — same network being structurally reconfigured between glitchy/sparse and lush/dense, no mode switching. Three candidate topologies were surveyed in `planning/spatial-effect-scoping.md`; the hybrid (selective-feedback multi-tap fed by a virtual reflector geometry, with a single shared listener-motion parameter that translates / rotates the field coherently) was selected as the target in `planning/spatial-effect-hybrid.md`.

CPU expectations were recalibrated against Pecto, which is the closest existing analog in this repo (24-tap NEON multi-tap comb, 12-20% stereo on AM335x). The hybrid lifts Pecto's NEON infrastructure verbatim (3-pass tap gather, LinearRamp Doppler smoother, idx-wrap ulp guard) and adds a geometry-derived per-tap parameter generator on top, sparse selectable feedback recycling, and stereo via per-tap L/R weights. The "soften" diffusion stage reuses Mutable's MIT-licensed `FxEngine` template that's already in `eurorack/`.

The unit lands in **catchall** (alongside Alembic / Lambda / Flakes / Sfera / Som — the experimental tier) and is named **Network** (instrumental name reflecting the multi-tap field structure with selectable feedback connectivity).

User-confirmed decisions:
- **Name**: Network
- **Phase 0 tap count**: 32 taps (straight to MVP target, skip the 24-tap Pecto-equivalent A/B step)
- **Stereo strategy**: shared idx/frac arrays + per-tap L/R weights (one Pass A/B, two FMAs per tap in Pass C)

## I/O and ply layout

**I/O**: mono in → stereo out (Pecto / Petrichor pattern).
- `od::Inlet mIn{"In1"}`
- `od::Outlet mOut{"Out1"}` + `od::Outlet mOut2{"Out2"}`

**Main view (7 plies, left to right):**

| # | Ply | Type | Range | Purpose |
|---|---|---|---|---|
| 1 | **density** | GainBias + CV | 0..1 | Active reflector fraction. Primary glitch↔lush axis. |
| 2 | **size** | GainBias + CV | 0..1 | Field scale → max tap delay (within the config-selected `max delay range`). |
| 3 | **motion** | GainBias + CV | 0..1 | Listener position along motion path. CV here = orbiting/sweeping space gesture. |
| 4 | **connectivity** | GainBias + CV | 0..1 | Fraction of active taps recycling into feedback. Plexus-flavor morph. |
| 5 | **decay** | GainBias + CV | 0..1 | Feedback gain scaler. Tail length. |
| 6 | **soften** | GainBias + CV | 0..1 | FxEngine diffusion send. Smoothness. |
| 7 | **wet** | GainBias + CV | 0..1 | Dry/wet mix. |

All plies follow the standard paramMode convention (`feedback_parammode_convention`) — encoder sets bias, shift-hold + encoder sets CV gain on the same ply.

**Config menu (header-hold):**
- `seed` (uint32 with "Randomize" button) — reflector field regeneration
- `tap allocation policy` — closest-first / random-subset / energy-weighted (Phase 1)
- `motion path shape` — circular / linear / figure-8 (Phase 1)
- `fb selection policy` — every-N when sorted / distance-weighted / random (Phase 2)
- `max delay range` — short ≈100ms / medium ≈400ms / long ≈1s; sets `MAX_TAP_DELAY_SAMPLES`

Comparable to Pecto's ply count and JF's main page. Morph-able / CV-able controls top-level; set-once topology choices in config.

---

## Phase 0 — 32-tap mono baseline

**Goal.** Stand up Network with Pecto's 3-pass NEON gather + Doppler smoother driving 32 taps at random fixed normalized positions. Validates the infrastructure transfer.

**Files created.**
- `mods/catchall/Network.h` — header-only inline class (per `feedback_no_out_of_line_virtuals`); subclasses `od::Object`. Holds `kMaxTaps = 64`, `int mActiveTaps = 32`, int16 delay buffer (`int16_t mBuf[maxDelay]` matching Pecto.cpp:550-551), `int mWriteIndex`, `float mSmoothedBaseDelay`, `float mTapPosition[kMaxTaps]`, `float mTapWeight[kMaxTaps]`. Single `process()` virtual, fully inline.
- `mods/catchall/assets/Network.lua` — minimal Unit with mono `In1`, `Out1` (mono out for Phase 0), and the placeholder ply set: `size`, `decay`, `wet` only (other plies stub-return defaults until later phases). Pattern from `mods/catchall/assets/Sfera.lua`.

**Files edited.**
- `mods/catchall/catchall.cpp.swig` — add `#include "Network.h"` and `%include "Network.h"` (mirror existing Sfera/Lambda/Alembic includes).
- `mods/catchall/assets/toc.lua` — add `{ title = "Network", moduleName = "Network", category = "Effect", keywords = "reverb, multitap, delay, spatial, network" }`.

**Architecture.** Direct lift of Pecto's process() skeleton, replacing the comb-density tap pattern with seeded random positions in [0,1]. Block-rate setup: clamp macros, NaN-guard `baseDelay`, seed `mSmoothedBaseDelay` on first process. Per-sample loop: one-pole base-delay smoother (Pecto.cpp:570-574) → Pass A NEON idx compute with idx0 ulp guard (Pecto.cpp:610-611, 626) → Pass B scalar gather with 8-ahead prefetch (Pecto.cpp:653-660) → Pass C NEON combine with shared `mTapWeight[t]` (Pecto.cpp:670-696) → single global feedback gain on `lastTapOut` (Pecto.cpp:710-711).

**Code references / lifts.**
- `mods/spreadsheet/Pecto.cpp:486` — smoothAlpha = `1/(0.025 * sr)` Doppler time constant.
- `mods/spreadsheet/Pecto.cpp:489-491` — first-process seed of smoothed scalar.
- `mods/spreadsheet/Pecto.cpp:560-574` — write-index + per-sample smoother step.
- `mods/spreadsheet/Pecto.cpp:577-633` — Pass A NEON idx compute with ulp guard.
- `mods/spreadsheet/Pecto.cpp:650-660` — Pass B scalar gather + prefetch.
- `mods/spreadsheet/Pecto.cpp:666-696` — Pass C NEON combine.
- `mods/spreadsheet/Pecto.cpp:710-711` — single-gain feedback (placeholder for Phase 0; replaced in Phase 2).
- `mods/catchall/Lambda.h` — header-only class skeleton precedent.

**Verification.**
- `make ARCH=linux PKGNAME=catchall` builds clean.
- `make ARCH=am335x PKGNAME=catchall` builds clean; objdump shows zero `:64`/`:128` NEON hints in Network symbols.
- Loads on hardware, audible 32-tap delay with believable density.
- `size` sweep: smooth Doppler glide, no zipper. CPU at 32 taps mono **target 8-13%** (Pecto-mono scaled by 32/24).

---

## Phase 1 — Geometry generator + per-tap (delay/gain/pan) + stereo

**Goal.** Replace random tap positions with a 2D reflector-field geometry, derive `(delay_target, gainL, gainR)` per tap from listener position, produce stereo output. Add the `density` and `motion` plies as live controls. Add the `seed`, `tap allocation policy`, and `motion path shape` config-menu items.

**Files created.**
- `mods/catchall/network/geometry.h` — header-only. Owns `Reflector { float x, y; }` array, deterministic LCG seeding (lift from `mods/spreadsheet/visadhara/pmm.h`'s LCG pattern), `regenerateField(uint32_t seed)`, `recomputeTaps(density, size, motion, listener_x, listener_y, allocPolicy, ...)` writing into externally-owned `tapDelayTarget[N]`, `tapGainL[N]`, `tapGainR[N]`, `tapActive[N]` arrays. Default allocation policy at low density: closest-first sort.
- `mods/catchall/network/trig_lut.h` — header-inline `poly_sin`/`poly_cos` (lift Bhaskara from `mods/spreadsheet/visadhara/morph.h:25-42`); add `poly_atan2` via range-reduce + Bhaskara-style 1st-octant rational. Per `feedback_package_trig_lut` — no libm trig.

**Files edited.**
- `mods/catchall/Network.h` — add `Reflector mReflectors[kMaxTaps]`, `mTapDelayTarget[kMaxTaps]`, `mTapGainL[kMaxTaps]`, `mTapGainR[kMaxTaps]`. Replace single per-tap weight with split L/R weights. Replace `currentBase * tapPosition[t]` with absolute `tapDelayTarget[t]` (size scaling folded into the precomputed array). Add `od::Outlet mOut2`. Add `density`, `motion`, `seed` parameters.
- `mods/catchall/assets/Network.lua` — add `Out2`, `density` ply, `motion` ply; populate the config menu with `seed`, `tap allocation policy`, `motion path shape`.

**Architecture.** Block-rate `recomputeTaps()` runs only on `(density|size|seed|alloc-policy|motion-path-shape)` dirty (Pecto.cpp:460-463 dirty-check pattern). Motion is *not* dirty-checked — it slews via the LinearRamp at sample rate, but the reflector geometry stays fixed (the listener moves through it). Per active reflector: `dx = ref.x - listener.x; dy = ref.y - listener.y; dist = sqrt(dx² + dy²); az = poly_atan2(dy, dx); tapDelayTarget[i] = size * dist_normalized * MAX_TAP_DELAY_SAMPLES; gain = 1/max(dist, MIN); tapGainL[i] = gain * 0.5*(1 - poly_sin(az)); tapGainR[i] = gain * 0.5*(1 + poly_sin(az))`.

Per-sample: keep ONE shared LinearRamp on `motion` (per `feedback_doppler_basedelay_smoother`). `mSmoothedMotion += (motionTarget - mSmoothedMotion) * smoothAlpha`. Listener position derived from the smoothed motion via the configured path shape. The shared LinearRamp keeps the *whole field's* slew coherent.

Pass C (per user-confirmed shared-idx/frac strategy): two FMAs per tap — `wetL = vmlaq_f32(wetLVec, tapV, gainLVec); wetR = vmlaq_f32(wetRVec, tapV, gainRVec)`. ~5-10% relative add over mono Pass C.

**Code references / lifts.**
- `mods/spreadsheet/Pecto.cpp:460-463` — block-rate dirty-check.
- `mods/spreadsheet/Pecto.cpp:489-491` — smoother seed.
- `mods/spreadsheet/visadhara/morph.h:25-42` — Bhaskara poly_sin reference.
- `mods/spreadsheet/visadhara/pmm.h` — LCG pattern for deterministic seeding.
- `mods/catchall/AlembicSphereGraphic.h:43-77` — in-package trig LUT precedent.
- `mods/spreadsheet/MultitapDelay.cpp` — per-tap pan plumbing reference.

**Verification.**
- Sweep `motion` → audible coherent rotation of the field; all taps shift together (LinearRamp upstream of geometry → no per-tap zipper).
- Snap `seed` → field re-randomizes cleanly; ~5ms crossfade on per-tap activation handles new-tap pop.
- Stereo image non-trivial — toggle `motion` and confirm L/R energy redistributes.
- Hardware CPU 32 taps stereo: target **16-22%**.
- Listening A/B vs Phase 0: distinctly geometric, not random.

---

## Phase 2 — Selectable sparse feedback recycling

**Goal.** Replace the single-gain feedback path with a sparse per-tap weighted recycle. Add `connectivity` ply and `fb selection policy` config option.

**Files created.** None.

**Files edited.**
- `mods/catchall/Network.h` — add `mFbWeight[kMaxTaps]`. Per-block in `recomputeTaps()`: select `k = round(connectivity * activeTaps)` taps according to selection policy. Normalize weights by `1/sqrt(k)` then scale by `decay`. Clamp `sum(|fb_weight|) ≤ 0.95` for stability. Per-sample: third NEON FMA pass on the per-tap `tapV` 4-vectors against `mFbWeight` to produce `fb`. Replace `lastTapOut * fbNorm` with `fb`.
- `mods/catchall/assets/Network.lua` — add `connectivity` ply; populate `fb selection policy` config option.

**Architecture.** Genuinely new vs Pecto, which uses `lastTapOut * fbNorm` (Pecto.cpp:710-711). One additional `vmlaq_f32` per 4-tap iteration (≈+5-10% relative cost). Stability mitigations: `1/sqrt(k)` normalization + post-decay clamp + per-tap fb_weight crossfade on activation. Branchless: zero-fb_weight taps just contribute zero to the sum — no per-sample dispatch branch (per `feedback_runtime_branched_dsp_dispatch`).

**Code references / lifts.**
- `mods/spreadsheet/Pecto.cpp:710-711` — Pecto's single-gain feedback (the pattern we're *replacing*).
- `mods/spreadsheet/Pecto.cpp:556-559` — read-before-write order; preserve.

**Verification.**
- Sweep `connectivity` 0→1 with `decay` at 0.7 → audible morph from "early reflections only" to "dense recirculation," no instabilities.
- Edge cases: `connectivity=1, decay=1` clamps gracefully (no NaN). `connectivity=0` reverts to direct-tap-only (Phase 1 sound).
- Hardware CPU 32 taps stereo with selectable fb: target **18-25%** (MVP envelope).

---

## Phase 3 — Soften (FxEngine) + 2D field viz + polish

**Goal.** Add an optional global diffusion stage using Mutable's `FxEngine` (~8KB instance), ship a custom 2D field visualization, and tune for release. Add `soften` ply and `max delay range` config option.

**Prerequisite (BEFORE coding):** `mods/catchall/mod.mk` does **not** currently include the `eurorack` path. Add `EURORACK = eurorack` and `$(EURORACK)` to `INCLUDES`, mirroring `mods/spreadsheet/mod.mk:6,33`. Without this, FxEngine can't be linked from `eurorack/rings/dsp/fx/`.

**Files created.**
- `mods/catchall/network/soften.h` — header-only wrapper around `FxEngine<8192, FORMAT_12_BIT>` with a 3-4-allpass diffusion network. Pattern: simplified mirror of `eurorack/rings/dsp/fx/reverb.h` with much smaller buffer.
- `mods/catchall/NetworkFieldGraphic.h` — header-only inline (per `feedback_no_out_of_line_virtuals`); renders the 2D reflector field as dots, listener as moving cursor, fb-weighted taps highlighted. Lift skeleton from `mods/catchall/AlembicSphereGraphic.h`.

**Files edited.**
- `mods/catchall/mod.mk` — add `EURORACK = eurorack`, add `$(EURORACK)` to `INCLUDES`. Bump `PKGVERSION` to dev iteration.
- `mods/catchall/Network.h` — own a `Soften` instance; per-sample mix `outL = (1-soften)*wetL + soften*softenedL` (and same for R) — branchless arithmetic blend. Optional: hard-bypass FxEngine block-rate when `soften == 0` block-constant.
- `mods/catchall/catchall.cpp.swig` — add `#include "NetworkFieldGraphic.h"` and `%include` for it.
- `mods/catchall/assets/Network.lua` — wire `NetworkFieldGraphic` into the main view; add the `soften` ply; populate the `max delay range` config option.

**Architecture.** FxEngine receives `(wetL+wetR)*0.5` summed input, runs 3 allpass + 1 LP diffusion network, produces softened L/R pair. Linear blend with the soften scalar. Buffer is class member (heap-allocated) per `feedback_neon_intrinsics_drumvoice`.

**Code references / lifts.**
- `eurorack/rings/dsp/fx/fx_engine.h` (301 lines) — Context API: `Read/Write/WriteAllPass/Lp`.
- `eurorack/rings/dsp/fx/reverb.h` (184 lines) — diffusion network composition reference.
- `mods/mi/rings/dsp/part.cc:40-57,554-559` — reverb buffer allocation + per-block parameter set.
- `mods/catchall/AlembicSphereGraphic.h` — header-only inline graphic precedent.

**Verification.**
- Sweep `soften` 0→1 → smooth airy diffusion add, no stepping.
- Insert/remove unit repeatedly with no crash (the AlembicSphereGraphic crash precedent at `AlembicSphereGraphic.h:17-23` is the canonical regression-watch).
- Hardware CPU worst case (32 taps stereo, soften=1, connectivity=1, decay=0.85): target **≤25%**, hard cap **28%**.
- Lint pass: `tools/check-graphic-virtual-defs.sh` clean; objdump shows zero NEON `:64`/`:128` hints in Network/NetworkFieldGraphic; no `sinf`/`cosf`/`atan2f` in Network or its helpers.

---

## Phase 4 — Test, vanilla-compat, version bump, release

**Goal.** Lock Network down for shipping in catchall.

**Files created.**
- `planning/network-test-procedure.md` — step list mirroring `planning/jf-initial-pass.md` style. Load preset, confirm CPU envelope, NaN-safe rapid macro sweeps, seed-randomize stress, mono-summed monitor stereo-collapse check.

**Files edited.**
- `mods/catchall/mod.mk` — final `PKGVERSION` bump (likely 0.3.1 → 0.4.0; minor bump for new unit).
- `mods/catchall/assets/toc.lua` — finalize Network keyword list.
- Release notes (in `planning/release-vX.Y.Z-{bbcode,discord,github}.md` style when next release is cut).

**Verification.**
- Hardware burn-in: 30 min continuous macro modulation through audio, no clicks, no NaN-poisoning, no CPU drift.
- Cold-load on stock ER-301 firmware: works (FxEngine is upstream Mutable, not Habitat-specific; trig is in-package; no Habitat-firmware-only API calls).
- Both Linux and AM335x builds clean. `tools/check-graphic-virtual-defs.sh` clean.

---

## CPU summary, anchored to Pecto

| Phase | Configuration | Target (AM335x) | Pecto anchor |
|---|---|---|---|
| 0 | 32 taps mono, single-gain fb | **8-13%** | Pecto-mono scaled by 32/24 |
| 1 | 32 taps stereo, geom-derived per-tap pan/gain | **16-22%** | Pecto-stereo scaled to 32 taps + per-tap-gain adder |
| 2 | 32 taps stereo, sparse fb recycle | **18-25%** | Phase 1 + per-tap fb FMA |
| 3 | 32 taps stereo + 8KB FxEngine soften | **22-28%** | Phase 2 + Mutable diffusion overhead |
| 4 | Same as Phase 3 (no DSP changes) | **22-28%** | Lock target |

## Critical files

**To create:**
- `mods/catchall/Network.h`
- `mods/catchall/network/geometry.h` (Phase 1)
- `mods/catchall/network/trig_lut.h` (Phase 1)
- `mods/catchall/network/soften.h` (Phase 3)
- `mods/catchall/NetworkFieldGraphic.h` (Phase 3)
- `mods/catchall/assets/Network.lua`
- `planning/network-test-procedure.md` (Phase 4)

**To edit:**
- `mods/catchall/catchall.cpp.swig` (Phase 0, again Phase 3)
- `mods/catchall/assets/toc.lua` (Phase 0)
- `mods/catchall/mod.mk` (Phase 3 add eurorack include; Phase 4 version bump)

**To reference (do not edit):**
- `mods/spreadsheet/Pecto.{h,cpp}` — primary infrastructure source
- `mods/spreadsheet/MultitapDelay.cpp` — stereo pan plumbing reference
- `mods/spreadsheet/visadhara/morph.h` (poly_sin Bhaskara) — trig LUT source
- `mods/spreadsheet/visadhara/pmm.h` — LCG pattern
- `mods/catchall/Lambda.h` — header-only unit class skeleton precedent
- `mods/catchall/AlembicSphereGraphic.h` — header-only inline graphic precedent
- `eurorack/rings/dsp/fx/fx_engine.h`, `reverb.h` — FxEngine API + diffusion reference
- `mods/mi/rings/dsp/part.cc` — reverb buffer allocation precedent

## Memory references (rules to comply with)

- `feedback_no_out_of_line_virtuals` — header-only class shape, no `.cpp` for Network if avoidable.
- `feedback_neon_delay_gather` — 3-pass tap gather pattern (Pass A NEON / Pass B scalar+prefetch / Pass C NEON).
- `feedback_doppler_basedelay_smoother` — single LinearRamp pipeline driving all per-tap delays.
- `feedback_multitap_idx_wrap_ulp` — symmetric `idx0 >= maxDelay` guard.
- `feedback_runtime_branched_dsp_dispatch` — branchless arithmetic dispatch in per-sample loop.
- `feedback_package_trig_lut` — no libm trig in package paths.
- `feedback_neon_intrinsics_drumvoice` — heap-allocated NEON state (class members, not stack-locals).
- `feedback_no_third_party_branding` — name discipline (Network is generic, fine).
- `feedback_swig_header_dep` — force-clean SWIG wrapper when `Network.h` changes during dev.
- `feedback_parammode_convention` — shift-toggle on each ply for CV-gain access.

## Verification — end-to-end

1. **Per-phase**: build both arches green, install pkg to emu (`cp testing/linux/catchall-X.pkg ~/.od/rear/`), audition; install pkg to hardware SD via mount.
2. **NEON hint check** after each phase: `arm-none-eabi-objdump -d testing/am335x/mods/catchall/catchall_swig.o | grep -cE '\.32.*:(64|128)'` should remain 0 for Network symbols.
3. **Lint check**: `tools/check-graphic-virtual-defs.sh` exit 0 (clean).
4. **CPU profile** on hardware at end of each phase, confirm target met.
5. **Phase 4 burn-in**: 30 min continuous modulation, no clicks / NaN / drift.
6. **Vanilla compat**: cold-load on stock ER-301 firmware before release.
