#pragma once

// Network geometry generator — Phase 1.
//
// Owns a 2D virtual reflector field (kMaxNetworkTaps points in [-1, +1]
// space) and computes per-tap (delay, gainL, gainR) from a moving
// listener position.
//
// The listener moves on a unit circle parameterized by the `motion`
// macro (0..1 = one full revolution). Per active reflector, distance
// to the listener gives the tap delay (scaled by `size`); the y
// component of the unit vector from listener to reflector gives the
// L/R pan; 1/distance gives the per-tap gain.
//
// No libm trig — listener position uses Bhaskara polynomial sin/cos
// from network/trig_lut.h. Per-tap pan derivation is just `dy/dist`
// which equals sin(azimuth) without needing atan2.
//
// Header-only inline per feedback_no_out_of_line_virtuals.

#include "trig_lut.h"
#include <math.h>
#include <stdint.h>

namespace stolmine
{
  namespace network_geom
  {
    struct Reflector
    {
      float x, y;
    };

    // Max tap distance for a [-1, +1] field with listener on the unit
    // circle: sqrt((2)² + (2)²) ≈ 2.83. Use 3.0 with a small margin
    // so dist/kMaxDist ≤ 1 always.
    static const float kMaxDist = 3.0f;

    // Min distance clamp — reflector right at listener would give 1/r
    // → infinity. 0.05 caps gain at 20× max.
    static const float kMinDist = 0.05f;

