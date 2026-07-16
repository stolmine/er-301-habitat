# Design note: gain compensation for Network (Fabula's makeup model, tap-weight basis)

Status: design note / not started. Ledger item `network-gain-compensation`.
Related: `diffusion-makeup-model-notate` (the portable-model write-up), Fabula's
shipped diffusion-makeup ([[project_fabula_am335x]]), Petrichor tap normalization
([[feedback_multitap_weighted_feedback]]).

Goal: bring Fabula's gain-compensation model to Network so its perceived loudness
stays stable as the spatial controls (Density, geometry, decay) move, without
pumping and without flattening the effect.

## The transferable principle (from Fabula)

Fabula's diffusion-makeup is `wetGain = mix * (1 + kDiff*Diffusion)`: a STATIC,
block-rate, param-driven scalar. The reusable idea is NOT that line, it is the
discipline: **the level change is a deterministic function of the controls, so
cancel it with a computed scalar, never a dynamic envelope-tracked AGC** (which
would pump the tail). That discipline transfers to Network verbatim. What
changes is the BASIS the scalar is computed from.

## Network's level drivers (grounded in Network.h)

Network is a 32-tap stereo field. Per tap: (delay, gainL, gainR) from distance +
azimuth. Params: Size (max tap delay), **Density (fraction of reflectors
active)**, Motion (listener orbit phase), **Connectivity (fraction of taps
recycling)** + **Decay (feedback gain scaler)**, Wet, InputLevel. Per-tap L/R
gains = azimuth panning. There is NO gain compensation today.

So level moves with:
- **Density / Size / geometry** -> the number of active taps and their gains,
  i.e. the active-tap weight sum. This is Network's "Diffusion" analog and the
  primary target.
- **Connectivity + Decay** -> a recirculating feedback loop; steady-state level
  ~ 1/(1-g). A separate term (see phasing).
- **Motion** -> re-pans energy L<->R at roughly constant total; should NOT change
  loudness (see stereo note).

## The basis: tap-weight energy, not a single knob

Fabula's single-knob makeup works because its loss is dominated by one control.
Network's level is a 32-way weighted sum, not a one-knob function, so the right
basis is Petrichor's tap normalization ([[feedback_multitap_weighted_feedback]],
`1/sqrt(N)`-style) rather than a scalar of one param:

- In the block-rate geometry loop that already sets gainL/gainR per tap,
  accumulate `E = sum(gainL_i^2 + gainR_i^2)` over ACTIVE taps.
- `makeup = sqrt(E_ref / max(E, eps))`, reference-normalized (E_ref captured at
  the default Density/geometry so makeup = 1 there), clamped to a sane range.

One computed scalar then tracks Density + geometry + tap-count together:
deterministic, block-rate, no pumping. Fabula's principle, vector-weight basis.

## The stereo trap (must get right)

Taps are azimuth-panned. Computing a PER-CHANNEL makeup and normalizing L and R
independently would FLATTEN the spatial image: as the listener orbits and energy
pans left, per-channel comp would boost the quiet right to match, erasing the
pan. So compute ONE makeup from the TOTAL L+R energy and apply the SAME scalar to
both channels. Orbiting re-pans at ~constant total -> makeup ~1 -> image
preserved; Density/geometry changes the total -> makeup compensates -> loudness
stable. Stabilize loudness, preserve balance.

## Judgment calls (where the real work is)

- **Partial, not total, compensation.** Fabula recovered a LOSS; Network's
  Density ADDS energy on purpose. Fully normalizing makes sweeping Density do
  nothing to loudness, which feels dead - the density swell is part of the
  effect. Tune the exponent below 1 (kill the gross jumps, keep some swell).
  This is the [[feedback_self_balancing_converges_to_bypass]] caution:
  over-compensation trends toward "nothing happens."
- **Feedback tail is a separate term.** Connectivity/Decay set a recirculating
  loop; its steady-state loudness is its own compensation problem (Fabula's
  Freeze-makeup kFreezeMakeup is the precedent). Decide whether to fold an
  f(Decay) term in or leave the feedback level as intended character.
- **Clamp hard at sparse Density** so a nearly-empty field does not request a
  huge boost.
- **Wet only.** Apply the makeup to the wet spatial field, not the dry.

## What it takes / phasing

1. **Phase 1 - direct-sum makeup (the high-value move).** Accumulate active-tap
   energy in the existing per-tap block loop (cheap: ~32 mul-adds + one
   sqrt/div per block, no per-sample cost). Reference-normalize, apply a common
   scalar to both wet channels, partial-strength exponent. Tune E_ref + exponent
   + clamp by ear plus an RMS sweep across Density/Size. This alone should fix
   the loudness jumps when sweeping the field.
2. **Phase 2 - feedback-tail compensation (optional).** Add an f(Decay,
   Connectivity) term for the recirculating loudness, modeled on Fabula's
   Freeze-makeup. Only if the tail loudness still swings after Phase 1.
3. **Feed back into `diffusion-makeup-model-notate`:** the "model" is one
   principle with two implementations - single-knob (Fabula) and tap-weight
   energy (Network/Petrichor). Document both.

## Verify

RMS/loudness sweep across Density and Size shows roughly constant perceived level
with the makeup on (vs stepping without it); Motion sweep shows the stereo image
intact (pan preserved, no loudness change); no pumping on transients; the density
swell still audible (not flattened). am335x: confirm the block-rate cost is
negligible (no per-sample additions).
