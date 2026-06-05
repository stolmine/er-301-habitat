# XYZ engine implementation plan

Status: **PLANNED 2026-06-05**. Second original-design reverb in the `house` package, post-RotCoat. Distilled from `planning/xyz-engine-design.md` (the concept doc) + `planning/refs/airwindows-port-handoff.md` §3.1 + §4.2 + lessons learned shipping the seven prior atoms (especially RotCoat — see "Lessons applied" section below).

**XYZ is a working codename**, not the final name, per `feedback_no_third_party_branding`. Final name (Cistern / Vault / Crypt / Reliquary / Ley / other) gets locked at Phase E once aesthetic is validated on hardware. Source files use the `XYZ` codename throughout development.

Per `feedback_atoms_as_components`: XYZ is a USER-FACING REVERB UNIT (gets Lua wrapper + toc entry). Its one new prerequisite atom (AllpassMono) ships as a C++-only component.

## Lessons applied from RotCoat (and the seven prior ports)

These shape every design decision below:

1. **No tape-rot character inside reduced-rate domain.** ChromeOxide's IIR coefficients become degenerate at reduced rates and the band-split stops working. XYZ's Y-axis saturation will use **Spiral** (proven feedback governor from RotCoat) — applied at the appropriate rate per Z-mode rather than wedged into a per-line cycle.
2. **Spiral + one-pole LP is the proven feedback-governor pattern.** Reuse verbatim. Spiral bounds magnitude; LP keeps the loop bass-dominated and prevents HF buildup. Same pattern as RotCoat.
3. **While-fire (not if-fire) for cycleStep > 1.0.** Same correctness fix RotCoat needed. Cap fires per sample at 4 max for safety (cycleStep clamp).
4. **Shared per-FDN firing, not per-line.** Per-line firing was RotCoat's signature feature (Mulch fans worlds). XYZ doesn't need it — Y is the undersample axis and acts on the FDN as a whole. Simpler state, easier reasoning.
5. **5-6 params is the right surface count.** Don't over-knob. XYZ ships as 5 params: X / Y / Z / Predelay / Wetness.
6. **Snap-target for stepped values worked well in RotCoat.** Z is naturally a 3-way switch (Nested / Folded / Coupled). Snap a continuous Z param to {0, 0.5, 1.0}.
7. **Hybrid float from Phase 1, no exceptions.** Memory + CPU both benefit.
8. **Codename in source until aesthetic validates.** Rename is mechanical at Phase E.
9. **Document sound-design corners in atom header.** RotCoat's World=1+high-Mulch corner pattern.
10. **One new primitive max, and ship as component-only.** AllpassMono (Schroeder APF with morphable g). Reusable elsewhere; no Lua wrapper.

## Source

No upstream source — original design. Primitive atoms drawn from:
- `mods/house/atoms/Spiral.h` (already shipped — feedback governor + Y saturator)
- `mods/house/atoms/RotCoat.h` (reference pattern for: Bezier undersample shell with variable cycleStep, per-line feedback Spiral+LP, while-fire, multi-line FDN with asynchronous fire — though XYZ uses shared-fire)
- `mods/house/atoms/CreamCoat.h` (reference pattern for predelay)
- `mods/house/atoms/Verbity.h` or `mods/house/atoms/WoodenBox.h` (reference pattern for 4×4 diff-Householder FDN)

New component to ship: `mods/house/atoms/AllpassMono.h` (Schroeder APF helper, ~30 lines).

## Primitive review — what's reusable, what's not

| Primitive | Use in XYZ? | Why |
|---|---|---|
| **Spiral.h** (shipped) | YES — Y saturator + feedback governor | Trivial cost, smooth sin() curve, proven. |
| **ChromeOxide.h** (shipped, unused) | NO | Same reasons it didn't work in RotCoat: IIR degenerates at reduced rates. XYZ's Y character comes from sat+undersample interaction, not tape rot. |
| **AllpassMono.h** (NEW) | YES — X morph diffuser | Schroeder APF: `y[n] = -g*x[n] + x[n-d] + g*y[n-d]`. State = 1 sample of y history per instance. At g=0 acts as pure delay; at high g acts as diffuser. X knob morphs g. |
| Density (AW catalog) | DEFERRED to Phase D | Bipolar sat with highpass-just-distorted-path. Richer character than Spiral but more state. Consider if Spiral feels thin. |
| Capacitor2 (AW catalog) | NO for v1 | Voltage-modulated filter is interesting but adds another axis user must learn. Cryptic-minimal design = 3 axes (X/Y/Z), don't add more. |
| PurestConsole3 channel+buss | NO | Spiral+LP already provides containment for the feedback loop. Console would be redundant. |
| Average (AW catalog) | NO | Could be alternative diffuser but APF is the canonical Schroeder choice and matches the design doc's "allpass-heavy" wording. |
| Aura (AW catalog) | NO for v1 | Resonant LP. Could give X-high a more dramatic ringing character but adds parameter. Defer. |

