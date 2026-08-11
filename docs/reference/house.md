# House (`house`) — v0.1.1

Eight spatial and character units. Six are ports of **Chris Johnson's Airwindows
plugins** (MIT licence) — kWoodRoom, WoodenBox, CreamCoat, BrightAmbience3,
Verbity, Galactic — kept algorithmically faithful to the upstream source and
shipped under their upstream names. TickerTape and Lacquer are original habitat
designs assembled from Airwindows-derived components (Console0, ChromeOxide,
Cojones, TapeFat).

Every control in this package is a `GainBias` ply: each has a CV input with its
own gain and bias, so all parameters are modulatable. All controls are unitless
0–1 unless noted. All units are stereo-capable — In1/In2 map to the DSP's In L/In
R and Out L/Out R back to Out1/Out2; in a mono chain only the left path is
connected. None of the units has a menu, sub-display, or expanded view: the
expanded view is simply the ordered ply row listed in each table.

---

## kWoodRoom

<mnemonic: kW> · Category: House

A woody, roomy small-space reverb: a 3×3 early-reflection trellis feeding a 6×6
feedback network tuned to a 249-seat club. Cross-feedback happens inside the
matrix, so it is internally stereo — a mono source still comes out wide. This is
the heaviest of the six reverb ports; budget CPU accordingly.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Regeneration | GainBias | 0–1 | 0.50 | Tank feedback. Mapped `(1 − (1−x)^1.618) × fdb6ck`, so decay stretches non-linearly toward the top. |
| Time (derez) | GainBias | 0–1 | 0.50 | Outer Bezier undersample divisor. Below 0.5 the divisor is stepped (integer, cleaner); above 0.5 it mirrors back down into a smoothed/continuous mode. Grinds the tank's time resolution rather than setting a decay time. |
| Tone (inner Bezier rate) | GainBias | 0–1 | 0.25 | Same stepped/smooth mirrored mechanic applied to the inner filter Bezier — acts as the tank's damping/brightness. |
| Early Reflections | GainBias | 0–1 | 0.50 | Level of the 3×3 early-reflection trellis, squared (`x²`), so the bottom of the dial is very gentle. |
| Position (delay-set) | GainBias | 0–1 | 0.75 | Picks a 9-tap window into the 36-entry early-reflection delay table (28 distinct start positions). Moves the listener/source position in the room. |
| Dry/Wet | GainBias | 0–1 | 0.50 | Straight crossfade; 0 = dry, 1 = wet. |

**I/O** — Stereo in/out (mono-capable). No V/Oct, gate, or trigger inputs.

---

## WoodenBox

<mnemonic: Wb> · Category: House

A small resonant box: four cascaded 4×4 Householder stages per side, walked in
opposite delay orders left vs right and cross-coupled at the final taps. Seventeen
prebaked delay-length tables give seventeen distinct box characters. The most
obviously "a physical space" of the set at short settings, and cheap enough to
use freely.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Box (1 of 17) | GainBias | 0–1, quantized to 17 steps (`int(x × 16.999)`) | 0.50 | Selects the delay-length table — the box's size and material character. Changing it zeroes the tank (audible cut, not a crossfade). |
| Resonance | GainBias | 0–1 | 0.50 | Feedback amount, mapped `(1 − (1−x)²) × 0.0336`. |
| Dry/Wet | GainBias | 0–1 | 0.50 | Crossfade with a `1 − (1−x)²` taper, so wet arrives quickly off zero. |

**I/O** — Stereo in/out (mono-capable). Note the port preserves Airwindows'
deliberate L/R swap through the tank: input L travels the right verb path and
vice versa.

---

## CreamCoat

<mnemonic: Cc> · Category: House

Bright ambience with the engine's internal divisor mechanic promoted to a user
knob. DeRez lets you deliberately grind the reflection resolution from lush down
to cheap and grainy, and it also scales the predelay time. Wetness is
submix-style rather than a crossfade, which makes this a natural send reverb.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Box (1 of 17) | GainBias | 0–1, quantized to 17 steps | 0.50 | Selects one of 17 delay-length tables. Changing it zeroes all 32 FDN lines. |
| Regeneration | GainBias | 0–1 | 0.50 | Feedback, mapped `(1 − (1−x)²) × 0.0625`. |
| DeRez (lush/cheap) | GainBias | 0–1 | 1.00 | Bezier undersample divisor, quantized to `1/int(1/x)`. 1.0 = full rate (lush); lower values decimate the tank's update rate for a grainier, cheaper texture. Also scales the effective predelay length. |
| Predelay | GainBias | 0–1 | 0.00 | Predelay into a 15000-sample buffer (≈340 ms at 44.1 k), multiplied by the current DeRez factor — so lowering DeRez shortens the predelay too. |
| Wetness (submix 0.5=full+full) | GainBias | 0–1 | 0.25 | **Submix, not a crossfade.** Internally `wet = 2x` (squared while below 1) and `dry = 2 − x`, each clamped at 1. At 0.5 both wet and dry are at full and sum; below 0.5 wet is attenuated against full dry; above 0.5 dry is attenuated against full wet. Confirmed in `atoms/CreamCoat.h`. |

