# Zaum — phased roadmap

Status: **planning**. Phases 1 and 2 are independent; both can begin immediately. Architecture detail: `planning/zaum-design.md`.

The governing principle: every phase ships a standalone unit with real user value AND yields one reusable primitive that the north-star Zaum needs. No throwaway work. No big-bang integration.

---

## Lessons folded in from network-cascade-postmortem

`planning/network-cascade-postmortem.md` documents an abandoned FDN rebuild that produced ~20 commits of circular fixes before being reverted. The structural failures:

- **Don't retrofit advanced topology onto a working primitive.** Start from a known-good DSP reference (Dattorro, Jot 1991, Gardner), then layer character on top.
- **Test T60 numerically, not by ear.** Build a per-phase impulse-response check rig. Catches per-sample-vs-per-round-trip math errors that audition alone misses.
- **Multi-tap and FDN feedback are different signals.** Early reflections (multi-tap, no feedback) and late tail (single-tap-per-line FDN) must be separate. Conflating them corrupts the feedback eigenvalues.
- **Don't tie matrix dimension to user-facing knobs.** The density×Hadamard-normalization entanglement produced perceptual sign inversions (more density = less lushness). Fix the structure, expose orthogonal controls.
- **Stable architectural reset beats incremental patching.** When commits start reverting changes from two commits ago, stop and return to a known reference.
- **Ship incrementally; verify each primitive standalone before integrating.** Each phase below ends at a hardware audition gate before the next begins.

These failures directly motivate the fixed-first-then-dynamic sequencing across Phases 3 → 4 and the standalone validation requirement for Phases 1 and 2 before any coupling work begins.

---

## Dependency graph

```
Phase 1 (APF-tank atom)          Phase 2 (STFT-spectral atom)
   [Dattorro/Gardner room]           [spectral magnitude-decay]
   standalone: TBD name              standalone: TBD name
          |                                   |
          +---------------+-------------------+
                          |
                    Phase 3 (weave/coupling atom)
                    [Portals v1 — fixed coupling]
                          |
                    Phase 4 (procedural-field atom)
                    [Portals v2 — golden-angle field + Tunnel]
                          |
                    Phase 5 (granular atom + unified selector)
                    [Zaum — heterogeneous pool, north star]

Atoms yielded per phase:
  1 → APF-tank atom
  2 → STFT-spectral atom + NEON-FFT infrastructure
  3 → weave/coupling atom + basic governor
  4 → procedural-field atom (generalized from Network) + in-loop governor
  5 → granular atom + heterogeneous field selector
```

Phases 1 and 2 are independent and can proceed in parallel. Phase 3 depends on both. Phases 4 and 5 are sequential after 3.

---

## Phase 1 — Fabula (believable room)

**Goal.** Ship the smooth, Valhalla-class algorithmic room the package currently lacks. Network reads as a multitap comb. The shipped Airwindows reverbs (Verbity, Galactic, WoodenBox, RotCoat) have character but none lands in the smooth long-decay niche. This fills it.

**Standalone unit shipped.** **Fabula** — a direct-use lush APF-tank room reverb. The believable substrate: what the room actually does, faithfully rendered. Valuable independently — this unit could be the only thing Phase 1 ever produces and it would be worth shipping.

**North-star primitive built.** APF-tank atom (`mods/house/atoms/APFTank.h`). Encapsulates the Dattorro/Gardner allpass-tank as an `od::Object` subclass per the atom architecture in `planning/house-atom-architecture.md`. Zaum's tank substrate reuses this atom verbatim.

**DSP specifics.**

