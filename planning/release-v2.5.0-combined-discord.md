**Stolmine release: firmware v0.7.0-stolmine.9.2.0 + habitat v2.5.0** - 2026-05-16

Two parallel drops. Firmware brings **Sequencer v2** (dual gates + transpose meta column on every step, per-gate L2 grammar, quicksave schema v2 migration, transport restore option, full bench at 17/17). Habitat brings two new units and a big optimization pass.

**New units, both in spreadsheet**:
- **Visadhara** - 8 osc additive bass/lead/drum voice taking after a certain popular digital Eurorack module. 8 voices on a NEON 4-lane bus, 2x oversampled inner DSP, golden-ratio phase offsets to avoid correlated-detune cancellation, dedicated post-fold envelope, pre-fold soft cap. 7 plies (V/Oct, f0, trig, Mode with Spread/Harm/Morph subs, fold, level, expanded). Animated viz with vertical K-gons on a carousel, trigger shockwave bands, Fold contour field, Harmonic radius + Morph star overlays.
- **Network** - Multi-tap comb / glitch reverb of original design. Useful as shallow/lush spatializer, chaotic resonator, or microsound/shoegaze generator. Headline gltch ply with G1-G8 character modes (S&H, multi-block stutter with zero-crossing loop boundaries, mute, transient events, scrub, reverse, tap respawn drift, bitcrush). 7 plies (glitch, size, density, motion, connectivity, decay, wet). Listener-centered Fibonacci sphere viz with sonar + reflector flash and tilted 3D ping rings. Heavy on CPU, fair warning.

**Hardware stability** - Plaits/Clouds engine/mode switching is now deterministically reliable on hardware. The intermittent "switch and crash" was a Cortex-A8 NEON codegen issue with GCC auto-vectorization; fixed at the build-flag level (`-fno-tree-vectorize` for am335x across every package) plus link-time NEON-hint lint. Plaits 6-op FM (engines 5/6/7) now runs 2x oversampled, less upper-octave aliasing on FM patches.

**Optimization pass** - none change audio character; all free CPU at same fidelity:
- Filterbank (was Tomograph): NEON SoA SVF bank, ~8% stereo CPU at 16 bands
- Petrichor: NEON SoA SVF kernel + multi-tap weighted feedback (true delay character) + default buffer 20s -> 5s
- Impasto + Parfait: NEON SoA + fast paths
- Helicase: polynomial sin/tanh saves ~5% CPU on lofi
- Rings modal kernel: ~10-15% inner-loop speedup

**Migration**: Network unit reference moved from `catchall.Network` to `spreadsheet.Network`; saved patches need a re-bind on this one unit. Firmware sequencer quicksaves auto-migrate v1 -> v2 on load.

Package versions: spreadsheet 2.6.1 -> 2.7.0, mi 1.0.1 -> 1.0.4, catchall 0.3.0 -> 0.4.0. biome, kryos, peaks, porcelain, scope unchanged.

Full notes:
- Habitat: <github habitat release URL>
- Firmware: <github stolmine release URL>