**Total new code**: AllpassMono.h (~30 lines) + XYZ.h (~400-500 lines) + XYZ.lua (~120 lines). Plus the plan doc.

## Macro topology by Z regime

### Z = Nested (clean serial chain — the "predictable home base")

```
in
  → predelay (host rate)
  → Spiral sat (drive = satDrive)
  → undersample shell (cycleStep from Y):
       per-cycle: 4×4 Householder FDN
         (per line: read tap → AllpassMono with X-controlled g →
          tap value going to Householder)
         feedback governor (Spiral + LP on each line)
  → Spiral de-sat (1/drive scale, undo)
  → wet/dry crossfade
  → out
```

Saturation outside loop, undersample at cycle boundary, FDN sees clean diffused signal. Sounds like a characterful digital verb with a lo-fi switch.

### Z = Folded (the signature sound — Y processing moves INSIDE feedback)

```
in
  → predelay
  → undersample shell with cycleStep from Y:
       per-cycle: 4×4 Householder FDN
         per line: read tap → AllpassMono → tap value to Householder
         Spiral sat applied to EACH line's feedback (in-loop) →
            Spiral governor + LP on feedback (chained sat first then governor)
            Per-pass aliasing of generated harmonics emerges naturally
            from the cycleStep > 1 undersampling.
  → wet/dry crossfade
  → out
```

In-loop saturation means each pass through the FDN adds harmonics; the undersampling (already present via cycleStep) aliases them per pass; harmonics evolve as the tail decays.

### Z = Coupled (two cross-modulating sub-FDNs — emergent / dramatic)

```
in
  → predelay
  → undersample shell with cycleStep from Y (capped at min 2.0 in Coupled mode for AM335x):
       per-cycle: TWO 4×4 Householder FDNs in parallel:
         FDN_A: line sizes [1709, 911, 433, 167]
         FDN_B: line sizes [1933, 1009, 461, 191] (slightly different primes → detune)
         For each FDN:
           per line: read tap → AllpassMono → tap value
         CROSS-LINK: each FDN's input includes a portion of the OTHER FDN's tap sum
            scaled by Spiral-saturated coupling amount (the Y leash)
         Householder reduce within each FDN
         feedback governor (Spiral + LP) per line per FDN
  → sum of FDN_A taps + FDN_B taps / 2
  → wet/dry crossfade
  → out
```

Two slightly-detuned rooms; saturation on the cross-link keeps runaway musical. At low decay (X low) → lush chorused double-room. At high decay → self-oscillation as evolving pad.

## Parameter math

### X — Size + texture morph

Two coupled things move with X:
- **FDN line delay length scale**: `lineSize[i] = baseSize[i] * (0.2 + X * 1.5)` → range `[0.2, 1.7]` × `baseSize`. Small at X=0, large at X=1.
- **APF g coefficient**: `g = (1.0 - X) * 0.6` → range `[0, 0.6]`. Heavy diffusion at X=0, pure delay at X=1.

### Y — Sat + undersample (the coupled-curve axis)

```
satDrive   = pow(Y, 1.4)            // [0, 1]   sat dominates early
cycleStep  = 1.0 + pow(Y, 2.2)*3.0  // [1, 4]   undersample kicks in at top
```

Exponents chosen per design doc sketch — listening-tune at Phase D. Net behavior: sat builds gracefully through low-mid Y, undersample joins in heavily at the top, giving the knob an arc instead of mid-zone mush.

**Per design doc**: "linear coupling sounds bad in the middle." Track this on hardware audition; if mid-Y feels muddy, push the exponent on cycleStep higher (e.g. `Y^3.0`) so it stays cleaner through more of the knob.

