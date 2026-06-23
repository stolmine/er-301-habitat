# Fabula Tank Density Redesign — Implementation Plan

Status: **ready to implement**. Staged, gated, incremental.
DSP rationale, diagnosis, echo-density math, and sources: `planning/fabula-tank-density.md`.
Design context: `planning/fabula-design.md`. Current build: zaum **0.1.0.6** (all 8 params live).

---

## 1. Problem and goal

The tank has **2 plain Schroeder allpasses per loop**, giving ~25–40 echoes/sec at
tail onset versus the ~**1000 echoes/sec** Schroeder smoothness threshold. It only
crosses that threshold after ~300–500 ms of recirculation, so the first few hundred
ms contain audible discrete echoes. Brownian modulation smears eigentones but cannot
create echoes that do not exist — it is a lushness layer, not a density mechanism. The
density mechanism (Gardner allpass structure) was removed in 0.1.0.2 to stop a runaway.

**Goal.** Raise the effective allpass count from 2 → **4 stages per loop** so echo
density crosses ~1000/sec within ~100 ms, eliminating audible discrete echoes in the
first ~150 ms, while:
- staying **passively stable** (unity-gain by construction; governors untouched),
- keeping the **approved default character** (denser is desired; must not become washy
  or metallic),
- adding negligible CPU/memory (~14 KB, ~8 MAC/sample).

**Success criteria.**
1. Offline echo-density curve crosses 1000/sec within ~100 ms (vs current ~300–500 ms).
2. Single-AP unity-gain check is flat to ±0.1 dB, 20 Hz–20 kHz.
3. 30 s white-noise at Decay=max → bounded output, Spiral governor never engages.
4. Perceptual: click/snare at Decay=0.5, Mix=1.0, Mod=0 → no individually audible
   echo in the first ~150 ms; smooth noise-like tail.
5. Default params sound denser, same tonal character — not washy/metallic.

---

## 2. Chosen structure: SAFE series-cascade allpasses (not in-feedback nesting)

**Critical clarification.** The research doc (`fabula-tank-density.md` §2A/§4) labels the
structure "Gardner nested," but its difference equations are a **series cascade of two
independent unity-gain Schroeder allpasses** per tank stage — the outer AP's feedforward
output feeds a second AP that has its **own separate buffer**. This is deliberately **NOT**
true in-feedback Gardner nesting (inner allpass embedded *inside* the outer allpass's
recirculating delay), which is what ran away in 0.1.0.2.

Why the series form is the right call:
- **Provably unity-gain:** the cascade transfer function is `H_outer(z)·H_inner(z)`, a
  product of two `|H|=1` allpasses, so `|H|=1` everywhere for `|g|<1`. Cannot run away.
- **No shared buffers, no in-loop feedback coupling** — the two failure modes from the
  0.1.0.2 runaway are structurally impossible here.
- Still doubles effective stages (2 → 4 per loop), and the shorter inner delays
  (367/491) add density *earlier* in each traversal (~8–10 ms vs 22–31 ms).

True in-feedback nesting (denser still) is held **in reserve** — used only if the
series cascade proves insufficient, and only behind the same offline unity-gain +
density + stability gate (§3).

### Per-loop signal flow (each tank AP: outer → inner, in series)

```
tankIn → [AP1 outer 1087, g=0.70] → [AP1 inner 367, g=0.50]
       → D1 (modulated) → HF damp
       → [AP2 outer 1471, g=0.50] → [AP2 inner 491, g=0.50]
       → D2 (modulated) → ×g_d → governor → cross-feed
```

### Exact difference equations (L loop; R symmetric, swap `_L`→`_R`)

