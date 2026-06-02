# Airwindows → ER-301 Reverb Porting & Original-Unit Design — Agent Handoff

**Project context:** Eris (ER-301 successor; CM4/CM5-class host) and direct ER-301 (AM3358) targets.
**Purpose of this doc:** Hand off three threads to other agents — (1) reconnaissance into viable Airwindows (AW) porting targets, (2) two original/novel reverb topologies (the "XYZ" engine and "RotCoat"), and (3) AM335x-specific feasibility and recommendations.
**Status:** Design exploration. No code committed. Sonic claims are reasoned from algorithm structure and AW documentation, not yet measured on hardware.

---

## 0. Ground facts (apply to everything below)

- **Licensing:** Every Airwindows plugin is MIT-licensed, single-file C++, zero dependencies, `float`/`double` in → out in a tight per-sample `process()` loop. Port effort per plugin is low and roughly constant. The gate is **CPU/memory per instance on the target**, not porting labor. Attribution required; recombination of mechanics is the original work.
- **Precision:** AW is **double-precision end to end**, including noise-shaped dither. This matters enormously for the AM335x (see §4) and is a non-issue on CM4/CM5.
- **What reverb costs:** Reverb is **memory-bandwidth-bound** (interpolated delay-line reads), not FLOP-bound. Householder feedback mixing is O(N)/sample via the reflection trick. Dampers and saturators are cheap arithmetic. The cost center is delay-buffer traffic.
- **Cost multipliers to watch when triaging the wider catalog (~450 plugins):** oversampling (Focus, Distortion, ADClip-class), large FDN/feedback matrices (MatrixVerb, Infinity), and many interpolated delay taps. Avoid these tiers first when budget-constrained.

---

## 1. Reconnaissance — viable AW porting targets

### 1.1 User-named anchors (confirmed current)

- **kWoodRoom** (Jan 2026, filed under Reverb). Runs the modern "k" engine: a 6×6 Householder feedback matrix fed by a 3×3 early-reflection matrix, plus tail modulation. **Cost is fixed regardless of room size** (~a dozen interpolated taps + the two matrices); being a small room it has short delay buffers. Moderate, viable as a single instance even on AM335x. Best dramatic-reverb anchor in the catalog.
- **WoodenBox** (Apr 2026, filed under *Tone Color*, **not** Reverb). A miniature DI-to-acoustic reverb — dense and confined, a tone-shaper rather than a real space. Smaller/cheaper than a full k-verb. Lowest-risk port.

### 1.2 Reverbs by CPU tier

**Cheapest — allpass-based, older engine (also where the drama lives):**
- **PocketVerbs** — bundles **Zarathustra** (huge ambient swells), **Stretch** (edgy Paulstretch-like), **Spring** (dub sproing), plus a gate section. Highest drama-per-cycle.
- **MV / MV2** — a pile of allpasses inside a Console instance with regeneration; does infinite *and* gated-reverb walls without runaway. The in-Console nonlinearity is the runaway governor.

