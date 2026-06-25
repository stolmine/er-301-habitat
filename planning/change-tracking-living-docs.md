# Living Documentation and Change-Tracking for er-301-habitat: A Decision Aid

> Research synthesis, 2026-06-25. Produced by a 7-agent workflow (4 web
> prior-art surveys + 2 codebase-surface maps + synthesis), ~246k tokens.
> Scope: a general theory and practical tooling for noticing when anything
> meaningful drifts (module contracts, behavior, ports, new units), with
> deterministic gates and AI reserved for sweeps, not the gate. The I/O
> contract map is one instance of the broader idea.

## 1. The general pattern: Baseline-Diff-Gate (BDG)

There is one pattern under all of this. Name it Baseline-Diff-Gate:

> A deterministic artifact is derived from a source of truth, committed
> next to that source, regenerated and diffed on every run, and any
> difference halts the build until a human either accepts the new baseline
> (intended change) or fixes the code (a bug).

Four invariants: deterministic generator, committed baseline, automated
diff, accept-or-fix gate. Plus two hygiene rules: orphan GC, and
"small/legible or review rots." Every tool in the surveys is a different
payload behind this identical control flow:

- VRT -> payload is a rendered screenshot; diff is pixel delta.
- Snapshot tests (Jest/Vitest/insta/syrupy) -> payload is a serialized
  value; accept is `-u` / `cargo insta review` / `--snapshot-update`.
- API-report manifests (API Extractor `.api.md`, Roslyn
  `PublicAPI.{Shipped,Unshipped}.txt`, cargo-public-api `public-api.txt`,
  libabigail `.abi`) -> payload is a sorted, body-free surface manifest;
  diff is the committed file vs a freshly extracted one; gate is a failing
  CI diff and/or a CODEOWNERS rule on the file path.
- Golden-master / characterization tests (ApprovalTests, TextTest, Go
  `-update` golden files) -> payload is "whatever the program currently
  outputs"; you do not author assertions, you review diffs.
- Changesets -> payload is a committed Markdown file declaring release
  intent; the gate is that no release happens without one.

The deep insight: BDG collapses assertion-authoring into output-review.
You never enumerate what is correct; you commit what the code currently
does and review the diff. That is why it covers behavior you never thought
to assert, and why it generalizes past "tests" to an audio render, a
serialized struct, or an I/O contract manifest. The I/O contract map is
just one payload.

Two load-bearing sub-ideas the surveys converge on:

- Printer/verifier split (golden-master survey, ApprovalTests): keep the
  generic verify+accept+diff machinery; invest your complexity budget in a
  domain-specific printer that renders a hard artifact (audio, a Lua table,
  a binary) into a stable, diff-friendly surrogate. The single most
  transferable concept for the DSP-output case.
- Extract-and-diff beats validate-against-spec (change-observability
  survey). For an imperatively-declared contract (this codebase: ports
  declared in C++ braces, not a schema), do not stand up a parallel schema
  you must keep in lockstep. Extract a manifest from the code and diff the
  committed snapshot against a fresh extraction. The code stays the single
  source of truth; the manifest is derived, never a second truth. This
  matches the repo's existing precedent (`tools/check-graphic-virtual-defs.sh`,
  the NEON-hint objdump check).

The crucial constraint, the gate is deterministic and AI is not in it, is
satisfied natively. Every BDG gate passes/fails on byte equality, a
compiler exit code, or a symbol-list diff. AI's place is outside the gate
(Section 4).

## 2. The 3-4 most proven + applicable techniques, ranked

### #1 - Extract-and-diff surface manifest (the .NET `PublicAPI.{Shipped,Unshipped}.txt` model). Winner, not close.

- What it is: a checked-in, canonically-sorted, body-free text manifest of
  a module's public surface; the build regenerates it and fails on any diff
  vs the committed copy (Roslyn RS0016/RS0017; cargo-public-api
  `UPDATE_SNAPSHOTS=yes`; API Extractor `.api.md` + CODEOWNERS; libabigail
  `abidw`/`abidiff`).
