# Design note: switched-capacitor filter clone (target: Bionic Lester Mk1)

Status: design note / not started. Ledger item `bionic-lester-clone`.

Goal: a profiling-informed clone of the **Industrial Music Electronics / The
Harvestman "Bionic Lester" Mk1** (Model 1873), a dual switched-capacitor
multimode filter. Same measurement-driven approach as the Canals rebuild
(external reference model + hardware capture corpus + validation harness), but
the target's switched-capacitor nature makes it a cleaner fit than an analog
continuous-time filter.

## Why an SCF is a good digital-modeling target

A switched-capacitor filter is already a sampled-data system: its core is a
discrete biquad clocked at the switch rate, `fc = fCLK / N`. So the linear model
maps to a digital SVF with almost none of the analog-modeling friction (no
bilinear warping to fight, the topology is discrete to begin with). The
character is the non-idealities: clock feedthrough, and aliasing/foldback
because the audio is sampled at a relatively low clock. Those are the fun part
and, per the CPU note below, cheap to reproduce.

## What the Bionic Lester Mk1 actually is (researched)

- Dual **12 dB/oct** switched-capacitor multimode filter (2-pole biquad per side).
- Per channel: simultaneous **LP + BP** outputs, plus a third output switchable
  **HP / allpass / notch**. Simultaneous LP/BP/HP/notch/AP = a universal SCF
  biquad, almost certainly **MF10 / LMF100-class**.
- **Aliasing switch: minimum or maximum.** Very likely the chip's **50:1 vs
  100:1 clock-to-cutoff ratio** pin (50:1 -> lower clock for a given fc -> more
  foldback; 100:1 -> cleaner).
- **Shared** across both sides: resonance, third-output mode, aliasing setting.
- **Per channel:** cutoff + CV; **ch1 CV normalled to ch2**. Clock-source
  selector (ch1, ch2, or both generators).
- **Does NOT self-oscillate** on the onboard resonance control (bounded Q).
- Input amps scaled to **distort**, and the distortion character **changes with
  the mode setting**.

Sources: analoguehaven.com/the-harvestman/model-1873/ ; postmodular.co.uk
/modules/bionic-lester/ ; modulargrid.net IME Bionic Lester.

## Requirement: audio-rate modulation from the start (structural)

A filter is only as good as its FM. Cutoff AND resonance (and ideally the switch
clock / aliasing) must be modulatable at **audio rate (per sample)**, not block
rate. This is a from-day-one architectural decision, not a bolt-on.

The ER-301 has a Parameter/Inlet split (code-verified, see
planning/audio-rate-modulation-notes.md): `process()` runs once per 128-sample
frame (~375 Hz). An `od::Parameter` is a per-frame scalar, and
`ParameterAdapter` + `tie` reads ONLY the last sample of the CV frame - a
~375 Hz sample-and-hold = the "flappy" artifact. An `od::Inlet` carries a
per-SAMPLE buffer, wired audio-rate via `GainBias` + `connect`. So cutoff,
resonance, and FM must each be an `od::Inlet` read per sample
([[feedback_inlet_vs_parameter_audio_rate_mod]]).

**The coefficient recompute is per sample, with a cheap approximation - that is
the universal pattern, not a block-coeff + interp hybrid.** The built-in
`StereoLadderFilter` recomputes ALL coefficients per sample from Inlets (NEON
simd_exp/sin + Newton-Raphson) at ~1.28% CPU; SistersSvf uses a cubic-Taylor tan
+ one fdiv per sample. No unit does block-rate coeffs + per-sample interp for a
modulatable cutoff. So design the Lester's SVF coeff update as a cheap per-sample
recompute from the inlet buffers from the outset.

**This composes well with the variable-rate switch clock.** At low fc the SVF
ticks at the low internal switch clock, so the per-sample coeff recompute runs at
that lower rate (cheaper) AND the cutoff modulation is naturally band-limited to
the switch clock - which is physically authentic, the real SCF samples its
control at the clock. At high fc it is a per-sample recompute at host rate.

**Canals cautionary tale (important):** audio-rate cutoff WAS tried on Canals and
FULLY REVERTED (spreadsheet 2.8.1.12) - Span's wide exponential cutoff leverage
popped, and entangled LUT/soft-knee changes added stepping, caught on a hardware
A/B. Lessons for the Lester: (1) hardware A/B the audio-rate cutoff EARLY, not
late; (2) knob-detent slew is a separate concern from CV rate; (3) crucially, the
Lester's Q is BOUNDED (no self-oscillation), so it lacks Canals' self-osc SVF
state memory + exponential leverage that caused the pops - the Lester is a
**safer audio-rate target than Canals was.** Ref: StereoLadderFilter,
mods/spreadsheet/SistersSvf.h, planning/audio-rate-modulation-notes.md.

## Modeling approach

