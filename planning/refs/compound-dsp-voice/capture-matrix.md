# Compound DSP voice - capture matrix

Profiling target (hardware under test, implementation reference): **Industrial
Music Electronics / The Harvestman 1873 "Bionic Lester"**, a 13 HP dual
switched-capacitor multimode filter. The shipped habitat unit gets a generic
functional name (`feedback_no_third_party_branding`); the source is named here
only as the DUT for the reverse-engineering trail.

Refs: modulargrid.net/e/industrial-music-electronics-bionic-lester ; panel render
`panel-render.png`. No factory manual located; signal flow from panel, spec, and
the user. Testing resolves the gaps.

## Architecture

Two switched-capacitor filters **A** and **B**, believed identical. Cutoff is set
per channel by a **clock**; the clock can be cross-routed (**CLK SRC**). Input
amps overdrive; **distortion character changes with mode**. Aliasing (switched-cap
artifact) is a deliberate feature, strongest at **low** cutoff. **Does not
self-oscillate.** **The two filters interact heavily at all settings** (shared
clock / resonance / aliasing / mode + normalling), so behavior must be sampled
across the shared configuration space, not extrapolated from one filter in
isolation.

### Shared discrete toggles (affect BOTH channels together)
- **Filter mode** = the multi-output mode; both channels' switchable outputs
  **always correspond**: **HP / AP / Notch / hidden** (4). **LP and BP are hard
  outputs** (always live, independent of the toggle - but may still shift with
  aliasing / clk).
- **Aliasing**: **LO / HI** (2).
- **CLK SRC**: **A / B / both** (3).

=> **discrete configuration space = 4 x 2 x 3 = 24 configs.**

### Per-channel continuous
- **Cutoff A**, **Cutoff B** (per-channel clock freq; CV each).
- **Gain A**, **Gain B** (independent drive; overdrives).

### Shared continuous
- **Resonance** (voltage-controllable).

### Normalling
- **A audio -> B input** (nothing at B in -> B gets A).
- **A cutoff (knob + CV sum) -> B's Fc CV input.**

### Routing (by patching)
`single-A` (feed A, tap A) | `single-B` | `series` (A out -> B in) | `parallel`
(A normalled to both).

## Open questions the testing resolves
1. A vs B: same circuit or analog drift?
2. What is the hidden mode?
3. CLK SRC A/B/both interrelation mechanism (AM / XOR / clock intermod)?
4. Aliasing LO vs HI effect (and its cutoff/res/mode dependence)?
5. How mode-dependent is the distortion?
6. How do the two cutoffs interact (esp. clk=both)?
7. Series behavior across mode pairs; normalling behavior.

## Conventions

**Knob positions** (5): `ccw` `9` `12` `3` `cw`. **Baseline** (hold here unless
swept): cutoffs `12`, resonance `9` (clean), gains `gcl` (clean).

**Tapping - stereo by default.** We tap **two outputs per take** (L and R of the
euro->line converter) and split them in analysis. Standard pairing: **L = the
config's multi out, R = a hard out (LP or BP)**; use `lp+bp` when both hard outs
are what we want. This roughly halves the transfer-function / survey captures.
Fall back to a single mono tap only when one output is all that matters.
Caveat: level-match the two converter legs once (a mono tone into each, trim to
equal RMS) so per-channel levels are comparable - or, for pure transfer-function
sets, rely on the fact that deconvolution normalizes each channel anyway;
distortion sets (absolute level matters) need the legs matched.
The shipped unit's I/O is designed for the 301, not a jack-for-jack copy, so we
only need to know which topology to tap per take.

**Excitations** (`gen_excitation.py`; sent at the locked level, `calibration.md`):
`ess` sweep (transfer fn + Farina harmonic orders), `burst` (ringdown),
`shf` 2->20 kHz (aliasing fold-back), `s1k`/`s110` (distortion probes),
`lvl` ladder `cal_1k_rms-30..-06` (distortion vs input level, from `gen_tones.sh`).

**Naming**: `bl_<phase>_<route>_<mode><alias><clk>_<tap>_<knobs>_<exc>.wav`
- `mode`: `hp|ap|no|hid`  `alias`: `lo|hi`  `clk`: `a|b|x` (x = both)
- `tap`: `lp|bp|m` (mono), or stereo pair `m+lp` / `m+bp` / `lp+bp` (L+R)
- `route`: `A|B|ser|par`
- `knobs`: `base`, or the non-baseline positions e.g. `cA9cB3rcw g A cw` -> `cA9_cB3_rcw_gAcw`
- `exc`: `ess|burst|s1k|s110|shf|lvl`

Example: `bl_C_A_nolo a_m_base_ess.wav` -> config Notch/LO/clkA, single-A, tap multi, baseline, ess.

---

## The combinatorial reality

Full Cartesian (24 configs x 5 knobs x 5 positions x 3 taps x routes x excitations)
is thousands. We do NOT sweep everything everywhere. Strategy: a **front-loaded
config survey** to see which of the 24 configs are distinct, then **nest the knob
sweeps only where they earn it**, plus targeted distortion/aliasing/interaction
sets. Capture order prunes before the expensive nested work.

## PHASE C - Config survey (backbone; do first)