**Moderate — modern engines, budget as ~one instance on AM335x, freely stackable on CM4:**
- **k-series** (shared 6×6 Householder + 3×3 ER engine): kWoodRoom, kCyberCity, kAlienSpaceship, kStation, kGuitarHall2, kPlateA/140/240, kCathedral2–5, kBeyond, kCosmos. Bulk of the modern realistic-space catalog; fixed cost per instance.
- **Galactic / Galactic2 / Galactic3 / GalacticVibe** — lush, known-efficient FDN. The clean lush option.
- **Verbity / Verbity2** — feedforward-with-one-feedback; spans slapback → infinite tail.
- **NonlinearSpace** — dark, natural at low wet.
- **CloudCoat** — 4×4 Householder of *allpasses* with cross-modulated, unsmoothed feedback. Nonlinear gated-verb drum-destroyer; sustain is 0-or-meltdown. The most dramatic verb in the collection.
- **Chamber / Chamber2** — golden-ratio feedforward Householder. Chamber2 doubles as blur-delay/glitch-buffer (avoid the degenerate thick≈0 = 4000-sample setting).
- **ClearCoat / CreamCoat / CrunchCoat** — bright-ambience engine. **CreamCoat** = the guts run at an integer-divisor internal rate, reconstructed via a Bezier curve → lush + cheaper. **CrunchCoat** = cursed-retro digital, pitch-swoopable undersampling, infinite regen. (CreamCoat's mechanic is the seed for RotCoat — see §3.)
- **BrightAmbience3** — bright gated halo. **Use the 3 (undersampled rebuild); the original BrightAmbience is the genuinely CPU-hungry naive-prime-tap version.**

**Skip on constrained hardware:** MatrixVerb, the MatrixVerb-derived **Reverb**, Infinity/Infinity2, largest long-tail k-cathedral variants (compute is fine; buffer length + tap count stack up), original **BrightAmbience**.

### 1.3 Dramatic non-reverb / character (cheap unless noted)

- **Creature** — stacked soft slew-saturators ("slew wavefolder"); roars, monstrous bass, near-free CPU at low Depth. Top non-reverb drama-per-cycle pick.
- **Capacitor2** — sweepable/FM'd analog filter, transient-popping. Pairs great in front of a verb.
- **ZAcidLowpass / Aura / AngleFilter** — wild resonant filters.
- **BitGlitter** (sampler grunge), **DeRez2/3** (bezier bitcrush/decimate) — trivially cheap.
- **Distance2** — air-absorption "push it far away" spatial.
- **Melt** (run low-wet as a diffuser), **TapeDust** (grainy treble), **ChromeOxide** ("dial-a-mulch" lo-fi tape — seed for RotCoat's per-line stage), **Pockey / Flutter**.
- **PointyDeluxe / ChimeyGuitar2 / ChimeyDeluxe** — brutal multi-stage amp/distortion. Heavier (ChimeyDeluxe = 16 EQ+comp stages); fine as single instances, costly stacked.

### 1.4 Tape / console / saturation (the "acceptable" bucket)

- **Cheapest:** Console family — Console Zero (~8 ops/sample/channel), PurestConsole, ConsoleChannel/Bus, tone desks (ConsoleMC/MD/LA). Waveshapers: Spiral (one `sin()`), Density, Mojo, Dyno, PurestSaturation, Coils2 (transformer overdrive). BussColors4 stays efficient even at high rate.
- **Moderate:** ToTape9 / IronOxide5 (multi-stage + flutter delay), TapeHack2 (lighter "tape hack").
- **Note:** Focus and Distortion bundle several algos *but oversample* — they cost more than the individual shapers they replace; only worth it for their built-in antialiasing.

### 1.5 Stack-cost watch items (fine as single instances, eat a dense chain fast)

Console7Cascade/Crunch (5 stages + ultrasonic, two `sin()` each), ChimeyDeluxe (16 stages), PointyDeluxe, MatrixVerb, original BrightAmbience, Chamber2's thick≈0 setting.

---

## 2. Design philosophy carried from AW

The AW algorithms are *simple*: a Householder feedback matrix, a pile of allpasses, a one-pole damper. Character comes from **tuning, where the nonlinearity sits, and how the signal is reconstructed** — not algorithmic complexity. The most interesting "combine two plugins" move is not a parallel verb you crossfade to; it's a **process the first verb runs through or inside**. AW already does this: MV is allpasses *inside* PurestConsole; CreamCoat is ClearCoat *through* Bezier undersampling.

**Primitives worth lifting:**
- Householder FDN — lossless mixing core, O(N)/sample.
- Allpass bank — flat magnitude, scrambled phase → bloom/smear. Swapping delays↔allpasses in the *same* matrix is the ClearCoat→CloudCoat difference.
- On-the-fly delay tuning — golden-ratio spacing → seamless tail at any size; prime spacing → density. The ratio choice is the voice.
- Feedforward-with-one-feedback topology — spans zero-feedback slapback → infinite tail.
- One-pole damper in-loop, tuned to an air-absorption curve.

**Combination mechanics (tame → wild):**
1. **Console wrapper as feedback governor** — run the loop inside a saturate→…→desaturate Console pair (MV's trick). Distorted feedback wraps quieter → infinite regen can't run away without a limiter. Range: clean → saturated wall.
2. **Undersample/Bezier as a character axis** — run guts at an integer divisor, reconstruct with a Bezier curve. Sane divisor = lush + cheaper; extreme = cursed-retro + pitch-swoopable. Connective-tissue morph, not an A/B fade.
3. **Topology morph (feedforward↔feedback) as a macro** — interpolate the routing matrix between an ER character and a sustained-tail character; make the endpoints genuinely different voices (golden-ratio vs prime spacing).
4. **Shared matrix, two read strategies** — two algorithms reading the same delay memory through different tap/matrix patterns, blended by how each reads. Cheap (one buffer), correlated-but-distinct hybrids.
5. **Cross-modulated feedback between two engines** — two FDNs whose feedback channels modulate each other without smoothing (CloudCoat's mechanic lifted across two engines). Range: two independent verbs → one coupled, semi-chaotic system. Dramatic ceiling.

---

## 3. Novel topology excursion

Two original units are in play. Both are single-engine-core designs that get their range from *connective processing*, not bolted-on parallel verbs. Both favor **highly discontinuous, compact parameter sets**.

### 3.1 The XYZ engine

A cryptic, emergent reverb with three parameters (X, Y, Z) where **Z reframes what X and Y mean**. That reframing is the source of the emergence and also the thing that breaks simple recall — so the design intentionally keeps one Z regime fully predictable as solid ground.

**Parameters:**
- **X — Size + texture morph.** Sweeps a single coherent perceptual axis: low = small + allpass-heavy = soft diffuse bloom/halo, no distinct echoes; high = large + pure-delay = long modal ringing cavern with discrete repeats and resonant pitch in the tail. (Delay↔APF morph emphasizes smearing vs tails.)
- **Y — Saturate/desaturate + character axis, coupled on one knob.** Low = pristine; high = saturated *and* undersampled simultaneously (vintage-gear-in-the-red + bandwidth-starved at once). Saturation adds harmonics up top; undersampling aliases them away — the interaction *is* the feature. **Curve requirement:** skew the coupling so saturation leads and undersampling follows, giving the knob an arc instead of a washy mud-zone at the midpoint. A linear coupling sounds bad in the middle.
- **Z — Meta-routing.** Changes topology / reroutes discrete algorithms through each other. Three regimes:

**Z low — Nested** (saturate → FDN → Bezier → desaturate, a clean serial chain). Resting state, the only fully predictable zone. X and Y behave as described above, independently. Sounds like an excellent characterful digital verb with a lo-fi switch. **Ship this as the predictable home base.**

**Z mid — Folded** (the Y processing moves *inside* the feedback path). Saturation and undersampling now compound per pass → **tails evolve**: clean transient in, tail that darkens, thickens, and detunes as it decays ("the room rots as the sound dies"). With Y high, each pass aliases the previous pass's saturation harmonics → tail drifts **inharmonic**, a clean hit blooming into an alien bell. **This per-pass aliasing of generated harmonics is the emergent payoff; neither mechanic alone produces it. This is the signature sound — make it the default.**

**Z high — Coupled** (split into two slightly-detuned sub-FDNs cross-modulating, saturation on the cross-link). Semi-chaotic. Low decay = two networks beating → lush chorused double-room. High decay = self-oscillation; stops being a reverb → evolving pad, inharmonic clangs, two rooms fighting. Dramatic ceiling, least predictable. A texture/drone generator wearing a reverb's clothes. **In Coupled, Y is not optional — the in-loop saturation + desaturate wrap is the leash that keeps runaway musical rather than a screech.**

**Cryptic hot-spots:**
- X-low / Y-mid, any Z → warm saturated bloom, shoegaze/ambient wash. Reliably musical.
- X-high / Y-high / Folded → haunted-cistern microtonal wavering, modal tails pitch-smeared.
- Coupled / high decay → emergent instrument, not an effect.
- Folded / low-mid Y / mid X → the evolving room. Novel but usable. **Default.**

**Three things to get right:** (1) Y's curve (above). (2) Keep Nested genuinely predictable so there's somewhere to stand. (3) Stability in Coupled — self-oscillation must die into a denormal floor or stay musical via the Y leash; add denormal handling regardless.

### 3.2 RotCoat (quantized-divisor + tape-rot)

A fusion of two mechanics: **#1 quantized-divisor verb** (CreamCoat taken all the way) + **#5 tape-rot per delay line** (ChromeOxide mechanic). The two interact at the reconstruction boundary in a way neither does alone — this is the richest emergent corner explored.

**Macro topology:** The entire reverb core runs inside a **reduced-rate domain** clocked by the World knob. Signal path: `in → Predelay → [reduced-rate domain: Decimate (host→reduced) → Householder FDN core (with Regen feedback) → Bezier Reconstruct (reduced→host)] → Output (wet/dry)`. A dry path bypasses the domain. The Bezier reconstruction is the boundary that interpolates back to host rate — it serves double duty as **both character and cost reduction** (the CreamCoat insight).

**Per-delay-line tape-rot (inside each FDN line — where #5 lives):** each line tap is **band-split (LP/HP)**; the **low band** gets head-bump saturation; the **high band** gets noise-FM warble + a bias delay (depth = Mulch); both **recombine into the Householder matrix**. A **band-recirc flip** selects which band the matrix feedback carries forward.

**Parameters:**
- **World** — stepped internal-rate divisor (÷1, ÷2, ÷3, ÷4, ÷6, ÷8). The discontinuous headline knob. Not a size sweep — a switch between quantized worlds, each a distinct clean room with its own characteristic Bezier cutoff/ceiling.
- **Regen** — feedback / decay.
- **Predelay** — pre-delay.
- **Mulch** — tape-rot depth (high-band noise-FM depth + bias).
- **Lock / Drift** — meta switch. Lock = hold a world (rack of pristine quantized rooms). Drift = slew the divisor between worlds → CrunchCoat-style pitch-swoop glitch instrument.
- **band-recirc flip** — lows-feedback vs highs-feedback (tail personality, below).

**Emergent payoff (the boundary interaction):** At low World divisors the Bezier reconstruction is already drawing coarse, near-straight segments between sparse points; the high band's noise-FM warble modulates a delay read *against that coarse grid*, so reconstruction quantization and warble swim **intermodulate**. Each divisor rots with a different flavor — ÷2 = subtle tape haze; ÷6–8 = full cursed-cassette wow where warble and reconstruction stairsteps beat against each other.

**Tail personality via band-recirc flip:** recirculate the lows → saturated low-mid bloom that thickens as it decays; recirculate the highs → swimming, detuning top band persists → tail wanders pitchward.

**Why it's cheap:** *cheaper than the FDN alone at host rate* — the reduced-rate domain cuts per-sample loop cost AND buffer length by the divisor. Warble is just a modulated buffer read (no extra filtering); band-split crossover is pure FLOPs.

**Watch items:** (1) **Clamp warble depth + bias** so the modulated high-band read can never go negative (indexing behind the write pointer → click). (2) **Denormal floor** in the feedback path — low divisors mean fewer samples flush the lines between hits, so denormals accumulate faster than at host rate.

---

## 4. AM335x (ER-301 / AM3358) specific recommendations

Target: single Cortex-A8 ~1GHz + NEON, 48k / 128 samples, 256KB L2, single-channel DDR3. The Pi-4 framing ("how many can I stack") **inverts** here to "does one fit, and in what mode."

### 4.1 Two architecture realities that dominate

**(a) Doubles are a flat tax — float+NEON is mandatory.**
AW is double-precision throughout, including dither. **ARMv7 NEON is single-precision SIMD only**; double-precision SIMD doesn't exist until AArch64. On the A8, every double op falls back to scalar VFPv3 (effectively non-pipelined, slow). **Step zero for any AW port to the 301 is converting the algorithm to `float` and running NEON.** This is **not sonically free**: dither/noise-floor behavior changes, and float precision in a long regenerating feedback loop is exactly where denormals and slow drift bite. **Validate the float port by ear against the double reference before trusting it.** Applies to every design here equally.

**(b) Memory bandwidth is the ceiling, not FLOPs.**
Reverb here is bound by interpolated delay reads. Six lines × a few seconds of tail blows past 256KB L2 into single-channel DDR3 (far less bandwidth than Pi-4 LPDDR4). Once in float+NEON, Householder mixing and dampers are nearly free; **cache misses on the delay reads are the cost.** Budget units by memory traffic, not op count.

### 4.2 Design-by-design verdict

**RotCoat — the AM335x-friendly design (structurally, not incidentally).**
The reduced-rate domain (÷World) cuts buffer length *and* read rate by the divisor simultaneously — at ÷4 you hold a quarter the samples for the same decay and touch them a quarter as often, attacking the exact memory bottleneck. **÷2 and below is comfortable; ÷1 (unity) is the tight corner** at full freight. Tape-rot cuts slightly the other way (warble read + bias delay = more taps/line), but paid at the reduced rate; band-split crossover is cheap FLOPs. **Net: fits, and World is the headroom knob. The honest constraint: ÷1 is a budget decision, not a free sound.**

**XYZ engine — mode-dependent.**
- **Nested:** fits.
- **Folded:** fits, and is *helped* by its own undersampling (same lever as RotCoat's World).
- **Coupled:** the problem child. Two cross-modulating sub-FDNs ≈ double the core *and* double the memory traffic — **not expected to fit at host rate on the A8.** Viable only if Coupled *also* runs in a reduced-rate domain (the cross-mod is per-sample math, rate-indifferent, so this is consistent with the design). **Decision: on the 301, Coupled must inherit the reduced-rate trick or it's the mode that pushes over budget.**

### 4.3 Port checklist for the AM335x

1. Convert target algorithm `double → float`; enable NEON. Treat as a **listening decision**, not just a recompile — A/B against the double reference, listen for dither/noise-floor change and feedback-loop drift.
2. Build the **float FDN core first** and **measure cycles/sample on real hardware at ÷1 and ÷4** before adding the tape-rot stage. (All sonic/cost claims here are reasoned from architecture, not a measured 301 profile.)
3. Budget by **memory traffic** (delay-buffer reads), not FLOPs.
4. Use **reduced internal rate as the primary headroom lever** (World / Folded undersampling / Coupled reduced-rate).
5. **Clamp warble + bias indexing** — a click is the same bug as on Pi 4, with less headroom to mask it.
6. **Denormal floor** everywhere in feedback paths — low divisors accumulate denormals faster.

### 4.4 Safe single-instance AW reverb picks for the 301 (post-float-port)

kWoodRoom, WoodenBox, Galactic-family, PocketVerbs, MV/MV2, BrightAmbience3, CreamCoat (its divisor structure ports the RotCoat lesson directly). Avoid on this chip: MatrixVerb, Infinity-family, original BrightAmbience, long-tail k-cathedrals.

---

## 5. Open items / next actions for receiving agents

- Pull `process()` source for the shared k-engine and for Creature/CloudCoat → real op-counts + delay-buffer sizing.
- `process()` skeleton for the RotCoat per-line tape-rot stage (band-split → head-bump / warble+bias → recombine).
- Parameter map for RotCoat: knob list, ranges, tapers, stepped vs continuous.
- Float-conversion reference: exactly what changes in an AW algorithm and what to listen for.
- Cycle-budget arithmetic worked per stage (decimate, FDN line, Bezier reconstruct, tape-rot) at ÷1 and ÷4.
- Confirm Coupled-mode reduced-rate path for XYZ on AM335x.
