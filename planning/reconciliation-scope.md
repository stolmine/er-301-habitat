# Reconciliation scope: written record vs HEAD

Execution plan for ledger item `reconcile-notes-memory-codebase` (ledger.toml:1896).
Produced 2026-07-22 by a bounded Fable scoping pass (4 audit subagents, one per
surface; every P1 finding re-verified first-hand against HEAD). This doc SCOPES the
reconciliation - nothing has been fixed yet. HEAD at time of audit: a714a94.

Legend: **P1** = actively misleading (an agent or the user acting on it would do
wrong work). **P2** = materially outdated (wrong paths/versions/status, low harm).
**P3** = cosmetic. "Verified" = re-checked against code/git first-hand by the
orchestrator, not just asserted by an audit agent.

---

## Surface 1: planning/ledger.toml

160 items: 43 done, 108 todo, 7 wip, 2 blocked. All 9 wip/blocked STATUSES were
judged accurate (including both blocked items: `kryos-load-hang`,
`scope-headerless-research` - genuinely still blocked). The real drift is one stale
`touches` path and four `todo` items whose work is finished in code.

### L1. `vitrail-unit` touches path stale - P1, verified
- ledger.toml:1577 (status wip, line 1579; touches at ~1582): `touches` lists
  `mods/spreadsheet/Vitrail.*` + `mods/spreadsheet/assets/Vitrail.lua`.
- Truth: Vitrail demoted spreadsheet->biome in 87063fe (2026-07-21). Verified:
  `mods/spreadsheet/Vitrail.*` does not exist; `mods/biome/Vitrail.{h,cpp}` +
  `mods/biome/assets/Vitrail.lua` do. The sibling item `vitrail-ux-fixes`
  (ledger.toml:1905+) already has the correct biome paths and self-flags this drift.
- Fix: update `vitrail-unit.touches` to the biome paths. 1 edit.

### L2. `spectrum-ply-versions` status todo but shipped - P1, verified
- ledger.toml:1809, status "todo" (line 1811). The item's own note ends
  "DONE 2026-07-22 (scope 1.2.3...)" - and code went further: commits a12c540
  (512-pt FFT, 1.2.4), bf93539 (sub-display, 1.2.5), 32af203 (polish, 1.2.6);
  scope mod.mk is at 1.2.6 and toc lists Spectrogram 3/4/6.
- Fix: flip to done + attest; append one line covering 1.2.4-1.2.6 (the note stops
  at 1.2.3). Residual: on-device sub-display render check was still owed per
  bf93539 - carry that caveat into the attestation. 1-2 edits.

### L3. `fademixer-6-8-plus-mutesolo-fix` status todo but shipped - P1, verified
- ledger.toml:1832, status "todo" (line 1834). Shipped complete in 202a59d
  (biome 2.2.1.4): unit-local mute/solo fix + 6/8-input variants; biome toc lists
  Fade Mixer 6/8.
- Fix: flip to done + attest. 1 edit.

### L4. `house-suppress-customs-optimize-ports` status todo but decided-closed - P2, verified
- ledger.toml:1851, status "todo" (line 1853). In code: RotCoat/Filament/Carriage
  suppressed from house toc (0.1.0.37); hybrid-float landed for
  Console0/ChromeOxide/Lacquer/kWoodRoom/WoodenBox/Verbity (0.1.0.38-42); and
  bde8bd0 (2026-07-22) recorded the closing decision: ~1% CPU win, memory-bound,
  "Ship the 7 as-is, skip Galactic/BA3, release house as-is."
- Fix: flip to done (or wip only if "release house" is meant to live inside this
  item) + append the bde8bd0 outcome to the note. 1-2 edits.

