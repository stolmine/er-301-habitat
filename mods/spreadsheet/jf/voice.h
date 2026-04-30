#pragma once

// JF — 4-lane NEON slope-engine primitives.
//
// Pattern adapted from tomf's er-301-custom-units/mods/polygon: float32x4_t
// IS the per-voice fan-out. One `Voice` instance handles 4 voices in
// parallel through NEON intrinsics. JF composes two `Voice` instances for
// its 6 voices (8 lanes total, 2 masked off).
//
// See mods/spreadsheet/jf/README.md for full attribution.

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#else
#include "neon_shim.h"
#endif

#include <math.h>
#include <od/config.h>

namespace jf
{
  namespace four
  {

    // ----- math helpers (vendored / adapted from polygon util) -----

    inline float32x4_t floor_f32(float32x4_t x)
    {
      // Truncate-then-cast pattern (polygon util::four::floor). Faster than
      // vrndmq_f32 on Cortex-A7/A8 and yields the same result for the
      // [0, +large] range we use for phase wrap.
      return vcvtq_f32_s32(vcvtq_s32_f32(x));
    }

    // Wrap any phase into [0, 1) with the +1 trick to handle negative input
    // (matters when TZFM lands in Phase 4 — bipolar FM signal can drive
    // phase delta below zero per sample).
    inline float32x4_t wrap_phase(float32x4_t x)
    {
      x = vsubq_f32(x, floor_f32(x));
      x = vaddq_f32(x, vdupq_n_f32(1.0f));
      x = vsubq_f32(x, floor_f32(x));
      return x;
    }

    inline float32x4_t fclamp(float32x4_t x, float32x4_t lo, float32x4_t hi)
    {
      return vminq_f32(hi, vmaxq_f32(lo, x));
    }

    inline float32x4_t fclamp_n(float32x4_t x, float lo, float hi)
    {
      return fclamp(x, vdupq_n_f32(lo), vdupq_n_f32(hi));
    }

    // Sum of all lanes (used by MIX combiner in Sound range).
    inline float sum_lanes(const float32x4_t v)
    {
      auto pair = vpadd_f32(vget_low_f32(v), vget_high_f32(v));
      pair = vpadd_f32(pair, pair);
      return vget_lane_f32(pair, 0);
    }

    // Convenience: build a 4-lane uint mask from 4 bools.
    // NB: the obvious "stack-local float v[4]; vld1q_f32(v)" pattern
    // generates `:64` alignment hints that trap on Cortex-A8 under -O3
    // -ffast-math (per feedback_neon_intrinsics_drumvoice). We use
    // vsetq_lane_* instead, which stays register-only on hardware.
    inline uint32x4_t make_mask(bool a, bool b, bool c, bool d)
    {
      uint32x4_t r = vdupq_n_u32(0);
      r = vsetq_lane_u32(a ? 0xFFFFFFFFu : 0u, r, 0);
      r = vsetq_lane_u32(b ? 0xFFFFFFFFu : 0u, r, 1);
      r = vsetq_lane_u32(c ? 0xFFFFFFFFu : 0u, r, 2);
      r = vsetq_lane_u32(d ? 0xFFFFFFFFu : 0u, r, 3);
      return r;
    }

    inline float32x4_t make_4(float a, float b, float c, float d)
    {
      float32x4_t r = vdupq_n_f32(0.0f);
      r = vsetq_lane_f32(a, r, 0);
      r = vsetq_lane_f32(b, r, 1);
      r = vsetq_lane_f32(c, r, 2);
      r = vsetq_lane_f32(d, r, 3);
      return r;
    }

    // ----- gate edge detection -----

    struct GateToTrigger
    {
      // Returns rising-edge mask (bits set where gate just went high).
      inline uint32x4_t process(uint32x4_t gate)
      {
        auto edge = vbicq_u32(gate, mPrev); // gate AND NOT prev
        mPrev = gate;
        return edge;
      }

      uint32x4_t mPrev = vdupq_n_u32(0);
    };

    // ----- 4-lane slope engine voice -----
    //
    // Mode is shared across all 4 lanes (it's a global JF unit setting).
    // The per-sample loop branches on mode at block boundary; lanes within
    // a block all run the same mode dispatch. Per-lane state (active /
    // sustaining / phase) is independent.

    enum Mode { kTransient = 1, kSustain = 2, kCycle = 3 };

    struct Voice
    {
      // 4 lanes of state.
      float32x4_t mPhase      = vdupq_n_f32(0.0f);
      uint32x4_t  mActive     = vdupq_n_u32(0);  // Transient: lane is mid-AR
      uint32x4_t  mSustaining = vdupq_n_u32(0);  // Sustain: lane gate-high
      GateToTrigger mGate;

