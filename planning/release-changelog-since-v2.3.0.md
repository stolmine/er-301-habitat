# Habitat changelog: since v2.3.0

Status: **release-prep draft**, 2026-04-29.
Reference: 135 commits on `main` ahead of remote tag `v2.3.0`
(2026-04-17). Local `HEAD` = `fc6923e`.

This is the source of truth for assembling final release notes /
GitHub release body / forum / BBCode posts. Organized by **theme**
rather than chronology.

---

## TL;DR for release notes

- **Three brand-new units**: Ngoma (drum voice, spreadsheet), Alembic
  (sample-trained 4-op phase-mod matrix, catchall), Constant Random
  (biome utility).
- **Major package reshaping**: 8 Mutable Instruments ports
  consolidated into one `mi` package (was: clouds + commotio + grids
  + marbles + plaits + rings + stratos + warps); Alembic moved from
  spreadsheet to catchall (experimental tier); Pecto already in
  spreadsheet; Som experimental voice work moved to catchall.
- **Pecto zipper-noise fix**: Doppler smoother on baseDelay +
  float-precision-edge guard on tap indices. Multi-iteration debug
  closed at `2.5.5.188`.
- **Shift-handling audit**: paramMode UI convention finalized + 7
  decisions implemented across spreadsheet + biome (one Decision-8
  visual indicator bug pinned).
- **Saved-patch break**: see "Migration notes" -- the package
  consolidation means saved patches that referenced
  `clouds.Clouds`, `plaits.Plaits`, etc. won't auto-resolve. Re-load
  + re-bind.

---

## NEW units

### Ngoma (analog macro drum voice, **spreadsheet**)

A self-contained drum-like voice. Six top-level Pattern A controls
(V/Oct, Character, Sweep, Decay, Level, Trigger) plus expanded
auxiliary controls. Internal architecture:

- 4-source mix (carrier osc + detuned osc + sub-sine + 3 inharmonic
  membrane modes), all summed in a 2× oversampled inner loop.
- NEON-vectorized 4-lane phasor + polynomial sine on Cortex-A8
  (-6% CPU on the optimization path).
- Per-partial NEON envelope decay quad with feature-tied decay
  scaling (kick = short, cymbal = long).
- Pitch-morph membrane mode ratios (55 Hz wide spread → 440 Hz
  tight cluster).
- Grit-knee saturation, optional EQ, clipper, level limiter.
- Rotating cube viz on Pattern A's Character control.

Shipped progression: `2.5.0 → 2.5.5.175`. Heavy Cortex-A8 NEON
codegen learning along the way (see "Codegen lessons learned"
below); xform/randomize feature deferred to a later release after
hardware crashes traced to dispatch-shape codegen.

### Alembic (sample-trained 4-op phase-mod matrix, **catchall**)

Brand-new sample-driven synthesis voice. Loads any audio sample,
analyzes once at commit time, populates a 64-slot per-instance
preset table from per-pick features. Three independent scan
positions traverse the trained dataset.

- 4-op phase-mod (PMM) voice with a 4×4 matrix routing structure.
- 256-entry per-node wavetable shaper with K=4 frame blend and
  feature-driven wavefolder.
- 2× TPT SVF filter pair with topoMix / bpLpBlend / shared Q /
  drive, all feature-trained per-node.
- 10-source × 6-destination × 8-lane mux routing matrix,
  trained per-node, hard-cut single-slot.
- 24-tap multitap comb (Pecto-identical engine clone) with 16
  patterns × 4 slopes × 4 resonator types (raw / Karplus /
  Clarinet / Sitar) + Karplus-Strong / sitar feedback.
- **Ferment** chaos scalar macro: single fader 0–1.5, multiplies
  trained matrix + routing attens. 0 = clean tonal voice; 1 =
  trained chaos; 1.5 = boost.
- Rotating Voronoi-style sphere viz on the scan ply.
- **Phase 8 non-audio derivations**: Order 2 scan-neighbor
  gradients into detune offsets, Order 3 topology metrics
  (eccentricity + sparsity) into wavetable blend bias,
  meta-mapping from sample-level features (variance, mean
  entropy, length).
