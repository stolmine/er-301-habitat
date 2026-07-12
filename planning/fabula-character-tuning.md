# Fabula Character Tuning — Three Issues, Concrete Levers

Status: **proposal only** — research and design, no code changes.
Addresses user critique of build 0.1.0.7: "slightly metallic, slightly washy /
transients vanish, defaults feel cavernous — distance and scale, not density."
Architecture reference: `mods/zaum/atoms/APFTank.h` (build 0.1.0.7).
Prior density work: `planning/fabula-tank-density.md`, `planning/fabula-density-redesign.md`.

---

## Signal-flow summary (0.1.0.7)

```
IN (mono sum) → Predelay (0..340 ms) → 4-stage input diffusion (229/173/613/449)
             → figure-8 tank:
               AP1 cascade (outer 1087 + inner 367) → D1 (Brownian-mod, Size-scaled)
               → one-pole HF damp → AP2 cascade (outer 1471 + inner 491)
               → D2 (Brownian-mod) → ×g_d → Spiral → cross-feed
             → multi-tap signed wet sum (ap1Out + 3×D1 intermediate + D1-end
               + 2×D2 intermediate + D2-end) × kWetLevel=2.2
             → dry/wet mix → OUT
```

Defaults: Size=0.5, Decay=0.5 (g_d≈0.85, RT60≈7 s at Size=0.5), Damp=0.25,
Diffusion=0.6, Mod=0.3, ModRate=0.2, Predelay=0, Mix=0.5.
No distinct early-reflection network. No allpass modulation (only tank delay-line
Brownian walks). Multi-tap output already ships.

---

## Issue 1 — Slightly Metallic

### What the literature says