```
// AP1_L  (outer kTA1=1087 g_out=0.70 ; inner kTA1i=367 g_in=0.50)
double vO1d = (double)mTA1_L[mWrTA1_L];      // outer read, v[n-1087]
double vI1d = (double)mTA1i_L[mWrTA1i_L];    // inner read, v[n-367]
double vO1n = tankIn_L + gTA1_out * vO1d;
double in1  = -gTA1_out * vO1n + vO1d;        // outer AP output = inner AP input
double vI1n = in1 + gTA1_in * vI1d;
double ap1Out_L = -gTA1_in * vI1n + vI1d;     // nested AP1 output
mTA1_L[mWrTA1_L]   = (float)vO1n;
mTA1i_L[mWrTA1i_L] = (float)vI1n;
mWrTA1_L  = (mWrTA1_L  + 1 >= kTA1)  ? 0 : mWrTA1_L  + 1;
mWrTA1i_L = (mWrTA1i_L + 1 >= kTA1i) ? 0 : mWrTA1i_L + 1;

//  ... D1_L modulated read (unchanged) → dampedD1_L (unchanged) ...

// AP2_L  (outer kTA2=1471 g_out=0.50 ; inner kTA2i=491 g_in=0.50)
double vO2d = (double)mTA2_L[mWrTA2_L];
double vI2d = (double)mTA2i_L[mWrTA2i_L];
double vO2n = dampedD1_L + gTA2_out * vO2d;
double in2  = -gTA2_out * vO2n + vO2d;
double vI2n = in2 + gTA2_in * vI2d;
double ap2Out_L = -gTA2_in * vI2n + vI2d;
mTA2_L[mWrTA2_L]   = (float)vO2n;
mTA2i_L[mWrTA2i_L] = (float)vI2n;
mWrTA2_L  = (mWrTA2_L  + 1 >= kTA2)  ? 0 : mWrTA2_L  + 1;
mWrTA2i_L = (mWrTA2i_L + 1 >= kTA2i) ? 0 : mWrTA2i_L + 1;
// d2 path uses ap2Out_L as before.
```

### New state (constants already declared: kTA1i=367, kTA2i=491)

```cpp
// inner AP buffers — UNMODULATED, exact size (no headroom)
float mTA1i_L[kTA1i];  float mTA1i_R[kTA1i];   // 367 each
float mTA2i_L[kTA2i];  float mTA2i_R[kTA2i];   // 491 each
int   mWrTA1i_L=0, mWrTA1i_R=0, mWrTA2i_L=0, mWrTA2i_R=0;
```
Memory: 4 × (367+491) × 4 B = **~13.7 KB**. CPU: ~8 extra MAC/sample. All four buffers
`memset` to 0 in the constructor; all four write heads init 0.

### Inner coefficients
`gTA1_in = gTA2_in = 0.50` fixed for Stage A (Dattorro-class value, safe). Hard-cap
every coefficient `|g| < 0.95` in code regardless of parameter.

---

## 3. Validation rig (numerical FIRST — postmortem discipline)

Per `network-cascade-postmortem` and `fabula-tank-density.md` §3: **test echo density
numerically, not by ear alone**. Build a throwaway offline harness in the scratchpad
(`.../scratchpad/density-rig/`) — plain C++, NO `od` dependency, replicating only the
tank's allpass + delay recurrences:

- **(a) Unity-gain isolation check.** Feed a log sine sweep (20 Hz–20 kHz) through ONE
  series-cascade AP in isolation. Assert output magnitude flat ±0.1 dB. Gate BEFORE
  wiring into the loop — catches a wrong `-g` sign or buffer error immediately.
- **(b) Echo-density curve.** Feed a unit impulse into the full tank with g_d at min,
  no modulation, no damping (isolate structure). Capture ~500 ms IR. Count local maxima
  > (peak − 40 dB) in sliding 10 ms windows → events/sec vs time. Run for the OLD (2-AP)
  and NEW (4-AP) structures; the new build-up curve must be steeper and cross 1000/sec
  at least ~2× earlier (target < ~100 ms).
- **(c) Stability regression.** 30 s white noise at g_d=cap. Assert bounded; assert the
  Spiral saturator is never triggered (any trigger ⇒ a coefficient is misconfigured).

The harness is not shipped. Its numbers are the gate for promoting each stage to an emu
audition.

---

## 4. Staged implementation

Each stage: **edit → offline-validate (§3) → `make zaum` → install (front+rear) →
emu audition → commit**. One stage per commit. Roll back to the prior tag on any gate
failure.

### Stage A — 0.1.0.7: add the inner series allpasses
- Add the 4 inner buffers + write heads (§2); constructor memset/init.
- Insert the inner AP after each outer AP in both loops per the §2 equations. Inner
  coeffs fixed at 0.50.