- **Phase 8 sample-pointer excitation**: voice reads the trained
  sample at runtime as a node-offset modulator. Direct injection
  + matrix routing of source 10. User-controllable via depth
  fader on the scan ply's shift sub-display.
- **Phase 8e** trained per-node filter dry/wet ratio (no user
  knob, fully feature-trained).

Phase 1 → 8e shipped across `2.5.5.120 → 2.5.5.181` in spreadsheet,
then Move 1 (commit `7947fcb`) relocates Alembic to catchall for
v1.0 release scoping. **Phase 6 serialization is still pending** --
quicksave round-trip won't preserve trained state across reboots.
Sample re-analyzes on re-load.

### Constant Random (utility, **biome**)

New utility unit -- single fader sets a probability; output is
either the held last value or a fresh random in the configured
range, picked at clock edges.

---

## PACKAGE RESHAPING

### `mi` (new): 8 packages → 1

The 8 Mutable Instruments port packages collapsed into one
consolidated `mi` package v1.0.1 (was: `clouds`, `commotio`,
`grids`, `marbles`, `plaits`, `rings`, `stratos`, `warps`):

- 9 units ship: Plaits, Clouds, Rings, Grids, Warps, Stratos,
  Commotio, MarblesT, MarblesX. Functionality unchanged.
- Cross-package source dep eliminated (Stratos's
  `clouds/dsp/{frame,reverb}.h` reuse + Rings's
  `plaits.EngineSelector` Lua reuse are now intra-package).
- Single shared stmlib build de-duplicates the previously per-pkg
  stmlib copies.
- Unit category in the picker is "MI" (replaces the previous
  per-pkg "Mutable" category).
- Saved patches that referenced `clouds.Clouds`, `plaits.Plaits`,
  etc. **won't auto-resolve**. Users re-load patches and re-bind
  units.

### Alembic moved spreadsheet → catchall

Per release scoping: Alembic v1.0 phase work is shipped but the
voice is "interesting but not at the bar for spreadsheet ship".
catchall houses experimental/WIP voices (Sfera, Lambda, Flakes,
Som). Alembic ships there for release exposure without anchoring
spreadsheet.

### Pecto already in spreadsheet (carryover)

Pecto migrated biome → spreadsheet during the v2.4.x shift-audit
session (commit `21dfa22`). Saved patches that referenced
`biome.Pecto` need re-binding to `spreadsheet.Pecto`.

### Som moved spreadsheet → catchall (early in cycle)

Som with Stuber voice engine + Voronoi viz development happened
early in this cycle, then moved to catchall as experimental work
not ready for spreadsheet flagship status (commit `81538b1`).

### Final package set (8, was 16)

`mi` (new), `spreadsheet`, `biome`, `catchall`, `kryos`, `peaks`,
`scope`, `porcelain`. Build target list in top-level Makefile
collapsed accordingly.

---

## Pecto improvements

### Zipper-noise fix (spreadsheet `2.5.5.184` → `.188`)

combSize and V/Oct knob motion produced audible zipper noise. Solved
in three iterations:

- `.184`: Doppler-style smoother on baseDelay scalar (linear ramp
  over 25 ms, snap-on-done semantics matching `od::Delay`'s
  pattern). Continuous Doppler glide during sweeps; musical fit
  for a comb filter.
- `.185`–`.187`: NaN-safe upstream guards + IIR finite-reset
  (DC-blocker, Karplus LP state, sitar envelope follower).
- `.188` (final): **float-precision-edge guard on tap idx0**.
  Latent ulp-edge bug in the multitap wrap math: when `p` is
  barely-negative below `ulp(maxDelayF)` (~0.0078 at maxDelay
  ~96k), `p + maxDelayF` rounds to exactly `maxDelayF` and
  `(int)maxDelayF = maxDelay` → `buf[maxDelay]` OOB. Continuous
  baseDelay smoothing made the read pointer slide across all
  positions and occasionally land in this danger zone, eventually
  triggering a hardware data abort under sustained V/Oct + size
  double-modulation. Symmetric `idx0 >= maxDelay -> 0` guard
  matches the existing `idx1` wrap.

