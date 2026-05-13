#pragma once

// 3-op closed-loop phase-modulation operator for Visadhara Metal mode.
// Topology per the BIA technical manual: "A pair of 3-operator
// phase-modulated oscillators for producing metallic, noisy, and alien
// sounds." Each pair is one 3-op voice; Visadhara runs two pairs in
// parallel and sums their outputs.
//
// Topology:
//   op1: phase += inc1; out1 = sin(phase + fb * lastOut1)        (self-fb)
//   op2: phase += inc2; out2 = sin(phase + mod12 * lastOut1)
//   op3: phase += inc3; out3 = sin(phase + mod23 * lastOut2)
//   voice_out = out3
//
// Header-only inline per feedback_no_out_of_line_virtuals. Reuses
// visadhara_morph::poly_sin (Bhaskara polynomial) — no libm trig per
// feedback_package_trig_lut.

#include "morph.h"
#include <math.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#else
#include "../jf/neon_shim.h"
#endif

namespace stolmine
{
  namespace visadhara_pmm
  {
    struct Voice
    {
      float phase[3];
      float lastOut[3];
    };

    static inline void reset(Voice &v)
    {
      v.phase[0] = 0.0f;
      v.phase[1] = 0.0f;
      v.phase[2] = 0.0f;
      v.lastOut[0] = 0.0f;
      v.lastOut[1] = 0.0f;
      v.lastOut[2] = 0.0f;
    }

    // Wrap phase + modulation back to [0, 1) for poly_sin. Handles
    // negative arguments (modulation can drive phase negative).
    static inline float wrap01(float p)
    {
      return p - floorf(p);
    }

    // Run one sample of the 3-op chain. Phase increments inc1/2/3 are
    // per-sample (cycles per sample = freq / sampleRate). fb is op1
    // self-feedback; mod12 modulates op2 by op1; mod23 modulates op3 by
    // op2. morphW is precomputed block-rate equal-power crossfade
    // weights for the per-operator waveshape (sine→tri→saw→sq). Output
    // is op3.
    static inline float tick(Voice &v,
                              float inc1, float inc2, float inc3,
                              float fb, float mod12, float mod23,
                              const visadhara_morph::Weights &morphW)
    {
      // op1 — self-feedback
      v.phase[0] += inc1;
      if (v.phase[0] >= 1.0f) v.phase[0] -= floorf(v.phase[0]);
      const float p1 = wrap01(v.phase[0] + fb * v.lastOut[0]);
      v.lastOut[0] = visadhara_morph::sample_w(p1, morphW);

      // op2 — modulated by op1
      v.phase[1] += inc2;
      if (v.phase[1] >= 1.0f) v.phase[1] -= floorf(v.phase[1]);
      const float p2 = wrap01(v.phase[1] + mod12 * v.lastOut[0]);
      v.lastOut[1] = visadhara_morph::sample_w(p2, morphW);

      // op3 — modulated by op2
      v.phase[2] += inc3;
      if (v.phase[2] >= 1.0f) v.phase[2] -= floorf(v.phase[2]);
      const float p3 = wrap01(v.phase[2] + mod23 * v.lastOut[1]);
      v.lastOut[2] = visadhara_morph::sample_w(p3, morphW);

      return v.lastOut[2];
    }

    // ---- NEON 2-pair-parallel tick ----
    //
    // Runs two 3-op PMM chains in parallel via NEON 2-lane (across
    // a 4-wide quad, lanes 0/1 carry pair A/B, lanes 2/3 padded with
    // zero state). The three ops within each chain remain sequential
    // (op2 reads op1's current lastOut; op3 reads op2's) — the
    // parallelism is purely across pairs.
    //
    // State storage layout matches the call site (Visadhara.h):
    //   pmmPhase[op_idx][pair_lane], lanes 0/1 used
    //   pmmLastOut[op_idx][pair_lane], lanes 0/1 used
    //
    // Coefficient arrays are block-rate-packed by caller:
    //   pmmIncPacked[op][pair_lane]: op-k phase increment
    //   pmmFbModPacked[op][pair_lane]: op0=self-fb, op1=mod12, op2=mod23

    // Wrap to [0, 1) for arbitrary p (positive or negative). Handles
    // negative inputs via truncating cast + adjust, since Cortex-A8
    // has no vrndmq_f32 (that's Cortex-A7+). PMM phase + fb*lastOut
    // can go negative when fb dominates, so the full wrap is needed.
    __attribute__((always_inline))
    static inline float32x4_t wrap01_4(float32x4_t p)
    {
      int32x4_t pi = vcvtq_s32_f32(p);          // truncate toward zero
      float32x4_t pf = vcvtq_f32_s32(pi);
      // For negative non-integer p, truncate gave us ceiling-toward-
      // zero, so floor(p) = pf - 1. For non-negative or integer p,
      // floor(p) = pf.
      uint32x4_t mask = vcltq_f32(p, pf);
      float32x4_t adj = vbslq_f32(mask, vdupq_n_f32(1.0f),
                                         vdupq_n_f32(0.0f));
      float32x4_t floor_p = vsubq_f32(pf, adj);
      return vsubq_f32(p, floor_p);
    }

