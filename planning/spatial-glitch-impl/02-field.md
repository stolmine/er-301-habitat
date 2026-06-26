# 02 — Spatial field engine (sparse ↔ dense morph) + stereo

Combines online research (dsp-research-expert, 2026-06-25) with the **Network cascade-FDN
postmortem** (`../network-cascade-postmortem.md`, summarized in `00`). This is the unit's
hardest subsystem: a field that travels CONTINUOUSLY from sparse, addressable, rhythmic
reflections (glitchy) to a dense diffuse wash, with NO mode switch — without repeating
Network's failure.

## The architecture (resolves the postmortem): Moorer two-stage + Erbe-Verb α-morph

The research maps Network's four failures precisely onto known FDN pitfalls and prescribes
**strict separation**: sparse multitap is FEEDFORWARD-only into the mix; the FDN feedback
loop stays UNITARY. The sparse "rhythmic echoes" come from two clean sources — the
feedforward taps AND the FDN's *near-identity* feedback matrix (self-loops) — **never from a
multitap sum inside the feedback path** (that was Network's instability: tap-mean |H|≠1 →
comb peaks → runaway).

### Stage 1 — Sparse feedforward tap array (the "glitch/spatial" pole)
- One delay buffer (~0.5–2 s). N=8–16 individually addressable read taps, each gain/pan/
  ±2-smp jitter (linear interp). Weighted sum written DIRECTLY to the stereo mix bus.
  **Zero feedback → cannot go unstable.** Addressable, rhythmic, "you can hear each tap."
- Per-tap ping-pong panning → natural stereo width with no comb coloration.
- Reuse Network's per-tap infra (`mods/spreadsheet/Network.h` 3-pass NEON gather, per-tap
  LFO/detune/LP, dual-read crossfade) — but **feedforward only**, dropping the in-loop
  feedback that caused the trouble.

### Stage 2 — N=8 Jot FDN late tail (the "dense wash" pole)
- 8 delay lines, mutually-PRIME lengths ~30–150 ms (e.g. 1669/1987/2311/2833/3299/3671/
  4049/4447 @48k). **Householder** feedback matrix (`A = I − (2/N)·11ᵀ`, 2N adds + 1 mul,
  the diff-Householder our house FDNs already implement — Galactic/Verbity/CreamCoat).
- **Per-line Jot T60 LP**: pole from `g_i = 10^(−3·m_i/(fs·T60))` (per-sample, per-line,
  proportional to line length — the formula Network got wrong). Second pole for HF air
  damping. **Smooth per-block (τ≈20 ms), never per-sample** (pole jumps = transient spikes).
- 4-stage **Schroeder allpass diffuser** chain (coeff 0.6–0.7) on the FDN INPUT to pre-
  diffuse impulses (raises early echo density). Fabula's series-cascade allpasses
  (`mods/zaum/atoms/APFTank.h`, proven unity-gain) are the in-tree version.
- Stereo out: tap channels 0+1 (L) and 4+5 (R) — Householder mixing already decorrelates
  them, so no extra decorrelator needed.

### The morph: Erbe-Verb α-matrix interpolation (the single "plexus" axis)
Tom Erbe (ICMC 2015): a scalar **α ∈ [0,1]** interpolates the 8×8 feedback matrix from
**near-identity** (α≈0: each line feeds mostly itself → individually-audible looping echoes,
sparse) to **Hadamard** (α≈1: maximum diffusion → dense wash). `A(α) = (1−α)·I + α·H_N/√N`,
re-normalized per block to stay orthonormal. Because it stays orthonormal at EVERY α, the
lossless prototype is ALWAYS stable, and morphing injects no transient energy (unlike
crossfading two discrete algorithms). At α≈0 the sparse echoes are STRUCTURAL (feedback-to-
self), so |H|≠1 multitap sums never enter the loop → all four Network failures avoided.

