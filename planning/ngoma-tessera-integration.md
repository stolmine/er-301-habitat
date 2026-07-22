# Ngoma x Tessera integration plan

Status: planning (2026-07-22). Ledger: `tessera-into-ngoma` (todo). Companion
memories: `project_ngoma_codex.md`, `reference_trinity_firmware_re.md`,
`feedback_neon_voice_bus_template.md`, `feedback_neon_soa_svf_bank.md`,
`feedback_neon_no_gather_lut_dsp.md`.

Scope: bring Tessera's Trinity-derived modal engine (heft, Shape/Character/Grit
behavior) into Ngoma's already-rolled DrumVoice interface, keep Ngoma's variable
clipping, and plan the NEON/CPU pass for the modal engine. Investigation only -
no code was changed for this document.

Verification basis: every file:line below was either read directly in this
session or reported by a mapping pass over the full files and spot-checked.
Claims marked "inferred" are reasoned, not measured.

---

## 1. Executive summary

**Sequencing verdict: B (port first), with Tessera frozen as the validation
reference.** Do the drive/clipping rework as PART of the port into DrumVoice,
not as a Tessera-first polish pass. Three code facts drive this: (1) Tessera is
already suppressed from the unit picker (commit 87063fe) - standalone Tessera
polish has no shipping surface; (2) the variable clipper we want is Ngoma's,
already wired to the Level control's "clip" sub-button
(DrumVoiceLevelControl.lua:30-70) - doing variable-drive work inside Tessera
first means building UI plumbing we would immediately discard; (3) the NEON
pass restructures the modal hot loop (pad 14 modes to 16, 4-quad SoA kernel,
polynomial sine) - doing it in Tessera and then transplanting guarantees doing
it twice. Port the engine verbatim-scalar first, prove parity against frozen
Tessera at the corpus operating point, then rework drive, then NEON - one pass
each.