**I/O** — Stereo in/out (mono-capable).

---

## BrightAmbience3

<mnemonic: Ba> · Category: House

Not a feedback tank: a windowed sum over up to 487 sparse prime-numbered taps
into a 32768-sample buffer, with a resonant state-variable filter in the feedback
path. The result is a bright, gated, halo-like ambience. Size is the CPU dial and
this unit has not had the optimization pass the others got — it stays heavy at
high Size.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Position | GainBias | 0–1 | 0.50 | Start offset into the prime-tap table (`int(x × 400) + 88`, clamped so start+length ≤ 488). Slides the halo earlier or later in time. |
| Size | GainBias | 0–1 | 0.50 | Number of taps summed: `int(x² × 487) + 1`. ~122 taps at default, 487 at maximum. Sets the length/density of the halo and dominates CPU cost. |
| Brightness | GainBias | 0–1 | 0.50 | Feedback amount (`x × 0.25`); also sets the SVF resonance, which is derived from tap count × feedback. Higher values ring brighter and longer. |
| Wetness | GainBias | 0–1 | 0.50 | Straight crossfade (not submix). |

**I/O** — Stereo in/out (mono-capable). Feedback is cross-coupled (L input takes
R's feedback and vice versa) — that cross is the stereo image.

---

## Verbity

<mnemonic: Vt> · Category: House

The most conventionally hall-like of the set: three cascaded 4×4 Householder
networks with input and output lowpasses, a per-tap interpolation smoother, and a
sub-low "thunder" chase underneath. The one that goes darkest. Cheapest trap
profile in the package — no transcendentals, no modulated reads, no LFOs.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Bigness | GainBias | 0–1 → size 0.1–1.87 | 0.25 | Scales all twelve delay lengths. Room size. |
| Longness | GainBias | 0–1 → regen 0.0625–0.09375 | 0.00 | Feedback amount / decay length. Also inversely scales the thunder term. |
| Darkness | GainBias | 0–1 | 0.25 | Drives three things at once: input/output lowpass (`(1 − x²)/√overallscale` — higher = darker), feedback-tap interpolation (`x² × 0.618`, a smearing smoother), and the amount of sub-low thunder. |
| Wetness | GainBias | 0–1 | 0.25 | **Submix, not a crossfade.** `wet = 2x`, `dry = 2 − x`, both clamped at 1: 0.5 sums full wet and full dry. Send-style. |

**I/O** — Stereo in/out (mono-capable).

---

## Galactic

<mnemonic: Gx> · Category: House

The lush, wandering one: three cascaded 4×4 networks preceded by a 256-sample
LFO-modulated predelay, with full left↔right cross-coupling at the feedback
stage. Ships at the Airwindows defaults — maximum size and full wet — so it is a
wash out of the box. Un-optimized this cycle and the heaviest atom for
transcendental work (four `sin()` per sample for the vibrato).

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Replace | GainBias | 0–1 → regen 0.125–0.0625 | 0.50 | **Inverted:** turning Replace up *reduces* feedback, so new signal replaces the tail faster. Input gain is compensated automatically (`attenuate = (1 − regen/0.125) × 1.333`). |
| Brightness | GainBias | 0–1 | 0.50 | Feedback-path lowpass; higher = brighter tail. |
| Detune | GainBias | 0–1 → drift `x³ × 0.001` | 0.50 | LFO rate for the modulated predelay. Cubic, so the lower half of the dial is very subtle and the top wobbles hard. |
| BigDim | GainBias | 0–1 → size 0.1–1.87 | 1.00 | Scales all twelve delay lengths. Room size. Defaults to maximum. |
| DryWet | GainBias | 0–1 | 1.00 | Standard crossfade with a cubic taper (`1 − (1−x)³`) — **not** submix, unlike Verbity and CreamCoat. Defaults to full wet. |

**I/O** — Stereo in/out (mono-capable). Feedback lines are fully cross-coupled
L↔R, which is where the wide wash comes from.

---

## TickerTape

<mnemonic: Tt> · Category: House

Not a reverb. An original habitat chain built from Airwindows parts: Console0
channel saturation → ChromeOxide tape rot → Console0 bus desaturation → dry/wet
mixer. The Console0 pair provides level-dependent containment around the tape
character, and the mix stage at the end makes it usable in parallel. Glue,
colour, and tape wobble rather than a clean tape emulation.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Drive | GainBias | 0–1 → gain 0.05–1.95 | 0.50 | Drives the Console0 channel and bus gains *symmetrically*, so 0.5 is a transparent unity round-trip; away from centre you push the saturation pair harder in one direction. |
| Tape | GainBias | 0–1 → intensity 0.9–1.9 | 0.50 | ChromeOxide drive: how hard the tape-rot mechanism (glitch-modulated bass subtraction plus saturated high band) bites. |
| Bias | GainBias | 0–1 → bias 0–0.76 | 0.30 | ChromeOxide's Output parameter, which sets the noise-FM warble bias — the depth of the tape wobble, not an output level. |
| Mix | GainBias | 0–1 | 1.00 | Linear dry/wet crossfade at the end of the chain. Defaults to fully wet. |

**I/O** — Stereo in/out (mono-capable). The Console0 pan controls are pinned to
centre internally and are not exposed.

---

## Lacquer

<mnemonic: Lq> · Category: House

Also not a reverb. An original mixed-rate character processor: gritty trajectory
distortion (Cojones) running inside a downsample shell where the aliasing *is*
the point, reconstructed to host rate, then clean averaging (TapeFat) inside a 2×
upsample bracket. Console0 saturation wraps both. The contrast between the rough
lacquer-cut side and the polished playback side is the unit's identity. All four
controls are continuous — nothing snaps.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Drive | GainBias | 0–1 → gain 0.05–1.9 | 0.50 | Symmetric Console0 channel/bus gain; transparent at 0.5. |
| Cut | GainBias | 0–1 | 0.50 | Two things at once: the downsample shell's rate (worldRate 1–8, i.e. host rate down to ÷8, fractional rates handled by the phase accumulator) and the Cojones disparity scalar (0.5–3.0). Sweeps from mild honk at 0 to heavy aliasing and strong honk at 1. A low-shelf compensation (up to +3.5 dB at Cut=1) restores the lows the reconstruction eats. |
| Polish | GainBias | 0–1 | 0.50 | TapeFat averaging: tap count 3–32 plus a wet blend of 0.2–1.0. The continuous blend smooths over the 1-tap jumps, so it reads as a single smooth "smoothing" control. |
| Mix | GainBias | 0–1 | 1.00 | Dry/wet crossfade. Defaults to fully wet. |

**I/O** — Stereo in/out (mono-capable).

<!-- VERIFICATION NOTES
Control-name discrepancies — RELEASE-2.8.0.md abbreviates; the on-screen
`description` strings (authoritative, from the Lua) are longer:
- kWoodRoom: notes say "Regen / Time / Tone / Reflect / Position / Mix";
  actual descriptions are "Regeneration", "Time (derez)",
  "Tone (inner Bezier rate)", "Early Reflections", "Position (delay-set)",
  "Dry/Wet".
- WoodenBox: notes "Box / Reso / Mix"; actual "Box (1 of 17)", "Resonance",
  "Dry/Wet".
- CreamCoat: notes "Predelay / Wetness"; actual "DeRez (lush/cheap)",
  "Wetness (submix 0.5=full+full)".
- Galactic: notes say the fifth control is "Wetness"; it is actually labelled
  "DryWet" on screen, and unlike Verbity/CreamCoat it is a plain cubic
  crossfade, not a submix.

CPU-weight claims:
- Release notes call kWoodRoom "the heaviest of the six" (reverbs) and Lacquer
  "the heaviest unit in the package". atoms/Galactic.h separately calls itself
  "heaviest atom in the package" / "heaviest house atom" (~25–30% stereo
  projected, from 4 sin() per sample). These three claims are mutually
  inconsistent as written; no measured figures are in the repo, so the doc
  reports the relative weights qualitatively rather than ranking them.
- BrightAmbience3.h projects ~20–30% stereo at default Size, ~50–70% at
  Size=1.0. Those are pre-hardware projections, not measurements.
- Release notes' "Galactic and BrightAmbience3 have not had that pass yet"
  is consistent with the headers (both still carry Phase 1 hybrid-float only;
  Verbity/kWoodRoom/WoodenBox carry float-baked per-sample paths).

Submix vs crossfade (verified in C++, not just prose):
- CreamCoat and Verbity: `wet = D*2; dry = 2 - D;` both clamped to 1, then
  summed — 0.5 really is full wet + full dry. CreamCoat additionally squares
  wet while it is below 1.0 (Verbity does not).
- kWoodRoom, WoodenBox, BrightAmbience3, Galactic, TickerTape, Lacquer are all
  ordinary crossfades.

Other notes:
- XYZ.lua, Carriage.lua, Filament.lua and RotCoat.lua exist in
  mods/house/assets/ but are not in toc.lua and are not documented here.
- No unit in the package defines onLoadMenu/onShowMenu, a collapsed view, or a
  sub-display; `collapsed = {}` for all eight.
- Ranges given as "0–1" are the ply's LinearDialMap(0,1) with 0.1/0.01/0.001
  stepping; the DSP additionally clamps out-of-range CV in most atoms, but
  kWoodRoom and BrightAmbience3 do not clamp every parameter, so extreme CV
  can push them past the documented range.
- Predelay's 15000-sample buffer is quoted as ≈340 ms at 44.1 kHz; the unit
  scales with the host rate via overallscale, so at 48 kHz the buffer is
  ≈312 ms.
-->
