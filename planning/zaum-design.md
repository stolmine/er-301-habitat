# Zaum — woven reverb north star

Status: design exploration. No code. No other files changed.
Phased build: `planning/zaum-roadmap.md`.

---

## Naming

The unit names are drawn from Russian Formalist narratology — the scheme is
the architecture made explicit.

- **Fabula** (фабула) — Phase 1, the standalone believable room. The raw
  chronological substrate: what actually happened, in order. The modulated
  APF-tank room is the ground truth that must hold up on its own.
- **Sujet** (сюжет, spelled here as "Sujet") — Phase 2, the standalone
  spectral fiction engine. The artful *telling*: the same material re-ordered
  and re-presented through frequency-domain manipulation. Fiction built on top
  of believable events.
- **Zaum** (заумь) — the flagship woven combination unit (Phases 3–5) and the
  package name. Khlebnikov/Kruchyonykh's "transrational language" — beyond
  sense, beyond the substrate-fiction split. Zaum fuses Fabula (believable)
  and Sujet (fiction) via the procedural field into something that no physical
  room and no single spectral trick can produce alone.

The names are not decorative. Fabula is the believable basis; Sujet is the
fictional twist; Zaum is what emerges when they are woven together. The
Formalist family leaves room for sibling names should the need arise.

---

## Thesis / design intent

The project's stated reverb move is *a process the first verb runs
through or inside*. MV is allpasses inside PurestConsole. CreamCoat
is ClearCoat through Bezier undersampling. Zaum takes that move up
one level: the fictional augmentation engine does not run alongside
the room — it runs **inside** the room's recirculating topology,
accessed through a procedural selector that picks which elements
are live targets.

**Three assertions:**

1. **Believable basis.** The substrate must work as a credible
   acoustic space on its own — a modulated allpass tank that sounds
   like a room, not a patch. The fiction only lands if the ground
   is solid.

2. **Fictional twist.** The aux engine applies operations that no
   physical room can do: frequency bins that refuse to decay,
   harmonic ghosts that double selected partials an octave up,
   selective freeze locking a drone into the room's eigentones
   while transients pass through clean.

3. **Woven, not cascaded.** The coupling is not a send/return.
   The procedural field selects individual *elements* (allpass
   outputs, delay taps, spectral bins, grain buffers) from a
   heterogeneous pool and routes them bidirectionally. The result
   is a room whose acoustic physics are locally rewritten, not
   layered over.

The unifying mechanic is the **selector field** — a procedural
golden-angle/phyllotaxis distribution (generalized from Network's
field generator) that defines which elements are "portals" at any
moment. The field drifts. The augmentation topology migrates. The
room breathes differently over time.

Network's real innovation was never its taps. It was the field: a
phyllotaxis selector deciding which elements are live/connected at
each moment. Zaum generalizes that selector from a homogeneous tap
pool to a **heterogeneous element pool** — tank nodes, spectral
bins, grains — each assigned a role by the field.

---

## Three-layer architecture

```
         ┌─────────────────────────────────────────────────────────────┐
         │                       SUBSTRATE (per-sample, always-on)     │
         │                                                             │
 IN ─────┤  Predelay → 4-AP input diffusion                          │
         │                  ↓                                          │
         │           ┌──────────────────────┐                         │
         │           │  figure-8 APF tank   │                         │
         │           │  L-loop: AP×2 + dly  │◄──── one-pole HF damp  │
         │           │  R-loop: AP×2 + dly  │      Gardner nest APs   │
         │           │  cross-feedback ×2   │      chaotic LFO mod    │
         │           └──────┬───────────────┘                         │
         │                  │ [probe points: AP outs, dly taps,       │
         │                  │  cross-fb intercepts]                    │
         └──────────────────┼──────────────────────────────────────────┘
                            │
         ┌──────────────────▼──────────────────────────────────────────┐
         │                        WEAVE (field, per-block)             │
         │                                                             │
         │  golden-angle field → portal set                           │
         │  each portal: SUBSTRATE NODE ↔ SPECTRAL BIN ↔ EMITTER     │
         │  Motion param drifts field angle → topology migrates       │
         │  Density/Connectivity controls how many portals are live   │
         │                                                             │
         └──────────────────┬──────────────────────────────────────────┘
                            │ per-hop spectral injection (parallel,
                            │ smooth crossfade into tank nodes)
         ┌──────────────────▼──────────────────────────────────────────┐
         │                       AUX (per-hop, sparse, fictional)      │
         │                                                             │
         │  STFT engine (1024-pt, 50% OLA, NEON pffft-class FFT)     │
         │  selected bins only (from WEAVE portal map)                │
         │  ops: ∞-RT60 partials / octave ghost / freeze /           │
         │       noise-skirt fatten / spectral blur                   │
         │  Bind ─── Portals mode: inject → wet sum only              │
         │       └── Tunnel mode: inject → re-enter tank loop         │
         └─────────────────────────────────────────────────────────────┘
                            │
                           OUT (wet/dry mix)
```