- Why it fits here: extraction is fully deterministic and single-source.
  Ports/params/options are brace-initialized scalar members under
  `#ifndef SWIGLUA` with inline string literals (no arrays, no dynamic
  ports, no macro indirection, single package namespace), so a regex over
  `od::(Inlet|Outlet|Parameter|Option)\s+(\w+)\{"([^"]+)"...\}` yields
  type+id+name+default cleanly (verified on Canals.h, JF.h, Mirror.h). The
  `.cpp` ctor `add*` calls and the `.swig` `%include` list give a second and
  third extractable set. Exactly the imperative-declared contract that is
  the right fit for extract-and-diff and the wrong fit for schema-first codegen.
- Closest existing analog in-repo: `tools/check-graphic-virtual-defs.sh`
  (source-grep, exits non-zero on hit) and the `neon_hint_check` make-macro
  wired into every `mod.mk` link rule. A manifest check is the same shape,
  one notch more structured.
- The Shipped/Unshipped split is a real gift: stable ports go in a "shipped"
  manifest; experimental ports land in "unshipped." It models the repo's
  dev-digit cadence (4th digit while iterating, 3rd at release).

### #2 - Golden-master / characterization audio render (ApprovalTests + Go `-update` model).

- What it is: render each unit on a fixed test patch, reduce to a stable
  surrogate, commit it, diff on every run; accept intended DSP changes,
  investigate unexpected ones.
- Why it fits here: the only technique that catches the category that
  escapes every static surface, `process()`-body changes (coefficient
  remaps, the `feedback_aw_param_default_subtle` "fill the travel" edits,
  NEON refactors) that alter audible output with zero signature change. The
  golden-master/characterization literature (Feathers) is the exact theory
  for "pin behavior you don't fully understand, then refactor under the
  net," which maps onto NEON rewrites and the Spiral/APF feedback-governor work.
- Why it ranks below #1: determinism is hard. Float/NEON is not bit-exact
  across arches (am335x has no DP NEON; `-ffast-math`; `-fno-tree-vectorize`).
  The printer/verifier split is the escape hatch: render on linux emu only
  (one fixed toolchain), and make the printer a tolerance-bearing surrogate
  (peak, RMS, spectral centroid, zero-crossing rate, plus a decimated/
  quantized waveform and a coarse hash), not raw bit-exact PCM. Bit-exact is
  the trap; a stable perceptual surrogate is the win.

### #3 - Doc-coverage completeness gate (rustdoc `missing_docs` / interrogate model, implemented as Cog `--check`).

- What it is: deterministically prove the code<->doc completeness invariant,
  every Outlet/param/option/unit has a description, and every description
  points at a real port. Emit two sets at build time (must-be-documented,
  has-a-description), compute the symmetric difference, exit non-zero if
  non-empty.
- Why it fits here: the docs map shows eight to nine uncoupled doc
  locations, zero generated from another, and that the `addOutput`-without-
  label / label-without-`addOutput` asymmetry is a shipped bug class (Canals
  2.7.1.7->.15; the `// FIXED: was missing` comments still sit at
  Canals.cpp:71-73). A generator enumerating ports from the header/Lua would
  have caught the `addOutput`/`unitOutputNames`/missing-label bugs before
  device insert. Presence check (deterministic), not accuracy check.
- Why it ranks #3: lower value than #1/#2 until #1 exists, because the
  manifest from #1 is the "must-be-documented" set. Build #1 first.

### #4 - Changeset-style committed change record (changesets discipline, no JS tooling).

- What it is: one committed Markdown stub per releasable change (package +
  bump + human prose) that a release script aggregates into per-package
  CHANGELOGs and forces the `PKGVERSION` bump.
- Why it fits here: the strongest realization of "if it's checked in I can
  find when it changed," designed for monorepos, which maps onto this repo's
  many independently-versioned packages. Addresses `feedback_package_version_bump`:
  ER-301 only re-extracts when PKGVERSION changes, so a build-touching diff
  with no `mod.mk` change is an anomaly. A changeset file is the gate that
  forces a deliberate bump + note, replacing the two parallel hand-maintained
  changelogs (README `## Changelog` and `RELEASE-*.md`).
- Why it ranks #4: process/cadence, not a code-contract gate; complements
  but does not catch the bugs #1-#3 catch.

## 3. Menu of concrete applications (each its own track)

