# 99 — Build order (one subsystem at a time)

The phased sequence. Same rhythm as Fabula/Sujet: each sub-step is implement → `make` →
install to emu → audition → commit, with a numerical rig gating the risky DSP. Build the two
engines at FULL rate first; wrap them in the reduced-rate CLOCK afterward (RotCoat shows the
harness can wrap an existing engine — lower risk than building everything at Fc up front).
Version `0.x.y.z` per audition step. Codename TBD (not "Mood").

## Phase 0 — Scaffold + first light
New package (`05-scaffolding.md`): mod.mk, `.cpp.swig`, internal-stereo atom, Lua unit.
Ship an **identity passthrough** (In L/R → Out L/R, Mix only) that builds, loads, and passes
audio on the emu. Set FPCR FZ in `process()`. **Gate:** transparent at Mix=1. Commit.

## Phase 1 — Spatial field (the centerpiece; build the rig first)
The novel/risky part — gate with a **T60 / echo-density rig** (offline scratchpad, impulse →
−60 dB crossing + density-over-time), the postmortem's #1 lesson.
1. **Stage 2 FDN tail** (`02-field.md`): N=8 Householder FDN, coprime delays, per-line Jot
   T60 LP (per-block-smoothed), 4-stage Schroeder input diffuser. Params: Size, Decay,
   Diffusion. **Gate:** rig confirms correct, size-independent T60; no metallic ring with a
   little delay modulation. Commit.
2. **Stage 1 sparse taps** (feedforward only): 8–16 addressable taps, per-tap gain/pan/jitter,
   ping-pong stereo, written to the mix bus. **Gate:** audible discrete rhythmic reflections,
   no instability (it has no feedback). Commit.
3. **α-morph + `density` macro**: Erbe-Verb `A(α)=(1−α)I+αH/√N` (renorm per block) + the
   density crossfade (taps-dominant ↔ FDN-dominant). **Gate:** continuous sparse↔dense travel,
   no zipper, no blowup across the full sweep; T60 unchanged by density. Commit. ← the
   "plexus" milestone.

## Phase 2 — Micro-looper (lift Clouds)
1. **Tape mode** (`01-looper.md`): always-on circular capture + Hermite read + signed-
   increment variable speed (discrete LUT incl. reverse + stalled) + dynamic AA LP. **Gate:**
   clean repitch, reverse, stall; no clicks on speed steps. Commit.
2. **Overdub / fade / freeze**: sound-on-sound `buf=fb·buf+gain·in`, 64-smp Hann seam
   crossfade, record-ramp, write-head-halt freeze. **Gate:** click-free layering; freeze
   holds. Commit.
3. **Stretch mode**: synchronous granular time-stretch (reverse via per-grain increment,
   jitter knob). Upgrade to Clouds WSOLA if transients demand. **Gate:** usable musical
   stretch + reverse. Commit.
4. **Env mode**: envelope-follower-armed slice capture (sensitivity). Commit.

## Phase 3 — Global CLOCK (wrap both engines)
Reduced-rate harness (`03-clock-grit.md`, RotCoat pattern) around looper+field: quantized
integer steps (LUT of musical ratios), polyphase-FIR-in / linear-interp-out for clean,
S&H-decimate + Mirror-style bit-crush for **broken**. **Gate:** one knob coherently warps
loop length + reverb time + pitch + bandwidth; clean↔broken crossfade. Commit. (Farrow
smooth-glide mode = later sub-phase.)

## Phase 4 — Fusion / cross-feedback (needs both engines)
Bidirectional looper↔field coupling with the full guard stack (`04-fusion-governor.md`):
Spiral in each cross-send, DC blockers, denormal tickle (FZ already set), per-block NaN guard,
delay-mod-extends-stability. **Build a small fusion rig** (sweep g_LR/g_RL/drive/g_R, watch
for NaN/overflow, measure settled self-oscillation amplitude — the one config with no
literature). **Gate:** Regen sings into bounded saturation, never heat-deaths. Commit. ← the
"instrument" milestone.

## Phase 5 — Surface + polish
6-ply adaptive UI (Looper · Field · Regen · Clock · Mix · Routing) with per-mode control
relabel/rerange; Routing source selector; multi-out taps (dry-loop / wet-field / per-stage);
stereo width polish (allpass decorrelation if wanted); labels/descriptions; first
user-facing version. Commit per step.

## Phase 6 — CM4 hardware
First on-hardware audition (emu-only until here). CPU/memory profile under worst case
(stretch + dense field + cross-feedback + low CLOCK, stereo). Trim if needed. Tag a release.

## Dependencies
Field (P1) and looper (P2) are independent — could swap order or parallelize. CLOCK (P3)
wraps both. Fusion (P4) needs both + CLOCK. P5/P6 need the engine complete. Recommended order
above builds the riskiest novel thing (field α-morph) first under a rig, then the well-trodden
looper, then ties them together.

## Standing rules
- Numerical rig before ears on any feedback/decay DSP (Fabula/postmortem discipline).
- Keep multitap FEEDFORWARD; FDN loop UNITARY (the postmortem law).
- Smooth every coefficient per block; never jump poles per sample.
- FPCR FZ in every audio thread; Spiral/DC-block/NaN guards in every feedback path.