    // Generate a deterministic reflector field from a uint32 seed.
    //
    // Phyllotaxis (golden-angle spiral) distribution — same pattern as
    // sunflower seeds, leaf arrangements, etc. Gives natural, near-
    // uniform spacing without the clumps and gaps of pure uniform
    // random. Each reflector at:
    //   theta = i × golden_angle + seedPhase  (+ small jitter)
    //   radius = sqrt((i + 0.5) / n)          (+ small jitter)
    // sqrt scaling makes density uniform per unit area.
    //
    // Seed shifts the starting angle and jitter — different seeds give
    // visibly distinct fields while preserving the phyllotaxis
    // structure. LCG matches the pattern in
    // mods/spreadsheet/visadhara/pmm.h.
    static inline void regenerateField(Reflector *reflectors, int n, uint32_t seed)
    {
      uint32_t state = seed ? seed : 0xDEADBEEFu;
      // Initial seedPhase from one LCG step.
      state = state * 1103515245u + 12345u;
      const float seedPhase =
        (float)((state >> 8) & 0xFFFFFFu) * (1.0f / 16777216.0f);

      // Golden angle in turns: (1 - 1/φ) where φ = (1+√5)/2.
      // Numerically: ≈ 0.3819660113 turns ≈ 137.5°.
      const float kGoldenAngle = 0.3819660113f;
      const float invN = 1.0f / (float)n;

      for (int i = 0; i < n; i++)
      {
        // Small angular jitter (±0.01 turns ≈ ±3.6°).
        state = state * 1103515245u + 12345u;
        const float angleJitter =
          ((float)((state >> 16) & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f) * 0.02f;
        float theta = (float)i * kGoldenAngle + seedPhase + angleJitter;
        // Wrap to [0, 1).
        theta -= (float)((int)theta);
        if (theta < 0.0f) theta += 1.0f;

        // Small radial jitter (±0.025 of unit field radius).
        state = state * 1103515245u + 12345u;
        const float radiusJitter =
          ((float)((state >> 16) & 0xFFFFu) * (1.0f / 65535.0f) - 0.5f) * 0.05f;
        const float baseRadius = sqrtf(((float)i + 0.5f) * invN);
        float radius = baseRadius + radiusJitter;
        if (radius < 0.0f) radius = 0.0f;
        if (radius > 1.0f) radius = 1.0f;

        reflectors[i].x = radius * network_trig::poly_cos(theta);
        reflectors[i].y = radius * network_trig::poly_sin(theta);
      }
    }

    // Per-block geometry computation. Writes into externally-owned
    // arrays sized for at least `kMaxTaps` entries:
    //   delayTarget[i] = per-tap delay in samples
    //   gainL[i], gainR[i] = per-tap L/R gains
    // Inactive taps (i >= activeTaps) get zeroed.
    //
    // density:    number of active taps (allocate-by-distance: closest
    //             `density` reflectors win)
    // sizeNorm:   0..1 field scale; multiplies the per-tap delay
    // motion:     0..1, listener phase around the unit circle
    // maxDelay:   buffer max-delay in samples; per-tap delay is in
    //             [0, sizeNorm * (maxDelay-1)]
    // gainScale:  global gain factor (typically 1.0 — kept for
    //             callers that want to renormalize)
    static inline void recomputeTaps(
      const Reflector *reflectors,
      int kMaxTaps,
      int activeTaps,
      float sizeNorm,
      float motion,
      int maxDelay,
      float gainScale,
      float *delayTarget,
      float *gainL,
      float *gainR)
    {
      // Listener position on circle of radius 1.3 (slightly outside
      // the unit-disc reflector field). Keeps minimum distance from
      // any reflector at ~0.3, preventing the close-pass amplitude
      // spikes that produced the 30-50Hz periodic-impulse artifacts
      // when the orbit radius matched the field radius. Listener still
      // moves spatially through the field, just doesn't pierce it.
      const float kListenerRadius = 1.3f;
      const float listenerCos = network_trig::poly_cos(motion);
      const float listenerSin = network_trig::poly_sin(motion);
      const float listenerX = listenerCos * kListenerRadius;
      const float listenerY = listenerSin * kListenerRadius;
      // Listener faces inward (toward origin). Right vector is
      // forward rotated -90° (CW): forward = -(cos, sin),
      // right = (-sin, cos). Pan is computed in this listener-
      // relative frame so stereo placement tracks listener
      // orientation as it orbits — without this, the "right ear"
      // would stay locked to world +Y regardless of orbit position
      // and produce systematically biased L/R distribution at
      // most motion phases.
      const float rightX = -listenerSin;
      const float rightY =  listenerCos;

      const float invMaxDist = 1.0f / kMaxDist;
      const float maxDelayF = (float)(maxDelay - 1);

      // Distance-based active-tap selection: simple "first N reflectors"
      // for Phase 1. Phase 2+ can replace with sorted-by-distance for
      // closest-first allocation.
      const int n = activeTaps < kMaxTaps ? activeTaps : kMaxTaps;

      for (int i = 0; i < n; i++)
      {
        const float dx = reflectors[i].x - listenerX;
        const float dy = reflectors[i].y - listenerY;
        const float dist2 = dx * dx + dy * dy;
        // sqrt of dist². Block-rate; Cortex-A8 VFP sqrt is fine.
        const float dist = sqrtf(dist2);
        const float distClamped = dist < kMinDist ? kMinDist : dist;

        // Delay: normalize distance to [0, 1], scale by size, scale
        // to delay buffer.
        float distNorm = dist * invMaxDist;
        if (distNorm > 1.0f) distNorm = 1.0f;
        delayTarget[i] = sizeNorm * distNorm * maxDelayF;

        // Constant per-tap magnitude (distance-independent). Schroeder/
        // Dattorro pattern — uniform tap gains, spatial illusion comes
        // from delay distribution + stereo placement only. Eliminates
        // the close-pass amplitude modulation that was the source of
        // the periodic-impulse train at walker traversal frequency:
        // total per-tap energy (gainL + gainR) = 0.5 is invariant
        // under listener motion, so mono output amplitude is constant.
        // Pan (below) still tracks azimuth, so stereo direction still
        // moves with the listener.
        const float gain = 0.5f * gainScale;

        // Pan: dot product of listener-relative reflector direction
        // with listener's right vector, normalized by distance.
        // Equivalent to sin(angle off forward axis) in the
        // listener's frame. Tracks listener orientation as the
        // walker orbits, so L/R distribution stays balanced.
        const float invDist = network_trig::reciprocal(distClamped);
        float pan = (dx * rightX + dy * rightY) * invDist;
        if (pan > 1.0f) pan = 1.0f;
        if (pan < -1.0f) pan = -1.0f;

        // Constant-power-ish L/R split. Linear (not equal-power) for
        // simplicity; Phase 1 polish can swap to sqrt curves if the
        // pan sweep feels phase-y.
        gainL[i] = gain * 0.5f * (1.0f - pan);
        gainR[i] = gain * 0.5f * (1.0f + pan);
      }

      // Zero inactive taps.
      for (int i = n; i < kMaxTaps; i++)
      {
        delayTarget[i] = 0.0f;
        gainL[i] = 0.0f;
        gainR[i] = 0.0f;
      }
    }
  }
}
