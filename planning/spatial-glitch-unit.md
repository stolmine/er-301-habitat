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
- **Control surface: 6 plies x 3 subs each** (see Control hierarchy). Regen/
  Feedback promoted to its own top ply; Character is one global macro; both
  engines mode-switch with adaptive per-mode semantics; the field's Mode is
  the topology selector.

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

## Control hierarchy (6 plies, 3 subs each - fits M1..M6 exactly)

Revised 2026-06-25. The original 5-sub engine submenus did not factor into
clean 3-wide shift rows, so Regen/Feedback was promoted to its own top ply.
Result: six top plies, each with exactly three sub-params on its shift row.
No multi-row expansion needed for control.

1. **Looper** (gate main: capture / re-trigger; clockable for rhythmic
   stutter; hold = overdub)
   - Length - captured window size (CLOCK-scaled)
   - Speed - playback speed + direction, discrete, reverse through a Stalled
     center to forward (the glitch core)
   - Mode - Env / Tape / Stretch (dynamics-gated slice / continuous loop /
     time-stretch); Sensitivity folds into Env
2. **Field** (gate main: freeze / latch)
   - Size - field extent / decay / delay time
   - Diffusion - how aggressively fragments smear into a wash
   - Mode - the topology set (sparse discrete taps / dense FDN / plexus); this
     is where "farther than Network" lives, as the field's mode selector, so
     it costs no extra slot; Size + Diffusion adapt per topology
3. **Regen** (master feedback macro main: scales all three toward singing /
   runaway; Spiral-governed so it saturates instead of clipping)
   - Loop Fade - looper self-feedback / sound-on-sound persistence
     (full = freeze/infinite)
   - Field Feedback - spatial field regeneration
   - Cross-feedback - field output recirculated back into the looper (the
     fusion coupling amount)
4. **Clock** (global sample-rate / grit main: the soul)
   - Smooth - stepped (harmonized) vs continuous
   - Range - sample-rate span / step coarseness
   - Character - one global color/degradation axis (clean <-> broken: the
     CLASSIC deterioration, collapsed from per-engine into one macro)
5. **Mix** (dry/wet main)
   - Dry-kill - remove dry (100% wet)
   - Trails - bypass spillover
   - Balance - looper/field level balance
6. **Routing** (field source / coupling main)
   - Source balance - field fed by loops vs input vs both
   - Direct-loop - clean micro-loop blend through the field
   - Spread - stereo imaging width

The three moves that make it factor:
- Regen promoted to top with its own 3 subs (pulls Loop Fade + Field Feedback
  out of the engine submenus and adds the cross-feedback coupling). The most
  performable axis in a spatial-glitch box earns top-level prominence.
- Character collapsed from per-engine (looper degradation + field tone) into
  ONE global Character sub on the Clock ply (the grit/tone home).
- Mode made symmetric: both engines mode-switch with per-mode adaptive
  semantics (pedal conceit); the field's Mode IS the topology selector.

Gates: the two engine plies have gate mains (CV-triggerable - clock the loop
capture, gate the field freeze) with their 3 subs on the shift row. The other
four plies are knob mains with 3 subs each.

## Open questions / next steps

- **Codename.**
- **Verify the CM4 DSP + memory budget** from the redesign docs on the
  er-301 rpidev branch (reference_redesign_docs) so sizing is fact, not
  estimate.
- **Design the mode-dependent control adaptation**: exactly how each engine's
  three subs relabel/rerange per mode (looper Env/Tape/Stretch; field topology
  set), Mood-style, and how that reads on the 301 sub-display surface.
- **Topology set design** (now the Field Mode selector): the sparse-taps ->
  dense FDN -> plexus options; relate to the Network cascade-FDN postmortem
  (what to reuse vs avoid). Decide whether it is 3 discrete modes or a few
  with per-mode-adaptive Size/Diffusion.
- **Viz placement**: six control plies use all M-buttons, so a Network-style
  plexus/field visualization is the EXPANDED view of the Field ply (M-hold),
  not a 7th ply. Decide if that is acceptable or if a dedicated overview ply
  is wanted (which would mean consolidating two control plies).
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