### SUBSTRATE — modulated APF tank

Dattorro figure-8 topology extended with Gardner nested allpasses
and decorrelated chaotic/Brownian delay-line modulation.

**Input diffusion:** 4 series allpasses, delays 142, 107, 379, 277
samples (at 48 kHz). Coefficients 0.75 fixed. Scrambles transients
before recirculation begins.

**Tank loops (L and R, cross-coupled):**

```
loop_L:
  AP (delay 672, coeff 0.70) → Gardner nested AP (delay 1800/908)
  → delay line D1 (prime-length, 4453 smp, mod ±42 smp)
  → one-pole HF damp (coeff 0.0005..0.9 via Tone param)
  → AP (delay 908, coeff 0.62) → Gardner nested AP (delay 2656/1344)
  → delay line D2 (prime-length, 3163 smp, mod ±38 smp)
  → cross-feed → loop_R
```

Delay lengths are mutually prime to suppress comb coloration — the
exact failure that sank Network's multitap (equal-spaced taps
produce flutter at any shared divisor). Length pool (smp @ 48 kHz):
3163, 3931, 4453, 5023, 6271, 7841, 8819, 10193. Pick eight primes
from this pool per Size setting; Size sweeps the pool selection, not
a linear scale.

**Modulation:** Each delay line driven by a separate LFO, frequency
0.1–2.0 Hz, excursion ±0.2–1.5 ms (9–72 smp at 48 kHz). LFO
waveforms are chaotic/Brownian (integrated random walk, bounded).
Multiple decorrelated LFOs — the Valhalla discipline for breaking
metallic eigentones. No two LFOs share phase or frequency.

**Gardner nested allpasses:** Each primary AP contains a shorter AP
in its inner loop (the "nest"). Nesting multiplies effective
allpass order without proportional CPU cost. Inner delays: ~1/3 the
outer. Coefficients independently tunable; default 0.5 inner.

**Optional dual-scale routing:** A toggle splits input at a
crossover (~800 Hz) — highs route to the smaller/brighter tank
(shorter delay lengths, faster HF damp), lows to the larger/warmer
tank. The two tanks cross-feed at their output. Cheap frequency-
dependent room character without a second full algorithm.

**Probe points** (what the WEAVE field can address):

| Label | Source | Notes |
|---|---|---|
| `ap_L1_out` | After first L-loop AP | Pre-Gardner |
| `ap_L2_out` | After second L-loop AP | Post-Gardner |
| `dly_L1_tap` | D1 read head | Unmodulated or modulated |
| `dly_L2_tap` | D2 read head | |
| `xfb_L→R` | Cross-feed inject point | Circular if fed back |
| `ap_R1_out` | Symmetric R-loop | |
| `ap_R2_out` | | |
| `dly_R1_tap` | | |
| `dly_R2_tap` | | |
| `xfb_R→L` | | |

Ten probe points. The field selects a subset.

### WEAVE — procedural selector field

A golden-angle/phyllotaxis distribution (θ = 137.508°·k, radial
r = √k, for k = 0..N-1 where N = pool size) defines portal
positions in a 2D field. The field is parameterized by:

- **Motion** (0..1): angular drift rate of the field per block.
  At 0 the field is frozen; at 1 it rotates ~2° per block at
  48 kHz / 64-sample blocks, completing a full revolution every
  ~2.8 s. Slow drift gives gradual topology migration; fast drift
  gives shimmer instability.
- **Density** (0..1): threshold on the portal map — fraction of
  pool elements that are live at any time. At 0.1, ~10% of
  possible portals are active (sparse, clean augmentation). At
  0.9, nearly all are (saturated, maximum coupling).