### Z — Topology switch

Continuous param snapped to one of `{0.0, 0.5, 1.0}`:
- `Z < 0.33`: Nested
- `0.33 ≤ Z < 0.67`: Folded  ← **default position**
- `Z ≥ 0.67`: Coupled

**Per design doc**: Folded is the signature sound, ship as default. Nested is the predictable home base. Coupled is the dramatic ceiling.

**Z transitions WILL pop** (state in the now-inactive paths is stale; sat/undersample placement changes abruptly). For v1, accept the artifact per design doc option 3 ("aesthetic cost — might be on-brand for a 'cryptic' reverb"). Revisit at Phase D if audition feels disruptive.

### Predelay

Standard: `0..kPredelay-1` samples, ~170ms at 48k.

### Wetness

Standard crossfade.

## State + memory budget

| Group | Size | Bytes (hybrid float) |
|---|---|---|
| FDN_A line buffers × 2 sides (lines I/J/K/L sized at max-scaled length) | ~6420 samples per side | ~50 KB |
| FDN_B line buffers × 2 sides (slightly different primes) | ~7212 samples per side | ~58 KB |
| Predelay buffers (host rate, shared) | 8192 × 2 | ~65 KB |
| AllpassMono state per line per FDN per side | 4 × 2 × 2 = 16 floats | 64 B |
| Per-line feedback taps, LP state, etc | ~32 floats + ~16 doubles | ~256 B |
| Bezier shell state per FDN | ~10 doubles per FDN | ~160 B |
| **Total per instance** | | **~175 KB** |

L2 budget on Cortex-A8 is 256 KB. Fits comfortably. Per-cycle access pattern is ~12-16 cache lines (one per line per FDN), well within working set. Standard FDN profile, validated by all prior atoms.

## CPU projection by Z mode

Per-cycle work:
- **Nested / Folded** (single FDN): 4 line reads + 4 APF computes + Householder + 4 Spiral+LP feedback governors ≈ 80 FLOPs + 4 sin per cycle
- **Coupled** (two FDNs): roughly 2× = 160 FLOPs + 8 sin per cycle, PLUS cross-link work (additional 2 Spiral-saturated cross-feeds = ~10 FLOPs + 2 sin)

Per-sample (always-on):
- Predelay write+read, Bezier accumulate, per-sample interp, wet/dry crossfade ≈ 25 FLOPs

**Projected stereo CPU on Cortex-A8:**
- Nested, Y=0 (no undersample, sin pre/post sat per sample): **~18-22%**
- Nested, Y=0.5 (cycleStep ~1.22): **~16-20%**
- Nested, Y=1 (cycleStep=4): **~8-12%**
- Folded, comparable to Nested per Z mode
- Coupled, Y=0.5 (cycleStep ≥ 2 enforced): **~14-18%** (same as Nested low-Y because the dual-FDN cost is mitigated by the cycleStep cap)
- Coupled, Y=1: **~8-12%**

Worst case ~22%. Lighter than Galactic, comparable to Verbity. Multiple instances stack.

## AM335x Coupled-mode compromise

The design doc explicitly states Coupled must inherit the reduced-rate trick. Implementation: in Coupled mode, force `cycleStep = max(cycleStep, 2.0)`. This means Coupled mode is **always at least ÷2 undersampled** regardless of Y position. Documented as the AM335x compromise — at lower-end Y settings in Coupled mode, the user gets some undersample character whether they asked for it or not.

If user wants Coupled at full rate, they can run it in emulator (where the cap can be lifted via a compile flag). For shipped product, the cap is the AM335x reality.

## CloudSeed-trap audit (preventive)

- **No `firstFrame` guards** — counts init to 1, state arrays memset to 0
- **No allocations after constructor**
- **No host APIs** beyond `getSampleRate()` (top-of-block)
- **No `std::vector`**
- **No modulated reads on FDN lines** — taps are at the count head, X-scaled delay is block-rate-constant
- **No transcendentals per sample EXCEPT pre/post-sat in Nested mode** — Spiral has one sin per call. In Nested mode applied at host rate to L and R = 2 sin/sample/channel = 4 sin/sample stereo. Plus per-cycle sins (4 or 8 depending on Z mode). All scalar libm. Should be fine on A8.
- **No runtime-branched DSP dispatch in per-sample loop** — Z mode read once at top of block to set static behavior for the block.
- **Per-sample dither dropped** per template.
- **`-fno-tree-vectorize`** in effect package-wide.
- **Cap fires per sample** — while-loop on cyclePhase, but cycleStep clamped to max 4.0 so max 4 fires per sample. Bounded.

