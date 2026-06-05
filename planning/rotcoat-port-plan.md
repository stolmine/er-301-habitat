# RotCoat implementation plan

Status: **SHIPPED 2026-06-05 (house-0.1.0.14), hardware-validated.** First original-design reverb in the `house` package. Distilled from `planning/rotcoat-design.md` (the concept doc) + `planning/refs/airwindows-port-handoff.md` §3.2 + §4.2 (the handoff rationale) + the empirical pattern from the six shipped AW-port atoms.

## What actually shipped (the implementation diverged from the original plan)

The plan below proposed a **ChromeOxide-per-FDN-line tape-rot architecture** (Phase A: ship Spiral+ChromeOxide as component-only atoms; Phase B-E: RotCoat composes ChromeOxide instances per FDN line, with Lock/Drift/Recirc toggles). That landed at house-0.1.0.9 through 0.1.0.12 and was iterated on hardware. Three pragmatic findings drove a restructure:

1. **ChromeOxide's IIR coefficients were never going to work at reduced rate.** ChromeOxide's `iirAmount` formula assumes host-rate operation; running inside a reduced-rate FDN cycle pushed coefficients into degenerate territory (`> 1.0`) even after rate-compensation. Mulch was inaudible in practice.
2. **Drift mode + Lock toggle wasn't earning its surface complexity** — user preference was always Lock-mode static character.
3. **Recirc=highs was confusing** (energy deficit made it feel like a volume drop instead of a tonal alternative) and Recirc=lows was strongly preferred.

The shipped implementation replaces the ChromeOxide-based Mulch with a **multi-world per-line cycleStep fan**: each of the 4 FDN lines gets its own reduced-rate divisor, with Mulch controlling how widely the per-line rates spread around the snapped World base rate. At Mulch=0 all lines share `worldRate` (classic single-rate FDN). At Mulch=1 they fan to spread factors `[1.4, 1.15, 0.85, 0.6]` → slowest line ~1.67× more undersampled (more Bezier stairsteps), fastest line ~1.67× less. Householder cross-feeds the four worlds. Genuinely novel and structurally clean.

Per-line lines fire asynchronously; the Householder for each firing line uses the most-recent stored taps from all 4 (some slightly stale across the others — musically benign). Per-sample output sums the 4 line-interpolations. A `while`-loop on cyclePhase handles `cycleStep > 1.0` cases (at World=1 + Mulch high, some lines fire slower than host rate) without the linear-interp extrapolation runaway that an `if` would have produced.

**Feedback governor**: Spiral saturator on each Householder output bounds magnitude to ~[-1, 1] regardless of Regen (prevents clip-then-kill on hot Regen). Then a per-line one-pole lowpass on the feedback path (target fc=500Hz, alpha computed block-rate as `2π·500·worldRate / sampleRate`) restores the "lows recirculate" tail character without dragging ChromeOxide back in.

**ChromeOxide.h still ships as a component-only header** — currently unused by RotCoat, but valid for future composition (Console-style unit, standalone tape-saturator unit, etc.). Spiral.h ships as component and IS consumed by RotCoat as the feedback governor.

**Final parameter surface (5 continuous CV-controllable params, no toggles, no inlets beyond audio I/O):**
- **World** (snapped to {1, 2, 3, 4, 6, 8})
- **Regen** ([0.1, 0.6])
- **Predelay** (~0 to 170 ms at 48k)
- **Mulch** (per-line cycleStep fan width [0, 1])
- **Wetness** (standard crossfade)

**Sound-design corner worth knowing**: at World=1 (host rate) with high Mulch, some lines have `cycleStep > 1.0` (fire slower than host rate while other lines fire AT host rate). The mismatch produces an aggressive crushed-aliasing character. Tamed by the while-loop fire + feedback LP, but still distinctly aggressive — a deliberate sound-design corner of the unit. User also noted low-World aliasing character interacts nicely with Mulch.

**Naming**: source uses the `RotCoat` codename. Per `feedback_no_third_party_branding` original work needs a habitat-native name (Lath / Cure / Sediment / Patina / other). Rename is mechanical at release polish.

---

(Original plan below preserved for historical reference. The shipped implementation supersedes the Phase B/C/D/E breakdown.)

---