- **Connectivity** (0..1): within active portals, what fraction
  are routed into the tank loop (Tunnel) vs wet-sum only
  (Portals). Acts as a bias on the Bind parameter at per-portal
  granularity.

Each portal is a 3-tuple binding:

```
(SUBSTRATE_NODE, SPECTRAL_BAND, EMITTER)
```

Where:
- `SUBSTRATE_NODE` is a probe point (above).
- `SPECTRAL_BAND` is a frequency range and associated bin group
  in the AUX STFT engine (e.g. bins 32–64, corresponding to
  ~1.5–3 kHz at 48 kHz / 1024-pt FFT, bin width 46.9 Hz).
- `EMITTER` is the output routing: either wet-sum injection or
  a re-entry point back into the tank.

The field generator is generalized from Network's golden-angle
field. Network's generator decides which taps are live; Zaum's
generalized version assigns heterogeneous types to portals as it
walks the phyllotaxis sequence — a tank-node portal, then a
spectral-bin portal, then a grain portal — with the type cycling
or stochastically assigned based on the pool configuration.

### AUX — STFT spectral engine

Per-hop (not per-sample). Runs on the signal extracted from the
SUBSTRATE probe points bound by active portals.

**FFT configuration:**

| Parameter | Value |
|---|---|
| Frame size N | 1024 smp (21.3 ms at 48 kHz) |
| Hop size H | 512 smp (50% OLA, Hann window) |
| Bin width | 46.875 Hz |
| Latency | 1024 smp = 21.3 ms (presented as predelay) |
| FFT engine | pffft-class NEON-accelerated real FFT |

Per-bin operations applied **only to selected bins** (those within
the SPECTRAL_BAND of active portals):

**∞-RT60 / very-long partial:** Per-bin magnitude decay coefficient
set to 1.0 (no decay) or near-1.0. Normal bins follow:

```
A_k[n+1] = A_k[n] · 10^(-3 · R / (RT60_k · Fs))
```

where `RT60_k` is the target decay for bin k. Selected bins have
`RT60_k → ∞`, so A_k[n+1] = A_k[n]. Partial refuses to die.

**Octave pitch-shift (harmonic ghost):** Bin k mapped to bin 2k
(or k/2 for sub-octave). Magnitude transferred; phase adjusted
by the pitch-shift factor. Ghost partial appears an octave above
chosen frequency regions while original decays normally.

**Freeze:** A_k held at a snapshot taken at a trigger event (CV
gate or Bind parameter crossing a threshold). The frozen bins hold
a spectral drone; transients bypass via the SUBSTRATE dry path.

**Noise-skirt fattening (Sumu-style narrowband):** A small noise
envelope is added to the magnitude of selected bins:
`A_k[n] += ε · rand_gaussian(0,1) · A_k[n]`, where ε ~ 0.05–0.20.
Fattens partials without pitch shifting; adds the lushness of
Loris-style narrowband noise. Effective on dense chords.

**Spectral blur/bloom:** A_k[n] convolved with a 3-bin Gaussian
kernel across frequency. Adjacent bins pick up energy from active
bins. Widens spectral events; soft halo around selected partials.

---

## The weave mechanics (the crux)

The coupling is a **parallel sidechain** — the spectral engine
processes signal derived from SUBSTRATE probe points and injects
back. This is not sample-locked; the "weave" is defined by the
shared field plus bidirectional coupling at block boundaries.

The headline axis is **Bind depth**:

### Portals regime (woven-to-output, stable)

Spectral injection flows from AUX → wet sum only. The tank's
feedback loop never sees the spectrally augmented signal. The room
decays physically; the ghosts and frozen partials float over it.

```
probe_out → [STFT] → ops → IFFT → crossfade → wet_sum
```

Freeze is unconditionally safe here: the infinite-RT60 bins
inject into the sum and do not feed back. Result is a
clean room with fictional halos. Maximum usability, minimum risk.

### Tunnel regime (woven-into-loop, wild)

AUX output re-enters the tank's feedback path at the bound
EMITTER point. The room's physics are rewritten: each pass of
the recirculating signal tunnels through the frequency domain
before re-entering. Selected spectral bins are operating on
energy that keeps coming back.

```
probe_out → [STFT] → ops → IFFT → crossfade → tank_inject_point
                                                    ↑
                                              re-enters loop
```