      // Process one sample, returning the [0..1] slope phase per lane.
      // Sustain mode interprets phase as the sustain-trapezoid level
      // (rises while gate-high, falls while gate-low). Cycle/Transient
      // interpret phase as 0..1 monotonic; downstream waveshaper turns
      // phase into the actual audio signal (triangle/CURVE-shaped).
      inline float32x4_t process(
          float32x4_t inc,    // per-lane phase delta this sample
          uint32x4_t  gate,   // current gate per lane
          int         mode    // shared across lanes
      )
      {
        auto rising = mGate.process(gate);
        auto p = mPhase;

        if (mode == kCycle)
        {
          p = vaddq_f32(p, inc);
          p = wrap_phase(p);
          // Hard-sync: rising edge resets phase.
          p = vbslq_f32(rising, vdupq_n_f32(0.0f), p);
        }
        else if (mode == kTransient)
        {
          // Start AR cycle on rising edge IF lane not currently active.
          // mActive = mActive | (rising & ~mActive)
          auto startMask = vbicq_u32(rising, mActive);
          mActive = vorrq_u32(mActive, startMask);
          // Lanes starting fresh reset phase to 0.
          p = vbslq_f32(startMask, vdupq_n_f32(0.0f), p);

          // Advance only active lanes.
          auto incMasked = vbslq_f32(mActive, inc, vdupq_n_f32(0.0f));
          p = vaddq_f32(p, incMasked);

          // Lanes hitting 1.0 → reset phase, deactivate.
          auto done = vcgeq_f32(p, vdupq_n_f32(1.0f));
          p = vbslq_f32(done, vdupq_n_f32(0.0f), p);
          mActive = vbicq_u32(mActive, done);
        }
        else // kSustain
        {
          // Rising edge → start ascending. Falling edge (computed from
          // the current gate vs old prev) → start descending. We have
          // the rising edge already; falling = ~gate & old_prev. Since
          // mGate has consumed prev already, derive falling from the
          // current and rising via: falling = !gate & sustaining.
          // Simpler: keep an extra prev-gate latch here.
          //
          // Actually: rising edge sets sustaining; gate=low clears it.
          // This is correct because gate-high keeps sustaining=1 (set
          // by rising once and never cleared until gate goes low).
          mSustaining = vorrq_u32(mSustaining, rising);
          mSustaining = vandq_u32(mSustaining, gate);

          // Direction: +inc if sustaining, -inc if not.
          auto dir = vbslq_f32(mSustaining, inc, vnegq_f32(inc));
          p = vaddq_f32(p, dir);

          // Clamp to [0, 1].
          p = fclamp_n(p, 0.0f, 1.0f);
        }

        mPhase = p;
        return p;
      }

      // Force a per-lane reset (used externally if needed).
      inline void reset(uint32x4_t mask)
      {
        mPhase = vbslq_f32(mask, vdupq_n_f32(0.0f), mPhase);
        mActive = vbicq_u32(mActive, mask);
        mSustaining = vbicq_u32(mSustaining, mask);
      }
    };

    // ----- waveshape: phase → audio signal -----
    //
    // Phase 3a uses a fixed triangle for Cycle/Transient and linear ramp
    // for Sustain (matching the scalar Phase 2 behavior). RAMP asymmetry
    // arrives in Phase 3b, CURVE morph in Phase 3c.

    inline float32x4_t triangle(float32x4_t phase)
    {
      // p < 0.5  →  2p
      // p >= 0.5 →  2 - 2p
      auto half = vdupq_n_f32(0.5f);
      auto two = vdupq_n_f32(2.0f);
      auto rising = vmulq_f32(phase, two);
      auto falling = vsubq_f32(two, vmulq_f32(phase, two));
      auto risingMask = vcltq_f32(phase, half);
      return vbslq_f32(risingMask, rising, falling);
    }

    // RAMP-shaped slope: per-tech-map "duty cycle" knob.
    //   threshold T in (0,1) is the phase fraction spent rising.
    //     T < 0.5: fall-heavy (saw down territory).
    //     T = 0.5: symmetric triangle.
    //     T > 0.5: rise-heavy (ramp up territory).
    // T is clamped externally to keep invT and inv(1-T) finite.
    // T is shared across lanes (RAMP is a global JF control), so the
    // reciprocals come in pre-broadcast as scalar arguments — saves
    // per-sample divides.
    inline float32x4_t ramp_triangle(
        float32x4_t phase,
        float32x4_t thresholdV,
        float32x4_t invThresholdV,
        float32x4_t invOneMinusThresholdV
    )
    {
      auto one = vdupq_n_f32(1.0f);
      auto rising = vmulq_f32(phase, invThresholdV);
      auto falling = vmulq_f32(vsubq_f32(one, phase), invOneMinusThresholdV);
      auto risingMask = vcltq_f32(phase, thresholdV);
      // Clamp to [0,1] so the saw-edge (near-zero division side) doesn't
      // briefly overshoot due to fp rounding.
      auto shaped = vbslq_f32(risingMask, rising, falling);
      return fclamp_n(shaped, 0.0f, 1.0f);
    }

