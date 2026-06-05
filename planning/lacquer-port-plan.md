# Lacquer implementation plan

Status: **PLANNED 2026-06-05**. Second chain-as-unit in the house package, post-TickerTape. Implements the "Option F mixed brackets" topology from the chain-design discussion: a downsample shell around the gritty character atom, then an upsample bracket around the smooth filter atom. The contrast between the lo-fi lacquer-cut character and the hi-fi polished playback IS the unit's identity.

**Name**: Lacquer (final, not codename). Evokes the lacquer cutting + playback process: acid-etched grooves cut deep and rough on the master lacquer, then played back through a hi-fi cartridge that smooths the artifacts. Captures both halves of the down-then-up signal flow in one image.

**Design ethos** (per user direction):
- 500-series "set and forget" controls, not designed for modulation
- Stepped/discontinuous controls are fine and even desired for character knobs
- 3-4 controls total: Drive + 1-2 macros + Mix
- Goal: simple, characterful, surprising tone coloring

## Macro topology

```
in (host rate)
  → Console0 sat (Channel-style input drive)
  → Downsample shell with Cojones inside (gritty trajectory distortion at reduced rate)
  → Bezier reconstruct back to host
  → 2x upsample bracket with TapeFat inside (clean averaging at oversampled rate)
  → Decimate back to host
  → Console0 desat (Buss-style output recovery)
  → Dry/wet mix via ChainMix
  → out (host rate)
```

The Console0 sat pair wraps the whole rate-bracket chain providing level-dependent containment. Cojones runs at REDUCED rate (its trajectory-tracker harmonics fold back into aliasing — the lacquer-cut grit). TapeFat runs at 2× rate (its averaging smooths harmonics with proper headroom — the polished playback).

## Why monolithic Object (not Lua chain)

The chain-as-unit pattern (per `planning/house-atom-architecture.md`) usually means atoms composed via Lua `addObject` + `connect`. That works at host rate because the graph compiler schedules per-Object using host-rate buffers.

**Rate brackets break this pattern.** A downsample shell needs the inner atom to process at a different rate, which can't be expressed as a host-rate Lua connection. Same for the 2x upsample bracket — TapeFat needs to see 2-samples-per-host-sample inside the bracket.

So Lacquer ships as a SINGLE monolithic Object that does everything internally. The "chain" is hand-coded in C++ rather than wired in Lua. The atoms used inside (Cojones, TapeFat, Console0 sat math, ChainMix logic) all stay as reusable component patterns — they're just embedded inline rather than wired through the graph.

Trade: lose the Lua-iteration loop on the chain composition; gain the ability to do mixed-rate processing inside a unit.

## Math/rate audit

### AW param-default subtlety check (per `feedback_aw_param_default_subtle`)

**Cojones**: AW source uses `breathy = A*2, cojones = B*2, body = C*2`. At AW defaults A=B=C=0.5, all three internal scalars = 1.0, and the per-sample formula reduces to:
```
output = body*storedL[0] + cojones*(input - storedL[0] - avg[1]) + breathy*avg[1]
       = 1.0*smoothed + 1.0*(input - smoothed - avg) + 1.0*avg
       = input
```
**Literally identity at defaults.** This is the AW pattern: character at extremes. For Lacquer, we need to map the "Cut" macro to push these scalars asymmetrically so the audible regime fills the knob.

**Cojones character mapping for Lacquer** (Cut knob drives the trajectory disparity):
- Fixed internal: `body = 0.3` (slight LP signal contribution), `breathy = 0.2` (slight smoothed signal)
- Variable internal: `cojones = 0.5 + Cut * 2.5` (range 0.5..3.0 across the Cut knob)
- This biases output toward the disparity / trajectory tracker signal as Cut increases
- Output formula at Cut=0: 0.3*smoothed + 0.5*(input - smoothed - avg) + 0.2*avg — subtle midrange honk
- Output formula at Cut=1: 0.3*smoothed + 3.0*(input - smoothed - avg) + 0.2*avg — strong honk, distorty edge