### L5. `house-hybrid-float-retrofits` superseded - P2, verified via L4
- ledger.toml:1192, status "todo" (line 1194). Explicitly subsumed by
  `house-suppress-customs-optimize-ports` (that item's note says so), and its named
  conversions (kWoodRoom, WoodenBox) are shipped.
- Fix: close as superseded, pointing at L4's item. 1 edit.

### L6. `fabula-am335x` note text behind code - P3, agent-verified only
- ledger.toml:1256 (wip - status itself accurate). Note's "OPEN: high-Size
  distortion" text predates d013b39 (SR/2 tank rate, 2026-07-15) and a
  drywet-crossfade fix touching APFTank.h.
- Fix: one-line note refresh. Optional. 1 edit.

### Non-findings (checked, no action)
- `ngoma-hardware-hang` (ledger.toml:541) is already status=done, stamped
  2026-07-22T11:04:12 - the "sat blocked" premise in the reconciliation item is
  itself stale. The attestation honestly says the exact fix commit was never
  identified and delegates that to THIS pass; see "Could not verify" below.
- Moire/Vivary/Tessera/Ferrum suppression (87063fe): spreadsheet toc has no
  entries for any of the four, sources remain on disk - matches the record; no
  ledger item contradicts it. `tessera-into-ngoma` correctly calls Tessera
  "in-package (suppressed) as the reference".
- Spot-checked touches/note paths across done/todo items: no other breakage.
  (`multimode-drum-voice-profile` touches a not-yet-created planning/refs dir, but
  the item is explicitly unstarted - expected-empty, not drift.)

---

## Surface 2: claude-memory vault
(~/.claude/projects/-home-bram-repos-er-301-habitat/memory/)

Five project/reference bodies are substantively wrong; three feedback files have
minor path rot; the ~70 feedback_* lesson files are otherwise durable (spot-checked
hard paths/versions - build-rule and pattern lessons hold).

### M1. `project_ngoma_codex.md` - P1, verified
- Says: osc1/2/3 morph core, membrane partials, wobble LFO, SVF noise, grit-knee,
  mPhaseBank/mPartialPhases members, sineLUT/neonAdvanceSines, "current package
  2.6.2.x".
- Truth: the entire engine was replaced by the Tessera modal lattice
  (a3956e3/aefe502/ce3a083/f312d80, spreadsheet 2.8.3.72-74). Verified first-hand:
  DrumVoice.h:12-17 documents "the modal lattice from Tessera.cpp ... transplanted
  verbatim"; none of the old symbols exist; DrumVoiceSineLUT.h include removed
  (the LUT file itself survives for Ferrum/Tessera); DrumVoiceRandomGateControl.lua
  deleted (addcf41). Still TRUE and worth keeping: the insert-crash bisect journey
  and its DrumCubeGraphic out-of-line-virtuals root cause.
- Fix: rewrite the architecture/modulator/NEON/knobs sections around the modal
  engine (point at planning/ngoma-tessera-integration.md section 4 + DrumVoice.h);
  keep the bisect/crash history as an explicitly historical section. Sync the
  MEMORY.md index line (I1).

### M2. `project_alias_synthesis_paradigm.md` - P1, verified
- Says: Mirror is a parking-lot idea; "next: design doc, then atom, then unit";
  soft-blocked behind Canals UI.
- Truth: Mirror is a shipped, live unit - verified `mods/spreadsheet/Mirror.cpp/.h`
  and spreadsheet toc.lua:25 lists it; long build-out through 2.7.1.38 (phosphor
  viz) with six mirror-* planning docs. An agent trusting this memory would draft a
  design doc for a unit that has been shipping for weeks.
- Fix: rewrite as "Mirror SHIPPED; paradigm status = what (if anything) is next for
  the family (Visadhara/Helicase/Ngoma/Rauschen refs)". Sync index line (I1).

### M3. `project_canals_redesign_state.md` - P1, verified
- Says: snapshot 2.7.1.20; "next scope = 4-input+normalling topology + UI".
- Truth: both shipped - verified Canals.h has mLowIn/mCentreIn (+High/VOct)
  normalling inlets; 0f3da40 (2.8.0.2) + 14b059b (2.8.0.12). Also missing: the
  audio-rate Span/Quality experiment shipped (2.8.1.6) then fully reverted
  (58705ce, 2.8.1.12) - a whole saga absent from this "read before resuming" doc.
  The vault's own MEMORY.md lines already cite Canals 2.8.1 patterns, contradicting
  this file.
- Fix: rewrite the state snapshot (current version, shipped topology/UI, the
  revert lesson, what actually remains). Sync index line (I1).

### M4. `project_alembic_codex.md` - P1, verified
- Says: files at mods/spreadsheet/AlembicVoice.* etc; 49-float preset row; state
  frozen at 2.5.5.164 with Phases 6-9 TODO.
- Truth: moved to mods/catchall/ (7947fcb) - verified files exist there and not in
  spreadsheet; `mPresetTable[64][50]` (AlembicVoice.h:210, verified) i.e. 50
  floats; Phase 8a-8e shipped through 2.5.5.181, only Phase 6 (serialization)
  pending per planning/alembic-phase-8.md. Also AlembicSphereGraphic is now
  header-only (no .cpp).
- Fix: rewrite location/preset-row/phase-status paragraphs. Sync index line (I1).

### M5. `reference_house_package.md` - P1/P2, verified
- Says: "DEPRIORITIZED INDEFINITELY (2026-06-16) ... don't propose new atoms ...
  treat as frozen; real state 0.1.0.36".
- Truth: house is at 0.1.0.42 (mod.mk verified); a hybrid-float optimization pass
  and a picker-suppression (RotCoat/Filament/Carriage) shipped 2026-07-21/22, ending
  in a "release house as-is soon" decision (bde8bd0). The don't-touch guidance is
  now contradicted by the record; the next likely action is a RELEASE.
- Fix: rewrite: current version, the 7 exposed units, the suppression, the
  hybrid-float outcome + its lesson pointer (feedback_f64_count_poor_cpu_proxy),
  and replace "frozen" with "ship-ready, pending release decision". Sync index (I1).

### M6. feedback_* path rot - P2/P3, agent-verified (paths spot-checked)
- `feedback_no_out_of_line_virtuals.md` (~line 107): names
  AlembicSphereGraphic.cpp:257 as an at-risk out-of-line draw() - already
  refactored header-only (2c7ad6c); the named risk is resolved. Could cause a
  wasted "go fix this" errand. Fix: mark that example resolved. P2.
- `feedback_stereo_pattern_selection.md` (~line 93): cites mods/biome/assets/
  Canals.lua; Canals lives in spreadsheet (de30755). Path fix. P3.
- `feedback_multitap_weighted_feedback.md` (~line 182): cites mods/catchall/
  Network.h; Network promoted to spreadsheet (881265a). Path fix. P3.

### M7. `project_spatial_glitch_cm4_unit.md` version nit - P3, verified
- Cites anamnesis 0.2.0.81; actual mod.mk is 0.2.0.83. One-token fix (or leave).

### M8. docs/claude-memory/ mirror is dead - P2, direction verified
- The in-repo mirror is frozen ~2026-05-11: dozens of newer vault files missing,
  6 shared files differ, plus a file the vault retired
  (project_ngoma_debug_pipeline.md) still present. (Two agents agree on the
  direction; their exact diff counts differ - see "Could not verify".)
- Fix: DECIDE, don't patch by hand - either delete the mirror (single source of
  truth = vault) or re-sync it via the brain-sync flow and add a sync step to the
  regime. User call; flag in execution pass.