STFT window latency (21.3 ms per hop) becomes **part of the loop
time**. The effective loop delay is tank delay + STFT hop. This
extends the apparent room size by the hop period.

**Stability governing:** Infinite-RT60 bins or high-∞ gain ops in
the loop path will self-excite. The governing leash is the project's
**Console saturate → desaturate wrapper** (mechanic #1 from
`planning/reverb-design-philosophy.md`):

```
tank_inject → Console saturate → [loop body] → Console desaturate
```

Distorted feedback wraps quieter; runaway becomes a saturated wall
rather than digital explosion. The same mechanic that makes MV's
infinite regen musical applies here at the spectral injection point.
Without this governor, Tunnel mode is the CloudSeed trap —
cross-coupled feedback without smoothing that turns DC divergent.

**Crossfade smoothing:** The injection crossfade weight is
per-portal, ramp-smoothed over ~5 ms at parameter changes.
Abrupt portal-weight changes produce clicks at the injection point;
the ramp is mandatory.

---

## The heterogeneous pool (the full vision)

The field selects over a mixed pool of element types:

```
POOL = { tank-node elements } ∪ { spectral-bin elements } ∪ { grain elements }
```

Each element is assigned a role by the field:

| Element type | Role options | Routing |
|---|---|---|
| `tank-node` | believable / shimmer | probe → ops → re-inject or wet-sum |
| `spectral-bin` | freeze / noise-skirt / ∞-RT60 | AUX STFT engine |
| `grain` | grain-scatter / bloom | grain buffer + stochastic scheduler |

**Grain element:** A circular buffer (64–512 ms) with a stochastic
grain scheduler. Grains: 20–200 ms duration, randomized start
position within the buffer, amplitude envelope (Hann), pitch ratio
1.0 ± σ (σ = 0 for clean scatter, > 0 for pitch smear). The grain
element reads from the SUBSTRATE probe and emits into the wet sum
or the tank. At low density it adds subtle pre-echo scatter; at high
density it produces a cloud of copies.

The field assigns each portal a type from the pool based on the
phyllotaxis position and the pool configuration (how many of each
type are available). As Motion drifts the field, portals shift type
over time — a tank-node portal at one moment may become a grain
portal after a field rotation, organically blending the augmentation
character.

This is the literal realization of "select individual units/bins/
grains/tank nodes procedurally for augmentation by the auxiliary
engine." The selector is the grammar; the pool is the vocabulary.

---

## Reusable primitives / atoms

Per `planning/house-atom-architecture.md`, Zaum factors into atoms
that other units can reuse.

| Atom | Status | Notes |
|---|---|---|
| `APFTankCore` | New — extends existing allpass banks | Dattorro figure-8 with Gardner nesting; probe-point outlets wired to Lua |
| `STFTSpectralEngine` | New — primary new cost | NEON pffft-class real FFT; per-bin op dispatch; frame/hop ring buffer |
| `ProceduralField` | Generalize Network's generator | Phyllotaxis golden-angle walk; portal type assignment; Motion drift |
| `WeaveCoupler` | New harness | Manages portal-weight crossfades; routes probe↔STFT↔inject; Bind regime switch |
| `GranularElement` | New | Circular buffer + stochastic scheduler; grain envelope + pitch ratio |
| `ConsolePair` | Existing | `Console0Channel` + `Console0Buss` atoms already in `mods/house/atoms/`; reused as the Tunnel governor |
| `BezierReconstruct` | Existing | Used in kWoodRoom outer loop; available if dual-scale tanks want reduced-rate domains |
| `OnePoleDamper` | Existing | Simple one-pole LP in every feedback path; already in AW atom inventory |
| `HouseholderFDN` | Existing | kWoodRoom's 6×6 trellis; Zaum uses allpass tank not Householder, but FDN available for optional modes |
| `GoldenAngleField` | Extend from Network | Network ships a golden-angle field generator; lift + generalize for heterogeneous pool |

**Already in the codebase (confirmed or implied by existing units):**
- Allpass banks (kWoodRoom input diffusion chain)
- Householder FDN core (kWoodRoom 6×6 trellis)
- One-pole dampers (inner Bezier loop)
- Console0Channel / Console0Buss wrapper (AW atom inventory)
- Bezier undersampling reconstruct (CreamCoat / kWoodRoom outer loop)
- Network's golden-angle field generator (Network unit, shipped v2.5.0)

