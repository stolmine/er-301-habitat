# Sujet — implementation-ready design

Status: **ready to implement**. No code yet.
Phase 2 of the Zaum roadmap. North star: `planning/zaum-design.md` §AUX.
Phased build context: `planning/zaum-roadmap.md` §"Phase 2 — Sujet".
Sibling unit: `planning/fabula-design.md` (Phase 1, shipped through dev 0.1.0.12).

Research basis (deep-research, 24/25 claims verified 3-0):
- **SMD architecture** — Vickers, Wu, Krishnan & Sadanandam, "Frequency Domain
  Artificial Reverberation using Spectral Magnitude Decay", AES 121st (2006):
  `sfxmachine.com/docs/FDReverbSpectralMagDecay.pdf`, `ccrma.stanford.edu/~larrywu/files/AES_121.pdf`.
- **STFT/OLA/COLA + phase vocoder** — JOS, *Spectral Audio Signal Processing*
  (`ccrma.stanford.edu/~jos/sasp/`).
- **Phase coherence** — Průša & Holighaus, "Phase Vocoder Done Right" / RTPGHI
  (arXiv:2202.07382); Laroche–Dolson, "Improved Phase Vocoder".
- **Transient handling** — Röbel, "A new approach to transient processing in the
  phase vocoder", DAFx-03.
- **Diffusion aesthetics** — Valhalla DSP (Costello) notes on diffusion / metallic
  artifacts / Gaussian-envelope bloom.
- **Real-time reference** — Mutable Instruments Clouds spectral mode, vendored at
  `eurorack/clouds/dsp/pvoc/{stft,phase_vocoder,frame_transformation}.{h,cc}`;
  FFT at `eurorack/stmlib/fft/shy_fft.h`.

---

## 1. Intent and scope

Sujet is the **fictional telling**: the same acoustic material re-presented through
frequency-domain manipulation. Where Fabula is a believable room rendered in
time-domain delay lines, Sujet does what no physical room can — bins that refuse to
decay, frozen spectra, frequency-staggered onsets, octave ghosts, noise-skirt
fattening. It is **pure STFT, no time-domain tank**; the counterpart to Fabula, not a
variant. The two fuse only in Zaum (Phase 3+).

