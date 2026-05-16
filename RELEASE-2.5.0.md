# er-301-habitat v2.5.0

Release date: 2026-05-16

Package updates: **spreadsheet 2.6.1 -> 2.7.0**, **mi 1.0.1 -> 1.0.4**, **catchall 0.3.0 -> 0.4.0**. biome, kryos, peaks, porcelain, scope unchanged.

## Highlights

Two new units, both in spreadsheet: **Visadhara** (8 osc additive bass/lead/drum voice taking after a certain popular digital Eurorack module) and **Network** (multi-tap spatial / glitch reverb of original design).

## New unit: Visadhara (spreadsheet)

Phase-modulation matrix voice (PMM), 8 voices wide. The voice bus runs as a NEON 4-lane structure (two quads, voices interleaved) so the inner DSP loop is fully vectorized. 2x oversampling on the inner DSP loop. Per-voice asymmetric pitch envelope, decoherent phase reset on trigger, golden-ratio per-voice phase offsets to avoid the cancellation patterns of correlated detune. Dedicated post-fold envelope; pre-fold soft cap to stay loud and clean simultaneously.

7 plies:
- **V/Oct** with shift sub for octave register (Bass / Alto / Tenor)
- **f0** with coarse/fine encoder stepping
- **trig**
- **Mode** with paramMode subs exposing Spread, Harmonics, Morph
- **fold**
- **level**
- **expanded** - full sub-display surface

**Viz**: vertical-standing K-gons on a carousel, per-pixel trigger shockwave bands, Fold contour field background with photographic inversion when Fold > 1, radial-wave interference field, Harmonic radius + Morph star overlays. Frame-cached so the field does not recompute every frame.

## New unit: Network (spreadsheet)

Multi-tap comb / glitch reverb. Optimized some but still heavy, fair warning. Useful as a shallow/lush spatializer, chaotic resonator, or microsound/shoegaze generator. Set space very low, conn and decay very high to produce metallic overtone structures. Add glitch to stutter and crush, drop density to hear glitch more clearly, push size and density up to produce lush spaces. Conn is feedback. Motn will alleviate runaway feedback and add just a bit of movement to the whole system.

7 plies:
- **glitch** - character macro (gltch ply, G1-G8 modes; see below)
- **size** - reverb space / scale
- **density** - tap density (push high for lush bed; drop to hear glitch more clearly)
- **motion** - global movement; alleviates runaway feedback, adds animation
- **connectivity** - feedback amount
- **decay** - decay time
- **wet** - dry/wet mix

Network's headline UI feature is the **gltch ply**, a character macro with G1-G8 mutually-exclusive modes:

- **G1 S&H** - sample-and-hold of taps
- **G2 stutter** - multi-block stutter, motion-driven reseed, triangular duration distributions, zero-crossing loop boundaries, NEON 4-wide playback
- **G3 mute** - mute with feedback weight handling
- **G4 transient events** - per-tap event triggering
- **G5 scrub** - in-buffer scrub
- **G6 reverse** - per-tap reverse
- **G7 tap respawn** - drift behavior (rather than teleport) on respawn
- **G8 bitcrush / decimate** - with sub-modes

Equal-share CRUSH / SCRUB / REVERSE / NORMAL at glitch=1 (no single mode dominates). Listener-relative stereo pan, cap of 16 active stutter taps.

**Network viz**: Listener-centered Fibonacci sphere with per-tap trails, sonar + reflector flash, motion-driven Fibonacci perturbation, tilted 3D ping rings with reverse counter-rotation, motion-independent ping speed, sphere connection length cap to suppress long-arm artifacts.

## Optimization pass (secondary)

Major NEON optimization arc across the existing units. None of these change audio character; they free CPU budget at the same fidelity.

- **Filterbank** (formerly Tomograph): full NEON SoA SVF bank, drops stereo CPU at 16 bands to about 8%.
- **Petrichor**: NEON SoA SVF kernel (matches Filterbank pattern) and multi-tap weighted feedback (true delay character rather than resonator). Default buffer length dropped from 20s to 5s to match typical use.
- **Impasto + Parfait**: NEON SoA refactor plus fast paths for the common cases; compressor visual block rate reduced to 50Hz to free render budget.
- **Helicase**: polynomial sin/tanh replacement saves about 5% CPU on lofi mode; carrier shape ply parity fix; morph-hoist + discFold polish.
- **Rings modal kernel** (mi): about 10-15% inner-loop speedup via SoA accumulator quads and padding-instead-of-scalar-tail.

## Minor

- **Cortex-A8 NEON build defense**: every `mods/<pkg>/mod.mk` now appends `-fno-tree-vectorize` last in CFLAGS for am335x. `tools/check-neon-hints.sh` runs automatically on every am335x link via a new `neon_hint_check` make function in `scripts/utils.mk`. This eliminates a recurring class of "works in emu, crashes on hardware" bugs caused by GCC auto-vectorization emitting NEON alignment hints that trap on Cortex-A8 when alignment inference is wrong under `-ffast-math`. Hot DSP is already hand-written NEON intrinsics so disabling auto-vec costs nothing on hot paths.
- **Plaits / Clouds engine/mode switching** is now deterministically reliable on hardware. The previous occasional hard-fault on engine swap is gone. `mods/mi/MiBarrier.cpp` provides `mi_barrier_noop()`, a no-op extern function called at audio-thread state-swap points (Plaits engine swap, Clouds mode swap). Functions as a belt-and-suspenders AAPCS NEON spill barrier in case `-fno-tree-vectorize` is ever stripped from a mod.mk.
- **Plaits 6-op FM** (engines 5/6/7) now runs 2x oversampled. Less upper-octave aliasing on FM patches; mild treble rolloff from the 2-tap MA filter. Single-line revert via `kSixOpOversampling = 1` in `six_op_engine.cc` if you preferred the brighter native-rate character. Still cooking on this and it remains quite noisy.
- **AlembicSphereGraphic**: preventative refactor to the inline-virtual pattern from v2.4.1's DrumCubeGraphic fix.
- **SWIG wrapper auto-tracks header changes** across every `mods/<pkg>/mod.mk` (catches header edits that would otherwise leave stale wrapper bindings causing heap corruption on quicksave).
- Various smaller polish (README header formatting, Network ply description cleanup to strip parenthetical clarifications).

## Known issues

- Plaits 6-op FM still has some inherent grain at the native 48kHz path; future work could swap the 2-tap MA decimation for a proper halfband FIR, or adopt the upstream 4x downsampler pattern that 2-op FM uses.
- The NEON alignment-hint suspects flagged by the lint (169-179 per DSP-heavy package) are explicit-intrinsic NEON on heap-allocated data that IS aligned at runtime; they are false positives the script cannot statically verify.
- D8 highlight bug (from v2.4.0) still present.
- Pecto Doppler slew-time exposure still deferred.
- Alembic Phase 6 serialization still deferred.

## Package version summary

| Package | v2.4.1 | v2.5.0 |
|---|---|---|
| spreadsheet | 2.6.1 | 2.7.0 |
| mi | 1.0.1 | 1.0.4 |
| catchall | 0.3.0 | 0.4.0 |
| biome | 2.2.0 | 2.2.0 |
| kryos | 1.0.0 | 1.0.0 |
| peaks | 1.0.0 | 1.0.0 |
| porcelain | 0.1.0 | 0.1.0 |
| scope | 1.1.0 | 1.1.0 |