**Verdict: clean by construction.** Same risk profile as the shipped atoms.

## LOAD-BEARING design invariants

Reversing any of these breaks the intended character or correctness:

1. **In Folded mode, sat AND undersample must BOTH be inside the loop.** Sat without undersample = clean tail. Undersample without sat = no harmonics to alias. Both together = the per-pass aliasing payoff.
2. **In Coupled mode, Y must NOT be optional.** Spiral on the cross-link is the leash that prevents runaway when both FDNs self-oscillate. At Y=0 in Coupled mode, force satDrive to a minimum non-zero value (e.g. 0.1) so the cross-link still saturates.
3. **Nested must stay genuinely predictable.** No subtle sat/undersample bleeding into the loop. Keep the chain strictly serial.
4. **AllpassMono state is per-line, not per-FDN.** Each FDN line has its own APF history.
5. **Cross-link in Coupled adds OTHER FDN's tap-sum to THIS FDN's input** (additive). Not amplitude modulation, not frequency modulation. Additive cross-feedback is what gives the "two rooms beating" character.
6. **While-fire on cyclePhase** (RotCoat lesson). cycleStep can reach 4.0 = at most 4 fires per sample.
7. **Z snaps to 3 stops** — continuous Z param would force constant mode-blending costs that don't add musical value.

## Phasing

Five phases, each gated by hardware audition. Bump PKGVERSION 4th digit per phase.

### Phase A — Skeleton (Nested mode only, single FDN, X+Y+Predelay+Wetness)

Strip-down version. No Z switch, no Coupled, no Folded. Just the Nested chain:
- in → predelay → Spiral sat → undersample shell (Y) → FDN with AllpassMono per line (X) → Spiral de-sat → wet/dry → out

Files:
- `mods/house/atoms/AllpassMono.h` (NEW component — Schroeder APF helper)
- `mods/house/atoms/XYZ.h` (NEW unit atom — Nested-only)
- `mods/house/assets/XYZ.lua` (NEW unit wrapper — 4 plies for now)
- SWIG + toc entries
- PKGVERSION 0.1.0.14 → 0.1.0.15

**Hardware gate**: X morph audibly sweeps from diffuse to resonant. Y curve has the right arc (sat builds, then undersample). No clip-then-kill. CPU under 25% stereo.

### Phase B — Add Folded mode (sat moves inside loop)

Add Z parameter (continuous, snap to {0, 0.5, 1.0}; v1 supports only 0 and 0.5). Folded mode routes Spiral sat onto each line's feedback (after Householder, before Spiral governor). 

Plus: in Folded mode, the per-pass aliasing should be audibly different from Nested with the same Y position. Validate the signature sound exists.

PKGVERSION → 0.1.0.16.

**Hardware gate**: Folded clearly distinct from Nested. Tail "evolves" character. Make Folded the default Z position.

### Phase C — Add Coupled mode (second FDN + cross-mod)

