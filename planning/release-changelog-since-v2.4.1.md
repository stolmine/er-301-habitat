# Changelog since v2.4.1 (raw source for v2.5.0 release notes)

204 commits between `v2.4.1` and HEAD. Grouped by theme. Versions
shown are the dev-iteration bumps that landed; final ship versions
are `spreadsheet 2.7.0`, `mi 1.0.4`, `catchall 0.4.0`.

## NOT DOCUMENTED in v2.5.0 public release notes

JF (`mods/spreadsheet/JF.{cpp,h}`, `mods/spreadsheet/jf/`, `assets/JF.lua`)
Phase 1-5 landed (commits `4d7171c` through `c491395`, plus housekeeping
`2277a99`). Hex-voiced harmonically-coupled slope-engine voice based on
the Mannequins public tech map. Phase 6 polish remains (trig LUT sweep,
hardware CPU profile, test procedures). Per user direction this release
ships the binary but does not announce JF. "Open secret" until Phase 6
ships in a future release.

## Major themes

### 1. Cortex-A8 NEON codegen defense (late session, 2026-05-15/16)

Root cause finally pinned for a class of "works in emu, crashes on
hardware" bugs that had been masked by multiple workarounds across
sessions: GCC `-O3 -ftree-vectorize -ffast-math` (the upstream
CFLAGS.speed default) emits NEON vld1/vst1 with `:64`/`:128`
alignment hints that trap on Cortex-A8 when the address isn't
actually aligned.

- `4583e16` am335x: -fno-tree-vectorize as the durable defense against Cortex-A8 NEON traps + lint at link
- `bd3734c` mi 1.0.3.15: AAPCS NEON spill barrier (`mi_barrier_noop`) fixes Plaits/Clouds mode-switch crash
- All 9 mod.mk files now append `-fno-tree-vectorize` last
- `tools/check-neon-hints.sh` wired into every am335x link via `scripts/utils.mk`
- New memory `feedback_disable_tree_vectorize_am335x` (top priority)
- New memory `feedback_neon_aapcs_call_barrier` (now flagged as masking workaround)
- Full bisect record: `planning/mi-mode-switch-aapcs-barrier-resolution.md`

### 2. mi 1.0.4 — Plaits 2x oversampling on 6-op FM + Clouds parity

- `8d020e4`-`bd3734c`-onwards: AAPCS barrier landed at every audio-thread state swap point
- 2x oversampling on Plaits 6-op FM (Phoenix engine sel 5/6/7), 2-tap moving-average decimation, entirely contained inside `SixOpEngine::Render` (no template/inline changes to `fm::Voice`)
- Rings modal kernel tightening (mi 1.0.2 commit `a8c8a3c`)
- Clouds mode-switch (Granular / Looping Delay / Spectral) covered by the same barrier pattern as Plaits

### 3. Spreadsheet 2.7.0 — new unit Visadhara + major optimization arcs + new viz layer on Network

#### Visadhara (new unit)

PMM voice with 8 active voices on a NEON 4-lane voice bus, dedicated post-fold envelope, asymmetric per-voice detune, decoherent phase reset on trigger, golden-ratio phase offsets, pre-fold soft cap, 2x oversampling on the inner DSP loop. 7 plies (V/Oct with shift sub for octave register, Mode with spread/harm/morph paramMode subs, octave coarse/fine encoder stepping). Corona viz pipeline (vertical-standing K-gons on a carousel, per-pixel trigger shockwave bands, Fold field background, Harmonic radius + Morph star, photographic Fold inversion).

- Commits `f521fb4` through `e44f28c` (BIA-parity polish pass + asymmetric detune + post-fold env)
- Commits `c98b621` through `dd4c81d` (NEON voice bus + 8-voice trigger reset)
- Commits `1c177d6` through `bb2f790` (2x oversampling + UI refinement to 7 plies)
- Commits `b21522f` through `8e7d6b0` (golden-ratio phase offsets + tighter pitch envelope)
- Commits `0dc1582` through `de4723c` (Corona viz Phase 3a through Phase 3e + Fold field reworked to radial-wave interference + frame-cache the Fold contour field)

#### Helicase polish

- `8d020e4` 2.6.2.51-.52: morph-hoist polish + discFold optimizations
- `0ebc904` 2.6.2.50: polynomial sin/tanh replacements + carrier shape ply parity

#### Petrichor

- `0fd8fef` 2.6.2.49: default buffer 20s -> 5s
- `b342898` 2.6.2.48: multi-tap weighted feedback (delay character)
- `a9560c6` 2.6.2.47: 3-pass NEON SoA SVF bank (Pecto-style restructure)

#### Impasto + Parfait

- `ad77be6` 2.6.2.46: CPU reduction (NEON SoA + fast paths)

#### Filterbank (formerly Tomograph)

- `8c7b79b` 2.6.2.42: NEON SVF bank — 8% stereo CPU at 16 bands
- File-level `no-tree-vectorize` pragma + padding pattern (canonical SoA SVF bank reference, see memory `feedback_neon_soa_svf_bank`)

#### Network reverb — large multi-pass arc

Originally in catchall, promoted to spreadsheet mid-cycle (`881265a` Promote Network from catchall to spreadsheet). Multi-pass FDN rebuild attempts (`58ed84d` Hadamard 16x16 FDN cross-feed, `bd6915b` Jot per-group T60 attenuation, multiple per-group LP / DC blocker / wet-bus compensation passes) culminating in a revert to the V1 parallel-multitap engine at `53466da` Network 2.6.2.0: revert cascade FDN rebuild. CloudSeed / Faust reverb port candidates noted as Network cascade-FDN follow-up.