### Sampled and healthy (no action)
project_jf_codex, project_fabula_am335x, reference_trinity_firmware_re,
project_ledger_regime_habitat, project_change_tracking_living_docs ("no code yet"
verified true), reference_release_v2_5_0/1, project_spreadsheet_effect_positioning,
idea-parking files. Remaining feedback_* files: durable lessons, no drift found.

---

## Surface 3: planning/*.md docs

Policy: flag with headers, don't rewrite - these are historical design records.

### D1. `drum-voice.md` - P1, verified
- Line 3: "Status: **planning**. No code yet." Plus whole-doc description of the
  old osc-morph/wobble/membrane engine as the design.
- Truth: shipped as Ngoma long ago, and the engine has since been REPLACED by the
  Tessera modal transplant. Already ledgered: `ngoma-viz-and-docs` verify text
  includes "drum-voice.md updated or superseded by the codex reference".
- Fix: SUPERSEDED header pointing at ngoma-tessera-integration.md +
  release-notes-ngoma-modal-engine.md; skim UI/viz sections for still-current bits
  before fully retiring. Coordinate with `ngoma-viz-and-docs` (don't double-track).

### D2. `moire-voice.md` - P2, verified
- Whole doc describes the v0 lattice voice as live; no mention of the resonant-
  network rebuild (898489b) nor the abandonment + picker suppression (87063fe).
- Fix: one-line SUPERSEDED/ABANDONED header (87063fe). Body stays as record.

### D3. `vitrail-unit.md` - P2, verified
- Line 18: "**Package: spreadsheet** (resonant/character territory; version
  2.8.3.31 -> .32)" and milestone text targeting spreadsheet swig/toc.
- Truth: demoted to biome (87063fe); follow-up work tracked in `vitrail-ux-fixes`
  with biome paths.