Hardware-verified stable through long modulation runs at `.188`.

### Migration notes for Pecto users

The smoother changes the perceived character of `combSize` /
`V/Oct` knob sweeps -- you now hear a smooth pitch glide instead
of an abrupt zipper-like step. If you specifically liked the
abrupt step character, this is a behavior change. (Future TODO:
expose Doppler slew time as a sub-param so 0 = snappy,
larger = sweeping tape character.)

---

## Shift-handling audit

Locked 7 decisions for the paramMode UI convention across
spreadsheet + biome:

- D1: shiftUsed flag suppresses toggle on any encoder touch
  during shift-hold.
- D2: Pattern C secondary-toggle drop.
- D3: subGraphic swap unified across Pattern A and C.
- D4: invert-flag normalization → unified `paramMode` state.
- D5: shift+sub in paramMode → keyboard for that readout.
- D6: Pecto migration (deferred separately, now done).
- D7: paramMode preserved across cursor leave/return; not
  serialized; paramFocusedReadout reset on leave.
- D8: subGraphic visible highlight indicator on paramMode entry
  (partial fix: subCursorController set to bias-bound default,
  but visible indicator still missing -- **PINNED for next
  release**).

PR1 (Pattern A persistence + inverted-flag normalization + Pattern
C migration) and PR2 (shift+sub keyboard rollout) both shipped.

---

## Codegen lessons learned (Cortex-A8 hardware-only crashes)

Three new memory entries persisted from this cycle, each documents
a real crash mechanism:

- **Snap-and-fade dual pipeline doubles NEON register pressure** on
  multitaps, producing trap-shaped `:64` codegen on Cortex-A8.
  Pecto attempted SDK Delay's two-read crossfade pattern at 24 taps
  in `.182/.183` and crashed; pivoted to single-pipeline Doppler
  smoother in `.184+`. **Prefer Doppler glide for multitap combs;
  reserve crossfade for clean-pitch single-tap delays.**
- **Float-precision-edge wrap bug** described above. Latent in any
  multitap delay using `if (p < 0) p += maxDelayF; idx = (int)p`.
  Surfaces only under continuous read-pointer slew. Symmetric guard
  on idx0 + idx1 needed.
- **NEON `:64` hint count alone is not a crash predictor** -- shown
  empirically during Ngoma debug. Pecto ships with 16 hints in ctor
  and runs cleanly. Trap mechanism is more specific than just hint
  presence: register-pressure spills + auto-vectorized init/copy
  loops are the actual surfaces.

Plus an investigation framework added: `tools/check-neon-hints.sh`
runs am335x objdump pre-flight and flags trapping `:64` patterns.

---

## Build / infra

- **aarch64 host detection** in `mods/spreadsheet/mod.mk` (RPi 4
  dev rig support) -- detects `uname -m` and drops `-msse4` for
  aarch64 hosts where NEON is in the base ISA. (Same pattern
  carried into the new `mi` package mod.mk.)
- **Install script update**: pick latest-version per package; new
  testing/SD cleanup scripts under `scripts/`.
- **`tools/check-neon-hints.sh`** (new): am335x objdump pre-flight
  for trapping NEON `:64` / `:128` hints. Documented in
  `feedback_multitap_idx_wrap_ulp.md` audit list.
- **`tools/run-emu-gdb.sh`** (new): launches the linux emu under
  gdb with auto-bt on SIGSEGV/SIGBUS/SIGFPE for hardware-crash
  reproduction in emu where possible.

---

## Planning / docs

New planning files in this cycle (all under `planning/`):

- `alchemy-voice.md` — Alembic full design doc (49-float preset
  row layout, signal chain, training pipeline).