**Net new atoms for Zaum:**
- NEON STFT via pffft-class FFT (`STFTSpectralEngine`) — the primary
  implementation investment
- Gardner nested allpass extension of the APF bank
- WeaveCoupler harness (manages the binding logic, crossfades, Bind
  regime switch)
- GranularElement (grain buffer + scheduler)

All new atoms follow the header-only `od::Object` subclass pattern
per house-atom-architecture. The WeaveCoupler is a C++ harness
(not Lua-wired) because it needs shared state across the probe
extraction and injection points — exactly the harness trigger
condition in the architecture doc.

---

## Parameter surface sketch

| Control | Range / type | Function |
|---|---|---|
| **Size** | 0..1 (selects prime delay pool) | Room dimension; sweeps delay length pool, not linear scale |
| **Decay** | 0..1 → RT60 0.3–120 s | Feedback / reverb time; controls `RT60_k` for normal bins |
| **Tone** | 0..1 → damp coeff | One-pole HF damping in loop; lower = darker room |
| **Diffusion** | 0..1 → AP coeffs 0.5–0.85 | Input + tank AP coefficients; higher = denser, less transient-clear |
| **Motion** | 0..1 → field drift rate | Phyllotaxis field angular drift; 0 = frozen topology, 1 = rapid migration |
| **Density** | 0..1 → portal threshold | Fraction of pool elements augmented; 0 = dry room, 1 = fully woven |
| **Bind** | 0..1 (Portals ↔ Tunnel) | Coupling depth; 0 = inject into wet-sum only, 1 = inject into tank loop; Console governor active in Tunnel half |
| **Fiction** | Menu + depth | Which AUX ops are active (∞-RT60 / ghost / freeze / noise-skirt / blur) + overall op intensity |
| **Mix** | 0..1 | Wet/dry blend |
| **Predelay** | 0..340 ms | Pre-delay before input diffusion; absorbs STFT hop latency optionally |

Ten controls. Bind and Fiction are the headline exotic pair; Size /
Decay / Tone / Diffusion are the standard room controls any player
will reach for first.

