# Reverb design philosophy (transferable primitives)

Distilled from `planning/refs/airwindows-port-handoff.md` §2,
checked against our existing port work on Cabinet (kWoodRoom)
and Network (multi-tap glitch reverb).

The Airwindows algorithms are *simple*. A Householder feedback
matrix, a pile of allpasses, a one-pole damper. Character comes
from **tuning, where the nonlinearity sits, and how the signal
is reconstructed** — not algorithmic complexity. The most
interesting combination move is not a parallel verb you crossfade
between, it's a **process the first verb runs through or inside**.
MV is allpasses *inside* PurestConsole. CreamCoat is ClearCoat
*through* Bezier undersampling. Cabinet (kWoodRoom) is a 6×6 FDN
*through* a Bezier-undersample shell.

## Primitives worth lifting

### Householder FDN — lossless mixing core

`out[X] = 2·h[X] - sum(others) = 3·h[X] - total` where `total =
sum(h[0..N-1])`. The shared `total` lets the per-line cost
collapse from `N adds + 1 mul` to `1 fmsub` after one shared
sum. O(N)/sample. NEON-friendly when N is padded to a vector
boundary (6→8, etc.). Cabinet's 6×6 trellis uses this everywhere;
the NEON-reduction win is queued for Cabinet Phase 4.

### Allpass bank — flat magnitude, scrambled phase → bloom/smear

Swapping delays↔allpasses in the *same* matrix is the
ClearCoat→CloudCoat difference: same skeleton, totally different
character. Allpass-based reverbs (PocketVerbs, MV) tend to feel
older / more dramatic; pure-delay FDNs feel modern / clean.

### On-the-fly delay tuning

The ratio choice is the voice:
- **Golden-ratio spacing** → seamless tail at any size (Chamber).
- **Prime spacing** → density, no comb-coloration (Cabinet's
  6×6 line lengths are all primes 109..832).
- **Equal spacing** → flutter, audible repeats.

### Feedforward-with-one-feedback topology

A single feedback-vs-feedforward macro can span zero-feedback
slapback through infinite tail (Verbity). One topology, full
range of "is this an echo or a room".

### One-pole damper in-loop, tuned to an air-absorption curve

The simplest possible HF rolloff in the feedback path that
correlates with room size. The Cabinet inner Bezier filter
serves this role; modern AW reverbs typically use an explicit
one-pole with frequency tied to the size parameter.

## Combination mechanics (tame → wild)

These are the reusable patterns for building character. Order is
roughly tame-to-wild.

### 1. Console wrapper as feedback governor

Run the loop inside a `saturate → … → desaturate` Console pair
(MV's trick). Distorted feedback wraps quieter; **infinite regen
can't run away without a limiter**. Range: clean → saturated
wall. Used in MV/MV2 (allpasses inside PurestConsole). Cheap to
add. Buys infinite-decay safely.

### 2. Undersample / Bezier as a character axis

Run guts at an integer divisor (host rate ÷ N), reconstruct with
a Bezier curve. **Sane divisor = lush + cheaper** (the cost
reduction is real — buffer length and read rate both drop by N).
**Extreme divisor = cursed-retro + pitch-swoopable**. Not an A/B
fade — connective-tissue morph. Used in CreamCoat (sane regime)
and CrunchCoat (extreme regime). Cabinet already implements this
as its outer Bezier loop.

The Bezier reconstruction is the boundary that interpolates back
to host rate; it serves **double duty as both character and cost
reduction**. This is the AM335x-friendly pattern — explicitly
called out in the handoff as the lever to use when constrained.

### 3. Topology morph (feedforward ↔ feedback) as a macro

Interpolate the routing matrix between an ER (early-reflection)
character and a sustained-tail character. Make the endpoints
genuinely different voices (golden-ratio spacing for one,
prime spacing for the other). Single knob, two real characters.
This is the XYZ engine's X-axis (`planning/xyz-engine-design.md`).

### 4. Shared matrix, two read strategies

Two algorithms reading the same delay memory through different
tap/matrix patterns, blended by how each reads. Cheap (one
buffer), correlated-but-distinct hybrids. Useful when memory
is the bottleneck — get two characters for one buffer's worth
of state.

### 5. Cross-modulated feedback between two engines

Two FDNs whose feedback channels modulate each other **without
smoothing** (CloudCoat's mechanic lifted across two engines).
Range: two independent verbs → one coupled, semi-chaotic system.
**Dramatic ceiling.** This is the XYZ engine's Coupled regime
and the core risk in CloudCoat itself: cross-modulated unsmoothed
feedback is exactly the CloudSeed-trap shape; needs a Console
governor (mechanic 1) to keep runaway musical.

## How the existing units map

| Unit | Primary topology | Combination mechanics used |
|---|---|---|
| **Cabinet** (kWoodRoom, in progress) | 3×3 ER + 6×6 Householder FDN, prime-spaced | #2 (Bezier undersample, outer + inner) |
| **Network** (spreadsheet, shipped v2.5.0) | Parallel multi-tap | Custom glitch macro (G1-G8) — character via the macro, not via these mechanics |
| **CreamCoat** (port queued) | Bright ambience | #2 (Bezier undersample, sane regime) |
| **MV/MV2** (port maybe-queued) | Allpass chain | #1 (Console wrapper as governor) |
| **CloudCoat** (port queued, MEDIUM-HIGH risk) | 4×4 Householder of allpasses | #5 (cross-modulated unsmoothed feedback) — needs #1 to be safe |
| **XYZ engine** (design) | Single FDN with Z-mode topology switch | #1 + #2 + #5 — Y axis couples 1+2; Coupled regime adds 5 |
| **RotCoat** (design) | FDN inside reduced-rate domain + per-line tape-rot | #2 (taken further) + per-line band-split character |

## Practical guidance

When designing or porting a new reverb:

1. **Start with the simplest topology that gives the character.**
   AW's lesson: most of the variety comes from tuning, not
   complexity. A 6×6 Householder + Bezier shell (Cabinet) gives
   a fully credible room; don't add layers without a reason.
2. **Pick where the nonlinearity sits.** Pre-loop (drive into
   the verb), in-loop (feedback governor, mechanic #1), or
   post-loop (limit before output). The choice is the character.
3. **Use the reduced-rate domain as the primary CPU lever on
   AM335x.** Per `planning/airwindows-reverb-research.md`
   addendum, this is the structural way to fit a reverb on the
   chip. Cabinet does this already.
4. **Denormal floor in every feedback path.** Low divisors mean
   fewer samples flush the lines between hits, so denormals
   accumulate faster than at host rate.
5. **Clamp modulated delay reads** so the read index can't go
   negative (behind the write pointer → click). Applies to
   warble / chorus / pitch-modulated reads inside any feedback
   path.

## Source

`planning/refs/airwindows-port-handoff.md` §2 (combination
mechanics) + cross-referenced against
`planning/airwindows-reverb-research.md` (source-read of the
existing AW catalog) and the in-progress Cabinet port
(`planning/kwoodroom-port-plan.md`).
