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

  } // namespace four
} // namespace jf