**Drive/clipping one-liner:** the 12x drive is a fitted calibration constant,
not a measured mechanism (the measured mechanism is "the output stage always
limits"); replace `y * 12` with Ngoma's Clipper-controlled variable drive whose
TOP of throw reproduces the 12x/limiting regime (Tessera's heft, calibration
intact) and whose lower throw progressively releases the modal bank's real
dynamics (Shape/Grit crest variation the fixed drive was flattening), with a
block-rate equal-loudness makeup law so backing off drive doesn't just get
quieter.

The old `ngoma-hardware-hang` gate is CLEARED (user-confirmed 2026-07-22:
Ngoma no longer hangs on hardware). CAVEAT on the exact fix: this doc's draft
credited commit 7fd1415, but that is the *earlier* v2.4.1-era DrumCubeGraphic
out-of-line-virtual fix (dated 2026-05-01), which PREDATES the post-2.5.1
regression this ledger item tracks (stamped 2026-07-09, "last known-good =
2.5.1") - so it cannot be the fix for this hang. The likelier resolver is the
xform removal at 2.5.5.175 (f5e0515), but that attribution is UNVERIFIED and
is explicitly deferred to the `reconcile-notes-memory-codebase` sweep. What is
certain: Ngoma at HEAD (spreadsheet 2.8.3.71) is hardware-stable per the user,
the ledger item is closed, and the port does not build on sand. The Cortex-A8
discipline the saga taught (class-member NEON arrays, branchless dispatch,
objdump pre-flight) still governs every step below.

---

## 2. Tessera anatomy

Files: `mods/spreadsheet/Tessera.cpp` (487 lines), `Tessera.h` (52),
`assets/Tessera.lua` (108). Modal engine fitted to the Trinity BLOCK
mode-structure campaign, 1143 hardware captures (header comment
Tessera.cpp:12-27). Currently picker-suppressed but compiled (commit 87063fe).

### 2.1 Signal chain

Per trigger, per sample (process(), Tessera.cpp:266-484):

1. **Mode bank** - `NM = 14` modes (cpp:28) on the intermod lattice
   `f = fc * (kH[m] + kK[m]*r)` (cpp:377), lattice indices kH/kK at cpp:33-34:
   (1,0) carrier, sidebands, subs (k<0, ~30% of energy below fc = the body),
   h=3/5/7 harmonics, and two fold-only intermod modes (3,3)/(5,1).
2. **Per-mode amplitude** - 6-feature regression `kAmpFit[14][6]`
   (cpp:43-58, features 1/dL/foldN/r/lf/gN), plus logistic presence gate
   `kGateFit` on fold-dependent modes 8-11 (cpp:385-391), plus Character
   additive raise/kill `kFoldRaise`/`kFoldKill` (cpp:393-409), plus
   measured-to-initial-amp window correction and HF tau ceiling (cpp:411-419).
3. **Decay** - cubed linear ramp, not exponential: `ramp[m]` decrements
   linearly (cpp:432,465), output uses `ramp^3` (cpp:466); ramp duration
   `T = 9.952*tau^0.8445` is a tau-preserving fit (cpp:204-208). Firmware-read
   mechanism (findings-envelope.md: linear decrement, audio path uses level^3).
4. **Attack** - none. Firmware attack path is dead code (attack-rate constant
   decodes to 0.0); the old 2ms one-pole was removed as provably inert
   *because the 12x drive flattens the onset identically with or without it*
   (cpp:288-293; commit a9f42b2 "byte-identical output"). NOTE: this
   equivalence holds ONLY in the always-limiting regime - load-bearing for
   section 4.
5. **Grit** - two mechanisms. (a) Common-mode noise FM: ONE shared lowpassed
   noise source `jitLp` (40 Hz one-pole, cpp:359) adds an identical Hz
   deviation to every mode's instantaneous frequency (cpp:457-463), depth
   `kappa(grit)*fc` from the measured 4-regime `kGritKappa` table
   (cpp:158-165, 443-448). Firmware-confirmed at instruction level (shared
   xorshift PRNG, injection into the phase accumulator). (b) Noise body above
   the 0.75 breakpoint plus attack burst at ccGrit>=115 (cpp:344-347,438-442,
   472-475). Grit > 0.75 also collapses decay time linearly to zero
   (cpp:324-333; firmware read: `1-(g-0.75)*4` at 0x2400b350).
6. **Pitch envelope** - exponential `pitchEnv` multiplier on all mode
   frequencies (cpp:452-453,463); Sweep sets depth (startMult, cpp:352), Time
   sets tau (cpp:353).
7. **Sum** - `y = sum(env[m] * ramp[m]^3 * sineLUT(phase[m]))` (cpp:460-468),
   noise mixed at cpp:475. Sine from the shared quarter-wave LUT
   `kDrumVoiceSineLUT[257]` with linear interp (cpp:167-177,
   DrumVoiceSineLUT.h:7).
8. **Output stage** - the crux, next section.

### 2.2 The drive stage and what depends on it

```
yd  = y * kDrive;                         // kDrive = 12.0f  (cpp:110, applied cpp:477)
ct  = yd / clipTh;
out = clipG * yd / sqrt(1 + ct*ct) * level;   // (cpp:478-483)
```

- The clipper itself is MEASURED and PARAMETRIC: `kClipTh[16]`/`kClipG[16]`
  tables (cpp:111-116) indexed by a Clipper parameter (ccClip/8, cpp:294-302).
  Measured law (cpp:88-101, findings-clipper.md, 152 captures): transparent
  below CC~12, threshold falls 0.221->0.070 across CC16-40, above CC40 the
  knob is pure drive with makeup asymptoting ~4.03. `kSMH = 0.2030/2.6143`
  converts hardware capture units to model units (cpp:101).
- **The Clipper parameter is dead in the shipped unit**: `mClipper` exists in
  C++ (default 0.378 = "48/127 corpus point", Tessera.h:43) but has no
  ParameterAdapter/branch in Tessera.lua (absent from the PARAMS list,
  Tessera.lua:24) - it is permanently pinned at the corpus point. Tessera
  ships as fixed-12x-drive into a fixed-CC48 clipper.
- **Why 12x**: hardware output peak is pinned at ~0.262 with CV 6.8% across
  all 1120 timbre-map cells because the hardware voice ALWAYS drives its
  limiter into limiting. The un-driven model peaked at 0.04 (below threshold)
  and swung 15.8x across the Grit throw vs hardware's 1.10x. 12x reproduces
  the pinning: spread 15.8x->2.2x, crest error +75%->+7% (cpp:102-109; commit
  635dd7e; STATUS.md drive verdict: swept {1,4,8,12,30,60}, higher overshoots
  crest -11%/-18% at 30/60 and stretches apparent decay).
- **Fitted vs measured** (from the RE corpus, decisive for the merge): the
  always-limiting output stage is the measured, essential mechanism; the
  number 12 is a fitted operating point chosen against the model's own signal
  scale. RE-NOTES.md policy is explicit: mechanisms essential, constants
  incidental, refit locally.
- **What was calibrated with the drive in place** (i.e. what to re-audition
  when drive becomes variable):
  - Attack removal - proven inert only under limiting (a9f42b2).
  - Character raise magnitudes (kFoldRaise) - tuned against post-clip
    crest/energy metrics; the crossfade-override variant "blew up
    e_hi/e_sub/crest" through this exact chain (b284e71, cpp:76-82).
  - Grit's level swing - the 15.8x pre-drive peak spread across the Grit
    throw is precisely what the drive was installed to flatten (cpp:104-109).
  - Apparent decay - drive level changes how long the tail sits in the
    limiting region, so perceived decay depends on drive (cpp:108).
  - The amp/gate/tau tables themselves are in PRE-drive units and are NOT
    structurally coupled to kDrive - the drive is a single multiply after the
    sum. Changing drive moves where the sum lands on the clip curve; it does
    not invalidate the fitted relative mode structure. (Verified: kDrive
    appears exactly once in the signal path, cpp:477.)

### 2.3 How Shape gets kneecapped (the mechanism)

Shape sets `r = clamp(0.0189*(ccShape-9.2), 0, 2)` (cpp:305), which moves
every k!=0 mode's frequency (cpp:377) and redistributes amplitude via the
kAmpFit r-terms (cpp:47-55) and the r-gates on the fold modes (cpp:382,401,
406). Those are level/crest changes as much as spectral ones. But with ct >> 1
the clipper output asymptotes to `clipG*clipTh*sign(yd)` - amplitude
differences in the sum are progressively flattened toward a common ceiling.
kDrive=12 was chosen specifically to keep the sum in that flattening region
across the whole map, so Shape's amplitude/crest expression survives only as
spectral balance, never as dynamics. Same for Grit's 15.8x throw and for
attack/punch transients. That is the "suppresses interesting behavior in favor
of sheer volume" complaint, located: it is not the clipper per se, it is the
guarantee of always being in its limiting region.

### 2.4 Parameters (for the mapping table in section 4)

Fundamental (f0 60Hz default, V/Oct inlet), Character (fold), Shape (r), Grit,
Sweep (pitch-env depth), Time (pitch-env tau), Hold (env plateau, up to 4s),
Decay (carrier tau, quad-in-log law cpp:309), Clipper (dead, see 2.2), Level
(post-clip linear). Trigger + V/Oct inlets, mono Out. All state per-instance in
heap-allocated `Internal` (cpp:224-236): `phase/env/mfreq/mdecay/ramp[14]`
plus scalars - already SoA and already heap-backed (NEON-relevant).

---

## 3. Ngoma anatomy

Files: `mods/spreadsheet/DrumVoice.cpp` (797 lines, 100% DSP), `DrumVoice.h`
(69), `DrumVoiceSineLUT.h` (257-entry table), graphics fully separate in
`assets/DrumCubeGraphic.h` (header-inline by hard-won necessity, commit
7fd1415). Six Lua control files. Codex: `project_ngoma_codex.md`.

### 3.1 Engine (what the port replaces vs keeps)

2x-oversampled inner loop (cpp:497-505,665). Sources summed per half-sample:
osc1 carrier + osc3 detuned unison (triangle->sine morph or wavefold by
Character, cpp:601-647), sub-sine + 3rd harmonic (NEON quad 1, cpp:576-582),
sub-octave + 3 membrane-mode partials with fixed pitch-morphing ratios (NEON
quad 2, cpp:589-597), pitch-tracked SVF noise (cpp:682-687). FM graph: Shape-FM
osc2, spacious-FM, metallic-FM (2.71x), grit noise-FM into carrier phase
(cpp:601-612). Envelopes: attack/hold/decay state machine (cpp:696-751), punch
env (3ms transient boost, cpp:756-757), per-partial NEON decay quad
(cpp:474-483), wobble LFO droop (cpp:485-495). Geometric pitch sweep
(cpp:456-466).

### 3.2 The variable clipper (KEEP - this is the stage Tessera's fixed drive
maps onto)

```cpp
// block rate (cpp:330-332)
bool clipperActive = clipper > 0.001f;
float driveLinear = clipperActive ? (1.0f + clipper * 9.0f) : 1.0f;  // 1..10x
float driveNorm   = clipperActive ? fast_tanh(driveLinear) : 1.0f;
// per sample (cpp:760-762), after punch, before EQ/comp/level
if (clipperActive) sample = fast_tanh(sample * driveLinear) / driveNorm;
```

`fast_tanh` is the rational Pade `x*(27+x^2)/(27+9x^2)` (cpp:25-29). The
Clipper parameter [0,1] rides the Level control's "clip" sub-button
(DrumVoiceLevelControl.lua:30-31,37-44) - shift-toggle paramMode, keyboard
entry, serialization all already rolled. Output chain order: envelope/punch ->
clip -> EQ (bipolar TPT SVF) -> one-knob compressor -> `* level`
(cpp:760-791).

### 3.3 UI surface (the vessel)

Top level (DrumVoice.lua:165-299): trig, tune (V/Oct + oct sub), character
(DrumCubeGraphic control with Shape/Grit/Punch subs, paramMode forced on),
sweep (+time sub), decay (+Hold/Attack subs), level (+Clipper/EQ/Comp subs).
14 parameters total incl. Punch, Attack, Hold, EQ, CompAmt, Octave - controls
Tessera has no equivalents for (Attack/Hold: Tessera has Hold but no Attack;
Punch/EQ/Comp: absent). Serialization at schema 4 with legacy migrations
(DrumVoice.lua:314-357). `DrumVoiceRandomGateControl.lua` is dead code from
the removed xform feature (not required anywhere; no applyRandomize in C++).

### 3.4 Hardware status - the hang is RESOLVED (ledger stale)

`ngoma-hardware-hang` (ledger.toml:541-547) says "blocked, bisect pending" -
that is stale. The full arc: .165-.172 probes (ngoma-debug-pipeline.md
execution log) eliminated NEON :64 hints (Tier-2 scan showed working units
ship MORE hints than DrumVoice), narrowed to differentiated xform dispatch
(.172 survived with dispatch gutted); xform was then removed outright at
2.5.5.175 (f5e0515, schema 4); the xform feature was removed outright at 2.5.5.175 (f5e0515), which is the
most likely resolver of the post-2.5.1 regression. (An EARLIER, separate
Ngoma insert-crash - the v2.4.1 era - was the DrumCubeGraphic out-of-line
virtual, fixed by inlining the graphic into the header at 7fd1415/2.6.0.22
plus `tools/check-graphic-virtual-defs.sh`; do not conflate the two - 7fd1415
predates this regression.) Ngoma at HEAD is hardware-stable (user-confirmed
2026-07-22). ACTION: ledger item closed; exact resolver commit to be pinned in
the reconciliation sweep. The port does not build on sand. What survives as discipline: class-member NEON
arrays (DrumVoice.h:51-63, cpp:63-67), memset ctor init (cpp:189-199), >0.5f
trigger threshold (cpp:357), branchless tier masking, objdump pre-flight
(`tools/check-neon-hints.sh`), and the "0 hints" habit every Tessera commit
already follows.

---

## 4. The merge

### 4.1 Shape of the merge

Replace DrumVoice's oscillator/FM/partial core with Tessera's modal lattice
engine; keep Ngoma's control surface, envelope state machine (attack/hold),
punch stage, and entire output chain (variable clipper, EQ, compressor,
level). This is an engine transplant into the vessel, matching the ledger
item's own framing ("bring cubed-ramp decay, noise-FM grit, lattice, output
stage into the DrumVoice macro"). It is NOT a wholesale unit replacement:
roughly half of DrumVoice survives verbatim (UI/Lua, trigger handling, AHD
machine, output chain, cube graphic plumbing), and the parts of the Ngoma
engine that die (osc morph core, membrane partials, metallic/spacious FM,
wobble) die because Tessera's measured equivalents are the better-sounding
versions of the same ideas.

Practical note: build it as a new engine path inside DrumVoice.cpp guarded for
A/B during development, not a fork of the unit - the Lua surface and
serialization schema stay continuous for existing presets (see 4.5).

### 4.2 Parameter mapping (Tessera mechanism -> Ngoma control)

| Ngoma control | Today drives | After merge |
|---|---|---|
| tune (V/Oct + Octave sub) | baseFreq 55-440Hz log | f0 (Tessera V/Oct + CLAMP 8..8000, cpp:279); Octave sub survives as-is |
| character (cube) | tri->sine->fold morph | Character fold: kFoldRaise/kill + presence gate (cpp:385-409) |
| character sub 1: Shape | FM depth macro | Shape r - lattice detune (cpp:305,377) |
| character sub 2: Grit | noise crossfade + FM | Grit: common-mode noise FM + 4-regime table + decay collapse + noise body (cpp:324-347,443-448) |
| character sub 3: Punch | punch env boost | KEEP Ngoma punch env (post-sum, cpp:756) - Tessera has no punch; under variable drive it becomes audible again; under full drive it is harmlessly flattened (same argument as the removed attack) |
| sweep (+time sub) | geometric converge + 0.3x sub-track | Tessera pitch env: startMult depth (cpp:352) + tauP time (cpp:353). Drop wobble LFO and the dual sub-sweep track (Tessera's measured pitch env replaces both; the lattice's sub modes provide the body the sub-track existed for) |
| decay (+Hold/Attack subs) | exp AHD + per-partial coeffs | Cubed-ramp per-mode decay (kTauR classes, cpp:84-86,431-432) driven by the Decay law (cpp:309); Hold sub -> Tessera holdSec law (cpp:356-357); Attack sub -> REINSTATED linear attack ramp on the bank (see 4.4) |
| level sub 1: Clipper | tanh drive 1-10x | THE CRUX - variable drive/clip, section 4.3 |
| level sub 2: EQ, sub 3: Comp | TPT SVF EQ, one-knob comp | survive unchanged (post-clip, engine-agnostic) |
| level | out gain | Tessera level semantics (post-clip multiply) |

Everything on Tessera's surface has a home; nothing on Ngoma's surface goes
dark. The only semantic changes users see: Sweep depth/time laws change feel
(measured Trinity laws vs Ngoma's 0.7x-fudged geometric converge), and
Character/Shape/Grit get Tessera's much richer measured behavior.

### 4.3 The drive/clipping swap (the crux)

Current Tessera: `y -> *12 -> sqrt-law clip(th,g fixed at CC48) -> *level`.
Current Ngoma: `sample -> fast_tanh(sample*drive)/fast_tanh(drive) -> ...`,
drive = 1+9*clipper.

Proposed stage, replacing both:

1. **Normalize the bank into clipper units once.** Define
   `kBankNorm` such that a reference hit (corpus point: default params,
   f0=60) peaks at ~1.0 at the clipper input with drive=1. From the model's
   own constants the unclipped base peak is 2.6143 model units (kSMH comment,
   cpp:97-101), so kBankNorm ~= 1/2.6143 to first order - verify empirically
   in the emulator, once. This decouples the fitted amp tables (pre-drive
   units, untouched) from the clipper's operating scale.
2. **Clipper sub-param becomes drive**, exactly as Ngoma users already have
   it: `driveLinear = 1 + clipper * (kDriveMax - 1)`. Set **kDriveMax so the
   top of the throw reproduces the 12x always-limiting regime** in the new
   units: 12x drive against a 2.6143-peak bank at threshold ~0.070/kSMH means
   the calibrated regime is ct ~ 12 * (y/2.6143) * (2.6143/0.070*kSMH-ish) -
   do not derive this on paper; calibrate kDriveMax by matching Tessera's
   measured output (peak spread across the Grit throw <= 2.2x, crest ~ +7%)
   with the drive at max. Expect kDriveMax in the 10-16 region.
3. **Default lands at Tessera-equivalent.** Ship the Clipper default at the
   value that reproduces the corpus point (Tessera 2.8.3.62 sound). The knob
   then REMOVES drive from there down to transparent, rather than adding
   character to a clean default - the unit's identity stays "Trinity heft",
   with the escape hatch below it. (Ngoma's current default clipper=0 would
   invert that identity; change the default, keep the range.)
4. **Which clip curve:** keep Ngoma's `fast_tanh` with gain-comp UNLESS the
   parity A/B (step P3 in section 6) shows the sqrt-law curve is audibly part
   of the heft. The two are close cousins (both odd, smooth, memoryless);
   tanh saturates harder into a flat top (crest lower at same ct). Decision
   is one A/B in the emulator with the spectral-descriptor harness; if
   sqrt-law wins, port Tessera's curve into the Ngoma stage verbatim (it is
   the same cost: one mul, one sqrt via `__builtin_sqrtf`, cpp:479-483) and
   keep Ngoma's control law on top. The gain-comp divisor plays the role of
   Tessera's kClipG makeup either way.
5. **Equal-loudness makeup (the re-balance).** With gain comp, peak stays
   ~constant across the throw but RMS drops as drive backs off (crest rises
   from ~3 toward ~11 - that is the point). Perceived loudness will drop
   noticeably at low drive. Add a block-rate makeup: estimate the RMS ratio
   as a smooth function of driveLinear (calibrate 4-6 points in the emulator
   against the reference hit, fit once, bake as a small polynomial - do NOT
   per-sample compute), apply post-clip pre-EQ. Cap makeup at ~+8dB so
   transparent-drive doesn't pump the noise floor. This is what keeps "less
   drive" meaning "more dynamics" instead of "quieter".
6. **What must be re-auditioned once drive is variable** (from 2.2's
   dependency list): (a) Attack - reinstate as a control (4.4); (b) Grit
   throw - at low drive the 15.8x level swing partially returns; audition
   whether it is musical (it is real hardware-model dynamics) or needs a mild
   block-rate grit-level trim (precedent: Ngoma's grit-knee attenuation,
   cpp:299-300); (c) Character raise magnitudes - fitted post-clip; at low
   drive Character will act more literally on amplitudes. Expect it to sound
   MORE expressive, not broken - the raise deltas are pre-drive quantities
   (cpp:393-408). No table refit is planned unless the A/B says otherwise.

What we explicitly do NOT do: re-fit kAmpFit/kGateFit/kGritKappa/kTauR for
the new stage. They are pre-drive laws (verified single-multiply coupling,
cpp:477); the drive change moves the operating point, which is the entire
musical intent.

### 4.4 Attack

Firmware has no attack (hard jump, findings-envelope.md), and Tessera removed
its 2ms ramp as inert UNDER LIMITING (a9f42b2). At variable drive the
equivalence breaks: at low drive an instant-on 14-mode bank with staggered
phases (cpp:425-426) will have an audible, possibly clicky onset that the
limiter previously ate. Ngoma already has an Attack sub-param (0..0.05s,
cpp:250) on the Decay control. Wire it to a linear attack ramp multiplying
the bank sum (default 0 = authentic firmware jump). Cheap (one scalar ramp),
gives the interface's existing control a real job, and the authentic behavior
remains the default.

### 4.5 What survives, what conflicts

Survives verbatim: all six Lua controls + cube graphic + shift/paramMode
plumbing, trigger/AHD state machine shell, punch env, EQ, compressor, level,
octave, serialization framework. Cube graphic pollers (getCharacter/getShape/
getGrit/getEnvLevel, DrumCubeGraphic.h:66-69) keep working - map getEnvLevel
to the carrier's `env[0]*ramp[0]^3` live value instead of Tessera's static
env[0] (Tessera.cpp:258 returns initial amp - slightly wrong even for
Tessera; fix in the port).

Dies (replaced by measured equivalents): osc1/2/3 morph core + spacious/
metallic FM (-> lattice + fold laws), membrane partials + pitch-morph ratio
table (-> lattice sub/sideband modes), per-partial exp envelopes (-> cubed
ramp + kTauR classes), wobble + dual sweep tracks (-> measured pitch env),
Ngoma grit knee/SVF noise crossfade (-> measured 4-regime grit; the
pitch-tracked SVF noise COLOR is worth an audition against Tessera's noise
body before deletion - flag, not blocker). 2x oversampling: drop it for the
modal bank (a sine bank generates no harmonics above its mode frequencies;
f cap at nyq already enforced, Tessera.cpp:410; the fold laws act on
amplitudes not waveforms). The clipper is the one nonlinearity - it stays at
1x like Tessera's does today. This makes the port CHEAPER than Ngoma's
current engine before NEON even starts. (Inferred but high-confidence; verify
aliasing by ear at high f0 + max drive in P3.)

Serialization: bump schema to 5. Params keep names/keys (character, shape,
grit, sweep, sweepTime, attack, hold, decay, clipper, eq, level, compAmt,
octave) so old presets deserialize; values will SOUND different (new laws) -
that is the point of the release. Migration note in the changelog, no
numeric remapping attempted (the old and new laws are not commensurable).

---

## 5. Tessera NEON / CPU plan

### 5.1 Hot-loop inventory (current scalar Tessera - verified firsthand)

Loop nesting: per-SAMPLE outer (`for i < FRAMELENGTH`, cpp:361), per-MODE
inner (`for m < NM`, cpp:461-468) - already the right nesting for a cross-mode
SoA kernel. Per sample, per mode (cpp:463-467):

- `fm = I.mfreq[m]*pmul + jitDev` - 1 mul + 1 add; pmul and jitDev are
  per-sample SCALARS (common-mode), so they become single vdup broadcasts.
- `I.phase[m] += fm / sr` - a per-mode DIVISION by sr every sample (cpp:464);
  bake `invSr` (and ideally `mfreq*invSr`) at trigger/block rate - free win
  even before NEON.
- `floorf` wrap (cpp:464) + a second `floorf` inside sineLUT (cpp:169) - the
  only libm in the loop; replace with the compare/subtract wrap (template
  Layer 10).
- `if (!held) { ramp -= mdecay; clamp 0 }` (cpp:465) - `held` is a per-sample
  scalar bool; vectorizes as a 0/1 mask multiply on the decrement (or vmax
  with a baked heldMask), no branch.
- `r3 = ramp^3` (2 muls) + `y += env*r3*sineLUT(phase)` (cpp:466-467) -
  sineLUT is the gather (5.2); the rest is 2 vmul + 1 vmla.

Per-sample scalar remainder (stays scalar, cheap): trigger edge test
(cpp:364), pitchEnv decay (cpp:452), jitter LCG + 40Hz one-pole (cpp:457-458),
noise body env/burst/one-pole + mix (cpp:472-475), drive + clip with
`__builtin_sqrtf` (cpp:477-483, deliberately chosen over sqrtf to avoid an
out-of-line `bl sqrtf` AAPCS call barrier in the loop). Per-trigger setup is
heavy (powf/logf/expf per mode, cpp:366-448) but trigger-rate - fine.

State: `phase/env/mfreq/mdecay/ramp[14]` are parallel float arrays in the
heap-allocated Internal (cpp:226-227) - already SoA, already NEON-legal
storage (heap, not stack-local). No doubles anywhere in the file's hot path
(all float; verified). No per-mode data-dependent branches besides the
maskable held/clamp pair; out-of-range modes are silenced at trigger time via
a=0 (cpp:410), and the sineLUT sign flip (cpp:170-176) is ternary-maskable.

No hardware CPU measurement for Tessera exists in the repo record (no CPU%
in any tessera commit message or planning doc). Scalar cost estimate: 14
modes x ~10-14 ops + LUT gather per sample - comparable to or below Ngoma's
current engine, but measure on the device at P3 before scoping (f64/FLOP
counts are poor proxies on this platform).

### 5.2 The LUT problem and the answer

The one obstacle is `sineLUT(phase[m])` (cpp:167-177): a per-mode indexed load
from the 257-entry table = a gather. Cortex-A8 NEON has no gather load
(feedback_neon_no_gather_lut_dsp); a 4-lane batch would need 8 scalar loads +
FPU<->NEON crossings per quad, break-even or worse. The proven escape is
polynomial sine substitution (JF, Visadhara: odd 5th/7th-order on a triangle
argument, ~-60dB error, pure quad FLOPs). For this engine the substitution is
LOW-RISK by construction: the modes are meant to be clean decaying sinusoids
(the hardware's own character comes from the lattice + noise FM, not LUT
quantization), and Ngoma's `neonAdvanceSines` (DrumVoice.cpp:68-108) is
already the exact helper needed - same polynomial in NEON and scalar fallback,
already living in the destination file. Audition gate per the memory: A/B LUT
vs poly on a sustained low-decay hit; expected inaudible under 40Hz-bandwidth
noise FM.

### 5.3 Kernel shape

Pad 14 modes -> 16 = 4 quads (pad lanes: a=0, mdecay=0, ramp=0 -
algebraically silent, no tail loop; same trick as the SoA SVF bank pattern).
Per sample, per quad:

- `p = vmlaq(p, mfreqV, pmulJitV)` - phase advance where
  `pmulJitV = vdupq(pitchMul) + jitter` is COMMON-MODE: the jitter and pitch
  env are one scalar broadcast per sample, not per-lane state. One vdup, one
  vmla. (This is why Tessera vectorizes better than a generic modal bank -
  the measured common-mode grit mechanism is exactly the NEON-friendly one.)
- wrap to [0,1) with the forward-only compare/subtract (voice-bus template
  Layer 10).
- poly sine on the quad (always_inline, template Layer 5).
- `ramp = vmax(ramp - mdecayV, 0)`; `r3 = ramp*ramp*ramp` (2 vmul);
  `acc = vmla(acc, sine, env*r3)` with env*... premultiplied per Layer 8.
- Horizontal sum once per sample across the 4 quad accumulators (vpadd
  cascade).

Scalar remainder per sample: jitter one-pole + RNG, noise body env, pitch env,
drive/clip stage - all cheap scalars. Apply the template disciplines: bake
`mfreq*invSr` at trigger/block rate (Layer 3), broadcasts named inside the
loop scope (Layer 4), no struct-ref args (Layer 7), class-member/heap arrays
only (already true), `tools/check-neon-hints.sh` on every build, and the
now-default `-fno-tree-vectorize` on am335x (commit 4583e16) as the backstop.

### 5.4 Expected win and when

16 lanes of pure-FLOP sine bank is the textbook case for the voice-bus
pattern (Visadhara: ~2x; SoA bank precedents: 2-5x on the kernel). The modal
sum is Tessera's dominant per-sample cost (the scalar remainder is a handful
of one-poles and the clip stage); expect a 2-4x kernel win and a unit total
well under Ngoma's historical ~30%, helped further by dropping Ngoma's 2x
oversampling (4.5) and baking the per-mode `fm/sr` division out (5.1). No
hardware CPU measurement of Tessera exists in the record (verified against
commit messages and planning/) - measure the scalar port on the device first
(f64/FLOP counts are poor proxies on this platform; profile before
investing), then do the NEON pass ONCE, in DrumVoice, after parity is proven
(sequencing, section 6). Doing NEON in Tessera pre-port would be double
work: the kernel restructure (padding, poly sine, bake-ins) IS the port's
engine-loop rewrite.