### Track A - Unit I/O contract manifest
- What: per-package committed file, e.g. `mods/spreadsheet/contract.txt`,
  one block per unit listing every inlet/outlet/parameter/option (type +
  member id + string name + raw default from `.h`), the ctor-registered set
  (from `.cpp`), the SWIG `%include` set, and Lua `channelCount`/`subOutLabels`.
  A `make`/precommit step regenerates and `diff`s it.
- Value: HIGH. Forces a manifest update on every port add/rename/remove;
  cross-checks declared-vs-registered (the `addOutput` bug),
  SWIG-exposed-vs-declared, and `subOutLabels`-count vs outlet-count.
- Effort: LOW-MEDIUM. One regex extractor (Lua or Python), reusing the
  brace-init convention. No new framework.
- Determinism: TIER 1 (fully byte/exit-code deterministic; static parse).
  One reconciliation to record: the C++ `od::Parameter` default is not
  always the user-facing default, the Lua `hardSet("Bias", ...)` fronts it
  (Canals.lua:123). The manifest should record both layers, not assume
  equality.
- Catches: added port without registration; rename that misses a Lua call
  site; silent SWIG omission; `subOutLabels`/`channelCount` drift; a
  build-touching change with no version bump if you fold `mod.mk:PKGVERSION`
  into the manifest.

### Track B - DSP behavioral golden-master
- What: a fixed test patch per unit rendered on linux emu; reduce to a
  surrogate (summary stats + decimated waveform + hash) committed as the
  baseline; diff on demand; accept with an `-update` flag (Go golden-file
  model) or an insta-style review queue.
- Value: HIGH and UNIQUE. Only thing that catches `process()`-body drift.
- Effort: MEDIUM-HIGH. Needs a headless render harness on the emu and a
  stable printer. This is where the budget goes.
- Determinism: TIER 2 with care. Render on one arch (linux emu) only. Use a
  tolerance-bearing surrogate, never bit-exact PCM. Seed/disable randomness
  (Visadhara, Rauschen, Ngoma randoms). Printer/verifier split mandatory.
- Catches: coefficient/gain remaps, "fill the travel" default remaps,
  feedback-governor tuning, NEON refactors that were supposed to be
  sound-preserving but were not (a NEON rewrite that should null against the
  scalar baseline but does not is exactly a characterization-test catch).

### Track C - Doc-coverage gate
- What: Cog generator (or a plain script) enumerates ports/units from `.h` +
  `toc.lua`, emits the description manifest, `cog --check` (or `diff`) fails
  when a port/unit lacks a `description=`/`subOutLabel`, or a description
  references a port that no longer exists. Bidirectional symmetric-difference.
- Value: MEDIUM-HIGH (rises once Track A exists, since A supplies the
  "must-be-documented" set).
- Effort: LOW once A exists.
- Determinism: TIER 1 (presence/existence check, not content correctness).
- Catches: new Outlet with no label (the shipped Canals bug class); orphaned
  description after a port removal; `unitOutputNames`/`subOutLabels` cap
  mismatches. Pair with Vale (deterministic, non-AI) to enforce the prose
  conventions already in memory: no third-party branding, no parenthetical
  clarifications, exact unit names. Vale enforces how prose is written; it
  cannot verify a description is true, that needs Track B or human review.

### Track D - Change observability
- What: a `changes/` dir of committed Markdown stubs (package + bump +
  prose); a release script aggregates them into per-package CHANGELOGs and
  drives the `PKGVERSION` bump, retiring the two parallel hand-maintained
  changelogs.
- Value: MEDIUM. Makes "what changed when" git-bisectable and consolidates
  README `## Changelog` + `RELEASE-*.md` into one generated output.
- Effort: LOW (discipline + a small aggregation script; no JS tooling
  needed, the publish leg is npm-centric but the change-tracking layer is
  language-agnostic).
- Determinism: TIER 1 for the gate (a release without a changeset fails; a
  `mod.mk` touch without one is anomalous).
- Catches: silent version non-bumps (stale device re-extract per memory);
  changelog drift; lost release intent.

## 4. AI-assisted vs deterministic