**RotCoat is a working codename, not the final name.** Per `feedback_no_third_party_branding` the AW open-source exception only applies to faithful ports; original work needs habitat-native naming. Final name (Lath / Cure / Sediment / Patina / other) gets locked at Phase E once the aesthetic is validated on hardware. Source files use the `RotCoat` codename throughout development.

Per `feedback_atoms_as_components`: this is a USER-FACING REVERB UNIT (gets a Lua wrapper + toc entry). The prerequisite atoms (ChromeOxide, Spiral helpers) ship as C++-only components.

## Source

No upstream source — this is original design. Component atoms drawn from:
- `~/repos/airwindows/plugins/MacVST/ChromeOxide/source/` (tape-rot core; MIT)
- `~/repos/airwindows/plugins/MacVST/Spiral/source/` (saturator helper; MIT)
- `mods/house/atoms/CreamCoat.h` (reference pattern for divisor+Bezier reduced-rate shell)
- `mods/house/atoms/WoodenBox.h` or `mods/house/atoms/Verbity.h` (reference pattern for 4×4 diff-Householder FDN)

## Open questions resolved (decisions for this plan)

The design doc left six implementation questions open. Decisions:

1. **FDN size: 4×4.** Per design doc rationale — World is already the headroom knob, FDN should be lean. Matches CreamCoat/WoodenBox/Verbity lineage. 4×4 diff-Householder per side.
2. **Crossover frequency: implicit (via ChromeOxide internal IIR), not user-tunable.** Avoids a redundant axis next to Mulch. ChromeOxide's alternating-IIR band-split has an effective crossover around 1-2 kHz dependent on iirAmount, which itself responds to the input's intensity. "Tuneable via Mulch" — one knob doing two coupled things, the AW style.
3. **World as discrete stops with snap.** Lock mode = quantized ÷1/÷2/÷3/÷4/÷6/÷8. Drift mode = continuous slew of a FLOAT divisor in [1, 8] (snap disabled). The two modes share the same underlying float `worldRate`; Lock just constrains it to a fixed set of allowed values.
4. **Drift rate: constant within Drift mode, no sub-control.** A future expansion could add a Drift Rate knob; for v1 keep the parameter surface to six controls. ~0.3 Hz drift rate (worldRate sweeps 1↔8 over ~3 seconds) feels right based on the design doc's "CrunchCoat-style pitch-swoop glitch instrument" framing.
5. **Output stage: standard wet/dry crossfade, no post-saturation.** Per design doc.
6. **Naming: defer to Phase E.** Source uses `RotCoat` codename; rename mechanical at release polish phase. Reserve namespace token `house::RotCoat` for now.

## Macro topology

```
in
  → Predelay (host rate)
  → Reduced-rate shell:
       Bezier accumulator (per-sample)
       Every (1/worldRate) samples, fire reverb cycle:
         FDN 4×4 input → write to lines I/J/K/L
         Per-line tap read
         Per-line tape-rot (ChromeOxide-style: highpass-split,
           band-saturate, lowpass-smooth, noise-FM, recombine)
         Householder 4×4 reduce (diff: out_i = tap_i - sum(other taps))
         Per-line band-recirc flip selects which band feeds back
         Sum/4 combiner → reverb output
       Bezier reconstruct (per-sample interpolate back to host rate)
  → Wet/dry crossfade with bypass-predelay dry path
  → out
```

**Stereo handling**: internal stereo. Per-line state is L+R paired; no cross-coupling at v1 (matches kWoodRoom/WoodenBox/Verbity/CreamCoat). Cross-coupling is a Phase 2 experiment if the tail feels too narrow.

## Component dependencies (ship before RotCoat)

### ChromeOxide (component-only header)

`mods/house/atoms/ChromeOxide.h` — header-only, hybrid float, but NOT necessarily an `od::Object`. Two paths:

**Path A (recommended)**: ship as a plain C++ class `ChromeOxideMono` plus a stateless `chromeOxideProcess(double in, ChromeOxideMonoState&, params)` per-sample function. RotCoat instantiates 4 per side (= 8 per RotCoat). Smallest surface.

**Path B**: ship as a full stereo `od::Object` per the AW port template. Lets it also be a standalone unit if user later wants it. More surface, but matches the template.

**Decision: Path A.** Smaller, single-purpose, no SWIG burden for RotCoat to consume. If user later wants ChromeOxide as a standalone unit, write a thin Object wrapper around the helper (~30 lines).