Fiction is a single control with a sub-menu per the spreadsheet
package pattern (similar to Network's G1–G8 macro). At Density = 0
and Bind = 0, Zaum is a clean Dattorro allpass tank — the substrate
alone. Fiction and Density together are the gradual revelation of
the fictional layer.

---

## CPU / latency reality

**Target:** CM4 / Cortex-A72 @ 1.5 GHz. No longer AM335x-
constrained. This is generous headroom compared to RotCoat's
design (which had to engineer for AM335x by construction).

**Per-layer cost estimates (stereo, 48 kHz, 64-sample blocks):**

| Layer | Cost estimate | Notes |
|---|---|---|
| SUBSTRATE (APF tank) | ~0.8–1.2% | 10 allpasses + 4 delay lines + LFO modulation per sample |
| WEAVE (field eval) | < 0.1% | Phyllotaxis walk is arithmetic; portal map is a lookup; runs per block |
| AUX (STFT, all bins) | ~0.26% stereo | pffft benchmarks: 1024-pt real FFT ~95 µs on Cortex-A53; A72 faster; 50% OLA = 2× FFT/block |
| AUX (per-bin ops) | ~0.05% at typical density | Sparse; only selected bins processed; magnitude/phase arithmetic |
| WeaveCoupler (crossfades, routing) | ~0.1% | Per-portal smooth crossfade; injection routing |
| GranularElement (at moderate density) | ~0.3% | Read + interpolate + envelope; cost scales with active grain count |
| **Total estimate** | **~1.5–2.0%** | Conservative; profile on hardware to confirm |

The real engineering cost is not compute — it's the **coupling
architecture**: managing the per-hop injection timing, the Console
governor in the Tunnel path, and keeping the crossfade logic clean
enough that rapid Motion or Density sweeps don't produce artifacts.

**STFT latency:** 21.3 ms at 1024/512. This is presented as predelay
and absorbed into the Predelay parameter display. At Bind = 0
(Portals), latency is irrelevant — it's a parallel injection. At
Bind = 1 (Tunnel), the hop period is part of the effective loop
time and changes the apparent room size. This is a feature, not a
defect: Tunnel mode at large hop periods sounds physically impossible.

Larger FFT (2048 smp = 42.7 ms) is available if finer bin resolution
is needed for the ghost/freeze ops. Tradeoff: doubled latency and
2× FFT cost, better frequency selectivity (23.4 Hz bins). Start
with 1024; move to 2048 only if freeze sounds smeared.

---

## Honest open questions / risks

1. **Coupling smoothness (per-hop vs per-sample):** The STFT
   produces output at hop boundaries (every 512 smp). Injecting
   that into a per-sample tank means the injection is
   block-rate-quantized. At 64-sample audio blocks this is already
   handled (STFT hops span 8 audio blocks). The crossfade ramp
   across the 512-smp hop needs to be verified as inaudible at
   high injection levels and rapid field motion.

2. **In-loop stability / runaway:** Tunnel regime with ∞-RT60 bins
   re-entering the tank is the design's most dangerous path.
   The Console governor is the plan; whether it's sufficient for
   all Fiction op combinations needs empirical testing. The
   CloudSeed incident (`planning/refs/airwindows-port-handoff.md`)
   is the cautionary shape here.

3. **Transient smearing through the spectral path:** STFT inherently
   smears transients within the window. Input signals with sharp
   attacks (drums, plucks) will have those attacks replicated with
   21-ms smear in any spectral bin the field activates. In Portals
   mode this is stylistically fine (the dry tank carries the
   attack). In Tunnel mode the smeared spectral energy re-enters
   the loop and the transient becomes a sustained smear artifact.
   Mitigation: transient detection on the probe signal, suppressing
   injection at fast-attack moments.

4. **Keeping it believable:** The risk of maximalist augmentation
   is that the fictional layer swamps the substrate and the result
   sounds like a broken effect. Design discipline: at any Density
   and Bind setting the substrate must be audible and convincing
   on its own. Test by bypass-sweeping Fiction depth to zero;
   the room should still sound like a room.

5. **How the field maps geometry → portals concretely:** The
   phyllotaxis field lives in a 2D plane; the probe points and
   spectral bins live in different spaces. The mapping needs to be
   defined: does each probe point get a fixed 2D coordinate, and
   nearby field points get assigned to it? Or does the field walk
   the pool sequentially and assign types as it goes? This is an
   implementation decision with perceptual consequences for how
   Motion sounds.

6. **Voice / stereo strategy:** The SUBSTRATE runs stereo (L and R
   tanks cross-coupled). The STFT can run mid/side or L/R
   independently. If the field assigns portals independently per
   channel, stereo augmentation is decorrelated (wide, interesting).
   If the field is mono (same portals L and R), augmentation is
   centered. The Stereo/Mono field-coupling axis may be worth
   exposing as a parameter or fixed design decision.

7. **Grain element scheduling on the audio thread:** Stochastic
   grain scheduling requires random number generation on the audio
   thread. Use a fast XorShift PRNG seeded at construction; never
   call libc `rand()` from `process()`. Standard discipline but
   worth flagging explicitly.

8. **SWIG surface growth:** Adding STFTSpectralEngine, WeaveCoupler,
   APFTankCore, and GranularElement adds four non-trivial atoms to
   `house.cpp.swig`. Per house-atom-architecture, past ~30–40 atoms
   the SWIG file needs grouping into category headers. Plan the
   category structure before implementing.

---

## Status

Design exploration. No code. No implementation.

First shippable steps — the Dattorro allpass tank substrate as a
standalone unit, the field generator lifted from Network, the basic
Portals coupling — are decomposed in `planning/zaum-roadmap.md`.

Zaum is the eventual ideal. Each roadmap phase is a usable unit in
its own right. The north star is the full three-layer woven reverb
with a heterogeneous pool and a bidirectional coupling field.
Phases land incrementally; the architecture doc
(`planning/house-atom-architecture.md`) is the connective tissue
that lets each atom be smoketest-gated on hardware before being
wired into the chain.

The combination mechanic it realizes is a generalization of all five
mechanics in `planning/reverb-design-philosophy.md`: the Console
governor (#1) is the Tunnel leash, the spectral engine is a
fictional undersample/reconstruct domain (#2 extended), the field
itself is a topology morph (#3) that drifts continuously, the probe
points share tank buffer state with the STFT read path (#4), and
the in-loop Tunnel injection is cross-modulated feedback (#5) with
the Console as its governor. Zaum is not one mechanic. It is the
combination surface made explicit.