### The single `density` / `plexus` macro (0→1)
Drives BOTH: (a) crossfade Stage-1-taps-dominant → Stage-2-FDN-dominant in the wet mix, and
(b) α from near-identity → Hadamard. Moves together: "addressable sparse echoes" →
"unitary dense wash." **Does NOT touch the T60 poles** → decay stays independently
controllable (this is the Size/density/T60 DECOUPLING Network lacked). `Size` = delay-line
lengths (temporal spacing of the sparse echoes / room scale); `Diffusion` = input allpass
coeff; `Decay` = T60 poles. All orthogonal.

## Liveliness / anti-metallic (safe to modulate)
- **Givens-rotation LFO** on the matrix (Schlecht–Habets 2015): `A(t)=A_0·R(θ(t))`, θ a
  0.1–0.3 Hz slow LFO, a few degrees depth. Unitary∘unitary = unitary → provably stable;
  continuously shifts eigentones → kills metallic ringing at the sparse end without touching
  the morph axis. (Or simpler: Fabula's Brownian delay-line modulation, ±~10 smp sub-Hz —
  same anti-standing-wave effect, already in-tree.)
- Last-resort: soft-clip FDN output at ±1 before re-entry (Spiral) — see `04-fusion-governor.md`.

## What's safe vs risky under morphing
- **Safe (cannot destabilize):** α interpolation, LFO delay mod, input allpass coeff (|c|<1),
  Stage-1↔Stage-2 blend, Givens angle.
- **Risky (smooth per-block):** per-line LP poles (T60), delay-length/Size sweeps (limit rate;
  fast sweep = pitch glitch, may be desirable), feedback gain >1 (Erbe allows ~1.25 for
  swells — guard with the output soft-clip).
- **Gate with a numerical T60 / echo-density rig** (impulse → −60 dB crossing) before trusting
  ears — the Fabula discipline; the postmortem's #1 "build the measurement rig" lesson.

## Stereo (subsystem 5, concise)
1. **FDN tap extraction** (preferred, zero cost): L/R from different Householder-decorrelated
   channels (0+1 / 4+5). Mild ≤1–3 dB mono-sum dips, no comb peaks.
2. **Per-tap ping-pong** on Stage 1 (zero cost): alternate tap pan → rhythmic width, no comb.
3. **Allpass-cascade decorrelation** (if more width needed): 4–6 allpass (poles 0.3–0.9,
   delays 0.6–10 ms) on ONE channel only → phase-only width, flat magnitude. ~10–20 MAC/smp.
4. (Exotic, defer) Optimized velvet-noise decorrelator (±1 FIR, offline-optimized coeffs).

## Liftable open source (all MIT / public domain)
- **Mutable Clouds `clouds/dsp/fx/reverb.h`** — Dattorro/Griesinger allpass-loop reverb,
  directly portable; the cleanest reference for Stage-2 diffusion + the `diffusion_` morph.
- **Signalsmith `reverb-example-code`** (+`mix-matrix.h`) — N=8 Householder FDN + Hadamard
  diffuser, the canonical modern reference.
- **Freeverb / STK FreeVerb** — Schroeder allpass + comb, Moorer separation reference.
- In-tree: house FDNs (Householder), Fabula APFTank (allpass tank + Brownian mod + Spiral),
  Network (sparse multitap gather — feedforward only).

## Well-established vs exotic
- Jot FDN + Householder + per-line LP, Dattorro allpass loop, Moorer separation, Schroeder
  diffuser, Hadamard diffuser, per-tap ping-pong, allpass decorrelation: ALL WELL-ESTABLISHED.
- **Erbe-Verb α interpolation: PUBLISHED/established math, few public C++ impls** — the one
  step into less-trodden territory, but the linear-interp-then-renormalize form is simple and
  is the unit's defining novelty. Implement from the Erbe 2015 paper.
- Givens-rotation matrix LFO, velvet-noise FDN/decorrelator: EXOTIC (defer; have simpler
  in-tree substitutes — Brownian mod, allpass decorrelation).

## CPU
Stage 1 ~50 ops/smp + Stage 2 ~150 ops/smp + stereo ~10 = ~210 ops/sample → <1% of one A72
core. Comfortable even stereo with the looper + CLOCK running.