- **Wet tap UNCHANGED** this stage (isolate the density change from the tap change).
- **Diffusion** still scales only the outer coeffs (inner fixed) this stage.
- Gates: §3 (a)(b)(c) pass; emu perceptual criteria 4–5 (§1) pass; no runaway at Decay
  max. Note any RT60 shift (inner APs add ~858 smp/loop to the round trip → tail ~5–6%
  longer at the same Decay); only recalibrate the Decay→g_d curve if it is perceptually
  off — likely leave it.

### Stage B — 0.1.0.8: D3 early tap + Diffusion recalibration
- **D3 early tap:** add the pre-D1 nested output to the wet sum to smooth the onset:
  `wetL = 0.25*ap1Out_L + 0.375*d1Read_L + 0.375*d2Read_L` (and R). Calibrate weights
  by ear — `ap1Out` is unmodulated, so it adds early density without pitch wander.
- **Diffusion recalibration:** extend the Diffusion→coeff map to scale the inner coeffs
  too. Suggested: outer `g_out = 0.40 + diff*0.35` (0.40..0.75), inner
  `g_in = 0.30 + diff*0.25` (0.30..0.55), centered so **Diffusion=0.6 reproduces the
  Stage-A defaults** (outer current values, inner ≈ 0.50) — keep the approved character
  at default. Hard-cap `|g| < 0.95`.
- Gates: Diffusion sweep stable and musical at both extremes (no metallic ring at high,
  acceptably dense at low — nesting means even low g gives diffusion); onset audibly
  smoother with D3; default preserved.

After Stage B, Fabula is dense + feature-complete. The remaining **pre-release polish**
(button labels/descriptions review, Mod-vs-ModRate consolidation decision, Size
prime-pool snapping, optional shorter Decay minimum via `kGdMin`, expanded-view layout,
PKGVERSION for first user-facing release) is the final pass — unchanged from
`fabula-design.md` §0.1.0.7, now following the density work.

---

## 5. Versioning note

The roadmap's original 0.1.0.8 (dual-scale frequency-routed tanks) was a stretch goal
and is **deferred** — these two density stages take 0.1.0.7 and 0.1.0.8. Dual-scale, if
ever pursued, moves to 0.1.0.9+. Density is the higher-value work and directly fixes a
reported defect.

---

## 6. Risks and rollback

| Risk | Mitigation |
|---|---|
| Series cascade still too sparse (discrete echoes persist) | Escalate to TRUE in-feedback Gardner nesting, or add a 3rd series AP / richer 6-stage input diffusion (Options B/C in `fabula-tank-density.md`). Same §3 gate applies. |
| Over-diffuse / washy at default | Lower inner g (0.50→0.40), or apply nesting to AP1 only, not AP2. |
| Metallic ring at high Diffusion | Tighten the Diffusion upper cap; verify inner+outer never co-peak. |
| RT60 drift from added loop delay | Recalibrate the Decay→g_d curve constant; minor. |
| Runaway regression | Impossible by construction if §3(a) passes; each stage is its own commit — revert to 0.1.0.6 / 0.1.0.7. |

---

## 7. Stage-A code-change checklist (for the implementer)

1. `mods/zaum/atoms/APFTank.h`:
   - Member block (inside `#ifndef SWIGLUA`): add `mTA1i_L/R[kTA1i]`,
     `mTA2i_L/R[kTA2i]`, and the four `mWr*i_*` ints.
   - Constructor (outside the guard, with the other memsets): `memset` the four inner
     buffers; init the four write heads to 0.
   - `process()`: replace the AP1/AP2 plain-allpass blocks in BOTH loops with the
     series-cascade blocks from §2. Keep `ap1Out_*`/`ap2Out_*` names so Stage B's D3 tap
     can reference `ap1Out_*`.
   - Header "BUILD SUB-PHASE" comment → 0.1.0.7 (series-cascade allpasses for echo
     density; explicitly note: series cascade, NOT in-feedback nesting).
2. `mods/zaum/mod.mk`: `PKGVERSION ?= 0.1.0.7`.
3. Build, run §3 rig, install, audition, commit.