Network viz redesign: phyllotaxis sphere, listener-centered sphere, sonar + reflector flash, motion-driven Fibonacci perturbation, tilted 3D ping rings, sphere connection length cap to fix long-arm artifacts. ~25 viz iterations.

### 4. catchall 0.4.0 — Network character macro pass

Network spent most of its dev cycle in catchall before being promoted to spreadsheet. The character macro (gltch ply with G1-G8 modes) shipped here:

- `3cc3797` 0.3.28: Phase 3a — gltch ply + G3 mute + G1 S&H
- `c3c9da3` 0.3.30: Phase 3b — G2 stutter + G8 bitcrush/decimate
- `579e6bd` 0.3.40: Phase 3c — G4 transient events + G7 tap respawn
- `50efd87` 0.3.46: Phase 3c complete — G5 scrub + G6 reverse
- Stutter loop boundaries snap to zero crossings (`d460e2b`)
- NEON 4-wide stutter playback Opt 1 (`4a5f0eb`, `ab5481e` fix for stack-array `:128` hints, `aa9a8e9` re-implement with class-member scratch after `7d64452` reverted the first attempt due to hardware crash)
- simd_pow batch for G8 bitcrush level (`43b4cfc`, reverted in `c5e4989`)
- Equal-share CRUSH/SCRUB/REVERSE/NORMAL at glitch=1 (`16ce29e`)
- Sign-change ZC for stutter loop boundaries (`9801537`)
- Listener-relative stereo pan (`0045f74`)
- Cap active stutter taps at 16 (`f09ef8d`)

### 5. NEON optimization audit + planning

- `9e873cb` plan: NEON optimization audit (`planning/neon-opportunities.md`)
- `58633d5` plan: NEON audit refresh — mi recursive sweep + biome audit
- `ad97819` plan: NEON audit refresh — Filterbank/Parfait/Impasto reassessed
- `d16acfa` plan: NEON audit — correct MultitapDelay + Larets per code read
- `41a41a8` plan: Filterbank (Tomograph) NEON refactor
- `676ab20` plan: NEON audit — Filterbank marked DONE at 2.6.2.42
- `d42e5f2` plan: Filterbank NEON refactor — record hardware result (8% at 16x stereo)
- `01147a9` plan: Rings non-modal NEON pass + modal tighten
- `759b974` plan: close out Rings audit entry; Plaits moves to position #5
- `2aa418f` plan: Plaits per-engine NEON recon + 6-op FM noise notes
- `3962eda` plan: CloudSeed reverb port to ER-301 (separate unit)

### 6. Build infrastructure

- `6363777` Spreadsheet build: SWIG wrapper auto-tracks header changes
- SWIG dep tracking applied to all 9 habitat packages (per memory `feedback_swig_header_dep`)
- `329736d` Dev-box portability: install script + claude-memory rehydrate snippets
- `56d3c53` docs: snapshot Claude auto-memory for dev-box portability
- `tools/check-neon-hints.sh` lint integration (new this session)
- `mods/mi/MiBarrier.cpp` (new) — provides `mi_barrier_noop()` for the AAPCS barrier
- All mod.mk files updated to append `-fno-tree-vectorize` last

### 7. Memory + KB additions

- `feedback_disable_tree_vectorize_am335x` (TOP PRIORITY)
- `feedback_neon_aapcs_call_barrier` (now banner-flagged as masking workaround)
- `feedback_always_build_both_arches`
- `feedback_multitap_weighted_feedback`
- `feedback_neon_soa_svf_bank`
- `feedback_neon_voice_bus_template`
- `feedback_neon_no_gather_lut_dsp`
- `feedback_no_paths_of_least_resistance`
- `feedback_no_assertions_without_code_evidence`
- `feedback_no_parenthetical_descriptions`
- `feedback_persist_plans_to_repo`
- `project_spreadsheet_effect_positioning`

## Migration / behavior notes for users

- **Plaits 6-op FM** (`mi.Plaits` engines 5/6/7): now runs 2x oversampled. Significantly less aliasing in the upper octaves on FM patches. Mild treble rolloff (~ -3dB at 12kHz) from the 2-tap MA decimation filter. If you preferred the brighter native-rate character, the OS factor is a one-line constant change in `eurorack/plaits/dsp/engine2/six_op_engine.cc` (`kSixOpOversampling = 1`) for a future build.
- **Plaits/Clouds mode/engine switching**: deterministically fixed on Cortex-A8. The previous "occasionally crashes when switching" symptom is gone.
- **Petrichor**: default buffer dropped 20s -> 5s. Patches that loaded with the previous 20s default may sound shorter on load until the buffer size param is re-set.
- **Petrichor**: multi-tap weighted feedback (delay character) replaces the previous single-tap source feedback. Sound is closer to a multi-tap delay, less like a resonator.
- **Network**: V1 parallel-multitap audio engine retained after a cascade FDN rebuild was attempted and reverted. No behavior change vs the v2.4.1 baseline.
- **Helicase**: polynomial sin/tanh replacement saves ~5% CPU on lofi mode. Audible character unchanged.

## Package version summary

| Package | v2.4.1 | v2.5.0 | Notes |
|---|---|---|---|
| spreadsheet | 2.6.1 | 2.7.0 | Visadhara new; Pecto/Petrichor/Filterbank/Helicase/Impasto/Parfait/Network polish + NEON |
| mi | 1.0.1 | 1.0.4 | AAPCS barrier; Plaits 6-op FM 2x OS; Rings modal kernel |
| catchall | 0.3.0 | 0.4.0 | Network character macro arc; later promoted to spreadsheet |
| biome | 2.2.0 | 2.2.0 | mod.mk lint touch only; no behavior change |
| kryos, peaks, porcelain, scope, stolmine | unchanged | unchanged | mod.mk lint touch only |
