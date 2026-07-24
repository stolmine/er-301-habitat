# 04 — Fusion / cross-feedback governor & guards

Combines online research (dsp-research-expert, 2026-06-25) with our Spiral governor.
The unit's signature is a **bidirectional loop**: looper out → field in, field out → looper
in. Goal is NOT to prevent self-oscillation but to make it musical — bounded, tonally rich,
parameter-controllable ("sings into saturation instead of blowing up").

## The structural problem
Aggregate loop gain = product of every stage around the combined loop:
`g_L · g_LR · g_R · g_RL` (looper playback · looper→field send · field feedback ·
field→looper return). >1 at any frequency where loop phase = 2πN → oscillation. Because the
loop traverses a high-gain looper AND a multi-resonance field, runaway is asymmetric and
frequency-dependent. Nonlinear saturation in the path makes effective gain amplitude-
dependent → that is what lets self-oscillation settle at a bounded amplitude.

## The single most important move
Insert a **soft-saturation governor in each cross-send**, before the signal enters the
looper and before it enters the field. We already have the proven one: **Spiral**
(`mods/house/atoms/Spiral.h`), `spiralSaturate(x,d) = sign(x)·sin(min(|x|·d, π/2))/d`,
bounded to ±1/d, unity at small x, fast Taylor variant. (Research recommends `tanh(d·x)/d`;
Spiral is our equivalent sin-clipper and is already used exactly this way inside RotCoat's
feedback path. Use Spiral — no need for a new tanh.) Drive `d` ← a FUSE/DRIVE control: as
loop gain crosses 1, output limits to ±1/d so oscillation settles at a predictable
amplitude/spectrum instead of exploding.

## Full guard stack (priority order)
1. **FPCR FZ bit** in every audio thread (ARM64, per-thread, NOT inherited): `fpcr|=(1<<24)`.
   Mandatory, free. Scalar FP feedback tails hit denormals → 10–100× stalls → missed
   deadline → full-volume noise burst. NEON is always FTZ; scalar isn't.
2. **Denormal DC tickle**: add `±1e-15f` (sign alternating per block) at each feedback
   junction. Belt-and-suspenders with FTZ, free.
3. **DC blocker** in each cross-path (post-saturation): `y[n]=x[n]−x[n-1]+R·y[n-1]`,
   R≈0.9987 (fc≈20 Hz @48k). Saturation injects DC that accumulates per pass and biases the
   clipper → asymmetric clipping. ~4 ops/sample for both paths. Use fc≈20–25 Hz (keeps
   settling <15 ms, doesn't thin the self-oscillation bass).
4. **Spiral governor** in each cross-send (above).
5. **Orthogonal field feedback matrix** (Hadamard/Householder, which the house FDNs already
   use) → guaranteed lossless prototype (‖A‖₂=1); add per-line one-pole LP for RT60 control.
   Lossless + unity cross-feedback = frozen infinite drone (freeze/sustain); near-unity +
   Spiral = sustained, spectrally-rich, bounded.
6. **Delay-line modulation** in the field (0.5–3 Hz, depth ~0.1–2 ms) — Valhalla/Costello:
   time-variation RAISES the max stable loop gain (distributes energy across freq/phase,
   stops coherent build-up) AND kills metallic coloration. Fabula's Brownian modulation is
   exactly this primitive.
7. **Periodic NaN guard** at each junction: `if(!isfinite(x)) x=0;` once per block (cheap).

## Energy-preserving vs lossy
Lossless FDN + unity cross-feedback → held drone (good for Freeze). Lossy + high cross-fb →
decaying self-oscillation. The musical sweet spot is near-unity with Spiral limiting: sustains
indefinitely at a bounded, harmonically-rich amplitude shaped by drive `d`.

## Well-established vs exotic
- Saturation-in-loop (Spiral/tanh), DC blocker, FTZ, DC tickle, orthogonal FDN matrix,
  modulation-extends-stability: ALL WELL-ESTABLISHED.
- Airwindows sin/asin even-harmonic pair: EXOTIC — defer to v2.
- **The coupled looper+FDN bidirectional config has NO direct academic literature** →
  stability must be **validated empirically**: sweep g_LR, g_RL, d, g_R while watching for
  NaN/overflow and measuring settled self-oscillation amplitude. This is the primary
  engineering validation task for fusion (build a small offline rig, à la Fabula density rig).

## Codebase tie-ins
- `mods/house/atoms/Spiral.h` — the governor (already used in feedback paths).
- `mods/zaum/atoms/APFTank.h` — Brownian delay modulation + Spiral placement + cross-coupled
  internal-stereo feedback shape to copy.
- House FDNs (`Galactic/Verbity/CreamCoat.h`) — orthogonal Householder feedback matrices.