An allpass of delay N has a flat frequency-domain magnitude |H(e^jω)|=1, but its
group delay is comb-shaped. A short impulsive signal through a series allpass chain
therefore produces "a ringing sound, where only certain frequencies are resonating"
(Valhalla DSP, "Reverbs: Diffusion, allpass delays, and metallic artifacts," 2011,
https://valhalladsp.com/2011/01/21/reverbs-diffusion-allpass-delays-and-metallic-artifacts/).
The ringing arises from the allpass's impulse response being an exponentially
decaying comb, not broadband noise — so eigentones persist at each delay length's
fundamental and harmonics until modulation or enough round-trip convolutions smear them.

Two structural causes in tank allpasses specifically:

1. The tank allpass delays (1087 and 1471 outer, 367 and 491 inner) ring at their
   own modal frequencies each time an impulsive signal traverses the loop. With four
   allpass stages per loop (cascade) the eigentone spectrum is denser than with two
   plain stages, but still coherent.

2. The intermediate static output taps (five unmodulated taps on D1/D2) read the delay
   line at fixed offsets. Comb coloration from fixed taps is well-documented: "taking
   output from inside an allpass will transform the allpass into a comb" (freeverb3 reverb
   tips, https://freeverb3-vst.sourceforge.io/tips/reverb.shtml). Our intermediate D1/D2
   taps are on pure delay lines, not allpasses — but the five fixed read positions impose a
   partial comb structure on the output spectrum that modulation does not fully remove.

Established fixes, per the literature:

- **Modulate the tank delay lines** (already done): spreading eigentones via time-varying
  delay is the primary tool. Dattorro 1997 (CCRMA, https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf)
  establishes this as the primary anti-metallic mechanism in the tank.
- **Modulate the allpass delay lines** (not yet done): the Valhalla/freeverb3 sources confirm
  that modulating the allpass delays themselves — not just the pure delay lines — further
  reduces residual eigentone coloration, provided modulation is applied to the *longer*
  allpasses (outer 1087/1471 in our case) not the short input diffusers (229/173/613/449).
  For short input diffusion APs, modulation produces "water sloshing in a metal pan" rather
  than reducing metallic sound (Valhalla, 2011, ibid.). The tank APs at 1087/1471 samples
  (22–31 ms) are long enough that slow shallow modulation should smear eigentones without
  audible chorusing.
- **Increase modulation depth or rate** (tuning knob, already present): more excursion
  spreads eigentones across a wider frequency range.
- **Increase the cascade diffusion** (inner/outer AP coefficient): more diffusion = faster
  blurring of eigentones per traversal, but risks washiness (Issue 2 tradeoff).

### Mapping to our architecture

| Lever | Where in APFTank.h | Implementation cost | Stability/CPU |
|---|---|---|---|
| **A. Extend Brownian modulation to outer tank APs** (AP1 outer d=1087, AP2 outer d=1471, L+R) | New: 2 walk accumulators + 2 seeds per loop side (8 new doubles total); modify the outer AP read in both loops to use a fractional modulated offset | Medium — requires fractional read and Brownian LFO per AP, same pattern as D1/D2 but smaller buffers and smaller excursion (suggest ±4–8 samples at most to avoid audible pitch waver) | Zero stability risk (delay modulation cannot break unity-gain). CPU: ~4 MACs/sample per AP × 4 APs = 16 extra MACs — negligible. Buffer headroom: outer AP buffers are sized exactly N samples and have no headroom. Must add headroom (±8 samples → buffers grow by 16 samples each: kTA1 1087→1103, kTA2 1471→1487). |
| **B. Increase Mod default** | `mMod` parameter default in constructor (currently 0.3f → try 0.4–0.45f) | Trivial — one constant | None |
| **C. Reduce Diffusion default slightly** (lower AP coefficient = smoother but slightly thinner onset) | `mDiffusion` default (currently 0.6f) — lowering toward 0.5 reduces outer AP1 coefficient from 0.70→0.675 | Trivial | None |

**Risk of regressing density fix:** Lever A is purely additive (it does not change the
allpass coefficients or the cascade structure); it cannot reduce echo density. Levers B and C
affect density only marginally. The 0.1.0.7 density work is safe.

---

## Issue 2 — Washy / Transients Vanish

### What the literature says

Two independent mechanisms cause washiness and transient loss:

**A. Input diffusion amount.** Cascading allpasses before the tank smears the onset.
The Valhalla blog (2011, ibid.) notes: "cascading a larger number of series allpasses extends
the attack time to the point where the sound seems to 'fade in.'" This is the textbook
input-diffusion vs. clarity tradeoff. Our four-stage input diffusion at Diffusion=0.6
(coefficients 0.75, 0.75, 0.625, 0.625) is on the denser side of this tradeoff. The
Dattorro paper's published defaults are exactly these values, but Dattorro's target was
a plate-like lush tail where transient definition is deliberately sacrificed for smoothness.
For a believable room, some transient survival at the wet output is desirable.

**B. Absence of discrete early reflections.** In a real room the first 20–80 ms of reverb
contains sparse, individually identifiable reflections arriving from specific surfaces.
These early reflections are perceptually fused with the dry sound, reinforcing it and
preserving the sense of attack. All-diffuse reverb from the tank onset (no distinct early
reflections) produces a smooth onset that "swallows" the dry transient. As Griesinger
observes: "strong early energy...gives you distance without changing intelligibility"
(Sound On Sound interview, https://www.soundonsound.com/people/david-griesinger-lexicon-creating-reverb-algorithms-surround-sound).
Without that early energy preceding the diffuse tail, the listener hears only enveloping
wash with no anchor to the attack.

The Moorer (1979) reverb explicitly addressed this by adding a tapped delay line for early
reflections before the comb-filter late reverberation — "of crucial importance in the
perception of acoustic space, more so than the late reflections" (flyingSand blog,
"Algorithmic Reverbs: The Moorer Design," https://christianfloisand.wordpress.com/2012/10/18/algorithmic-reverbs-the-moorer-design/).
The Dattorro architecture omits a dedicated early-reflection section.

Established fixes:

- **Reduce input diffusion** or expose Diffusion default at a lower value — partially restores
  transient identity at the cost of a less smooth onset.
- **Non-zero predelay** (5–15 ms) separates the dry transient from the wet onset in time,
  so the attack lands on the ear first before the diffuse wash begins. Psychoacoustically
  this "frees" the attack from the reverb smear.
- **Discrete early-reflection tap network** placed in parallel with the tank input path — a
  handful of non-diffused, fixed (or lightly modulated) delay taps that fire before the
  diffuse tank energy arrives, preserving the room's initial impulse character and restoring
  transient definition.

### Mapping to our architecture

| Lever | Where in APFTank.h | Implementation cost | Stability/CPU |
|---|---|---|---|
| **D. Predelay default** — raise from 0 → ~10 ms (480 samples at 48 kHz, ~0.029 on the 0..1 scale) | `mPredelay` parameter default | Trivial — one constant; already available | None |
| **E. Reduce input diffusion default** — lower Diffusion default from 0.6→0.45, mapping outer ID1/ID2 coefficients from 0.75→0.7125, ID3/ID4 from 0.625→0.587 | `mDiffusion` parameter default | Trivial — one constant | None; lower g = less diffusion but still denser than no diffusion |
| **F. Early reflection tap network** — parallel path: 4–6 discrete taps (5–55 ms range, non-diffused, signed gains decaying with delay, summed into wet path with separate wet-level scalar) added to the output before Mix | New: 1 ring buffer (e.g. 4096 samples, ~16 KB), 4–6 integer read positions, a separate early-wet parameter or fixed baked-in balance | High — new structure, new buffer, new signal path. Low stability risk (FIR, no feedback). CPU: negligible (5–6 muls + reads per sample). No regression to tank density. | Adds ~16 KB ring buffer; otherwise O(1) per tap. No feedback → unconditionally stable. |
| **G. Expose Diffusion as explicit Early/Late balance** — scale input diffusion independently of tank diffusion, so user can reduce ID1-4 coefficients without affecting tank AP density | Requires splitting the single `Diffusion` parameter into two: `InputDiff` and `TankDiff`, or mapping the existing `Diffusion` control to only input diffusion while leaving tank APs fixed | Medium — parameter UI change + mapping logic change | None |

**Early reflection tap pattern (lever F) — concrete sketch:**

```
// Parallel path alongside tank: read pre-diffusion signal (after predelay, before ID chain)
// Six taps at: 7, 13, 23, 37, 53, 71 ms (336, 624, 1104, 1776, 2544, 3408 samples at 48k)
// Gains:       0.65, 0.55, 0.45, 0.35, 0.25, 0.15 (geometric decay)
// Signs:       +, -, +, -, +, -  (Schroeder-style alternation for flatter spectrum)
// Required ring buffer: 3409 samples (71 ms) + 1 = 3410. Use a 4096 power-of-two.
// Wet-mix coefficient "earlyWet" ~ 0.15–0.25 (the early energy should be quiet
// relative to the tank, present but not echo-audible — Griesinger target: -5 to -10 dB
// relative to dry).
```

Tap positions chosen to be prime (or at least mutually coprime) to avoid comb
coincidences and to span the 7–71 ms pre-diffuse window that exists before the
diffuse tank energy fully builds.

Note: early reflections are best drawn from the signal AFTER predelay but BEFORE
input diffusion (i.e., from `diffIn` at the predelay read point, before the ID chain).
This preserves transient sharpness in the early reflections, which is their perceptual
purpose.

**Risk of regressing density fix:** Zero. The early-reflection path is a parallel FIR
with no feedback. The tank density work is entirely separate.

---

## Issue 3 — Distant and Cavernous, Not Present and Dense

### What the literature says

This is the most deeply structural issue and the one with the richest literature support.

**The direct-to-reverberant ratio (D/R) governs perceived distance.** As D/R decreases
(more reverb relative to dry), a source is perceived as farther away. Griesinger
(IOA proceedings, https://www.ioa.org.uk/system/files/proceedings/d_griesinger_the_importance_of_the_direct_to_reverberant_ratio_in_the_perception_of_distance_localisa.pdf)
and his work on perceptual distance and localization establish that "changes in reverberant
level (D/R) are far more audible than changes in reverberation time (RT)." At Mix=0.5
(equal dry and wet) with RT60≈7 s and no distinct early reflections, the perceived D/R
is low: the wet energy builds quickly and is sustained, pushing the source into the distance.

**Early reflections are the cue for proximity, not just for room size.** The perceptual
literature is clear that the first 50 ms of reverb is the key window for proximity and
envelopment. As Griesinger states: "You have the sense of distance, which is influenced
by nearly any time range — reflected energy coming in nearly any time period will cause a
feeling that you are at some distance from the sound source," but the *early* energy
(before 50 ms) is what creates a sense of presence rather than remoteness. The worst
region perceptually is 50–120 ms ("Between 50mS and 120mS is probably the worst possible
time to get energy from an intelligibility point of view"). A reverb with no discrete
early energy before the diffuse onset, and whose diffuse onset begins immediately at the
tank input, fills this worst-case zone with undifferentiated wash.

**RT60=7 s at default is a cathedral, not a room.** The computation from APFTank.h
gives RT60 ≈ 7 s at Decay=0.5. Real small-to-medium rooms have RT60 of 0.3–1.0 s.
Concert halls: 1.5–2.5 s. Cathedrals: 5–10 s. The default Decay=0.5 places Fabula
squarely in cathedral territory. This is architecturally intentional (Decay maps as a
power curve centered on g_d=0.85 at 0.5) but perceptually defaults to the extreme.
The user correctly observed: "The character at defaults is a massive space, cavernous."

**All-diffuse late energy without early reflections reads as distant cavern.** The
classic room-acoustics model (Schroeder, Moorer, Griesinger) separates the impulse
response into: (1) direct sound, (2) early reflections 5–80 ms — defining the room's
character, proximity, and clarity; and (3) late diffuse tail — defining decay envelope
and envelopment. Fabula 0.1.0.7 has only categories 1 and 3. The early-reflection
window is filled by the onset of diffuse tank energy, which is smooth (after the density
fix) but still "undirected" — the ear perceives no distinct room surfaces, only a
distant enveloping wash.

Established fixes:

- **Discrete early reflections** (same lever F from Issue 2): the most direct fix for
  all three issues simultaneously. Early reflections create the room's spatial image,
  restore transient definition, and shift the perceived location toward "present room"
  rather than "distant cavern."
- **Lower default Decay** (shorter RT60): shifting the default toward Decay=0.25–0.35
  gives RT60 ≈ 3–5 s, still lush but less cathedral-scale. The user noted "you can get
  a more contained space by upping Damp and turning down Decay and Size" — the fix is
  simply making this the default.
- **Higher default Damp**: more HF absorption reads as a smaller, more absorptive space
  (wood, fabric, people). At Damp=0.25 the tail is relatively open. Raising to Damp=0.40
  gives a warmer, more room-like tail character.
- **Lower default Size** (already noted by user): Size=0.5 maps to sizeFactor=1.0 (the
  base tank lengths: RTT≈330 ms, implying a very large space). Lowering to Size=0.3–0.35
  gives sizeFactor≈0.7, RTT≈230 ms, which reads as a medium hall rather than a cathedral.
- **Wet level / Mix default**: Mix=0.5 is equal dry/wet power, which is perceptually a
  very wet setting. Reducing Mix default to 0.35–0.40 increases D/R without touching
  the actual reverb character.

### Mapping to our architecture

| Lever | Specific value change | Perceptual effect | Implementation cost |
|---|---|---|---|
| **H. Lower Decay default** | 0.5→0.30 (g_d≈0.76, RT60≈4.0 s at Size=0.5) | Less cathedral, more large hall | Trivial — one constant |
| **I. Raise Damp default** | 0.25→0.40 (coeff≈0.62, noticeable but not heavy HF roll) | Warmer, more absorptive room, less airy cavern | Trivial — one constant |
| **J. Lower Size default** | 0.5→0.35 (sizeFactor≈0.85, RTT≈280 ms) | Smaller apparent room | Trivial — one constant |
| **K. Lower Mix default** | 0.5→0.40 | Higher D/R → source perceived closer | Trivial — one constant |
| **F. Early reflection network** (same lever as Issue 2) | See above | Strongest fix: installs the proximity cue that is structurally absent | High — new path |

---

## Prioritized Recommendation

### Tier 1 — Immediate default retuning (try first, trivial cost)

All four levers are single-constant changes and can be evaluated in one build cycle.
They address all three issues simultaneously, partially:

| Param | Current default | Proposed default | RT60 result (Size=0.35) |
|---|---|---|---|
| Decay | 0.5 | 0.30 | ~3.5 s (large room, not cathedral) |
| Damp | 0.25 | 0.40 | warmer HF rolloff, small-room character |
| Size | 0.5 | 0.35 | ~0.85× base RTT — medium-large space |
| Predelay | 0.0 | 0.029 (~14 ms) | separates dry attack from diffuse onset |
| Mix | 0.5 | 0.40 | slightly higher D/R, more present source |
| Mod | 0.3 | 0.40 | slightly more eigentone smear |

**Expected net effect:** the default sound shifts from cathedral to large hall; transient
attacks are separated from the wet onset by 14 ms of dry signal before the tank energy
arrives; the more open Mod reduces residual metallic coloration. The user's own diagnostic
("you can get a more contained space by upping Damp and turning down Decay and Size") is
implemented as a factory default. The full range is still available by sweeping the
controls.

**Validation:** feed a snare hit at Mix=1.0 (wet only), Mod=0.0 — the onset should not
begin until ~14 ms after the hit. Compare before/after on a sustained piano tone: the new
defaults should feel less diffuse, closer, warmer.

### Tier 2 — Allpass modulation for residual metallic quality (medium cost)

Lever A: extend Brownian modulation to the outer tank allpasses (AP1 outer d=1087, AP2
outer d=1471), with small excursion (±4–6 samples, ~0.1–0.12 ms at 48 kHz). This targets
the eigentone ringing that persists even after tank delay modulation and increased Mod depth.
The mechanism is well-supported by Dattorro 1997 and the Valhalla/freeverb3 sources.

Implementation requires: 4 additional Brownian walk accumulators + 4 seeds (8 doubles);
modifying the AP outer-buffer reads to use a fractional modulated offset; adding ±8-sample
headroom to kTA1/kTA2 buffer allocations (net +64 bytes). The approach is the same
Brownian walk pattern already used on D1/D2 — no new mechanisms.

Keep modulation of the inner APs (d=367/491) off. Inner APs are too short for meaningful
modulation without audible artifacts.

**Do not modulate** the input diffusion allpasses (ID1–ID4, d=229/173/613/449). All four
are in the short range where modulation produces "water sloshing" artifacts (Valhalla, 2011,
ibid.).

**Validation:** feed a sustained sine at 440 Hz, Mix=1.0, Damp=0, Decay=0.5, Mod=0 then
Mod=0.4. Listen for residual ringing/metallic tone in the tail. After lever A, the ringing
should be less tonally coherent even at lower Mod values.

### Tier 3 — Early reflection tap network (high cost, highest perceptual leverage)

Lever F: a 4–6 tap FIR drawn from the post-predelay / pre-diffusion signal, spanning
7–55 ms, with geometrically decaying gains and alternating signs. This addresses the
structural absence of an early-reflection section and is the highest-leverage single fix
for all three issues (reduces metallic coloration by providing a clear pre-tail attack cue;
restores transient definition; shifts perceived space from distant cavern to present room).

This is architecturally a new signal path and warrants its own sub-phase. Before implementing,
the Tier 1 retuning should be committed and auditioned — it may reduce the urgency of Tier 3
enough that a simpler 2-tap version (just two discrete echoes at 15 ms and 35 ms) suffices
for an initial listen.

A suggested minimal implementation for a first listen:
```
// After predelay, before input diffusion: read two taps from the predelay buffer.
// These arrive as distinct, un-diffused echoes at the output (wet path only).
// They do not enter the recirculating tank.
int er1 = predelayTap + (int)(0.015 * 48000);  // 15 ms after predelay offset
int er2 = predelayTap + (int)(0.035 * 48000);  // 35 ms after predelay offset
// clamp both to [0, kPD-1], wrap with (kPD-1) mask
double erOut = 0.50 * mPD[(mWrPD - er1) & (kPD-1)]
             - 0.35 * mPD[(mWrPD - er2) & (kPD-1)];
// Mix into wet at ~-8 dB relative: earlyWet = 0.40
double wetL = earlyWet * erOut + tankWet * kWetLevel * (signed tap sum);
```

Both er1 and er2 use the predelay buffer that already exists — zero additional memory if
predelay is already non-zero. The cost is 2 additional mod-and-reads per sample.

**Validation for Tier 3:** feed a single click (impulse) at Mix=1.0, Mod=0, Decay=0.
You should hear 2 discrete pre-echoes arriving before the diffuse tank onset. They should
be quiet enough not to be heard as a distinct slapback on musical material but present
enough to change the spatial impression from "diffuse wash from the start" to "identifiable
room surfaces then decay."

---

## Risks and interactions

| Lever | Risk | Mitigation |
|---|---|---|
| Tier 1 default retuning | Users familiar with 0.1.0.7 defaults will notice change. At Mix=0.40 some wet-send patches may need recalibration. | Ship as a clearly version-bumped parameter change. Document in changelog. |
| Allpass modulation (A) | AP outer buffers currently have no headroom — read pointer could go out of range at maximum modulation. Must add buffer headroom before enabling. | Size buffers to kTA1+16/kTA2+16 (±8 samples). Trivial. |
| Early reflections (F) | Er taps spill from the predelay buffer when Predelay=0 (er1/er2 taps go negative). Must guard: only enable ER path when predelay ≥ the er2 offset, or use a separate small ER buffer. | Use a dedicated 4096-sample ER buffer at the pre-diffusion tap point, independent of the predelay ring buffer. |
| Lower Decay default | Decay=0.30 → g_d≈0.76 → RT60≈3.5 s at Size=0.35. Some users want long lush defaults; the full range is still available at Decay→1. | Document "long" as an intentional excursion, not the default room. |

---

## What the offline rig can measure

The density rig from `planning/fabula-density-redesign.md` §3 can be extended for two
additional numerical checks:

1. **Early/late energy ratio:** after implementing any ER tap, capture the first 150 ms of
   an impulse response. Split at 50 ms. The ratio E_early/E_late should be positive dB —
   i.e., more energy in the first 50 ms than in 50–150 ms. Currently this is inverted
   (tank energy builds after 50 ms, not before).

2. **Eigentone coherence with and without AP modulation:** feed a single-frequency sine
   at the tank AP resonance frequency (≈f_s/1087 ≈ 44 Hz), Mod=0 vs Mod=0.4. Measure
   the peak-to-average ratio of the output power spectrum over a 2 s window. AP modulation
   should reduce the peak, distributing the resonance energy across a wider band.

---

## Sources

- Dattorro, J. (1997). "Effect Design Part 1: Reverberator and Other Filters." CCRMA.
  https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf
- Griesinger, D. "The Importance of the Direct to Reverberant Ratio in the Perception of
  Distance, Localization, Clarity, and Envelopment." IOA proceedings.
  https://www.ioa.org.uk/system/files/proceedings/d_griesinger_the_importance_of_the_direct_to_reverberant_ratio_in_the_perception_of_distance_localisa.pdf
- Griesinger, D. (interview). "Creating Reverb Algorithms For Surround Sound." Sound On Sound.
  https://www.soundonsound.com/people/david-griesinger-lexicon-creating-reverb-algorithms-surround-sound
- Costello, S. (Valhalla DSP). "Reverbs: Diffusion, allpass delays, and metallic artifacts." 2011.
  https://valhalladsp.com/2011/01/21/reverbs-diffusion-allpass-delays-and-metallic-artifacts/
- freeverb3 reverb algorithm tips (includes Costello's modulation and eigentone analysis).
  https://freeverb3-vst.sourceforge.io/tips/reverb.shtml
- Floisand, C. "Algorithmic Reverbs: The Moorer Design." flyingSand, 2012.
  https://christianfloisand.wordpress.com/2012/10/18/algorithmic-reverbs-the-moorer-design/
- Smith, J.O. "Early Reflections." Physical Audio Signal Processing, CCRMA.
  https://ccrma.stanford.edu/~jos/Reverb/Early_Reflections.html
- ValhallaRoom: "Early Reflections versus Early Energy." Valhalla DSP, 2011.
  https://valhalladsp.com/2011/05/04/valhallaroom-early-reflections-versus-early-energy/