**TapeFat**: AW uses `leanfat = (A*2)-1` (range -1..1) and `fatness = floor(B*29+3)` (range 3..32 taps). At AW default A=0.5, leanfat=0, wet=0 → effect is BYPASSED. At A=0.7, leanfat=0.4, wet=0.4 → 40% blend of averaged signal.

**TapeFat character mapping for Lacquer** (Polish knob drives the averaging amount):
- Fixed internal: `leanfat = +Polish` (always in "fat" / lowpass direction, not "lean" / highpass)
- Variable internal: `fatness = (int)(3 + Polish * 29)` (3..32 taps stepped by user value)
- Polish=0: 3 taps minimal smoothing, 0% wet (transparent)
- Polish=0.5: 17 taps, 50% wet — clear high-freq taming, audible micro-reverb character
- Polish=1: 32 taps max smoothing, 100% wet — heavily lowpassed, distinctive micro-reverb tone

This makes Polish a single-knob "character intensity" axis from clean to heavily-coated.

### Rate bracket math

**Downsample shell** (around Cojones — same pattern as RotCoat):
- `cyclePhase` accumulates `1.0 / worldRate` per host sample
- `inAccumL/R` averages input samples between fires
- On fire: average input → push to Cojones → store cycleOutL/R
- Per-sample output: linear interp between cyclePrev and cycleOut at cyclePhase
- `worldRate` snapped to {1, 2, 3, 4, 6, 8}

**2× upsample bracket** (around TapeFat — simpler):
- Per host sample, produce 2 upsampled samples via linear interpolation:
  - `upSample_a = (prevHost + currentHost) * 0.5` (midpoint between previous and current)
  - `upSample_b = currentHost` (the current sample itself)
- Run TapeFat on both upsampled samples; collect two outputs
- Decimate: one-pole IIR LPF at ~Nyquist/2 (alpha ~0.5) to anti-alias, then take the second of the two LPF'd outputs as the host-rate output

For v1, simple linear interp + one-pole IIR LPF is cheap and produces enough of the "polished" character. Higher-order polyphase FIR is a Phase B optimization.

### AW scalar concerns (Cojones + TapeFat both reviewed)

| Atom | Default behavior | Remap chosen |
|---|---|---|
| Cojones | Identity at A=B=C=0.5 | Fixed body=0.3, breathy=0.2; cojones = 0.5 + Cut*2.5 |
| TapeFat | Bypassed at A=0.5 (leanfat=0, wet=0) | Fixed leanfat=+Polish (always lowpass), fatness stepped 3..32 by Polish |

Both atoms ship with deliberate remapping so the user-facing knob travel is characterful end-to-end.

### Cojones at reduced rate — character implications

Cojones's 5-sample trajectory window operates on what it sees as consecutive samples. At reduced rate (e.g. ÷4 in the shell), each "sample" Cojones sees is actually an AVERAGE of 4 host samples. The trajectory tracker measures disparity over what's effectively a 20-host-sample window (= ~0.4 ms at 48k).

This shifts the character downward in frequency. At host rate, Cojones tracks sample-to-sample disparity (mostly high-frequency content). At ÷4, it tracks frame-to-frame disparity (mid-frequency content). At ÷8, low-mid.

So Cut also functions as a "character frequency band selector":
- Cut=0 (÷1, host rate): bright honk, high-frequency disparity
- Cut=0.5 (÷4): mid-frequency honk, lo-fi
- Cut=1 (÷8): low-mid honk, cassette-tape territory

This is musically interesting — Cut isn't just intensity, it's also tonality. Worth documenting in the unit description.

### TapeFat at 2× rate — character implications

TapeFat's averaging cutoff scales with sample rate. At host rate, 17 taps averages ~17 samples = high-frequency cut around 1.4 kHz. At 2× rate, 17 taps averages 17/2 = 8.5 host-samples-equivalent = cut around 2.8 kHz.