    // ----- CURVE LUT -----
    //
    // 5 anchor shapes × 256 entries each. Tech map:
    //   CURVE full CCW  → rect  (instant rise, instant fall — zero-time
    //                            slopes, RAMP becomes a PWM control)
    //   CURVE CCW half  → log   (fast initial rate, slowing toward end)
    //   CURVE noon      → lin   (linear ramp; matches Phase 3b behavior)
    //   CURVE CW half   → exp   (slow initial rate, accelerating toward end)
    //   CURVE full CW   → sine  (S-curve, slow start + slow end)
    //
    // Input to the LUT is the 0..1 "slope progress" coming out of
    // ramp_triangle (already represents the current rise- or fall-stage
    // position normalized to [0, 1] over its own stage). LUT bends that
    // monotonic 0..1 into the chosen shape.
    //
    // Stored as five 256-entry tables. Lookup is per-lane scalar gather
    // (vsetq_lane after vgetq_lane) — NEON has no native gather; we keep
    // the gather scalar to stay register-only and avoid the stack-array
    // alignment-hint trap (per feedback_neon_intrinsics_drumvoice).

    static constexpr int kCurveLutSize = 256;
    static constexpr int kCurveLutAnchors = 5;

    struct CurveLut
    {
      float lut[kCurveLutAnchors][kCurveLutSize];

      // Initialize once at construction. Cheap (5*256 = 1280 ops).
      CurveLut()
      {
        for (int i = 0; i < kCurveLutSize; i++)
        {
          float u = (float)i / (float)(kCurveLutSize - 1); // 0..1

          // 0: rect — step at u >= 0.5 (RAMP would shift this; here
          //          we use the "stage-position" interpretation: full
          //          height for the whole rise stage). Tech map: rect
          //          slope rises instantly to peak and stays there.
          //          Implement as step: 0 below midpoint, 1 above.
          //          (For PWM-from-RAMP behavior, the slope phase
          //          itself shifts the duty; rect just maps stage
          //          progress to a step.)
          lut[0][i] = (u < 0.5f) ? 0.0f : 1.0f;

          // 1: log — fast initial rate, slowing. Use 1 - (1-u)^k for k>1.
          {
            float v = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
            lut[1][i] = v;
          }

          // 2: lin — passthrough.
          lut[2][i] = u;

          // 3: exp — slow initial rate, accelerating. u^k for k>1.
          lut[3][i] = u * u * u;

          // 4: sine — S-curve. (1 - cos(pi*u))/2 maps 0→0, 1→1 with
          //          slow ends. Per tech map "sinusoidal".
          lut[4][i] = 0.5f * (1.0f - cosf((float)M_PI * u));
        }
      }

      // Per-lane lookup with linear interpolation across LUT index AND
      // across anchor shapes. Returns the curved 0..1 stage value.
      //
      //   shape0/shape1 — anchor indices to blend (e.g. lin→exp at CURVE > 0)
      //   morph         — [0,1] blend between shape0 and shape1 (broadcast)
      //
      // Per-lane scalar gather: NEON has no native gather, and a stack-
      // array gather would re-introduce :64 hints. vget/vset lane stays
      // register-only.
      inline float32x4_t lookup(
          float32x4_t progress,
          int shape0,
          int shape1,
          float32x4_t morph
      ) const
      {
        progress = fclamp_n(progress, 0.0f, 1.0f);
        const float scale = (float)(kCurveLutSize - 1);
        const auto idxF = vmulq_f32(progress, vdupq_n_f32(scale));

        const float *t0 = lut[shape0];
        const float *t1 = lut[shape1];

        // Per-lane gather + intra-LUT linear interp + cross-anchor lerp.
        float32x4_t r = vdupq_n_f32(0.0f);
        for (int lane = 0; lane < 4; lane++)
        {
          float f;
          switch (lane)
          {
            case 0: f = vgetq_lane_f32(idxF, 0); break;
            case 1: f = vgetq_lane_f32(idxF, 1); break;
            case 2: f = vgetq_lane_f32(idxF, 2); break;
            default: f = vgetq_lane_f32(idxF, 3); break;
          }
          int i0 = (int)f;
          if (i0 < 0) i0 = 0;
          if (i0 > kCurveLutSize - 1) i0 = kCurveLutSize - 1;
          int i1 = (i0 < kCurveLutSize - 1) ? (i0 + 1) : i0;
          float frac = f - (float)i0;

          float a0 = t0[i0] + (t0[i1] - t0[i0]) * frac;
          float a1 = t1[i0] + (t1[i1] - t1[i0]) * frac;

          float m;
          switch (lane)
          {
            case 0: m = vgetq_lane_f32(morph, 0); break;
            case 1: m = vgetq_lane_f32(morph, 1); break;
            case 2: m = vgetq_lane_f32(morph, 2); break;
            default: m = vgetq_lane_f32(morph, 3); break;
          }
          float v = a0 + (a1 - a0) * m;

          switch (lane)
          {
            case 0: r = vsetq_lane_f32(v, r, 0); break;
            case 1: r = vsetq_lane_f32(v, r, 1); break;
            case 2: r = vsetq_lane_f32(v, r, 2); break;
            default: r = vsetq_lane_f32(v, r, 3); break;
          }
        }
        return r;
      }
    };

  } // namespace four
} // namespace jf
