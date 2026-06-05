# WoodenBox port plan

Status: **SHIPPED and hardware-validated 2026-06-04 (house 0.1.0.5)**. Per user: "wooden box works perfectly, selection transit sounds quite a bit smoother than i would have expected, about 14% cpu in stereo." Second AW atom in the house package after kWoodRoom; first port to exercise the "add second atom" wiring path through SWIG and toc.

## Result baseline (for later optimization comparison)

- **CPU**: ~14% stereo on Cortex-A8 (vs kWoodRoom's 29%, vs my estimate of 8-12%). The smaller FDN + single Bezier delivered roughly half the per-sample cost as predicted. Estimate underran slightly; actual is closer to ½ kWoodRoom than ⅓.
- **Memory**: ~53 KB / instance (double-precision baseline)
- **Sound**: "works perfectly", Select transitions "quite a bit smoother than i would have expected" — the AW state-reset on Select change doesn't audibly pop. No mitigation needed.
- **Hardware-validated**: first insert, first audio, all 3 params responding, no hangs / glitches
- **Phases 2+** (float-convert b4 arrays, NEON Householder reduction): deferred until a CPU regression actually bites.

---

(Original plan content preserved below for reference.)

Target: house package, atom architecture.
Per `feedback_aw_atom_port_template` (validated by kWoodRoom),
this is a fill-in-the-template port. WoodenBox is the next
catalogued port after kWoodRoom per
`planning/airwindows-reverb-research.md` addendum.

## Source

- Local: `~/repos/airwindows/plugins/MacVST/WoodenBox/source/{WoodenBox.h, WoodenBox.cpp, WoodenBoxProc.cpp}`
- License: MIT (Airwindows, Chris Johnson)
- Filed under AW "Tone Color" (not Reverb) — per handoff:
  "miniature DI-to-acoustic reverb — dense and confined, a
  tone-shaper rather than a real space"
- Naming: keep upstream as **WoodenBox** (faithful port, per
  `feedback_no_third_party_branding` open-source exception)

## Topology (verified from source)

A 4-stage 4×4 diff-Householder FDN per side, walked in opposite
delay-line orders between L and R. The two sides are
cross-coupled via the final feedback taps.

```
input
  → Bezier-undersample accumulator (bez[])
  → (when cycle > 1.0:)
      4×4 FDN stage 1: 4 input lines + g4 feedback
      4×4 FDN stage 2: 4 mid lines via hX-(sum others)
      4×4 FDN stage 3: 4 mid lines
      4×4 FDN stage 4: 4 mid lines
      g4 feedback taps captured, dualmono output = sum*0.125
  → Bezier reconstruction (every input sample)
  → wet/dry mix
  → output
```

**Cross-feedback**: L's stage-4 output → R-side feedback taps
(g4DR, g4HR, g4LR, g4PR); R's stage-4 output → L-side feedback
taps (g4AL, g4BL, g4CL, g4DL). Same internal-stereo pattern as
kWoodRoom — the C++ atom holds both channels.

**Intentional L/R swap through the verb**: lines 132-133 of
`WoodenBoxProc.cpp` route inL → bez_SampR and inR → bez_SampL.
The source comment says "stereo got reversed somewhere?" — AW
ships it that way regardless. Preserve literally per
`feedback_identical_means_identical`.

**Householder coefficient**: `hX - (hB + hC + hD)` (not
`2*hX - sum` like kWoodRoom's 6-line version). 4-line variant
of the same sum-preserving reflection.

**Single Bezier-undersample** (vs kWoodRoom's outer + inner) —
no in-cycle filter stage. Simpler.

**Select knob does a state reset**: when `(int)(A*16.999)`
changes from the prior `clearcoat` value, all 32 delay arrays
are zeroed and all 32 counters reset to 1, then `short*` table
loaded from the matching switch case (17 cases, 0..16). This
runs on the audio thread on first Select change.

## State arrays (per channel)

| Group | Lines | Element type | Bytes / side | Notes |
|---|---|---|---|---|
| 4×4 FDN `b4{A..P}{L,R}` | 16 | double | ~27 KB | max sizes summed = 3292+80 slack |
| Counters `c4*` | 32 ints | int | ~128 B | |
| Feedback floats `g4*` | 8 | double | 64 B | |
| `bez[9]` (no bezF) | 9 | double | 72 B | |
| `short*` (current delay lengths) | 16 ints | int | 64 B | reloaded on Select change |
| `prevclearcoat` | 1 int | int | 4 B | |
| `fpdL/R` (dropped on port) | 2 | uint32_t | (dropped) | |

**Total ≈ 53 KB / instance** with the original `double`-based
arrays. **~half of kWoodRoom's ~113 KB**, matching the handoff
characterization ("smaller/cheaper than a full k-verb").

Phase 1 keeps everything as `double` per the template; Phase 4+
can listen-test floating the b4 arrays for another ~14 KB save.

## Public parameters

| AW name | Range | Default | Mapping | Effect |
|---|---|---|---|---|
| A "Select" | int 0..16 (17 stops via `(int)(A*16.999)`) | 0.5 → 8 | `Select` (stepped) | Picks one of 17 prebaked `short*` delay-length tables. Headline knob — each preset is a different "box" character. Triggers state reset on change. |
| B "Reso" | 0..1 continuous (mapped `(1-(1-B)^2)*0.0336`) | 0.5 | `Reso` (zeroOne) | Per-line feedback amount. Max effective `reg4n ≈ 0.0336`. |
| C "Depth" | 0..1 continuous (mapped `1-(1-C)^2`) | 0.5 | `Mix` (zeroOne) | Wet/dry. Quadratic curve so the wet builds in non-linearly. |

3 params total — half the surface area of kWoodRoom.

The "Select" parameter being a 17-stop integer needs a stepped
DialMap on the Lua side. Two patterns to choose from:

- **Stepped GainBias** with `LinearDialMap(0, 16)` and
  `setSteps(1, 1, 1, 1)` + `setRounding(1)`. Encoder snaps to
  integers. Standard pattern. (Used in spreadsheet's intMap
  helpers.)
- **OptionControl with 17 choices**. Doesn't fit the standard
  6-button M-row; would need a scrollable approach. Defer.

Recommend stepped GainBias. C++ side takes the float `0..16` and
casts via `(int)(value*16.999/16.0)` — wait, the AW source uses
`A*16.999` where A is `0..1`. If we expose A as a 0..16 stepped
on the Lua side, the C++ atom should normalize internally:
`int select = (int)(mSelect.value() * 16.999 / 16.0)` OR keep
the atom param as `0..1` and let Lua map the display.

Cleaner: keep the atom param `mSelect` as `0..1` (matches AW
naming convention and the `*16.999` math). On the Lua side, the
GainBias readout label could expose either 0..16 or 0..1; user-
facing is fine as 0..1 with the audible character changing at
17 detents. Defer the discrete-stepped UX to a follow-up if it
feels coarse.

## Per-sample work estimate

Two regimes (same shape as kWoodRoom):

**Every input sample (~always-on):**
- ~15-25 FLOPs (Bezier reconstruction, wet/dry, no IIR filter
  since there's no bezF in WoodenBox)
- Drop the per-sample `pow(2, expon+62)` dither (per template).

**Every reverb-cycle hit (fires once per input sample at the
hard-coded `derez = 1.0`, modulo `/overallscale`):**
- ~100-150 FLOPs (4 stages × 4 lines × ~7 ops per Householder)
- ~64 counter-advance branches (32 per side)
- ~64 delay-line reads — all fixed-tap (NEON-friendly)

Compared to kWoodRoom (~640 FLOPs/sample at default), WoodenBox
should be ~1/4 to 1/3 the per-sample cost. **Projected CPU
estimate: ~8-12% stereo on Cortex-A8** at the double-precision
baseline, vs kWoodRoom's measured 29%. Treat as a guess until
measured on hardware.

State reset on Select change: 32 arrays × ~206 doubles average
× 8 bytes = ~53 KB to zero. Roughly 13 μs at typical memory
bandwidth — well within one audio-thread block. Acceptable.

## CloudSeed-trap audit

Verified against source:

- **No `if (firstFrame)` guards.** Counters init to 1 (per AW
  source pattern); `bez[bez_cycle] = 1.0` forces first cycle to
  fire (load-bearing — preserve).
- **No allocations after constructor.** All arrays fixed-size.
- **No init-order dependencies.**
- **No host APIs.** `getSampleRate()` is the only host call,
  read at top of `process()` per block.
- **No `std::vector` resizes.**
- **No modulated reads.** Same fixed-tap read pattern as
  kWoodRoom (`arr[c - ((c > d) ? d+1 : 0)]`).
- **No runtime-branched DSP dispatch in the per-sample loop.**
  The Select switch runs once per Select change (not per
  sample); during steady-state operation the work is identical
  across cycles. Safe.
- **Per-sample dither** is the only risk, dropped per template.

**Verdict: clean.** Structurally identical risk profile to
kWoodRoom which worked first try on hardware.

## Phasing

Per `feedback_aw_atom_port_template`, follow the worked pattern.
Since kWoodRoom's hardware-first-try validates the structural
approach, **skip Phase 0 Smoketest gate** and go straight to
Phase 1. If WoodenBox hangs on first insert, reconstitute
Smoketest from git `5c0f29c` and bisect — but the structural
similarity to kWoodRoom makes a hang unlikely.

### Phase 1 — atom + unit (full port in one cycle)

1. Drop `mods/house/atoms/WoodenBox.h` (header-only `od::Object`
   subclass per the template).
   - Apply all template adaptations: drop VST host deps, drop
     dither, drop `rand()` fpd seed, memset state arrays, keep
     `bez[bez_cycle] = 1.0` first-frame init literal, keep
     counters at 1.
   - Stereo handling: cross-coupled inside the atom; takes
     `In L` / `In R`, emits `Out L` / `Out R`.
   - 3 params: `mSelect` (0..1, default 0.5), `mReso` (0..1,
     default 0.5), `mDepth` (0..1, default 0.5).
   - Preserve the literal stereo "swap" through the verb that
     the AW source has (lines 132-133, 264-272) — don't
     "correct" it.
   - Preserve the Select-change reset logic literally.
2. Drop `mods/house/assets/WoodenBox.lua` (thin unit per
   template).
   - `addObject("op", libhouse.WoodenBox())`.
   - Standard channelCount In/Out wiring.
   - 3 ParameterAdapter ties → 3 GainBias plies (`select`,
     `reso`, `mix`).
   - Default biases match the C++ defaults.
3. Update `mods/house/house.cpp.swig` (two-line addition per
   template):
   - `%{ #include "atoms/WoodenBox.h" %}` block addition
   - `%include "atoms/WoodenBox.h"`
4. Update `mods/house/assets/toc.lua` (one line):
   - `{ title = "WoodenBox", moduleName = "WoodenBox", category = "House", keywords = "reverb, room, wood, tone, box, woodenbox, airwindows" }`
5. Bump `mods/house/mod.mk` PKGVERSION: `0.1.0.4 → 0.1.0.5`.
6. Build verification (per template):
   - `make house-clean ARCH=am335x && make house ARCH=am335x`
   - `make house` (linux)
   - `tools/check-graphic-virtual-defs.sh` — pass
   - `tools/check-neon-hints.sh testing/am335x/libhouse.so` — 0 suspect
   - `arm-none-eabi-nm -C testing/am335x/mods/house/house_swig.o | grep "vtable for house::WoodenBox"` — expect V
   - `ls testing/am335x/mods/house/WoodenBox.o` — expect not found
7. Install linux to `~/.od/rear/`, ship am335x to hardware,
   audition.

Estimated time: one focused session. The DSP body is shorter
than kWoodRoom's (~200 lines of per-sample work vs ~430), the
parameter surface is smaller (3 vs 6), and the template is
established.

## Future phases (deferred)

Match kWoodRoom's deferred-optimization shape:

- **Phase 2 (later)**: float-convert the `b4*` arrays (saves
  ~14 KB / instance). Listen-test against the double reference
  per the handoff's float-conversion rule.
- **Phase 3 (later)**: NEON Householder reduction. WoodenBox's
  4-line variant uses `hX - (hB+hC+hD) = hX - (total - hX) =
  2*hX - total`. Per-stage cost drops from 16 ops to ~6 ops
  (1 sum + 4 fmsub). Same pattern as kWoodRoom Phase 4.
- **Stepped Select UX polish (later)**: if 0..1 continuous feels
  coarse on the discrete 17 steps, swap to a proper stepped
  DialMap or OptionControl.

## Open questions

1. **Select param shape**: continuous 0..1 with implicit
   17-step quantization (cleanest first pass) vs explicit
   stepped DialMap (better tactile UX). Default: continuous
   first, polish if it bothers.
2. **Stereo swap preservation**: keep the AW source's apparent
   L/R reversal through the verb literal. Don't try to "fix"
   it. AW ships it that way intentionally.
3. **Select-change state reset on audio thread**: ~13 μs work.
   Acceptable. No mitigation needed.

## Risk audit

| Risk | Mitigation |
|---|---|
| First-frame hang on Cortex-A8 (CloudSeed pattern) | Source-audit clean; kWoodRoom (structurally similar) worked first try; if it hangs, reconstitute Smoketest from `5c0f29c` |
| `pow()` per-sample dither hangs | Dropped per template |
| Select-change reset causes audible click | Cleanest is a fade-down or reset deferral; defer mitigation until audible |
| Stereo "swap" looks wrong to user | Document in commit message + atom header that it's literal AW behavior |
| 17-stop Select coarse on continuous knob | Acceptable for Phase 1; polish later if needed |
| Wrapper class-layout drift | `SWIG_HEADER_DEPS` auto-regenerates wrapper on header touch (verified) |

## Files / commits to reference

- Template: `feedback_aw_atom_port_template` memory
- Worked-pattern reference: `mods/house/atoms/KWoodRoom.h`
- Unit reference: `mods/house/assets/KWoodRoom.lua`
- Architecture rationale: `planning/house-atom-architecture.md`
- Per-bucket rationale: `planning/airwindows-reverb-research.md`
  addendum
- Phase 1 commit reference: `0a89103` (kWoodRoom Phase 1)