So inside the 2x bracket, TapeFat operates with a HIGHER effective cutoff than it would at host rate. "Polished" because the lowpass is gentler, preserving more brightness.

The micro-reverb artifacts from the prime-tap arrangement also compress in time — a 32-tap average at 2x rate has total tap span 32/96k = 333 µs vs 32/48k = 667 µs at host rate. Tighter, cleaner tail.

## State + memory budget

| Group | Size |
|---|---|
| Console0Channel state (avg AL/AR/BL/BR) | 4 doubles |
| Downsample shell (cyclePhase, cycleOut/Prev, inAccum/Count) | ~10 doubles + 1 int |
| Cojones state (storedL/R[2], diffL/R[6]) | 16 doubles |
| 2× upsample interp (prevHostL/R, decimateLP_L/R) | 4 doubles |
| TapeFat state (pL[256], pR[256] as int) | 2 KB |
| Console0Bus state | 4 doubles |
| **Total** | ~2.5 KB per instance |

Trivially fits L2. No allocations after construction.

## CPU projection

Per host sample stereo, by mode:

| Stage | Cycles |
|---|---|
| Console0Channel sat (~15 ops/side, no transcendentals) | ~30 |
| Downsample shell per-sample work (accumulate + advance + interp) | ~10 |
| Cojones cycle (when fired) | ~80 |
| At World=÷1 (every sample fires): full Cojones cost | +80 |
| At World=÷4 (every 4th sample): Cojones cost amortized | +20 |
| 2× upsample interp (linear midpoint) | ~10 |
| TapeFat at 2x rate (called twice per host sample, ~80 cycles each) | ~160 |
| Decimate IIR LPF + dropping | ~10 |
| Console0Bus desat | ~30 |
| ChainMix crossfade | ~10 |
| Per-sample overhead (denormal flush, etc) | ~10 |

**Worst case** (Cut=0 = ÷1, Polish=high): ~350 cycles per sample stereo = ~490 ns at 720 MHz = **~2.4% CPU**.

**Typical** (Cut=0.5 = ÷4, Polish=0.5): ~280 cycles per sample stereo = ~390 ns = **~1.9% CPU**.

**Best case** (Cut=1 = ÷8, Polish=low): ~250 cycles per sample stereo = ~350 ns = **~1.7% CPU**.

Multiple instances stack trivially. Comparable to TickerTape.

## Parameter mapping (4 plies, 500-series character)

**Option 1 — full surface (4 plies, recommended)**:

| Knob | Range | Default | Behavior |
|---|---|---|---|
| **Drive** | continuous 0..1 | 0.5 | Console0Channel + Bus gain (symmetric, transparent at 0.5) — same continuous shape as TickerTape |
| **Cut** | snapped to {÷1, ÷2, ÷3, ÷4, ÷6, ÷8} | ÷4 | Downsample World divisor for Cojones. Also functions as character-band selector (high → low frequency content emphasized). **Discontinuous stepped knob** per the 500-series ethos. |
| **Polish** | snapped to {3, 6, 10, 16, 24, 32} | 16 | TapeFat fatness (number of delay taps averaged) at 2× rate. **Discontinuous stepped knob.** |
| **Mix** | continuous 0..1 | 1.0 | Dry/wet via ChainMix-style crossfade. The only "always smooth" knob. |

**Option 2 — macro surface (3 plies, even more set-and-forget)**:

| Knob | Range | Default | Behavior |
|---|---|---|---|
| **Drive** | continuous 0..1 | 0.5 | Same as Option 1 |
| **Coat** | continuous 0..1 (drives BOTH Cut + Polish via a curve) | 0.5 | Single character macro. At 0: light coat (Cut=÷2, Polish=6). At 0.5: medium coat (Cut=÷4, Polish=16). At 1: heavy coat (Cut=÷8, Polish=32). Walks a coupled curve so Cut and Polish move together. |
| **Mix** | continuous 0..1 | 1.0 | Same as Option 1 |