- Dattorro figure-8 topology: 4-allpass input diffuser → recirculating tank of two allpass chains + delay lines per side.
- Gardner nested allpass variant as alternative for denser early diffusion — choose by ear at hardware audition.
- All delay-line lengths mutually prime to prevent modal clustering.
- Tank delay modulation: decorrelated chaotic/Brownian LFOs per line, not synchronized sines. This is the primary lushness lever — depth and rate tunable. Per-line modulation depth kept below 2 samples to avoid audible pitch wobble at low-decay settings.
- One-pole HF damping in the feedback path (Schroeder/Jot form: per-line LP filter coefficient calibrated to a user-facing Damp parameter).
- Optional dual-scale frequency-routed tanks (low band → shorter delays, high band → longer delays, summed at output) — attempt only if single-tank version lacks air; not required for v1.
- Feedback governor: Spiral saturator per line (existing `mods/house/atoms/Spiral.h`) bounds magnitudes before feedback sum. Prevents clip-then-kill at high Regen. Directly reuses the RotCoat feedback governor pattern.
- No reduced-rate domain — tank runs at host rate. RotCoat's Bezier undersample shell is a character device; a clean room reverb needs full-rate modulation resolution.

**Parameter surface (target five continuous CV-controllable params).**

- Size (scales all delay lengths; mutually-prime relationships preserved)
- Regen (feedback amount, shaped to stay stable)
- Damp (one-pole LP cutoff in feedback path)
- Mod (Brownian modulation depth across all tank lines)
- Wetness

**Reuse from existing atoms.** Allpass helper pattern from Verbity/WoodenBox. One-pole LP from RotCoat's feedback LP. Spiral saturator from `Spiral.h`. Predelay ring buffer from CreamCoat/RotCoat.

**Dependencies.** None. Self-contained. Can begin immediately.

**CPU projection.** Full-rate Dattorro: ~8–12 allpasses + ~4–6 delay reads per sample per side. Estimate 8–14% stereo at 48k. Lighter than Galactic at its heaviest World setting.

**Risks.** Low. Dattorro topology is well-documented (JAES 1997). Gardner nested allpasses are simpler. Main risk is modulation calibration — Brownian depth too high produces pitch wobble, too low sounds like Verbity. Hardware audition gates this.

**Definition of done.**
- Builds both arches (linux + am335x) clean.
- Hardware audition: smooth, dense, non-resonant tail at Size=mid, Regen=0.7, Mod=mid. No clicks, no runaway.
- T60 measurement: feed impulse, measure -60 dB crossing, verify it tracks Size × Regen intuition (larger size → longer tail, higher Regen → longer tail).
- CPU within budget (< 15% stereo measured on hardware).
- APFTank atom isolated in `atoms/APFTank.h`, no Zaum-specific coupling in it.
- Unit registered in toc, user-facing, PKGVERSION bumped.

---

## Phase 2 — Sujet (spectral fiction engine)

**Goal.** Ship a spectral magnitude-decay reverb — a different and unique-to-catalog sonic niche. Huge fictional space, freeze, frequency-sculpted decay. No other unit in the package does this. Independent of Phase 1; can be built in parallel.

**Standalone unit shipped.** **Sujet** — a direct-use spectral reverb. The artful telling: the same acoustic material re-ordered and re-presented through frequency-domain manipulation. Valuable independently regardless of whether Zaum ever ships. The most unusual unit in the catalog if it lands.

**North-star primitive built.** STFT-spectral atom (`mods/house/atoms/STFTSpectral.h`) + NEON-FFT infrastructure. Zaum's spectral element type reuses this atom. The FFT infrastructure is new to the package — first time NEON-accelerated transform appears in house.

**DSP specifics.**