- `alembic-phase-8.md` — Phase 8 attack plan (Order 2/3,
  meta-mapping, sample-pointer).
- `drum-voice.md` — Ngoma design notes.
- `just-friends.md` — port-candidate scoping.
- `ngoma-debug-pipeline.md` — three-tier debug pipeline persisted
  during the long Ngoma crash investigation.
- `pecto-zipper.md` — Pecto Path A/B/C attack plan.
- `release-package-consolidation.md` — Move 1/2 plan.
- `release-changelog-since-v2.3.0.md` — this file.
- `redesign/00-overview-and-roadmap.md` through `13-shipping-and-
  field-service.md` — 14-doc CM4/CM5 successor hardware design
  series (architectural / not for this release; lives in repo as
  reference).
- `multi-output-units-author-guide.md` (under `docs/`) — author
  guide for the multi-output unit framework shipped in stolmine
  fork.

---

## Migration notes for users

- **Saved patches with old MI port references**: re-load patches.
  Any unit that resolved against `clouds.Clouds`, `plaits.Plaits`,
  `rings.Rings`, etc. now needs to resolve against
  `mi.Clouds` / `mi.Plaits` / `mi.Rings` / etc. The saved-patch
  format references units by package-prefixed module path, so
  resolution will fail and you'll see "missing unit" indicators
  on chains that referenced the old path.
- **Pecto in saved patches**: same issue -- if a saved patch
  pre-dates the v2.4.x Pecto-from-biome migration, it'll
  reference `biome.Pecto`. Now lives at `spreadsheet.Pecto`.
- **Pecto knob-sweep character changed**: zipper noise replaced
  by Doppler glide on combSize / V/Oct sweeps. If you specifically
  liked the zipper-step character, this is a behavior change you
  may notice. Future release will expose slew-time as a sub-param.
- **Alembic in saved patches**: any patch from the brief window
  Alembic shipped in spreadsheet (Phase 5d-4 to .181) needs the
  unit reference updated to `catchall.AlembicVoice`. Phase 6
  serialization isn't shipped, so trained state won't persist
  across reboot anyway -- expect the sphere viz to look "fresh"
  after any reload.

---

## Known issues going into release

- Decision 8 visible-highlight bug on Helicase / Larets / MixInput
  paramMode shift sub-displays (per-pkg pin in `todo.md`).
- Pecto Doppler slew time is hard-coded to 25 ms; sub-param TODO.
- Alembic sample-swap-without-detach can fail to retrain reliably
  (Sample.Pool race); workaround: detach-attach via menu.
- Alembic comb retrofit (Doppler smoother + idx wrap ulp guard +
  isfinite IIR reset) hasn't yet been ported from Pecto into
  Alembic's comb -- latent at block-rate, not crash-causing.
  Tracked in `todo.md` Alembic section.
- Alembic Phase 6 serialization not yet shipped -- trained state
  doesn't persist quicksave round-trip.

---

## Suggested release version

`v2.4.0`. Major bumps justified by:
- New `mi` package (1.0.0 fresh public release).
- Two new units in spreadsheet/catchall (Ngoma, Alembic).
- Pecto behavior change (zipper smoothing).
- Saved-patch break for the 8 retired MI packages.

Patch / minor bumps elsewhere:
- `spreadsheet` 2.6.0 (release)
- `catchall` 0.3.0 (Alembic added)
- `biome` 2.2.0 (Pecto removed earlier; Constant Random added)
- `mi` 1.0.1 (post-1.0.0 GridsCircle SWIG fix)

## Cross-references

- `planning/release-package-consolidation.md` — Move 1/2 plan + tradeoffs
- `feedback_doppler_basedelay_smoother.md` — Pecto smoother pattern
- `feedback_multitap_idx_wrap_ulp.md` — idx wrap fix
- `project_ngoma_codex.md` — Ngoma architecture + bisect history
- `project_alembic_codex.md` — Alembic architecture
- `planning/shift-handling.md` — D1–D8 audit decisions
- `planning/alchemy-voice.md` — Alembic full design