Sujet ships as a standalone unit in the existing **CM4-only `mods/zaum/`** package,
alongside Fabula. Its DSP atom `mods/zaum/atoms/STFTSpectral.h` is the Phase 2
north-star primitive (the package's first FFT-based engine) and is reused verbatim
as Zaum's spectral element.

### The Fabula/Sujet symmetry (why SMD is the right engine)

SMD's per-bin feedback gain is the **exact frequency-domain twin** of Fabula's
Jot/Gardner time-domain decay — the hop size `R` replaces the delay-line length `M`:

```
Fabula  (time domain) :  g_i  = 10^(−3·M_i / (RT60·fs))      per delay line
Sujet   (freq domain) :  g(k) = 10^(−3·R   / (RT60(k)·fs))   per FFT bin
```

Fabula decays energy in delay lines; Sujet decays it in bins; same formula. The
Formalist split (фабула = events in order / сюжет = artful re-presentation) is made
literally true in the DSP. This is the design's organizing idea.

---

## 2. Architecture — Spectral Magnitude Decay (SMD)

Per-bin recursive leaky accumulation of STFT **magnitudes**, with **phase synthesized
separately**:

```
M_out(u,k) = |X(u,k)|  +  M_out(u−1,k) · g(k)          // magnitude accumulator
g(k)       = 10^(−3·R / (RT60(k)·fs))                  // per-bin decay, clamp ≤ 1
φ_out(u,k) = φ_in(u,k) + V(k) · ξ(u,k)                 // artificial phase (see §4)
Y(u,k)     = M_out(u,k) · e^{ j·φ_out(u,k) }           // recombine for IFFT
```

Properties that make Sujet cheap and flexible:
- **CPU independent of decay length** (unlike convolution) — a 60 s tail costs the
  same as a 0.5 s tail.
- **Less memory than an FDN** — one magnitude accumulator + one phase value per bin.
- **Per-bin independent decay** — `RT60(k)` is a frequency→time curve, so tilt,
  damping, and "bins that refuse to decay" are all just `g(k)` shaping.
- **Magnitude/phase are decoupled** — decay lives in magnitude; the entire aesthetic
  (lush↔grainy, coherent↔diffuse) lives in phase (§4).

Stability: `g(k) ≤ 1` unconditionally (the SMD analogue of Fabula's `g_d < 1`).
`g(k) = 1` is the freeze case (infinite, bounded). A hard per-bin magnitude clamp is
the safety net for any op that could pump energy (shimmer feedback, freeze+bloom).

---

## 3. STFT real-time framework

Standard analysis → per-bin process → synthesis → overlap-add, run **inside
`process()`** at hop boundaries (Clouds-style; CM4 has ample headroom for inline
FFTs, so no separate worker thread is needed).

| Parameter | Choice | Rationale |
|---|---|---|
| Frame size **N** | **1024** (start) | ~21 ms, 46.9 Hz bins at 48 kHz; responsive. 2048 reserved if freeze/shimmer want finer bins (doubles latency + cost). |
| Hop **R** | **256 (4× / 75% overlap)** | The research recommends high overlap *because the spectrum is heavily modified between analysis and synthesis* (smoother, fewer artifacts). 2× (hop 512) is the CPU/latency fallback. |
| Window | **Hann (or MLT sine²)** | COLA-satisfied; analysis×synthesis product = Hann for OLA. Apply on **both** analysis and synthesis (modification regime). |
| COLA | **strong COLA** | `Σ_m w(n−mR) = const`; prefer the bandlimited (no-aliasing) regime since we modify the spectrum. Size `N ≥ M+L−1` if any bin-shift/convolution op (shimmer/blur) risks circular aliasing. |
| FFT | **ShyFFT** (`eurorack/stmlib/fft/shy_fft.h`) | Already vendored — **eliminates the new-dependency / license risk** the roadmap flagged. Scalar radix-2; NEON optimization deferred. Add `eurorack` / `eurorack/stmlib` to `mods/zaum/mod.mk` INCLUDES (or vendor a copy into `atoms/`). |
| Latency | **~N samples ≈ 21 ms**, presented as **predelay** | Inherent; documented in the unit description, folded into the Predelay readout. In Zaum-Tunnel it becomes part of the loop time (a feature). |
| Control rate | **per audio block** | Read params every `process()` block (FRAMELENGTH 32–128), apply to the next frame — this is what makes Clouds spectral feel responsive despite STFT latency. Never block the audio path on the FFT. |

Plumbing (mirror `clouds/dsp/pvoc/stft.cc`): an input analysis ring + an output
overlap-add synthesis ring sized `N + R`; accumulate block samples into analysis,
fire FFT+process+IFFT when a hop's worth has arrived, window-and-add into synthesis,
stream synthesis out continuously.

---

## 4. The phase voicing lever — Diffuse (V) — THE headline control

Magnitude is decayed; **phase is the entire aesthetic axis**. A single per-bin
randomization control `V ∈ [0,1]`:

```
φ_out(u,k) = φ_in(u,k) + V · ξ(u,k)        // ξ uniform on [−π,π]
```

- **V = 0** — coherent phase. Freeze sounds *mechanical / "tonal noise"* (the
  frame-to-frame phase increment is identical — verbatim from Vickers).
- **V rising** — each spectral line broadens into a **narrowband noise resembling
  the FT of the synthesis window** → lush, diffuse, **decaying-white-noise reverb**.
  This is the believable-but-impossible sweet spot.
- **V = 1** (especially short N) — full randomization → **"whisperization,"** loses
  sub-bin resolution → outright **spectral mangling**.

**`V` IS the "believable reverb ↔ spectral mangler" axis** the whole unit pivots on —
one knob, not a mode switch. It is Sujet's signature control (UI name: **Diffuse**).
Stereo width is decorrelated per-bin phase (independent `ξ` per channel — see §8).

Phase-coherence ladder (for the pitch-coherent ops, §5): classical PV phase
propagation (cheap, Clouds' 16-bit phase-delta form) → Laroche–Dolson scaled phase
locking (less "phasiness") → RTPGHI (full coherence, no peak-picking). Sujet uses
**classical PV phase propagation** for shimmer at v1; `V` then dials *away* from that
coherence toward diffuse noise. RTPGHI is a reserved upgrade if shimmer phasiness
bites.

---

## 5. The op palette (each maps onto SMD almost free)

**Core (v1):**

| Op | Mechanism in SMD | Param |
|---|---|---|
| **Spectral reverb** | the `M_out` accumulation with `g(k)` from a master RT60 | Decay |
| **Tilt / Damp** | `RT60(k)` rolled off toward HF (HF bins decay faster) | Damp |
| **Diffuse** | per-bin phase randomization `V` (§4) | **Diffuse** |
| **Freeze** | blend `g(k) → 1`; continuous (0..1) not just a gate; clamp magnitudes | Freeze |
| **Blur** | smear `M_out` across adjacent bins (short Gaussian/running-avg kernel) → spectral diffusion, transients melt | Blur |
| **Bloom** | frequency-staggered attack: per-bin onset/attack envelope (LF responds before HF, or inverted) → reverse-swell. Connects to the cascaded-diffusor → Gaussian-envelope result (diffusion 0.618 ≈ equal attack/decay = backwards reverb). | Bloom |
| **Mix** | wet/dry crossfade (dry path is latency-compensated) | Mix |

**Reach (v1.x, behind a "Fiction" menu/macro if the surface gets crowded):**

| Op | Mechanism | Notes |
|---|---|---|
| **Shimmer** | octave-up spectral feedback: bin `k`'s decayed magnitude also feeds bin `2k` (phase-propagated) → rising ghost over the original | The classic shimmer; needs phase coherence + a magnitude clamp (feedback). |
| **Noise-skirt** | per-bin magnitude expanded into a narrow noise band around the bin (SMS/Loris sines+noise) → fattens partials without pitch shift | Lushness; ε·rand envelope on neighbor bins. |
| **Scramble / Paulstretch** | bin permutation or extreme `V`=1 smear | The заумь edge; reserved "mangler" territory. |

Op ordering per frame (after analysis → `RectangularToPolar`):
`accumulate (g·M_out + |X|)` → `tilt` → `blur` → `bloom` → `shimmer feed` → `noise-skirt`
→ `phase synth (V, transient-gated)` → `freeze clamp` → `PolarToRectangular` → IFFT → OLA.

---

## 6. Transient handling (the artifact cure)

Phase-vocoder attacks smear and pre-echo because the per-bin stationarity assumption
breaks. The established fix (Röbel DAFx-03), applied to SMD:

- **Detect onsets** (per-frame spectral flux / energy jump).
- **Across a transient:** reset the phase of transient bins (don't randomize), and
  **suspend magnitude accumulation** (let the dry transient pass clean rather than
  smearing it into the tail). I.e. transient frames bypass the `V` randomization and
  the leaky accumulator momentarily.

This directly addresses the roadmap's flagged "transient smearing" risk and keeps
attacks crisp while the tail stays diffuse. Expose a small **Transient** sensitivity
or fold it into a fixed sensible default for v1.

---

## 7. Parameter and UI surface

DSP parameter map (target ~8, all 0..1 CV-able):

| Control | Range | Default | Lever |
|---|---|---|---|
| **Decay** | 0..1 → RT60 ~0.3..120 s | 0.5 | master `RT60` scalar → `g(k)` |
| **Damp** | 0..1 | 0.3 | HF `RT60` rolloff (tilt) |
| **Diffuse** | 0..1 | 0.4 | phase randomization `V` (headline) |
| **Freeze** | 0..1 | 0.0 | blend `g(k) → 1` |
| **Blur** | 0..1 | 0.0 | bin-smear kernel width |
| **Bloom** | 0..1 | 0.0 | frequency-staggered attack amount |
| **Predelay** | 0..1 → 0..~340 ms | (incl. inherent ~21 ms) | onset delay; absorbs STFT latency in the readout |
| **Mix** | 0..1 | 0.4 | wet/dry |

Shimmer / Noise-skirt / Scramble arrive as a **Fiction** menu (OptionControl) +
depth, à la Network's macro, if added — keeps the continuous surface small.

Lua wrapper `mods/zaum/assets/Sujet.lua` mirrors `Fabula.lua`: one `ParameterAdapter`
+ `GainBias` ply per continuous control, `zeroOneMap`, expanded view lists all plies.
Button labels: `dcy / damp / diff / frz / blur / blm / pre / mix`. Document the
inherent latency in the unit description.

---

## 8. Stereo strategy

Internal-stereo. One `STFTSpectral` object owns L and R analysis/synthesis rings.
Two viable widths:
- **Independent L/R STFT** (2× FFT cost) — full per-channel processing; decorrelated
  per-bin `ξ` (phase randomization) gives wide, lush stereo. Start here.
- **Mid/side** — process M and S separately for controllable width at ~same cost.
  Reserve as an option.

Mono input (channelCount == 1): In1 feeds both; decorrelated phase randomization
spreads it to stereo within the first frames (the spectral analogue of Fabula's
figure-8 mono-spread).

---

## 9. Atom plan

### STFTSpectral.h — the new atom
`mods/zaum/atoms/STFTSpectral.h` — header-only `od::Object` subclass per
`planning/house-atom-architecture.md`. Contains: analysis/synthesis rings (float,
`N+R` per channel), the magnitude accumulator + phase arrays (per bin, per channel),
the Hann window table (compute once / static), the ShyFFT instance(s), a fast
XorShift PRNG for `ξ` (never libc `rand()` on the audio thread), onset-detector
state, and all op state. Inlets/outlets/parameters public outside `#ifndef SWIGLUA`;
`process()` and members inside it (the package SWIG invariant).

**Zaum hook (build it in now, even though Sujet's UI doesn't use it):** expose the
per-bin magnitude/phase arrays and a per-bin `g`/`V` override path so the Phase-4
procedural field can address *selected bins* as portals. Keep the op pipeline
addressable per-bin-range. This is the whole reason Sujet's atom is reused by Zaum.

### FFT integration
Reuse **ShyFFT** from `eurorack/stmlib/fft/shy_fft.h`. Add the include path in
`mods/zaum/mod.mk` (`INCLUDES += eurorack eurorack/stmlib`) — confirm it resolves and
compiles into the zaum lib. Fallback: vendor a copy into `mods/zaum/atoms/`. NEON-FFT
optimization is deferred (scalar ShyFFT fits the budget, §11).

### Package integration
Add to `mods/zaum/zaum.cpp.swig` (`#include "atoms/STFTSpectral.h"` + `%include`),
`mods/zaum/assets/toc.lua` (Sujet entry, category "Zaum"), and `Sujet.lua`. PKGVERSION
advances on the zaum package (Sujet starts its own dev sub-sequence, e.g. 0.2.0.x, to
keep it legible against Fabula's 0.1.0.x).

---

## 10. Build sub-phases (staged; each ends at an emu audition gate)

The first gate is the one everything depends on — **prove the framework is
transparent before adding any fiction**.

- **0.2.0.1 — STFT passthrough (THE gate).** Analysis → (identity) → synthesis →
  overlap-add. No SMD, no ops. Audition + offline rig (§12) must show **bit-transparent
  COLA reconstruction** (output == input delayed by the latency, within float epsilon).
  Nothing proceeds until this is clean.
- **0.2.0.2 — SMD magnitude decay.** The `M_out` accumulator + `g(k)` from Decay.
  Coherent phase (V=0) — expect it to sound mechanical; that's correct at this stage.
  Verify a tail builds and decays; numerically check T60 vs the `g(k)` formula.
- **0.2.0.3 — Diffuse (V) phase randomization.** The headline. Sweep V 0→1: mechanical
  → lush decaying-noise → whisperization. This is the calibration gate (like Fabula's
  Mod gate). Wire Damp (tilt) here too.
- **0.2.0.4 — Freeze + clamp.** Continuous `g→1` with the magnitude safety clamp;
  confirm no runaway, smooth infinite hold, V breaks the mechanical quality.
- **0.2.0.5 — Blur + Bloom.** Bin-smear + frequency-staggered attack.
- **0.2.0.6 — Transient handling.** Onset detect → phase reset + accumulation suspend;
  verify attacks stay crisp, pre-echo gone.
- **0.2.0.7 — Reach ops + Fiction menu** (Shimmer, Noise-skirt) if pursued.
- **0.2.0.8 — Surface + UI polish**, first user-facing release.

---

## 11. CPU / memory / latency budget

**CPU (stereo, 48 kHz, N=1024, 4× / hop 256):**
- ~187.5 hops/s × (1 fwd + 1 inv FFT) × 2 ch = **750 transforms/s**. Scalar ShyFFT
  1024-pt on A72 ≈ tens of µs each → rough order **~3% CPU** for transforms.
- Per-bin ops: 513 bins × (accumulate/tilt/phase) × 187.5 hops × 2 ch — light
  arithmetic, **~1–3%**.
- **Total estimate ~4–8% stereo** (matches the roadmap projection). 2× overlap halves
  the FFT share if needed. Comfortable on CM4; NEON-FFT is the optimization lever if a
  later Zaum integration tightens the budget.

**Memory (per instance, N=1024, stereo):** analysis+synthesis rings (`N+R` ×2 ×2 ch),
magnitude+phase arrays (`N/2+1` doubles ×2 ×2 ch), window + scratch, predelay buffer.
Order **~100–200 KB** — fits CM4 L2 (1 MB) easily.

**Latency:** ~N = 1024 smp ≈ **21 ms**, presented as predelay (documented, not a bug).

---

## 12. Validation

**Offline transparency/COLA rig** (scratchpad, like Fabula's density rig — no `od`
dep): replicate the STFT pipeline, feed an impulse / sweep / noise, and numerically
verify (a) **COLA reconstruction is transparent** (passthrough error < −80 dB), (b)
**latency** is exactly N/hop as specified, (c) **T60** of the SMD tail tracks the
`g(k)` formula, (d) no DC/Nyquist leakage. This gates each sub-phase before ears.

**IR / reference capture (aesthetic targets, not the engine).** SMD is not
convolution, so reference IRs aren't loadable — but capturing impulse/sweep responses
and freeze textures from reference spectral reverbs (Eventide Blackhole/Shimmer,
Valhalla Supermassive/Shimmer, Clouds spectral mode) gives **A/B voicing targets**:
match Sujet's Decay/Diffuse/Freeze character against them by ear and by spectrogram.
Use these to calibrate the `V` sweet spot, the tilt curve, and the shimmer balance.
(Industry *internals* are unconfirmed by the research — treat captured IRs as targets
to emulate, not algorithms to reverse-engineer.)

---

## 13. Open questions / risks

1. **FFT size / overlap final call.** Start N=1024 @ 4×; the offline rig (§12) should
   A/B 1024 vs 2048 and 2× vs 4× for the latency/quality/CPU sweet spot before locking.
2. **ShyFFT include-path integration** is the first real infra task (untested in the
   zaum package). Fallback: vendor the header. Confirm scalar precision/perf at N=1024.
3. **Shimmer phasiness.** Classical PV phase propagation may sound phasey on complex
   input; RTPGHI is the reserved upgrade. Decide at the 0.2.0.7 gate.
4. **Freeze + Bloom/Shimmer runaway.** Any op that pumps magnitude in the accumulator
   needs the hard per-bin clamp; test impulse-into-freeze for unbounded growth.
5. **Transient detector tuning.** Too sensitive = choppy; too loose = pre-echo.
   Reserve a calibration pass (0.2.0.6).
6. **Whisperization as a feature vs bug.** `V=1` mangling — keep it reachable as the
   intentional "spectral fiction" extreme, or soft-limit V to a musical ceiling?
   Decide by ear at 0.2.0.3.
7. **Zaum bin-addressability** must not bloat Sujet's audio path — design the per-bin
   hook as a no-op when unused.

---

## 14. File manifest

### Files to create
```
mods/zaum/atoms/STFTSpectral.h     # SMD + STFT engine atom (new; reuses ShyFFT)
mods/zaum/assets/Sujet.lua         # unit Lua wrapper (mirrors Fabula.lua)
```
### Files to edit
```
mods/zaum/zaum.cpp.swig            # %{ #include %} + %include "atoms/STFTSpectral.h"
mods/zaum/assets/toc.lua           # add Sujet unit entry
mods/zaum/mod.mk                   # INCLUDES += eurorack eurorack/stmlib ; PKGVERSION
```
### Reuse / reference
```
eurorack/stmlib/fft/shy_fft.h                         # the FFT (vendored)
eurorack/clouds/dsp/pvoc/stft.{h,cc}                  # real-time pipeline reference
eurorack/clouds/dsp/pvoc/frame_transformation.{h,cc}  # spectral-op reference
mods/zaum/atoms/Spiral.h (house)                      # output governor, if needed
```

---

## 15. Naming

**Sujet** (сюжет/syuzhet) — the artful telling, the fictional re-presentation. Final
name. The most unusual unit in the catalog if it lands; the spectral counterpart that
makes the Fabula↔Sujet (events↔telling) pairing whole, and the spectral element Zaum
weaves with the tank.