---

## 6. Sequencing & risks

### 6.1 Verdict: B - port first, Tessera frozen as reference

Reasons (all from code facts above): Tessera is picker-suppressed (87063fe),
so "perfect Tessera first" polishes a unit nobody can insert; the variable
clipper + its UI exist only in DrumVoice; the drive rework needs the variable
stage to exist, so it cannot meaningfully happen "in Tessera first" without
building throwaway plumbing; NEON restructures the same loop the port
rewrites - one pass, in the destination. The counter-argument for A (perfect
first) is calibration safety - answered not by sequencing but by FREEZING
Tessera at 2.8.3.62 as the executable reference model and gating the port on
parity with it (P3). Tessera stays untouched until the port ships, then gets
retired (or kept suppressed as the reference).

### 6.2 Ordered steps

- P0. Close/annotate `ngoma-hardware-hang` (resolved via f5e0515 + 7fd1415);
  delete or archive dead `DrumVoiceRandomGateControl.lua`. Housekeeping
  commit.
- P1. Engine transplant, scalar, verbatim: move Tessera's tables + trigger
  math + per-sample loop into DrumVoice behind the existing param set (map
  per 4.2). Drop 2x OS/wobble/membrane/FM cores. Keep Ngoma output chain;
  clipper temporarily forced to the Tessera-equivalent operating point
  (fixed drive 12-equivalent) for parity testing.