1. **Linear core = MF10-class universal SVF biquad**, derivable from the
   datasheet and tuned/validated by profiling. `fc = fCLK / N`, N in {50, 100}
   set by the aliasing switch. Simultaneous LP/BP/HP/notch/AP taps from the one
   biquad state (standard SVF outputs). Bounded resonance, no self-oscillation
   (removes the limit-cycle match that dogged Canals).

2. **Aliasing = the switch clock, modeled directly.** Run the internal SVF at
   the modeled switch clock and deliberately do NOT anti-alias, so it folds like
   the hardware. Aliasing min/max = N = 100 vs 50. This is the exact mechanism,
   not an approximation, and it overlaps the aliasing-as-synthesis work
   ([[project_alias_synthesis_paradigm]], Mirror).

3. **CPU: clamp the internal rate to the host rate.** The audible foldback only
   exists when the switch clock drops below the host Nyquist, i.e. at LOW cutoff
   (clock = 50-100 x fc, so below ~48 kHz clock means fc under ~0.5-1 kHz). So:
   internal rate = min(fCLK, hostRate). Low fc -> low internal rate -> cheap AND
   authentically gritty; high fc -> clock above 24 kHz, foldback inaudible, run
   at host rate, clean. **Never oversample.** The character costs less than a
   clean filter, and the grit is free where it matters. Verify the CPU envelope
   across the cutoff range early (Fabula lesson).

4. **Per-mode input distortion** = the main profiling job. Capture the input
   transfer curves per mode (the "distortion character changes with mode") and
   fit a shaper; drive it from the scaled input amp model.

5. **Clock feedthrough tone** (last 10%): inject a small component at the switch
   clock; profile its level/spectrum. Diminishing returns, do last / optional.

## What profiling captures vs what is derived

- **Derived (datasheet):** biquad topology, the LP/BP/HP/notch/AP output algebra,
  `fc = fCLK/N`, the 50/100 ratio.
- **Profiled (Canals harness, [[reference_three_sisters_model]]):** the exact
  cutoff CV map, the resonance/Q behavior and its ceiling, per-mode input
  distortion curves, and reference foldback + clock-bleed spectra to validate
  the aliasing model against.

## Architecture (maps onto patterns we already own)

- **Dual filter, one shared config block.** Resonance / mode / aliasing are
  shared, so a single config drives two SVF instances. Internal-stereo Object or
  dual-instance; use the shared-state route since resonance/mode are coherent
  across sides ([[feedback_stereo_pattern_selection]]).
- **CV normalling ch1 -> ch2** = the Canals multi-input normalling pattern
  ([[feedback_multi_input_normalling_pattern]]).
- **Clock-source routing (ch1 / ch2 / both):** per-side clock assignment; the
  "both generators" case shares one clock (both sides fold identically), the
  independent case decorrelates the grit.
- **Modulatable switch clock** exposed as a first-class param (the
  "disruption"/aliasing character), audio-rate mod = a distinctive feature.
- Multi-output picker for the LP/BP/third taps ([[project_multi_output_framework]]).

## How far it gets us (estimate)

- Filter shapes, dual routing, CV/normalling, resonance, aliasing min/max:
  **~90-95%** (discrete MF10 core + no self-osc are both in our favor).
- The signature gritty foldback: **~85-90%,** essentially free via the clamped
  variable-rate model. This is what makes it a Lester, and it is the easy part.
- Last ~10%: exact per-mode input-distortion coloration + clock-feedthrough
  tone. Profilable but diminishing returns; ear-voicing, not a research problem.

Verdict: a better candidate than Canals. Dual-SVF is more wiring, but the SCF
being MF10-class removes the hardest analog-modeling step and the no-self-osc
removes the nastiest nonlinear match; the aliasing falls out of a cheap trick we
are already fluent in.

## Phasing

1. **POC / feasibility:** MF10-class SVF at a clamped fc-tracked switch clock,
   aliasing min/max = N 50/100, LP/BP/HP outputs. A/B the foldback against a
   hardware capture at a few low cutoffs. Confirm the CPU envelope across fc.
2. **Profile + fit:** cutoff CV map, resonance ceiling, per-mode input distortion
   curves (reuse the Canals harness).
3. **Architecture:** dual sides + shared config + CV normalling + clock routing +
   multi-output picker.
4. **Voicing / last 10%:** clock-feedthrough tone, per-mode distortion by ear.

## Open questions / risks

- Confirm the actual SCF IC (MF10/LMF100 vs a rarer part) - changes the exact
  ratios and output algebra. A datasheet match de-risks the whole linear model.
- am335x CPU at high fc (host-rate dual SVF + 5 output taps x 2 sides) - measure
  early; the low-fc undersampled path is cheap, high-fc is the cost to watch.
- Scope: a faithful Lester clone vs a general "SCF-character dual filter." The
  clone's value is its recognizability; decide after the POC whether the last
  10% is worth chasing.
- Naming: generic functional name, no third-party branding
  ([[feedback_no_third_party_branding]]).
