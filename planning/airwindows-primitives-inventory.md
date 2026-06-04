# Airwindows Tone-Shaping Primitives — Inventory & Parts Bin

Companion to `airwindows-port-er301-handoff.md`. Source: Airwindopedia (Chris Johnson's own per-plugin notes), MIT. This is the atom set for building tone-shaping **chains** and **harnesses** (see handoff §2/§5). Selection bias: small, composable, cheap building blocks — not finished mix tools.

## How to read this

- **Everything here is stereo in/out, double-precision in AW.** Port = `double → float` + NEON; treat as a listening decision, not a recompile (dither/noise-floor behavior shifts). Not repeated per entry.
- **Tone-shapers are FLOP-bound, not memory-bound** (unlike reverbs). Chains of them are cheap on float+NEON. The only expensive moves are the ones that wrap a chain in oversampling or a filter bank.
- **Cost:** `trivial` = per-sample scalar math, ~no state · `cheap` = small filter memory or short delay buffer · `moderate` = many stages / predictive filter / cascade.
- **Contract fields** (omitted when default): *Params* · *State* (stateless / filter-mem / short-delay) · *Latency* (0 unless noted — matters for feedback & parallel harnesses) · *SR* (neutral unless fixed-frequency or stage-count scales with rate) · *Role* (chain-link / driver / glue / container).

## Lineage map (why these are "primitives")

Chris's catalog is recombination on recombination — these trees are the reason a small atom set covers huge ground:

- **`sin()` saturation:** Spiral → Density (multi-stage, ± ) → Drive (positive-only grit) → Mojo (curve^4, loudenator) → Dyno (intensenator) → PurestDrive / PurestSaturation (minimal, high-res)
- **Slew / derivative domain:** Slew → Acceleration (2nd deriv) → GoldenSlew (golden-ratio chain) → Sinew (slew+sine, tape-bias) → Creature (stacked = slew wavefolder) → GuitarConditioner (Slew+Highpass matrix)
- **Averaging filter:** Average → ToneSlant (tilt EQ) / GrooveWear (HF scrub)
- **Rate/bit crush:** DeRez → BitGlitter (sampler emu) → Pockey (µLaw 12-bit)
- **Tape (ToTape internals, standalone):** Flutter (warble) · TapeBias (bias) · ChromeOxide (dual-band rot)
- **Console:** Console0 (bit-shift sin) → PurestConsole3 → Console9 (ConsoleX summing)

---

## 1. Saturators / waveshapers (mostly trivial, the workhorse atoms)

- **Spiral** — golden-ratio `sin()` saturation; the smoothest-curve building block the rest descend from. *State:* stateless. *Cost:* trivial. *Role:* chain-link.
- **Density** — `sin()` transfer, **multi-stage**, bipolar (positive = saturate/fatten, negative = antisaturate/thin); can highpass just the distorted path and remix. The Swiss-army atom. *Params:* density(±), highpass, out, dry/wet. *Cost:* trivial. *Role:* chain-link.
- **Drive** — Density's curve without negative range: pure grit/crunch, "hides nothing." *Cost:* trivial.
- **Mojo** — Spiral curve raised to powers (up to 4th): biggenator + loudenator. *Cost:* trivial.
- **Dyno** — Mojo's sibling for **intensity** not loudness; reshapes waveform without volume boost. *Cost:* trivial.
- **PurestDrive** — minimal, high-resolution saturation; subtle, "French house." No oversampling — relies on internal precision. *Cost:* trivial. *Role:* glue.
- **PurestSaturation** — softclip via deliberately "wrong" `sin()`/Taylor coefficients. *Cost:* trivial.
- **Fracture** — West Coast **wavefolder**: sine wrap with settable max fold → hard clip; drive into it for more folds. *Params:* drive, fracture, out. *Cost:* trivial. *Role:* chain-link (dramatic).
- **Trianglizer** — waveshaper that morphs sines toward triangles; zero-crossing-centered. *Cost:* trivial.
- **Loud** — models air-molecule breakup; transfer varies with level; good **glue at zero boost**, supernova at max. *Cost:* trivial→moderate.
- **Creature** — up to 32+ **stacked soft-slew saturators** ("slew wavefolder"); acts between distortion and filter. *State:* per-stage slew mem. *SR:* stage count scales with rate. *Cost:* moderate. *Role:* chain-link (signature dramatic atom; cheap at low Depth).
- **Mackity** — Mackie 1202 input-stage slam (pre-VLZ). *Cost:* cheap. *Role:* glue/character.
- **NCSeventeen** — soft-clip loudenator + Chebyshev modulation to regenerate 2nd-harmonic/bass. *Cost:* moderate.
- **UnBox** — distortion that only lets **non-aliasing** harmonics distort (frequency-aware). *Cost:* moderate. *Use:* low-alias drive without oversampling.

## 2. Slew / derivative-domain shapers (buffer-free, all cheap)

These darken/tame via sample-to-sample delta limits — no filters, tiny state, ideal chain links and Creature-style ladders.

- **Slew** — slew clipper (limits Δ/sample): darkens treble, adds grind. AW's first plugin. *State:* 1-sample. *Cost:* trivial.
- **Acceleration** — acceleration (2nd-derivative) limiter: tames edge, **keeps brightness**; mastering-grade. *Cost:* trivial.
- **GoldenSlew** — chain of progressively-restrictive slew clippers scaled by golden ratio. *Cost:* cheap.
- **Sinew** — sine + slew clipping → analog-tape-like HF saturation behavior (tape bias feel) without tape model. *Cost:* cheap.
- **Smooth** — clipper that tames pointy transients or, pushed, "makes drums explode." *Cost:* cheap. *Role:* chain-link / driver fodder.
- **Cojones** — 5-sample trajectory tracker; heightens or minimizes waveform disparities → midrange sonority or distort. *Cost:* cheap.
- **Bite** — midrange/treble **edge-maker** (or inverse to remove edge). *Latency:* ~couple samples. *Cost:* cheap.

## 3. Filters / tone-tilt (cheap one-poles, averaging, FM-cutoff)

- **Capacitor2** — LP/HP with **signal-voltage-modulated cutoff** (barium-titanate cap model, asymmetric FM). Sweepable, transient-popping. *State:* filter-mem. *Cost:* cheap. *Role:* chain-link / driver-modulated.
- **Baxandall** — transparent 2-band shelf EQ whose voicing shifts as you push it. *Cost:* cheap. *Role:* glue.
- **ToneSlant** — tilt EQ via a 100-tap linearly-faded averaging block; very transparent corner. *Latency:* block (~100 taps). *Cost:* moderate. *Role:* glue.
- **Average** — simple interpolating averaging lowpass; flangey cancellation node, "incorrect" but pleasing. Basis of ToneSlant/GrooveWear. *Cost:* cheap.
- **Weight** — resonant **sub-bass** boost (Holt-based). *Cost:* cheap.
- **Aura** — resonant lowpass EQ, analog-flavored; wet control intensifies resonance (scary near full wet). *State:* filter-mem. *Cost:* moderate. *Role:* chain-link (dramatic).
- **AngleFilter** — meant-to-be-brickwall filter that instead does wild phase and steepens as cutoff drops. *Cost:* moderate. *Use:* weird/synth filtering.
- **Kalman** — predictive "not-a-filter" from GPS math; strange lowpass-ish coloration. *Cost:* moderate. *Use:* character, not correctness.
- **Distance2** — depth/space shaper (Distance + Atmosphere): air-absorption + small delay; drums → huge without reverb. *State:* short-delay + filter-mem. *Cost:* cheap→moderate. *Role:* glue/spatial.

## 4. Lo-fi / destruction (cheap; pair with §3 filters to tame aliasing)

- **DeRez2** — continuous-rate / fractional-bit crusher with **interpolated** output (smooth top). The "analog" bitcrusher. *State:* hold + interp. *Cost:* cheap.
- **BitGlitter** — old-**sampler emulator** (tonal, more than a crusher; gateable with DC offset). *Cost:* cheap→moderate.
- **Pockey** — 12-bit µLaw lo-fi hiphop texture (Pocket-Operator vibe). *Cost:* cheap.
- **ChromeOxide** — dual-band tape: lows saturated, highs delayed by a **noise warble + bias** (the RotCoat per-line seed). *State:* short-delay + noise. *Cost:* cheap.
- **Flutter** — input-driven warble (rate derived from the signal), standalone from ToTape6. *State:* modulated delay. *Cost:* cheap. *Role:* driver-ish (reacts to input).
- **TapeBias** — under/overbias control (adds supersonic bias tone). *Cost:* cheap.
- **PowerSag2** — power-supply **starve** (grungey/gatey), with inverse control to hear what's removed; coded for efficiency. *Cost:* cheap.

## 5. Console atoms (the containment primitive — build harness first)

Channel = encode (saturate) **before** sum; Buss = decode (desaturate) **after** sum. Wrapping any inner chain in a channel→buss pair is the level-dependent, anti-runaway containment harness (handoff §2, mechanic 1).

- **Console0Channel / Console0Buss** — most minimal: `sin()`/`asin()` via bit-shift-only multiplies (~8 ops/sample/ch). *Cost:* trivial. *Role:* container.
- **PurestConsole3Channel / Buss** — refined bit-shift sin approximation. *Cost:* trivial. *Role:* container.
- **Console9Channel / Buss** — ConsoleX summing core. *Cost:* trivial→cheap. *Role:* container.
- **Console7Cascade** — 5 ultrasonic-filtered slam stages; high-gain channel. *Cost:* moderate. *Use:* heavy drive container.

## 6. Dynamics / transient atoms (cheap; great harness drivers)

- **Point** — explosive **transient designer**, 3 controls; secret-weapon simplicity. *Cost:* trivial. *Role:* chain-link / driver.
- **Swell** — dial-an-attack auto-volume (sidechain *feel* without sidechain). *Cost:* cheap. *Role:* **driver** (envelope-style harness modulator).
- **Surge** — ballistics compressor (rate-of-change³) for groove accentuation. *Cost:* cheap. *Role:* driver.
- **Pop** — overcompressor with exaggerated attack/squish. *Cost:* cheap→moderate.
- **BeziComp** — alias-free Bezier compressor, no attack/release, density-driven (tameable → wild). *Cost:* moderate. *Use:* weird dynamics.
- **Pressure5** — full-featured 2-buss compressor (mewiness, after-boost, built-in ClipOnly2). *Cost:* moderate. *Use:* finished-ish, less "atom."

## 7. Treble / air / exciter atoms

- **Air3** — air-band EQ via Kalman extension. *Cost:* moderate. *Role:* glue.
- **Silken** — HF boost via reversed PrimeFIR ("silky" lead-vocal highs). *Cost:* moderate.
- **Energy** — fixed-frequency (integer-SR-multiple) high-Q treble slams; **untunable**, SR-locked labels. *SR:* fixed-frequency. *Cost:* cheap→moderate. *Role:* dramatic brightener.
- **Exciter** — aural exciter; subtle → blows up into distortion at the top. *Cost:* moderate.
- **GuitarConditioner** — Slew+Highpass parallel matrix, Tube-Screamer-adjacent djent voicing. *Cost:* cheap→moderate. *Role:* chain (a pre-built recombination worth studying).

---

## First 15 to port (chain/harness starter kit)

Covers the widest tonal ground with the cheapest atoms and the key containers/drivers:

1. **Density** (bipolar sat workhorse) · 2. **Spiral** (smooth curve) · 3. **Drive** (grit) · 4. **Mojo** (loud) · 5. **Slew** (treble tame) · 6. **Acceleration** (edge tame, keeps air) · 7. **Creature** (dramatic ladder) · 8. **Capacitor2** (FM filter) · 9. **Baxandall** (clean EQ) · 10. **Console0 Channel+Buss** (containment harness) · 11. **DeRez2** (crush) · 12. **ChromeOxide** (tape rot, feeds RotCoat) · 13. **Point** (transient) · 14. **Swell** (envelope driver) · 15. **Fracture** (wavefolder, dramatic).

## Notes for chain/harness builders

- **Latency-aware harnesses:** Bite (~2 smp), ToneSlant (block), and any averaging filter carry latency — keep them out of tight feedback returns, or compensate.
- **Driver atoms** (Swell, Surge, Point, Flutter, Capacitor2's FM) are the ones whose *output or envelope* can modulate an inner chain — the basis of the envelope-follower harness.
- **Container atoms** (Console family) are the keystone: build the channel→buss containment harness first; the feedback-return and morph harnesses both lean on it.
- **Anti-alias pairing:** §4 destruction atoms want a §3 lowpass before/after (or the oversampling wrapper harness) to stay usable — the "utilitarian rescues the dramatic" pattern.
- **SR-sensitive:** Energy (fixed freq), Creature (stage count scales) — pin behavior per target rate.