- Real-time STFT: N = 1024 (48k) or 2048 (higher rates). 4× overlap. Hann window, COLA satisfied.
- Analysis → per-bin processing → synthesis overlap-add.
- Per-bin magnitude decay: `d_k = 10^(-3 R / (RT60_k × Fs))` where RT60_k is the per-bin decay time (user-shapeable via a Decay Curve parameter mapping frequency to RT60 scalar). This is the Jot per-round-trip formula applied correctly in the frequency domain — not the per-sample mistake from the cascade postmortem.
- Phase randomization for diffusion. L/R decorrelated phase advance for stereo width.
- Freeze mode: `d_k = 1.0` (no decay) + random per-bin phase walk for shimmer-freeze texture.
- Blur: per-bin magnitude smear across adjacent bins (running average). Smears transients into smooth spectral tails.
- Bloom: frequency-staggered attack — high bins respond to input later than low bins. Inverted transient smearing, useful for reverse-swell textures.
- FFT implementation: pffft or equivalent NEON-optimized real FFT. Must build on am335x (Cortex-A8 NEON). Evaluate against libm fallback — if pffft adds build complexity without measurable gain at N=1024, use scalar; N=1024 at 4× overlap is ~192 FFT frames/sec stereo, manageable scalar.

**Latency.** N/2 inherent at 4× overlap = 512 samples ≈ 10.7ms at 48k. Present as predelay (document in unit description, not a bug). For Zaum, this latency becomes the spectral element's natural onset delay — a feature when routing shimmer/freeze.

**Parameter surface (target six params).**

- Decay (master RT60 scalar)
- Damp (high-frequency RT60 rolloff — maps Decay Curve to tilt toward shorter HF tails)
- Freeze (binary or continuous blend to d=1 mode)
- Blur (bin-smear depth)
- Bloom (frequency-staggered attack amount)
- Wetness

**CPU projection.** Real FFT N=1024 at 4× overlap stereo: ~0.26% CPU for transforms at 48k (pffft class; validated against public benchmarks for Cortex-A8). Per-bin processing: 1024 bins × multiply + accumulate per overlap frame. Total estimate: 4–8% stereo including overlap-add. Well within budget.

**Reuse from existing atoms.** Hann window coefficients (compute once, store static). Overlap-add ring buffer pattern similar to predelay ring buffers. Spiral saturator on output stage if needed.

**Dependencies.** None. Self-contained. Can begin in parallel with Phase 1.

**Risks.** Medium.

- New FFT infrastructure — build system must accept pffft (or equivalent) as a new source dependency. Evaluate license (pffft is FFTPACK-derived, BSD-style).
- Transient smearing is inherent at N=1024. Not a bug, but users expecting a clean dry path with reverb added should use short Blur and Bloom=0.
- am335x scalar FFT fallback may be necessary if NEON path has alignment or pipeline hazards — plan for both paths.
- Freeze + Bloom interaction can produce runaway if per-bin magnitudes accumulate without bound in freeze mode. Add a hard per-bin magnitude clamp at 1.0 in freeze path.

**Definition of done.**

- Builds both arches clean.
- Hardware audition: large, smooth spectral tail at moderate Decay. Freeze mode produces sustained shimmer without clicking or magnitude runaway. Blur smears transients audibly. Bloom produces reverse-swell texture.
- Latency documented in unit description (~10ms inherent, presented as predelay).
- STFTSpectral atom isolated in `atoms/STFTSpectral.h`, no coupling logic in it.
- T60 measurement: impulse into spectral reverb, measure -60 dB crossing, verify Decay parameter tracks.
- Unit registered in toc, PKGVERSION bumped.

---

## Phase 3 — Portals v1 (first weave; fixed coupling)

**Goal.** First real "believable + fiction" hybrid unit. Combine the Phase 1 tank substrate with the Phase 2 spectral element. Use a small set of HAND-CHOSEN, fixed node↔band portals, woven to output only (no in-loop coupling, no procedural field). Proves the core thesis in its simplest shippable form.

**Standalone unit shipped.** Portals v1 (provisional name). A tank-plus-spectral reverb with a fixed character weave. Users who want "believable room with spectral shimmer blended in" get a dedicated unit. Still valuable even if Phases 4–5 never ship.

**North-star primitive built.** Weave/coupling atom — parallel sidechain from specific tank delay-line taps to specific spectral bins, with smoothed per-hop-to-per-sample injection. Also: basic governor (output limiter/saturator on the mixed output, ensuring the blend stays bounded regardless of portal gain settings).