    // noinline is load-bearing: keeping tick2 as a real function call
    // gives it its own NEON register window, separated from the caller's
    // voice-bus pressure. Inlining produced 5+ [sp :64] quad-spill hints
    // because the combined live-quad set exceeded Cortex-A8's 16-register
    // budget. The function-call cost (~10 cycles × 96k calls/sec) is
    // trivial vs the safety win.
    //
    // Weights are passed as individual floats (not Weights&). Reference
    // indirection produced a quad load from the struct address with a
    // [reg :64] alignment hint; the struct is on caller's stack frame
    // and may not be 8-byte aligned. Unpacked args avoid the trap surface
    // entirely — they pass via stack slots that GCC manages.
    __attribute__((noinline))
    static void tick2(
        float pmmPhase[3][4],
        float pmmLastOut[3][4],
        const float pmmIncPacked[3][4],
        const float pmmFbModPacked[3][4],
        float w_sin, float w_tri, float w_saw, float w_sq,
        float &outA, float &outB)
    {
      const float32x4_t oneV  = vdupq_n_f32(1.0f);
      const float32x4_t zeroV = vdupq_n_f32(0.0f);
      // Build the local Weights struct from the scalar args so we can
      // pass it through to sample_w_4. The struct lives on tick2's
      // stack frame (sp-aligned per AAPCS), and sample_w_4 broadcasts
      // each field via vdupq_n_f32 inside its body — no reference-
      // dereference quad load surface.
      visadhara_morph::Weights morphW;
      morphW.w_sin = w_sin;
      morphW.w_tri = w_tri;
      morphW.w_saw = w_saw;
      morphW.w_sq  = w_sq;

      // -- Op 0: self-feedback. Mod source is lastOut[0] from previous
      //    sample (read-before-write).
      {
        float32x4_t p    = vld1q_f32(&pmmPhase[0][0]);
        float32x4_t inc  = vld1q_f32(&pmmIncPacked[0][0]);
        float32x4_t fb   = vld1q_f32(&pmmFbModPacked[0][0]);
        float32x4_t prev = vld1q_f32(&pmmLastOut[0][0]);

        p = vaddq_f32(p, inc);
        uint32x4_t wmask = vcgeq_f32(p, oneV);
        p = vsubq_f32(p, vbslq_f32(wmask, oneV, zeroV));
        vst1q_f32(&pmmPhase[0][0], p);

        float32x4_t mp = wrap01_4(vmlaq_f32(p, fb, prev));
        vst1q_f32(&pmmLastOut[0][0],
                  visadhara_morph::sample_w_4(mp, morphW));
      }

      // -- Op 1: modulated by op 0's just-written lastOut[0].
      {
        float32x4_t p   = vld1q_f32(&pmmPhase[1][0]);
        float32x4_t inc = vld1q_f32(&pmmIncPacked[1][0]);
        float32x4_t m12 = vld1q_f32(&pmmFbModPacked[1][0]);
        float32x4_t src = vld1q_f32(&pmmLastOut[0][0]);

        p = vaddq_f32(p, inc);
        uint32x4_t wmask = vcgeq_f32(p, oneV);
        p = vsubq_f32(p, vbslq_f32(wmask, oneV, zeroV));
        vst1q_f32(&pmmPhase[1][0], p);

        float32x4_t mp = wrap01_4(vmlaq_f32(p, m12, src));
        vst1q_f32(&pmmLastOut[1][0],
                  visadhara_morph::sample_w_4(mp, morphW));
      }

      // -- Op 2: modulated by op 1's just-written lastOut[1].
      //    Output extracted from lanes 0/1.
      {
        float32x4_t p   = vld1q_f32(&pmmPhase[2][0]);
        float32x4_t inc = vld1q_f32(&pmmIncPacked[2][0]);
        float32x4_t m23 = vld1q_f32(&pmmFbModPacked[2][0]);
        float32x4_t src = vld1q_f32(&pmmLastOut[1][0]);

        p = vaddq_f32(p, inc);
        uint32x4_t wmask = vcgeq_f32(p, oneV);
        p = vsubq_f32(p, vbslq_f32(wmask, oneV, zeroV));
        vst1q_f32(&pmmPhase[2][0], p);

        float32x4_t mp = wrap01_4(vmlaq_f32(p, m23, src));
        float32x4_t out2 = visadhara_morph::sample_w_4(mp, morphW);
        vst1q_f32(&pmmLastOut[2][0], out2);

        outA = vgetq_lane_f32(out2, 0);
        outB = vgetq_lane_f32(out2, 1);
      }
    }
  }
}