Add FDN_B with slightly different prime line sizes. Cross-link both FDNs additively (each FDN input gets a Spiral-saturated cross-feed from the other's tap sum). Force `cycleStep ≥ 2.0` in Coupled mode (AM335x compromise).

PKGVERSION → 0.1.0.17.

**Hardware gate**: Coupled mode produces the "two rooms beating" character at low Y. Doesn't go unstable at high Y (Spiral leash works). CPU under 25% stereo at default Y.

### Phase D — Y curve listening-tune + Z transition assessment

Audition Y curve on real material. Adjust exponents (`1.4` and `2.2`) until mid-Y feels musical, not mushy. Consider if Density is needed in place of Spiral for richer character.

Decide on Z transition handling: accept pops, crossfade, or zero-crossing snap.

PKGVERSION → 0.1.0.18.

**Hardware gate**: Y curve has the arc. Z transitions don't disrupt musical flow (or pops are confirmed as "on-brand").

### Phase E — Final habitat-native name + release polish

Lock final name. Candidates (per the cryptic + house theme):
- **Cistern** — references the "haunted-cistern microtonal wavering" cryptic preset, water-vessel architecture, fits house theme
- **Vault** — house architecture, mysterious
- **Crypt** — directly cryptic
- **Reliquary** — vessel for relics, ornate / mysterious
- **Ley** — geological/architectural, cryptic minimalism
- **Lich** — house spirit, dark

Rename mechanical: source files + class name + unit title + toc entry. PKGVERSION → 0.1.0.19 or directly to 0.1.0 (first release) if all retrofits also landed.

## Open implementation questions (defer to during-implementation)

1. **Cross-mod coupling strength in Coupled mode**: fixed coefficient or scale with Y? Start fixed at ~0.3 (moderate); revisit at Phase C audition.
2. **FDN_B line sizes**: started with `[1933, 1009, 461, 191]` for slight detune. Could go more or less detuned. Listen-tune at Phase C.
3. **AllpassMono g at X=0 = 0.6**: heavy diffusion. May need to ease back to 0.5 if it sounds metallic. Or push to 0.7 if it's too tame.
4. **Nested mode pre-sat and post-de-sat**: Spiral curve isn't strictly invertible. The de-sat at output is an approximation. May produce slight character even when Y=0 in Nested. Acceptable; document.
5. **Z param UX**: continuous knob that snaps internally? Stepped Option? Snap with visible "tick" feedback? Will decide at Phase B based on what feels right.
6. **Z=1 is Coupled, which is heaviest**: user dialing Z up hits CPU wall. Could clamp Z to 0.5 max when CPU is tight. v1 skip — let user manage.

## How this maps to the combination mechanics

(References `planning/reverb-design-philosophy.md`.)

- **X axis** uses mechanic #3 (topology morph between APF-heavy and pure-delay).
- **Y axis** uses mechanics #1 + #2 (saturation-as-character coupled with undersample-as-character on a single knob). Curve coupling is the design distinction.
- **Z = Nested**: clean #1 + #2 in series. Saturation outside loop, undersample at cycle boundary.
- **Z = Folded**: #1 + #2 move into the feedback path; per-pass interaction is the emergent feature.
- **Z = Coupled**: adds mechanic #5 (cross-modulated feedback between two engines). Mechanic #1 mandatory here as runaway governor.

## Files to be created

```
mods/house/atoms/AllpassMono.h    # Phase A (new component)
mods/house/atoms/XYZ.h            # Phase A (grows through C/D)
mods/house/assets/XYZ.lua         # Phase A (grows through B)
planning/xyz-port-plan.md         # this doc
```

PKGVERSION bumps per phase:
- 0.1.0.14 → 0.1.0.15 (Phase A: Nested skeleton)
- 0.1.0.15 → 0.1.0.16 (Phase B: Folded)
- 0.1.0.16 → 0.1.0.17 (Phase C: Coupled)
- 0.1.0.17 → 0.1.0.18 (Phase D: curve tune + Z transitions)
- 0.1.0.18 → 0.1.0.19 (Phase E: final name + polish), or jump to 0.1.0 for first release

## Why this plan respects all established rules

- `feedback_atoms_as_components`: AllpassMono ships without Lua unit. XYZ itself IS a unit.
- `feedback_aw_atom_port_template`: hybrid float from Phase A.
- `feedback_no_third_party_branding`: codename for development, habitat-native name at release.
- `feedback_identical_means_identical`: Spiral math is upstream-faithful; APF formula is canonical Schroeder.
- `feedback_no_out_of_line_virtuals`: header-only atoms throughout.
- `feedback_disable_tree_vectorize_am335x`: package mod.mk already enforces.
- `feedback_always_build_both_arches`: every phase build runs both `ARCH=linux` and `ARCH=am335x`.
- `feedback_linux_build_auto_install`: each linux build copies pkg to `~/.od/rear/`.
- `feedback_package_version_bump`: PKGVERSION bumps per phase.
- `feedback_option_vs_parameter`: Z is a Parameter with internal threshold (3 regimes, > 2 stops, fits the Parameter-not-Option rule).
- `feedback_comparator_gate_threshold`: N/A (no gate inlets).
- `feedback_persist_plans_to_repo`: this plan doc lands before code.
- **RotCoat lessons**: applied throughout — see "Lessons applied" section at top.