**DSP specifics.**

- Tank runs as Phase 1 standalone. Spectral element runs as Phase 2 standalone. They process in parallel; outputs are mixed.
- Portals: a fixed set (e.g., 4–6) of injection points. Each portal taps a specific delay-line node from the tank and injects a scaled copy into a target spectral bin range (or vice versa: spectral magnitude output drives a tank delay-line gain). Both directions may be present.
- Injection is additive, smoothed (one-pole coefficient-change smoothing to prevent zipper noise on gain changes).
- Fixed coupling: portal nodes and bin targets are compile-time constants for v1. No user control over which nodes couple to which bins — that complexity is Phase 4.
- User controls the portal depth (master coupling gain) and direction balance (tank→spectral vs spectral→tank ratio).
- Governor: Spiral saturator on the final mixed output. Prevents any portal resonance from escaping to clip.

**Depends on.** Phase 1 (APFTank atom) and Phase 2 (STFTSpectral atom) both shipped and hardware-validated.

**Folding in postmortem lessons.** The cascade postmortem showed that coupling complexity added before structural stability leads to circular patching. Phase 3 deliberately limits coupling to fixed, verified portals. No procedural field, no in-loop coupling, no dynamism — those are Phase 4. This sequencing means the weave thesis can be validated (does tank+spectral sound good?) before any dynamism complexity is added.

**Risks.** Medium.

- Portal injection can introduce feedback paths if spectral→tank coupling is non-negligible and tank output feeds the spectral input. Map the signal graph explicitly before committing to ensure no unintended loop. If a loop exists, add a one-sample break or make spectral injection unidirectional for v1.
- Phase 2's inherent latency (~10ms) means tank dry signal and spectral wet signal are offset by one FFT frame. Handle at the mixing stage with a matching predelay on the tank path to align onsets.
- CPU: Phase 1 + Phase 2 running simultaneously. Budget: 12% (Phase 1) + 8% (Phase 2) + ~1% (coupling) = ~21% stereo estimate. Validate on hardware.

**Definition of done.**

- Builds both arches clean.
- Hardware audition: tank tail is smooth (Phase 1 character intact), spectral element adds shimmer/freeze texture layered over it (Phase 2 character intact), portal coupling produces audible interaction at Portal Depth > 0.
- No runaway at any portal gain setting (governor catches it).
- Latency alignment verified (tank and spectral onsets coincide).
- Weave/coupling atom isolated in `atoms/PortalCoupling.h`, governor isolated in `atoms/WovenGovernor.h`.
- PKGVERSION bumped.

---

## Phase 4 — Portals v2 (procedural field + Tunnel mode)

**Goal.** Replace the fixed hand-chosen portals with a procedural golden-angle/phyllotaxis field. Add Motion drift so portals migrate continuously. Add the in-loop Tunnel bind-depth mode — portal injection into the tank's feedback path rather than its output tap. This may ship as a v2 update to the Phase 3 unit or as a sibling unit.

**North-star primitive built.** Procedural-field atom (generalized from Network's existing field generator — REUSE, don't rewrite). In-loop governor: the Console saturate→desaturate wrapper pattern applied inside the feedback coupling path to prevent in-loop runaway.

**DSP specifics.**

- Golden-angle/phyllotaxis field: N portal positions distributed on a unit disk using the golden angle (≈ 137.5°). Each portal's position encodes both tank-node index and spectral-bin target via a radial/angular mapping. The field generator algorithm already exists in the Network unit's geometry code (`mods/spreadsheet/network/geometry.h`) — generalize it as a standalone atom.
- Motion: each portal's position drifts at a slow rate (Brownian or sinusoidal) proportional to a Motion parameter. As portals drift, their node/bin targets interpolate smoothly (one-pole smoothing on the injection index to avoid zipper noise).
- Tunnel mode: instead of tapping post-tank delay-line output, portals inject INTO the tank's feedback accumulation path. This creates true in-loop spectral coloration of the reverb tail — a fundamentally different and riskier character.
- In-loop governor: Console-style saturate→desaturate wrapper around the Tunnel injection point. Saturate before injection, desaturate after. Bounds the in-loop energy regardless of portal depth or Motion state.