Single-A route, baseline knobs, `ess`, **stereo tap**. Map the frequency-shape of
every config.

| Sub | Configs | Tap (L+R) | Count | Reveals |
|---|---|---|---|---|
| C-survey | all 24 (mode x alias x clk) | m + lp | 24 | multi shape per config (**hidden mode**, clk/alias effect) AND LP-vs-config for free on R |
| C-bp | 6 (alias x clk), mode=N | lp + bp | 6 | BP vs alias/clk (BP is mode-independent core out) |

Subtotal ~30 (stereo folds the old LP/BP-vs-mode checks into C-survey's R channel).
**Read these before proceeding** - RMS/peak/rough-freq per take, then FFT. Prune
configs that duplicate; flag the characterful ones for Phase K.

## PHASE K - Knob laws (nested in chosen configs)

Reference config = `N / lo / clk-a`, single-A. Full sweeps here; then repeat the
starred sweeps in each config Phase C flagged distinct (est. 4-6 configs).

| Set | Sweep | Tap | Exc | Count/config | Notes |
|---|---|---|---|---|---|
| K-cutA * | cutoff A ccw..cw (5) | m | ess | 5 | cutoff->corner law |
| K-cutB | cutoff B ccw..cw (5) | m | ess | 5 | (matters most clk=b/x) |
| K-res * | resonance ccw..cw (5) | bp | ess | 5 | Q law |
| K-res-rd | res 3,cw (2) | bp | burst | 2 | ringdown; confirm no self-osc |
| K-hid | cutoff+res in hidden mode | m | ess | ~7 | hidden-mode character |

Reference config full: ~24. Each additional flagged config (starred sets):
~10. With ~5 flagged configs: ~24 + 5x10 = ~74.

## PHASE D - Distortion (mode-dependent)

Gain drives the onboard overdrive; character changes with mode. Cut12 res9,
single-A unless noted.

| Set | Config coverage | Sweep | Tap | Exc | Count |
|---|---|---|---|---|---|
| D-gain | each mode {lp,bp,hp,ap,no,hid} x alias{lo,hi}, clk=a | gain A gcl..cw (5) | tapped mode | s1k | 6x2x5 = 60 |
| D-lvl | modes {lp,bp,hid}, gain cw, alias lo | input ladder (5) | tapped | lvl | 15 |
| D-ess | modes {lp,bp}, gain gcl,cw, alias lo | 2x2 | tapped | ess | 4 |

Subtotal ~79. D-gain is large; if alias has no distortion effect (check first at
one mode), drop to clk=a alias=lo only -> 6x5 = 30.

## PHASE X - Interactions (both filters in play)

| Set | Config | Sweep/pairs | Route | Exc | Count |
|---|---|---|---|---|---|
| X-cutAB | clk {a,b,x}, mode N, alias lo | cutA x cutB grid (3x3 offsets) | series | ess | 27 |
| X-clk | mode N alias lo, cutoffs offset (A12 B3) | clk a,b,x | series | s1k+ess | 6 |
| X-series | clk a, alias lo, baseline | mode pairs (LP-LP,LP-HP,BP-BP,HP-LP,N-BP,hid-LP,LP-hid,BP-hid) | series | ess | 8 |
| X-series-od | clk a | 3 pairs driven (gain 3/cw) | series | s1k+ess | 6 |
| X-norm | mode N alias lo, patch A only | cutA ccw,12,cw; tap A and B | single | ess | 6 |
| X-alias-cut | mode N, alias{lo,hi}, clk a | cutoff ccw,9 (low) x2 | single | ess+shf | 8 |

Subtotal ~61. X-cutAB (the two-cutoff interference, esp clk=both) and X-clk are the
heart of the "heavy interaction" the module is prized for.

## PHASE B - A/B match / drift

Mirror Phase C reference config + a few knob spot-checks on B; compare to A.
~12. If B tracks A within tolerance, model one filter + note drift; else promote B.

## Totals and order

Rough **~180 captures** with stereo tapping (was ~270 mono) if thorough - large,
matching a compound, clock-based, heavily-interacting, mode-dependent module.
Stereo pairs the multi out with a hard out on every transfer-function / survey /
knob take, so most sets that wanted two outputs now cost one capture. Tiers let
you stop early:

0. **THD-floor-with-DUT**: single-A, LP, clean gain, cut12 res9, N/lo/clk-a, `s1k`
   -> `thd.py`. Confirms the loop with the module in; module's own clean-settings
   distortion floor. First capture.
1. **PHASE C** (config survey) - the backbone. Prune here: it tells us how many of
   the 24 configs are genuinely distinct, which decides how much of K/D/X we owe.
2. **PHASE K** in the reference config + flagged configs.
3. **PHASE D** distortion.
4. **PHASE X** interactions (the payoff sets).
5. **PHASE B** A/B, and re-scope once single-filter models exist.

Reassess after Phase C: the hidden mode, the real clk/alias effect, and how many
configs collapse together will reshape K/D/X counts (likely downward).

## Analysis note

Read distortion harmonics at their exact bins (2k, 3k, 4k...) to dodge the
documented interference (50/120/240 Hz hum, ~5.2 kHz whine - `calibration.md`).
ESS deconvolution rejects that stationary interference anyway.