- Fix: successor note at top ("shipped in spreadsheet, demoted to biome 87063fe
  2026-07-21; paths below are historical").

### D4. `house-ports-optimization.md` - P3, verified
- Line 3 "Status: PROPOSED 2026-07-21" but an accurate "## OUTCOME (2026-07-22)"
  section already exists at line 194. Only the header misleads a skim-reader.
- Fix: header -> "Status: DONE 2026-07-22 - see OUTCOME". One line.

### Healthy (no action)
ngoma-tessera-integration.md, release-notes-ngoma-modal-engine.md, TODO.md
(regenerated 2026-07-22, matches ledger), todo-archive.md (intentional archive).
Zero other planning docs reference the old Vitrail path or treat Moire as active
(grepped).

---

## Surface 4: MEMORY.md index

88 bullets, 88 sibling files. Structurally sound: zero dangling pointers.

### I1. Index lines propagating body drift - P2 (blocks on M1-M5)
- The house (M5), Canals (M3), alias/Mirror (M2), Ngoma (M1), Alembic (M4) bullets
  repeat their files' stale claims (e.g. "0.1.0.36 frozen", "49-float preset row,
  Phase 5 done", "Mirror next: design doc").
- Fix: update these 5 bullets in the SAME commit as the body rewrites.

### I2. Unindexed file - P3, verified
- `feedback_model_means_model.md` exists (added ~2026-07-14) but has no MEMORY.md
  bullet - the only unreferenced file (verified: 0 index hits; mod_gain_default_zero
  has 1). Fix: add a bullet.

### I3. Date nits - P3, agent-reported
- House bullet says deprioritized "(2026-06-16)", file header says "as of
  2026-06-05" - resolves itself in the M5 rewrite.
- Fabula bullet "released 2026-07-15" vs file "merged to main 2026-07-14" - likely
  both true (merge vs release); treat as non-issue unless the M-pass touches it.

---

## Recommended execution approach

One bounded follow-up pass (Fable-orchestrated or direct), in this order - ledger
first because it is the machine-read surface the regime trusts most:

1. **Ledger batch** (mechanical, ~15 min): L1 touches fix; L2/L3 flip to done with
   attestations; L4 done + outcome note; L5 close-superseded; L6 optional note
   refresh. Regenerate TODO.md via the regime tooling (`scripts/dev`), commit
   `[hab:reconcile-notes-memory-codebase]`.
2. **Memory vault batch** (judgment, the bulk): rewrite M1-M5 bodies (M1 and M3
   need real care - preserve the durable history sections, replace the state
   sections), then M6 path fixes + M7, then I1/I2 index edits in the same commit.
   Use the verified facts in this doc as the source; re-verify any claim not marked
   "verified" before writing it into a memory.
3. **Planning-doc headers** (mechanical, ~10 min): D1-D4 supersession/status
   headers. D1 coordinates with the open `ngoma-viz-and-docs` item - either do its
   drum-voice.md clause here and note that in its ledger entry, or leave D1 to it.
4. **Mirror decision** (M8): ask the user - delete docs/claude-memory/ or wire a
   sync. Do not hand-patch 60+ files either way.
5. Close `reconcile-notes-memory-codebase` with an attestation listing what was and
   was not done (esp. the unresolved ngoma-hang fix-commit identification below).

**Estimated edit count**: ~20 files touched, ~35-40 discrete edits.
Ledger: 1 file, 6 items. Vault: 9 files + MEMORY.md. Planning: 4 header edits.
Mirror: 1 decision (delete = 1 `git rm -r`). Suggested as 3-4 commits (one per
batch), all tagged `[hab:reconcile-notes-memory-codebase]`.

## Could not verify (flagged, not guessed)

- **ngoma-hardware-hang exact fix commit**: the ledger's own attestation says the
  hang's resolution was user-observed but never bisected to a commit, and assigns
  identification to this pass. Nothing in this audit identifies it either (the
  engine has since been wholesale replaced, so the original faulting code path may
  be gone entirely - a bisect may now be moot). Recommend: record "moot after
  engine transplant" in the item rather than spending a hardware bisect, unless the
  user wants the forensic answer.
- **Mirror diff exact counts** (M8): one agent reported 64 total differences,
  another ~45 missing + 6 differing + 2 repo-only. Direction (mirror dead since
  May) is consistent and trusted; exact counts are not. Irrelevant if the mirror is
  deleted.
- `project_multi_output_framework.md` "no habitat unit ships on it yet" - judged
  defensible (JF/Canals use an ad hoc sub-out pattern predating the framework);
  not confirmed false. Leave unless someone who knows the intent says otherwise.
- `project_uconsole_deploy_flow.md` - procedural, internally consistent, not
  re-verified against the CM4 rig (no device access). Low risk.
- On-device residuals noted in shipped items (Spectrogram live sub-display render;
  Ngoma hardware insert/CPU pass from f312d80): not verifiable without the rig;
  keep them as attestation caveats, they are not drift.