State per `ChromeOxideMono` instance: 8 doubles (iirA/B/C/D + secondSample/third/fourth/fifth) + 1 bool flip. ~72 bytes per instance. 8 per RotCoat instance = ~576 B. Trivial.

Per-sample cost: 4 IIR steps (one-pole each), 1 glitch-overdrive multiply, 1 noise-interp branch chain (5 sample lookup), 1 sin() (Spiral-style saturation on high band), 1 recombine sum. Cheap — but the **1 sin() per sample per line per side** matters: 4 lines × 2 sides × 1 sin = 8 sin per RotCoat per CYCLE (not per sample — only fires at reduced rate). At World=÷4 that's 2 sin/sample average, similar to Galactic's LFO sin cost. Acceptable.

Tape-rot's noise FM in source uses `fpdL` (a 32-bit RNG state) as the noise source. We don't have an RNG in the port. Substitute: a tiny LCG seeded per `ChromeOxideMono` instance (different seed per line for decorrelation). Adds 1 uint32 state + 4 ops/sample/line.

### Spiral (component-only inline function)

`mods/house/atoms/Spiral.h` — stateless. One `static inline double spiralSaturate(double x, double densityA)` function. Identical math to ChromeOxide's high-band saturator (lines 129-143 of ChromeOxideProc.cpp): bounded `sin(|x|*densityA)/densityA` with sign preserved.

If ChromeOxide already does Spiral on its high band, **why ship Spiral separately?** For the optional Phase 2 swap: if ChromeOxide's low-band glitch-overdrive doesn't sound like "head-bump" on the FDN lines (likely — glitch is a different character), we may want to replace it with explicit Spiral on the low band. Having Spiral as a small reusable helper makes that swap one-line.

Per-sample cost: 1 sin + 1 fabs + 1 multiply + sign branch. Trivial.

## Inline mechanics (lifted from existing atoms, not extracted)

These patterns are duplicated inline in RotCoat rather than abstracted:

- **Bezier undersample shell**: copy from CreamCoat.h (the bez[] accumulator + cmco_cycle counter + reconstruct math). Adapt for variable `worldRate` (CreamCoat hard-locks to integer 1/N; RotCoat in Drift mode wants continuous float rate, requiring fractional-cycle accumulation).
- **4×4 diff-Householder FDN**: copy the stage shape from WoodenBox.h or Verbity.h. Four lines with sizes scaled from a `size` parameter (use Verbity's: I=3407, J=1823, K=859, L=331 per side scaled by size). Per-line read formula `arr[count - ((count > delay) ? delay+1 : 0)]` is the standard wrap.
- **Predelay**: copy from CreamCoat.h (single fixed-tap read at user-controlled offset).
- **Sum/N combiner**: standard sum-of-outputs / 4 (or 8 if we want intentional gain reduction per the Verbity/Galactic pattern). **Decision: sum/4** for RotCoat — clean unity gain; let user set Regen carefully.
- **Multi-pole averaging tail (`lastRef[]`)**: copy from any shipped atom for sample-rate-promotion smoothing. Mostly irrelevant at 48k (cycleEnd=1) but free to include for future-proofing.

## State + memory budget

| Group | Size per instance | Bytes (hybrid float) |
|---|---|---|
| FDN lines × 4 × 2 sides (Verbity's I/J/K/L scales) | 12540 samples max | ~50 KB |
| Predelay buffer × 2 sides | 8192 samples (max ~170ms @ 48k) | ~64 KB |
| Bezier accumulator + counters | ~13 doubles | ~104 B |
| 8 ChromeOxideMono instances (4 lines × 2 sides) | 8 × 72 B | ~576 B |
| Feedback taps (4 × 2 sides) | 8 floats | 32 B |
| lastRefL/R[7] | 14 floats | 56 B |
| iir state for input/output LP (if added) | ~4 doubles | 32 B |
| Counters + delay sizes | ~16 ints | 64 B |
| LFO/Drift state (worldRate, drift accumulator) | ~4 doubles | 32 B |
| **Total per instance, ÷1 max** | | **~115 KB** |

At higher World divisors the FDN line lengths can stay max-allocated but be virtually shorter (decay still scales with size). Memory budget is the ÷1 case. **Fits L2 comfortably** at all World settings — RotCoat is the leanest house atom by memory.

## Public parameters

| Knob | Type | Range | Default | What it does |
|---|---|---|---|---|
| **World** | Continuous w/ snap (Lock mode) or continuous (Drift mode) | 1.0..8.0 | 4.0 | Reduced-rate divisor. Lock: snaps to {1, 2, 3, 4, 6, 8}. Drift: continuous slew through the range. |
| **Regen** | Continuous | 0..1 | 0.5 | Per-line feedback (regen = 0.0625 + B*0.0625, range 0.0625..0.125, same shape as Galactic's regen) |
| **Predelay** | Continuous | 0..1 | 0.0 | Predelay tap (0..170 ms at 48k host rate) |
| **Mulch** | Continuous | 0..1 | 0.3 | Tape-rot depth — maps to ChromeOxide's combined Drive (A) + Output (B) params. Higher Mulch = more intensity AND more bias (noise-FM swing). |
| **Lock** | Toggle (Option, 1=Lock, 2=Drift) | — | 1 (Lock) | World snap-vs-continuous mode. Per `feedback_option_vs_parameter` — Option uses 1/2 values not 0. |
| **Recirc** | Toggle (Option, 1=Low, 2=High) | — | 1 (Low) | Band-recirc flip. Low = lows recirculate (saturated low-mid bloom). High = highs recirculate (swimming tail). |

**Six controls total.** World is the headline. Lock + Recirc are toggles — keeps the surface scannable (4 plies of continuous + 2 toggle plies).

## CPU projection

Per-sample work (always-on, host rate):
- Denormal flush, drySample capture: ~5 ops
- Predelay write + read: ~6 ops
- Bezier accumulator add: ~3 ops
- Bezier reconstruct (cubic interp): ~10 ops
- Wet/dry crossfade: ~6 ops
- ~30 FLOPs / sample / channel

Per-cycle work (fires every 1/worldRate samples):
- FDN 4 writes + 4 reads + Householder 4×4 reduce: ~30 FLOPs
- 4 × ChromeOxideMono process: 4 × (4 IIR + 1 sin + ~10 other) = ~80 FLOPs + 4 sin
- Recombine + sum/4: ~6 FLOPs
- ~120 FLOPs + 4 sin per cycle per channel

At **World=÷1 (max load)**: per-sample = 30 + 120 = 150 FLOPs + 4 sin. At 48k stereo, ~14.4M FLOPs/sec + 384k sin/sec. Sin cost on Cortex-A8 scalar libm ≈ 50ns each → ~19ms/sec for sin alone ≈ 1.9% CPU. Total projection **~12-18% stereo**.

At **World=÷4**: per-sample = 30 + 30 (1/4 of cycle work) = 60 FLOPs + 1 sin avg. Projection **~6-9% stereo**.

At **World=÷8**: **~3-5% stereo**. Multiple instances stack.

This is **lighter than every house atom except WoodenBox (post-retrofit)**. The reduced-rate architecture earns its keep.

## CloudSeed-trap audit (preventive — original design)

For an original-design atom we don't have an upstream reference to compare against, so the audit is preventive:

- **No `firstFrame` guards needed** — counters init to 1, state arrays memset to 0, Bezier accumulator starts at 0 (first cycle fires after `worldRate` samples accumulated — at ÷1, first cycle fires on first sample; at ÷8, first cycle fires after 8 samples of silence, ~0.17 ms — inaudible).
- **No allocations after constructor.**
- **No host APIs** beyond `getSampleRate()` (top-of-block).
- **No `std::vector`.**
- **No modulated reads on FDN lines** — taps are at the count head, fixed per cycle. Predelay tap is at block-rate-constant offset.
- **One modulated path: ChromeOxide's 5-sample noise-FM interpolation.** Indexes are bounded to [0..4] by the conditional chain. Safe; same shape as the source we're lifting from.
- **No runtime-branched DSP dispatch in per-sample loop.** The Lock/Recirc toggles read once at top of block to set static behavior for the block.
- **Per-sample dither dropped** per template.
- **`-fno-tree-vectorize`** in effect for the package (`mod.mk`), satisfying the top-priority rule.
- **Sin per cycle (not per sample)** — fires at reduced rate. Safe scalar libm path.
- **Drift mode worldRate slew rate is bounded** (~0.3 Hz) — divisor never changes faster than per-block, so the Bezier reconstruct math always sees a stable rate within a block.

**One new risk class** (per design doc §watch items): **warble depth clamp**. ChromeOxide's noise-FM index `randy` can go negative or beyond delay length if bias + noise overshoot. Already guarded in source (the 4 conditional branches handle [0..4] only; anything outside falls through with `bridgerectifier = inputSample`, the identity case). Preserve verbatim. **Add a hard clamp** at our integration point as belt-and-suspenders.

**Verdict**: clean by construction. RotCoat has the lightest trap profile of any house atom because the reduced-rate domain pays multiple safety dividends.

## LOAD-BEARING preservation (original-design invariants)

These are the design choices that MUST stay in place; reversing any breaks the intended character or correctness:

1. **Reduced-rate domain hosts BOTH the FDN AND the tape-rot** — not FDN at reduced rate + tape-rot at host rate. The intermodulation between reconstruction stairsteps and warble swim REQUIRES both to be at the reduced rate. Splitting them defeats the emergent payoff.
2. **Per-line tape-rot, not post-FDN tape-rot.** Each FDN line carries its own rot history. A single post-FDN ChromeOxide would average the lines together and lose the "different rot per line" character.
3. **Band-recirc flip is per-line, single flag, applied at the feedback selection.** Not per-band-instance. The flag picks which of the ChromeOxide split bands the matrix feedback carries.
4. **Lock mode snaps to {1,2,3,4,6,8}, not {1,2,3,4,5,6,7,8}.** The non-uniform spacing is intentional ("each stop is a different world"); even spacing would feel like a continuous control.
5. **Drift mode IS continuous through fractional divisors.** Don't quantize Drift mode internally — fractional worldRate is what produces the pitch-swoop glitch character. Bezier reconstruction handles fractional rates naturally.
6. **Denormal floor in feedback path** — same `1.18e-23 → 1.18e-17` flush we use everywhere. Especially important at low divisors where fewer flushes happen per second.

## Phasing

Sequential. Each phase ends with a hardware audition gate before proceeding.

### Phase A — Prerequisite atoms (ChromeOxide + Spiral)

1. `mods/house/atoms/Spiral.h` (header-only, ~30 lines, single inline saturator function)
2. `mods/house/atoms/ChromeOxide.h` (header-only, ~150 lines, `ChromeOxideMono` class + per-sample process function, hybrid float)
3. **No SWIG wiring, no Lua units, no toc entries.** Per `feedback_atoms_as_components`.
4. Build verification per template (both arches). Atoms compile into the package even when unused — confirms they don't break anything.
5. Bump PKGVERSION 0.1.0.8 → 0.1.0.9.

No hardware audition needed at this phase (no user-visible change).

### Phase B — RotCoat skeleton (no tape-rot, fixed World)

1. `mods/house/atoms/RotCoat.h` — header-only, hybrid float `od::Object` subclass. Implements:
   - Predelay (CreamCoat pattern)
   - Bezier undersample shell with FIXED worldRate = 4.0 (no Lock/Drift logic yet)
   - 4×4 diff-Householder FDN (Verbity pattern, 4 lines, sum/4 combiner)
   - Wet/dry crossfade
   - NO tape-rot, NO band-recirc, NO Drift, NO Lock
   - Params: Regen, Predelay, Wetness (3 params)
2. `mods/house/assets/RotCoat.lua` — minimal 3-ply unit wrapper
3. SWIG + toc entries (RotCoat IS a user-facing unit)
4. Build, install linux, audition in emu.
5. Validate: does the reduced-rate FDN + Bezier reconstruct produce a clean reverb at ÷4? CPU profile on hardware.
6. **Hardware gate**: must sound like a clean reverb, no clicks, no instability.

### Phase C — Add tape-rot

1. Wire `ChromeOxideMono` instances per FDN line (4 per side = 8 total) inside the FDN cycle.
2. Add Mulch parameter mapping to ChromeOxide's A (Drive/intensity) and B (Output/bias).
3. Recombine per-line and feed into Householder reduction.
4. Bump PKGVERSION dev digit, build, install, audition.
5. **Hardware gate**: does it sound like RotCoat — wow + bleach + rot? If yes, proceed. If the low-band character is wrong (glitch-overdrive instead of head-bump), swap low-band path to explicit Spiral on bass.

### Phase D — Add World stepping + Recirc + Lock

1. Promote worldRate from fixed-4 to a Parameter (`World`).
2. Lock mode (Option toggle): snap worldRate to nearest of {1,2,3,4,6,8} on parameter read.
3. Recirc mode (Option toggle): select which band feeds back per line.
4. Bezier shell math must handle the runtime-variable worldRate (block-rate update, not per-sample).
5. Bump PKGVERSION, build, audition each World stop.
6. **Hardware gate**: each World setting must produce a distinct character per the design doc table (÷2 subtle haze, ÷6/÷8 cursed wow).

### Phase E — Drift mode + final name + release polish

1. Drift mode: when Lock=2 (Drift), worldRate slews continuously through [1.0, 8.0] at ~0.3 Hz using a triangle LFO.
2. Final name decision (Lath / Cure / Sediment / Patina / other). Rename source files + class + unit title + toc entry. Mechanical.
3. README + release notes if shipping a release at this point.
4. Bump PKGVERSION, ship.
5. **Hardware gate**: Drift mode produces the "CrunchCoat-style pitch-swoop glitch" character; doesn't crash or saturate to silence.

Each phase commits independently. Phase E rename is its own commit so history shows the codename→final transition cleanly.

## Open implementation questions (TBD)

These resolve during implementation, not in the plan:

1. **Predelay buffer size**: 8192 samples (~170 ms) seems right; revisit if user wants longer.
2. **Drift mode LFO shape**: triangle ramp through [1,8] feels right; could try sine. Decide by ear at Phase E.
3. **Recirc=High at low World divisors**: may saturate to silence (high band gets all the warble + all the feedback). May need a per-mode regen cap. Surface during Phase D audition.
4. **Lock snap UX**: as user turns World knob continuously in Lock mode, does it snap-and-hold or slew-then-snap? Default snap-and-hold; revisit if it feels too abrupt.
5. **What if a user wants Recirc to itself rotate over time?** Cool idea, not in v1. Could be a Phase F if user asks.

## Files to be created

```
mods/house/atoms/Spiral.h         # Phase A
mods/house/atoms/ChromeOxide.h    # Phase A
mods/house/atoms/RotCoat.h        # Phase B (grows through C/D/E)
mods/house/assets/RotCoat.lua     # Phase B (grows through D)
planning/rotcoat-port-plan.md     # this doc
```

PKGVERSION bumps:
- 0.1.0.8 → 0.1.0.9 (Phase A)
- 0.1.0.9 → 0.1.0.10 (Phase B)
- 0.1.0.10 → 0.1.0.11 (Phase C)
- 0.1.0.11 → 0.1.0.12 (Phase D)
- 0.1.0.12 → 0.1.0.13 (Phase E, includes rename)

If we ship a first house release after RotCoat lands: drop 4th dev digit → `house-0.1.0`. Six AW ports + RotCoat is a strong launch surface.

## Why this plan respects all established rules

- **`feedback_atoms_as_components`**: prereq atoms ship without Lua units; RotCoat itself IS a unit.
- **`feedback_aw_atom_port_template`**: hybrid float from Phase B (memory not the bottleneck here but consistency matters).
- **`feedback_no_third_party_branding`**: codename for development, habitat-native name for release.
- **`feedback_identical_means_identical`**: ChromeOxide and Spiral patterns lifted literally from AW source (only port-template adaptations: drop dither, drop fpd seeding, drop VST host deps).
- **`feedback_no_out_of_line_virtuals`**: header-only atoms throughout.
- **`feedback_disable_tree_vectorize_am335x`**: package mod.mk already enforces; nothing to add.
- **`feedback_always_build_both_arches`**: every phase build runs both ARCH=linux and ARCH=am335x.
- **`feedback_linux_build_auto_install`**: each linux build copies pkg to `~/.od/rear/`.
- **`feedback_package_version_bump`**: PKGVERSION bumps per phase per the table above.
- **`feedback_option_vs_parameter`**: Lock + Recirc use Option with 1/2 values (not 0/1).
- **`feedback_persist_plans_to_repo`**: this plan doc lives in `planning/` before any code lands.