- P2. Wire the mapping: Attack ramp (4.4), Hold law, Sweep/Time laws, cube
  graphic pollers (incl. the getEnvLevel fix), schema 5 serialization.
- P3. PARITY GATE (emu): render the 9-case verified suite + a small
  Grit/Shape/Character grid from both units at the corpus operating point;
  descriptor-level match (the trinity-midi-harness descriptor stack exists
  for exactly this). Also the aliasing spot-check (4.5) and LUT-vs-poly-sine
  A/B prep. Hardware smoke test: insert, trigger, no hang, 0 suspect hints.
- P4. Drive/clip rework per 4.3: kBankNorm, kDriveMax calibration at top-of-
  throw vs frozen Tessera, default at corpus point, curve A/B (tanh vs
  sqrt-law), equal-loudness makeup fit. Audition pass on Shape/Grit dynamics
  at low drive (the payoff step). Re-audition attack default.
- P5. NEON pass per section 5, with hardware CPU measurement before (scalar)
  and after; check-neon-hints + hardware insert test per build.
- P6. Polish: Punch interaction under new engine, EQ/comp defaults, release
  notes (no third-party naming), version bump discipline (4th digit per dev
  build), close `tessera-into-ngoma`.

### 6.3 Risks

1. **Parity gap at P3** (transplant subtly diverges - envelope timing, phase
   init, per-block vs per-sample rate differences between the two process()
   shells). Mitigation: transplant verbatim first, diff descriptors, only
   then adapt. The verified suite exists; use it.