**Folding in postmortem lessons.** In-loop coupling is the same class of risk that made the cascade unstable. The sequencing defense: the field and Motion are tested first in the woven-to-output-only configuration (Phase 3 still running, no Tunnel mode yet). Only after the procedural field is validated out-of-loop does Tunnel mode get enabled. This is the fixed-then-dynamic sequencing pattern that the postmortem recommends.

**Depends on.** Phase 3 shipped and hardware-validated.

**Risks.** Medium-high.

- In-loop coupling can produce runaway that the governor only partially suppresses if the Console saturator is miscalibrated. Test with an impulse and a silence gate: if in-loop gain > 1, output should decay to silence within a bounded time, not grow without bound.
- Portal migration (Motion) changes injection targets per-sample — interpolation must be smooth enough to avoid zipper noise and abrupt spectral jumps. Validate at maximum Motion rate.
- Network geometry.h generalization may carry assumptions about the Network unit's specific tap structure. Read carefully before extracting.

**Definition of done.**

- Procedural field distributes N portals visually and audibly distinguishable from fixed v1 portals.
- Motion parameter produces audible slow migration of spectral texture without clicks or zipper noise.
- Tunnel mode: in-loop coupling produces a denser, more saturated tail character without runaway at any Portal Depth setting.
- In-loop governor: impulse test in Tunnel mode decays to silence (not sustained oscillation) at all portal-gain settings with Regen below instability threshold.
- Procedural-field atom isolated, reusable, documented in `atoms/PhyllotaxisField.h`.
- PKGVERSION bumped.

---

## Phase 5 — Zaum (heterogeneous pool, north star)

**Goal.** Add the granular element type. Unify the field selector over the full element set {tank-node, spectral-bin, grain}. Per-element roles: {believable, shimmer, freeze, noise-skirt, grain-scatter}. This is the Zaum north star: transrational fusion of Fabula and Sujet via the procedural field.

**Standalone unit shipped.** **Zaum**. The woven endpoint. Fabula (believable substrate) and Sujet (fictional telling) fused inside a single recirculating topology by the golden-angle field. Name is final.

**North-star primitive built.** Granular atom (`mods/house/atoms/GrainCloud.h`). Unified heterogeneous field selector (extends PhyllotaxisField to carry element type per portal node).

**DSP specifics.**

- Granular element: a simple grain cloud (short overlapping grains of the input signal or tank tap, windowed, randomized in pitch/position). Granular reverb already has a distinct sonic niche (noise-skirt, scatter) that neither the tank nor the spectral element covers.
- Heterogeneous selector: the phyllotaxis field from Phase 4 extended so each portal node carries an element type tag. The governor routes coupling accordingly: tank-tagged nodes feed into APFTank, spectral-tagged nodes feed into STFTSpectral, grain-tagged nodes feed into GrainCloud.
- Role assignment: user selects a macro role per element type (believable → APFTank weight up, shimmer → STFTSpectral high-bin weight up, freeze → STFTSpectral d=1 weight up, noise-skirt → GrainCloud pitch-randomized scatter, grain-scatter → GrainCloud with input-triggered grains).

**Depends on.** Phases 1–4 all shipped and stable. Do not begin until 1–4 are released and hardware-validated.

**Risks.** High.

