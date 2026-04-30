# jf/ — 4-lane NEON DSP for the JF voice

NEON 4-lane SIMD primitives composed for habitat's JF unit (mods/spreadsheet/JF.cpp).

## Attribution

The 4-lane polyphony pattern (`float32x4_t` *is* the per-voice fan-out, struct-of-arrays implicit, one group of 4 lanes per `Voice` instance, multi-group composition for >4 voices) is adapted from tomf's `er-301-custom-units` module **polygon** (`https://github.com/tomf/er-301-custom-units/tree/master/mods/polygon`). The pattern has shipped on Cortex-A8 hardware in polygon since its release without the GCC `:64`-hint codegen issues that have bitten other habitat NEON forays — see `feedback_neon_intrinsics_drumvoice.md` and `feedback_neon_hint_surfaces.md` in the auto-memory archive.

Vendoring uses tomf's verbal blessing to learn from his implementations. The code in this directory is JF-specific composition (slope-engine state machine with Cycle / Transient / Sustain dispatch + INTONE ratio morph + per-voice trigger inputs); the *pattern* itself is tomf's, the specific composition is fresh-written for JF.

Files:
- `voice.h` — `jf::four::Phase`, `jf::four::GateToTrigger`, `jf::four::Voice`. 4-lane NEON state. JF.cpp instantiates two `Voice` objects (8 lanes total, 6 active for the 6-voice unit, 2 lanes masked off).

## Why not vendor polygon's `common/dsp/*` wholesale

JF's needs are simpler than polygon's: no DualPhaseReverseSync, no SVF filter, no Plaits-style detune-relative-pitch tracker. Vendoring the full ~3000 lines of polygon's common/ would import surface area JF doesn't use. Per repo policy (`feedback no third party branding`, cross-package dependency audit), vendored code lives intra-package and stays minimal.