2. **Low-drive regime is unvalidated territory** - by construction the model
   was only ever fitted in the limiting regime; below it we are trusting the
   pre-drive laws to be right (they are the fitted quantities, but no
   hardware ever confirmed them unclipped - the hardware CANNOT run
   unclipped). This is a musical judgment zone, not a fidelity zone; treat
   P4 auditions as sound design, with the frozen corpus point as the anchor
   that must not move.
3. **Loudness law wrong** -> low drive reads as "broken/quiet" instead of
   "dynamic". Mitigation: the P4 makeup calibration + cap; keep the default
   at the corpus point so the risk zone is opt-in.
4. **am335x regressions** - engine swap in a unit with Ngoma's crash
   history. Mitigations already systemic: -fno-tree-vectorize default +
   link lint (4583e16), check-neon-hints, check-graphic-virtual-defs, class-
   member arrays, hardware insert test at P3 BEFORE the NEON pass adds
   variables. Keep the DrumCubeGraphic untouched (header-inline).
5. **Preset expectations** - existing Ngoma presets change sound. Accepted
   and intended; schema-5 note in release notes.
6. **Punch/noise-color regressions** - Ngoma flavors (punch env, SVF noise
   color) interacting with the new bank. Both kept behind their existing
   controls with defaults auditioned at P6; SVF-noise-vs-Tessera-noise-body
   A/B flagged in 4.5.

---

## 7. Open questions

1. **Clip curve identity** (tanh vs sqrt-law): needs the P3/P4 A/B; cannot be
   settled from code. Cost of either is identical; default recommendation is
   whichever matches the frozen reference at top-of-throw.
2. **Grit low-drive level swing**: musical or needs trim? Hardware cannot
   answer (it never runs unclipped); user's ears at P4.
3. **LUT vs polynomial sine tone**: expected inaudible for clean decaying
   modes under noise FM, but it is a formal audition gate per
   feedback_neon_no_gather_lut_dsp.
4. **SVF noise color**: keep Ngoma's pitch-tracked LP->BP noise as regime-4
   color, or Tessera's measured noise body alone? A/B at P4/P6.
5. **kDriveMax value** (10..16?) and the makeup polynomial: empirical, emu +
   reference hits.
6. **Tessera's fate post-port**: retire, or keep suppressed as the living
   reference model? (Recommend keeping until one release after the port
   ships.)
7. **CPU baseline**: no recorded hardware CPU% for Tessera scalar exists in
   the repo record; measure at P3 on the device before committing to the NEON
   scope.
