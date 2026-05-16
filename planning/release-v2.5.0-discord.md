**er-301-habitat v2.5.0** - 2026-05-16

Two new units, both in spreadsheet: **Visadhara** (phase-modulation matrix voice, 8 voices wide on a NEON 4-lane bus, animated Corona viz) and **Network** (multi-tap spatial / glitch reverb, promoted from catchall with a fully built-out character macro and new 3D phase-space viz). Big NEON optimization pass across Filterbank, Petrichor, Impasto, Parfait, Helicase, Rings modal. Plaits/Clouds mode switching deterministically fixed; Plaits 6-op FM now 2x oversampled.

**New unit: Visadhara** (spreadsheet) - PMM voice, 8 voices wide, NEON 4-lane voice bus, 2x oversampled inner DSP, per-voice asymmetric pitch envelope, decoherent phase reset on trigger, golden-ratio phase offsets, dedicated post-fold envelope, pre-fold soft cap. 7 plies (V/Oct + octave register sub, f0 with coarse/fine, trig, Mode with Spread/Harm/Morph subs, fold, level, expanded). Corona viz: vertical K-gons on a carousel, trigger shockwave bands, Fold contour field with photographic inversion, Harmonic radius + Morph star overlays.

**New unit: Network** (spreadsheet) - Multi-tap spatial / glitch reverb. V1 parallel-multitap audio engine (cascade FDN rebuild was attempted and reverted). Headline UI: **gltch ply** with G1-G8 mutually-exclusive character modes (S&H, multi-block stutter with zero-crossing loop boundaries, mute, transient events, scrub, reverse, tap respawn drift, bitcrush with sub-modes). NEON 4-wide stutter playback, listener-relative stereo pan, cap of 16 active stutter taps. Viz: listener-centered Fibonacci sphere with per-tap trails, sonar + reflector flash, motion-driven Fibonacci perturbation, tilted 3D ping rings with reverse counter-rotation.

**Optimization pass (secondary)** - none change audio character meaningfully, all free CPU at the same fidelity:
- **Filterbank** (was Tomograph): full NEON SoA SVF bank, ~8% stereo CPU at 16 bands
- **Petrichor**: NEON SoA SVF kernel + multi-tap weighted feedback (true delay character) + default buffer 20s -> 5s
- **Impasto + Parfait**: NEON SoA + fast paths
- **Helicase**: polynomial sin/tanh saves ~5% CPU on lofi; carrier shape parity fix
- **Rings modal kernel** (mi): ~10-15% inner-loop speedup

**Minor**:
- Cortex-A8 NEON build defense: `-fno-tree-vectorize` appended globally on am335x + link-time NEON alignment-hint lint via tools/check-neon-hints.sh. Eliminates a recurring class of "works in emu, crashes on hardware" bugs
- Plaits / Clouds mode switching no longer hard-faults on hardware (was the recurring "switch and crash"). Belt-and-suspenders `mi_barrier_noop()` AAPCS spill at swap entries
- Plaits 6-op FM (engines 5/6/7) now runs 2x oversampled, less upper-octave aliasing on FM patches with mild treble rolloff from the gentle MA filter
- AlembicSphereGraphic preventative refactor to v2.4.1's inline-virtual pattern
- SWIG wrapper auto-tracks header changes across every package
- Dev-box portability install script + Claude memory rehydration snippets

**Behavior changes**: Plaits 6op less grainy with mild treble rolloff (one-line revert if you preferred brighter native rate). Petrichor default buffer halved-and-a-bit, multi-tap feedback gives delay character not resonator. Helicase polynomial sin/tanh audibly unchanged at hifi, saves ~5% on lofi.

**Migration**: Network unit reference moved from `catchall.Network` to `spreadsheet.Network`. Saved patches need a re-bind on this one unit. No other breaking changes.

Package versions: spreadsheet 2.6.1 -> 2.7.0, mi 1.0.1 -> 1.0.4, catchall 0.3.0 -> 0.4.0. biome, kryos, peaks, porcelain, scope unchanged.

Full notes + binaries: <github release URL>