**Recommendation: Option 1** — the discrete Cut and Polish controls give the user direct access to the unit's two distinct character regimes (cut depth + playback smoothness) and the stepped feel matches the 500-series ethos. Option 2 is the "less is more" alternative if Cut+Polish coupling feels right after audition.

For the **stepped knob UI** in Lua: use a custom biasMap with `setSteps(big_step, medium_step, small_step, tiny_step)` where all values are >= the inter-value spacing. This snaps the knob to discrete positions. Example for Cut (6 positions):
```lua
local cutMap = app.LinearDialMap(0, 5)  -- discrete positions 0..5
cutMap:setSteps(1.0, 1.0, 1.0, 1.0)  -- always snaps to integer
```
Then in C++, map the integer 0..5 to the World divisor table {1,2,3,4,6,8}.

## CloudSeed-trap audit (preventive)

- No `firstFrame` guards needed — all state init via memset / ctor
- No allocations after constructor
- No host APIs beyond `globalConfig.sampleRate` (used by overallscale for Cojones if needed — actually Cojones doesn't use overallscale, math is rate-independent ✓)
- No `std::vector`
- No modulated reads — Cojones state is 6-deep history (bounded); TapeFat reads at fixed-prime offsets (bounded by buffer size 256)
- No transcendentals per sample (Cojones is all polynomial/branching; TapeFat is integer math; Console0 is polynomial; ChainMix is linear)
- No runtime-branched DSP dispatch in per-sample loop — all knob mappings read once at top of process()
- Per-sample dither dropped per template
- `-fno-tree-vectorize` package-wide
- TapeFat int-summation safe at typical signal levels (input ≤ 1.0, *8388608 = ~8.4M, sum of 32 taps ≤ 270M; well inside int32 max ~2.1B)
- Downsample cycleStep `while`-loop fire bounded (cycleStep ≤ 1.0 since worldRate ≥ 1)

**Verdict: cleanest expensive atom in the catalog.** Should work first try on hardware.

## LOAD-BEARING design invariants

Reversing any of these breaks the intended character or correctness:

1. **Cojones inside DOWNSAMPLE shell, TapeFat inside UPSAMPLE bracket** — swapping them reverses the lacquer-cut metaphor and loses the character contrast
2. **Cojones order: drive → smooth** — body=0.3, breathy=0.2, cojones=variable. Doesn't reduce to identity at Cut=0; gives clear character at all settings
3. **TapeFat always in "fat" direction (leanfat = +Polish)** — never goes into "lean" (highpass) territory. Lacquer is about smoothing, not exciter
4. **Console0 wraps the whole rate-bracket chain** — provides level-dependent governor for the in-loop nonlinearity. Wrap order: in → Channel → [bracket chain] → Bus → out
5. **2× decimation MUST have at least a one-pole LPF before sample-drop** — otherwise the upsample bracket's character is dominated by alias garbage instead of "polished" smoothing
6. **ChainMix at the OUTPUT, not inside the chain** — dry path takes original input; wet path takes fully-processed signal. Parallel processing semantics.

## Phasing — single-shot (Phase A)

The atoms are simple, the rate bracket math is well-understood (downsample is RotCoat-proven, upsample is straightforward), no novel risks. One commit:

1. **`mods/house/atoms/Lacquer.h`** (NEW monolithic Object — header-only, hybrid float for state, double for math)
2. **`mods/house/assets/Lacquer.lua`** (NEW unit, 4 plies)
3. **`mods/house/house.cpp.swig`** (add `%include` for Lacquer.h)
4. **`mods/house/assets/toc.lua`** (add Lacquer entry)
5. **`mods/house/mod.mk`**: bump 0.1.0.18 → 0.1.0.19

Build both arches + lints + install linux. Hardware audition.

**Hardware gate**: dial-a-coat behavior across Cut + Polish stops. Drive sweeps from clean to console-saturated. Mix gives parallel blend. At default settings (Drive=0.5, Cut=÷4, Polish=16, Mix=1.0) — CLEAR characterful effect, not "where's the sound?". CPU under 5% per instance.

If first audition works: commit + push, then evaluate Option 2 (Coat macro) as alternate variant or move to next chain unit.

## What this validates / establishes

This is the **first chain unit with mixed-rate brackets** in the package. Proves the pattern for future units that want lo-fi-then-hi-fi character contrast. Also adds two new component-ready helpers (Cojones, TapeFat math patterns) that could ship as their own monolithic Objects later if useful. The downsample shell + 2x upsample bracket boilerplate could be extracted into reusable helper functions in a future header (e.g. `RateBrackets.h`) if Spool / similar units want them.

## Open implementation questions

1. **Cojones character setting at Cut=0** — chose body=0.3 / breathy=0.2 / cojones-variable. May need empirical tuning at audition. Could expose a hidden "Body" or "Breathy" mid-character toggle if the single-axis Cut feels too monolithic.
2. **TapeFat polarity** — chose always-fat (leanfat=+Polish). Could expose a polarity toggle for "lean" mode (highpass / exciter) but this complicates the surface. Defer.
3. **Decimation LPF cutoff** — chose one-pole at ~Nyquist/2 (alpha ~0.5). Higher quality FIR could shift this; one-pole is the cheap-and-good-enough version. Iterate on audition.
4. **Cut stepped vs continuous** — chose stepped per 500-series ethos. Could be continuous with internal snap (smooth interp through divisor space) but stepped is the "intentionally discontinuous" choice the user asked for. Audition to confirm.
5. **Mix curve** — linear by default. Could use equal-power crossfade (`cos(mix*π/2)` and `sin(mix*π/2)`) for less notch at 50% — but that adds 2 sin calls per sample. Use the spiralFastSaturate polynomial alternative if needed.

## Files

```
mods/house/atoms/Lacquer.h        # Phase A new (monolithic Object)
mods/house/assets/Lacquer.lua     # Phase A new (4-ply wrapper)
planning/lacquer-port-plan.md     # this doc
```

PKGVERSION: 0.1.0.18 → 0.1.0.19

## Why this plan respects established rules

- `feedback_atoms_as_components`: monolithic by NECESSITY (rate brackets don't graph-compose at host rate); the math patterns inside could be extracted as components later if useful for other chain units
- `feedback_aw_atom_port_template`: hybrid float, dropped dither, header-only `od::Object`
- `feedback_no_third_party_branding`: Lacquer is habitat-native (original chain composition); Cojones + TapeFat math credited verbatim in headers; Console0 sat curve math same as TickerTape
- `feedback_aw_param_default_subtle` (NEW): both Cojones and TapeFat AW defaults reduce to identity / bypass; remapped Lacquer plies so audible regime fills knob travel
- `feedback_identical_means_identical`: Cojones trajectory math + TapeFat tap arithmetic preserved verbatim, only the user-facing knob remapping changes
- `feedback_no_out_of_line_virtuals`: header-only
- `feedback_disable_tree_vectorize_am335x`: package mod.mk already enforces
- `feedback_always_build_both_arches`: Phase A builds both
- `feedback_linux_build_auto_install`: linux auto-installed
- `feedback_package_version_bump`: 0.1.0.18 → 0.1.0.19
- **XYZ lesson**: zero libm sin/asin per sample. Cojones is polynomial-only; TapeFat is integer-only; Console0 is polynomial-only; ChainMix is linear-only.
- **RotCoat lesson**: while-fire on cyclePhase, cycleStep ≤ 1.0 ensured by worldRate ≥ 1 snap. While-loop technically unnecessary here but kept for consistency.
- **ChromeOxide lesson**: no rate-dependent IIR coefficients inside Lacquer. Cojones is rate-independent. TapeFat's averaging cutoff naturally scales (which we WANT for the "polished" character at 2x).
