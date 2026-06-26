# Spatial-Glitch Unit (CM4) - design

Feature branch: `planning/spatial-glitch-cm4`. Codename: TBD (no
third-party branding per feedback_no_third_party_branding; not "Mood").
Source research and full control inventory: `planning/mood-mkii-architecture.md`.

## Status / decisions locked (2026-06-25)

- **Target: CM4 ONLY, unapologetic.** Not built for am335x.
- **Approach C** (the fused spatial-glitch instrument), built around the
  pedal conceit with the two engines kept INDEPENDENT (not dissolved into
  one algorithm). Controls adapt per mode rather than being shared.
- **Mode-dependent adaptive controls** (knob semantics relabel/rerange per
  mode, the Mood way).
- **Stereo: internal-stereo C++ object (Pattern B)** per
  feedback_stereo_pattern_selection - the spatial field is shared coherent
  L/R state, so one internal-stereo object, not dual instances.
- **Do not clone a generic wet channel.** The "wet" engine is the
  Network-descendant spatial field, which is where we push past Network.

## Why CM4-only (feasibility prediction)

Grounded in shipped-unit costs. Two INDEPENDENT engines (no shared-state
amortization) + cross-feedback + shared CLOCK + stereo + mode-dependent
semantics:
- Looper Tape/Env (buffer + variable-speed interp read + overdub, stereo):
  ~5-8% (Pecto/Petrichor territory).
- Looper Stretch (granular time-stretch, overlap-add, gather loads): ~10-20%,
  the single most likely am335x budget-breaker; does not vectorize cleanly.
- Spatial field (FDN + diffusion + feedback + topology morph, stereo):
  ~15-30% (house reverbs: Galactic 25-30, Verbity 15-20).
- Variable-rate CLOCK boundary: ~3-5% (low CLOCK is cheaper - fewer internal
  samples).
- Total: Tape/Env + modest field ~25-40% stereo; Stretch + rich field
  ~40-60%. am335x flagships top out ~25-30% and mostly own the chain.
- The quieter am335x killer is MEMORY: 256KB L2. Loop buffers (2x length,
  stereo, overdub layers) + field delay lines stereo blow L2 (cf. CreamCoat
  284KB), adding cache-miss stalls on top of the FLOP cost.

CM4 (quad A72 ~1.5GHz, DP support, far more cache/RAM, parallel-DSP MVP
landed on rpidev) absorbs the full vision comfortably, including granular
Stretch, deep topology, high-quality resampling, multiple instances. The
project already points here (v2.6.1 ended vanilla-firmware support; the
redesign is the active future). The standalone micro-looper ("B" unit from
the approach discussion) remains the am335x-friendly thing if ever wanted;
this full unit is CM4-only by design.

## Concept (fused signal flow)

Micro-looper stage captures and glitches the input into living fragments;
the spatial field stage builds a room out of those fragments (and/or input);
the field feeds back into the looper; one global CLOCK (sample rate = tone +
length + pitch + grit) warps both engines together. Fusion lives in the
signal flow + feedback + shared grit, while the two control groups stay
distinct (pedal conceit).

## Control hierarchy

Top level (5 plies), economical:
1. **Looper gate** - rising edge captures / re-triggers the slice (clockable
   for rhythmic stutter); hold = overdub. Submenu beneath.
2. **Field gate** - freeze / latch the spatial field (gateable for momentary
   holds). Submenu beneath.
3. **Routing** - the field<->looper coupling.
4. **Clock** - the global sample-rate / grit axis (the soul).
5. **Mix** - dry/wet.

Looper submenu (mode-dependent semantics):
- **Length** - captured window size (CLOCK-scaled).
- **Speed** - playback speed + direction, discrete, reverse through a Stalled
  center to forward. The glitch core.
- **Mode** - Env / Tape / Stretch (dynamics-gated slice / continuous loop /
  time-stretch). Reconfigures capture behavior; Sensitivity folds into Env.
- **Fade** - loop persistence / sound-on-sound decay (full = freeze/infinite);
  also the looper's self-feedback amount.
- **Character** - looper-side degradation (CLASSIC deterioration: bit/alias/
  decay on the captured loop).

Field submenu:
- **Size** - field extent / decay / delay time.
- **Diffusion** - how aggressively fragments smear into a wash.
- **Topology** - sparse discrete taps -> dense FDN -> plexus/network field.
  The "farther than Network" continuum (replaces Reverb/Delay/Slip modes).
- **Feedback** - field regeneration, Spiral-governed so runaway sings.
- **Tone** - hi-cut / damping. (Stereo spread/width -> this ply's shift row.)

Shift sub-rows on the three top-level utility plies:
- **Routing**: field-source balance (loops vs input vs both), field->looper
  feedback amount, direct-loop blend.
- **Clock**: Smooth (stepped vs continuous), range.
- **Mix**: dry-kill, trails, level balance. (Global Character may live here.)

## Open questions / next steps

- **Codename.**
- **Verify the CM4 DSP + memory budget** from the redesign docs on the
  er-301 rpidev branch (reference_redesign_docs) so sizing is fact, not
  estimate.
- **Design the mode-dependent control adaptation**: exactly how each engine's
  submenu relabels and reranges per mode (the Mood-style per-mode semantics),
  and how that reads on the 301 ply/sub-display surface.
- **Topology axis design**: the sparse-taps -> FDN -> plexus morph; relate to
  the Network cascade-FDN postmortem (what to reuse vs avoid).
- **Fusion + feedback governor specifics**: the looper->field->looper loop,
  the Spiral placement, denormal/runaway guards.
- **Buffer/memory budget at CM4**: loop buffers (2x, stereo, overdub layers)
  + field delay lines; what the CM4 actually affords.
- **Multi-out taps**: dry loop, wet field, per-stage taps (multi-output
  framework).

## References

- `planning/mood-mkii-architecture.md` - source research + full control
  inventory.
- `planning/network-*` - the spatial-engine lineage and the cascade-FDN
  postmortem.
- Memories: project_alias_synthesis_paradigm, feedback_spiral_feedback_governor,
  feedback_apf_phase_resonance, feedback_stereo_pattern_selection,
  reference_redesign_docs (CM4), feedback_no_third_party_branding.
