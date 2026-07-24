# Fabula: body + decay (tail) work plan

Baseline: 0.2.0.56 on branch `fabula-am335x`. Motivation: A/B against the
faustian `DattorroPlusPlus` (a faithful full-rate Dattorro) shows Fabula has
(1) less low-end **body** and (2) a shorter **tail**. Root causes were traced in
the source (see the architectural comparison in session notes); this plan turns
those findings into concrete, staged edits. All changes are to
`mods/zaum/atoms/APFTank.h` unless noted; every build is both arches +
NEON/vtable checks; bump the 4th version digit per dev build.

Guiding constraint: these are the levers we discussed - do not add new ones
without asking. Keep the am335x rules (no double in hot loops, branchless where
practical, verify at the binary level).

---

## Part A - BODY (low-end weight / density)

### A1. Open the wet highpass  [biggest body win, lowest risk]
- **Now:** static 200 Hz 12 dB/oct HPF on the whole wet output
  (`kWetHpF=200`, `kWetHpA=0.025918`; two cascaded one-pole LPs, `hp = x - lp`).
  This was "housekeeping" to keep the tail out of the mud - but that band *is*
  the body.
- **Change:** lower the corner to ~60 Hz. New coeff `a = 2*pi*fc/SR`:
  60 Hz @48k -> `kWetHpA ~= 0.007854` (70 Hz -> 0.009163 if 60 feels too loose).
  Keeps a gentle sub-sonic / DC-buildup guard while restoring 60-200 Hz body.
- **DECIDED (D1):** fixed 60 Hz (no new control).
- **Risk:** low. Restores lows; may reintroduce a little mud at high feedback -
  acceptable, that is the ask.

### A2. Rebalance the shipped defaults  [voicing]
- Decay `0.30 -> 0.55` (fuller, more sustained default room).
- Damp `0.40 -> 0.25` (brighter tail, matches faustian's default and its
  perceived length; see B3 - same change).
- **Risk:** low (defaults only). Preset-voicing pass; confirm by ear.

### A3. Input bandwidth LP (faustian `bw_filter`)  [warmth]  -- DECIDED (D2): SKIP
- Cut from scope. Rely on A1 + A2 for body. Revisit only if A1/A2 fall short.

### A4. Structural density  [DEFER - larger rewrite]
- Faustian's single long serial tank (4 decay diffusers + 4 multi-tap delay
  nodes + mid-loop input re-injection + 7-tap output) is intrinsically denser.
  Options: add a decay-diffusion allpass per loop, or re-inject `diffIn`
  mid-loop. Bigger change; note as a future phase, not this pass.

---

## Part B - DECAY (tail length)

RT60 ~= -3 * (loop time) / log10(loop gain per lap). Fabula loses on loop time
(shorter cross-coupled loop), loop linearity (a nonlinearity bleeds gain every
lap), and ceiling (capped feedback). Note: it is NOT mainly the Decay default -
the Decay->g_d map is generous (0.30 already -> g_d 0.78, per-lap ~0.61 vs
faustian's ~0.64). The gap is structural.

### B1. Soft-knee the feedback saturator  [biggest tail win]
- **Now:** every lap the cross-feed runs through
  `spiralFastSaturate(d2Read * g_d, 1.0)`. That approximates `sin(x)`, and
  `sin(x) < x` for all `x>0`, so it bleeds gain on *every* recirculation
  (compressively - more when loud). Faustian's loop is perfectly linear.
- **Change:** make the limiter **linear below a threshold** and only saturate
  above it, while still hard-bounding to ~1 (keep the runaway governor). Shape:
  ```
  a = |x|
  if (a <= thr) return x;                      // tail rings linearly
  over = (a - thr) / (1 - thr);
  sat  = spiralFastSaturateF(over, 1.0) * (1 - thr);
  return sign(x) * (thr + sat);                // bounded to thr + (1-thr) = 1
  ```
  `thr ~= 0.7`. Below 0.7: no gain loss -> long clean ring. Above 0.7: still
  saturates toward +-1 -> runaway still caught. Prefer a **branchless**
  formulation (mask on `a>thr`) per the am335x branch-dispatch caution; the
  existing saturator already has an `if`, so a value-branch is acceptable if
  branchless is awkward.
- **Risk:** moderate. Must stay bounded/stable at max Decay - the whole point of
  the governor. Verify: at Decay=1 the tail must be long but **decaying**, not a
  sustained drone. The above-threshold region preserves the +-1 bound so a true
  runaway saturates (bounded), and loop losses (damping, DC blocker) must still
  win - confirm by ear + by watching the tank level not plateauing.

### B2. Raise the feedback ceiling / remap Decay  [only if still short after B1]
- **Now:** `kGdMin=0.30`, `kGdMax=0.97`, `kGdCap=0.985`, `kDecayShape=0.277`.
- **DECIDED (D3): aggressive.** Raise `kGdMax 0.97 -> 0.992` and
  `kGdCap 0.985 -> 0.995` (still < 1.0). Re-solve `kDecayShape` only if we want a
  specific Decay->g_d pin; else accept the curve shift (A2 moves the default to
  0.55 anyway). Still gated behind a B1 audition - do B1 first, and if the tail
  is already long enough, we can stop short of the full 0.992.
- **Risk:** high near unity. `g_d` must stay < 1 with margin so damping + DC
  blocker guarantee decay; the soft-knee saturator only bounds amplitude, it
  does not force decay. **Must** test max Decay for drone/self-oscillation and
  confirm the tank level keeps falling (does not plateau) before shipping .59.

### B3. Lower default damping
- Same edit as A2 (Damp default 0.40 -> 0.25). Damping is an in-loop LP applied
  each lap; less damping = the bright tail lingers = reads as longer. Consolidated
  with A2 so it happens once.

### B4. Lengthen the loop  [NOT a decay-specific change]
- Bigger delays lengthen RT60 but that is the Size/room-scale axis, not decay.
  No separate edit; note only.

---

## Phasing (each phase = build both arches + emu smoke + user audition)

- **Phase 1 (quick, low-risk, high-impact):** A1 (HPF -> 60 Hz) + A2/B3
  (defaults Decay 0.55, Damp 0.25). One build. A/B vs faustian.  -> 0.2.0.57
- **Phase 2 (tail):** B1 (soft-knee saturator, thr 0.7). Re-audition tail length
  and max-Decay stability. Then B2 (aggressive ceiling, kGdMax 0.992) if still
  short. -> .58 (/ .59)
- **Phase 3 (defer):** A4 structural density - separate effort, own plan.
  (A3 input-bandwidth cut per D2.)

## Decisions - RESOLVED
- **D1** - Wet HPF: **fixed 60 Hz** (no control).
- **D2** - Input bandwidth (A3): **skipped**.
- **D3** - Decay ceiling: **aggressive**, kGdMax 0.992 / kGdCap 0.995 (test drone).

## Verification per phase
- Both arches build clean; `check-neon-hints.sh` clean; FabricGraphic vtable
  weak (V); no new `floor()`/`vdiv` in the tank; luac-clean.
- Emu: insert + Size/Decay/Damp sweeps + (Phase 2) hold at max Decay to confirm
  the tail decays and does not self-oscillate.
- Ear: A/B against faustian at matched Decay/Damp to separate the
  nonlinearity/ceiling contribution from loop-length.