Deterministic (the gate, never AI):
- Contract-manifest diff (Track A): regex extract + `diff`/exit code.
- Golden-master diff (Track B): surrogate compare against tolerance.
- Doc-coverage symmetric-difference (Track C): set diff + exit code.
- Changeset/version-bump gate (Track D).
- Prose convention enforcement (Vale): regex/rule packs, the deterministic
  alternative to an LLM.
- Internal link/anchor integrity (`lychee --offline`) across `planning/`
  and `docs/`.

AI-assisted (outside the gate, big sweeps, linguistic judgment, reconciliation):
- Reconciling the unstructured `todo.md` against actual code state (the
  Parfait/Impasto exercise: read `MultibandSaturator.cpp`, hand-flip
  `[ ]`->`[x]`). Nothing parses code into todo today; genuinely linguistic +
  cross-file, the right AI job. AI proposes; a human accepts the diff.
- Authoring the human-readable changeset prose and the README per-unit
  description table (AI drafts, deterministic gate checks presence/format).
- Triaging an accepted golden-master diff: AI summarizes which metrics moved
  and correlates with the `process()` diff, but the gate already fired
  deterministically; AI only explains.
- Initial backfill: generating the first baselines and the first description
  sweep across ~20 units is a big-sweep AI job; once committed, the gates
  hold the bar.

Rule of thumb: AI generates and reconciles; the committed artifact and its
diff hold the line. If an AI step were load-bearing in a pass/fail decision,
it is misplaced.

## 5. Phased adoption path

Anchor on existing precedent so this feels native: the repo already ships
post-link/source-grep advisory lints defined as a `scripts/utils.mk` macro
and called from each `mod.mk` (`neon_hint_check`, fully wired;
`check-graphic-virtual-defs.sh`, written-but-unwired with a "CI hook: add to
pre-release checks" comment). The path below extends that exact muscle.

- Phase 0 - wire what already exists (a day). Turn on
  `tools/check-graphic-virtual-defs.sh` in the pre-release path it already
  advertises. Zero new code; proves the team will accept a failing
  (non-advisory) gate, since today's only wired check is `|| true` advisory.
- Phase 1 - Track A, one package, manifest-only, no doc-linking. Pick
  `spreadsheet` (richest: Canals/JF/Mirror already analyzed). Emit
  `mods/spreadsheet/contract.txt`; add a `make spreadsheet-contract-check`
  that regenerates to a temp file and `diff`s. Run it advisory (`|| true`)
  for one release cycle, then flip to failing. Include the
  declared-vs-registered check immediately, it catches the `addOutput` bug
  class the codebase already shipped. Accept via `make
  spreadsheet-contract-update` (Go `-update`-flag model), cleanest minimal
  expression for a hand-rolled harness in a non-JS repo.
- Phase 2 - Track A across all packages + Track C doc-coverage. Generalize
  the extractor (the brace convention is identical across packages). Layer
  the symmetric-difference doc gate on top, reusing Phase-1's
  "must-be-documented" set. Add Vale rule packs encoding the in-memory prose
  conventions. Now every port add forces a manifest touch and a description.
- Phase 3 - Track D changesets. Introduce the `changes/` stub + aggregation
  script; retire the duplicate changelogs.
- Phase 4 - Track B golden-master, one unit first. Build the headless emu
  render harness, pick one stable unit (not a noise/aliasing one), define
  the surrogate printer and tolerance, commit the baseline, prove a
  deliberate coefficient tweak shows up as an acceptable diff and a botched
  NEON refactor shows up as an unexpected one. Only then widen. Last because
  its determinism budget is the largest and its printer design is the only
  genuinely novel engineering; everything before it is regex + diff.

Stop-and-prove gates between phases: each phase ships advisory-first, runs
one release cycle, then flips to failing only after it has caught at least
one real drift or cleanly accepted one intended change. Keep each manifest
per-unit-blocked and human-legible; do not grow scope until the prior gate
has visibly earned its place.

## One-line verdict

Adopt the .NET `PublicAPI.txt` extract-and-diff model as Track A first
(highest value, lowest risk, native to the existing `tools/*.sh` precedent
and the imperative brace-declared contracts), treat golden-master audio
render as the eventual crown jewel for `process()`-body drift, keep AI
strictly outside every gate, and grow one package at a time.