- Scope and complexity are substantial. Three element types, heterogeneous routing, per-element role assignment, plus all Phase 4 machinery (procedural field, Motion, Tunnel). Any instability in Fabula or Sujet compounds here.
- CPU budget: Phase 1 + Phase 2 + grain cloud + field routing overhead. Grain cloud at modest density (~8–16 active grains) adds ~4–6% stereo. Total estimate 25–30% stereo. Validate carefully; may require optimizations before shipping.
- GrainCloud requires careful grain lifecycle management (onset, sustain, release, overlap scheduling) without dynamic allocation. All grain state preallocated in the Object constructor.
- The heterogeneous selector is new code with no existing reference in the package. Build it from the PhyllotaxisField atom incrementally — add element type tag first, verify routing works with all portals of one type, then enable mixed types.

**Definition of done.**

- All three element types active simultaneously without instability.
- Each role macro produces a perceptually distinct character shift.
- CPU within an acceptable budget for a flagship unit (< 30% stereo on hardware).
- Grain cloud: no clicks on grain onset/release; no dynamic allocation in audio thread.
- Zaum ships as a named, versioned, user-facing unit with full Lua wrapper and toc entry.
- PKGVERSION bumped; optional minor version increment if this warrants a release.

---

## Standalone value guarantee

Fabula and Sujet each stand as released units independently. If weaving never ships:

- Fabula delivers the smooth algorithmic room the package currently lacks — fills a real gap.
- Sujet delivers a spectral magnitude-decay reverb unlike anything else in the catalog.

There is no wasted work. Every atom built in Phases 1–4 is reused by Zaum. Every unit shipped in Phases 1–3 is kept in the catalog regardless of whether Zaum lands.

---

## Naming (final)

Names are drawn from Russian Formalist narratology. They describe the architecture, not just the units.

- **Fabula** — Phase 1 standalone. The believable substrate (фабула = the raw chronological events, what actually happened). Final name.
- **Sujet** — Phase 2 standalone. The fictional telling (сюжет/syuzhet = the artful re-presentation of events). Spelled "Sujet". Final name.
- **Zaum** — Phase 5 flagship and package name. Transrational fusion (заумь = Khlebnikov/Kruchyonykh's beyond-sense language). Final name.
- Phase 3 unit: Portals v1 (provisional; may receive a Formalist name at release).
- Phase 4 update: Portals v2 or a sibling unit (provisional).

The Formalist family leaves room for sibling names later if the package grows. Banked options (not commitments): a granular standalone could be "Skaz" (сказ = oral narration / skaz technique); a defamiliarization-focused unit "Ostranenie" (остранение = making-strange). These are reserved, not planned.

---

## PKGVERSION sequence (projected)

Each phase commits independently. Hardware audition gates advancement.

| Phase | Description | PKGVERSION bump |
|---|---|---|
| 1 | APFTank atom + standalone room unit | +1 dev digit |
| 2 | STFTSpectral atom + spectral unit | +1 dev digit (parallel; merge after both pass hardware) |
| 3 | Portals v1 + coupling atom + governor | +1 dev digit |
| 4 | Portals v2 + procedural field + Tunnel | +1 dev digit |
| 5 | Zaum + granular atom + heterogeneous selector | +1 dev digit; consider release |

---

## Files to be created (projected)

```
mods/house/atoms/APFTank.h              # Phase 1
mods/house/atoms/STFTSpectral.h         # Phase 2
mods/house/atoms/PortalCoupling.h       # Phase 3
mods/house/atoms/WovenGovernor.h        # Phase 3
mods/house/atoms/PhyllotaxisField.h     # Phase 4 (generalized from Network geometry)
mods/house/atoms/GrainCloud.h           # Phase 5
mods/house/assets/<Phase1Unit>.lua      # Phase 1 (name TBD)
mods/house/assets/<Phase2Unit>.lua      # Phase 2 (name TBD)
mods/house/assets/Portals.lua           # Phase 3
mods/house/assets/Zaum.lua              # Phase 5
planning/zaum-design.md                 # architecture detail doc
```

Existing reuse: `atoms/Spiral.h`, predelay ring buffer pattern, CreamCoat Bezier shell (if reduced-rate domains needed in Phase 5 for grain cloud), Network geometry.h (generalized in Phase 4).
