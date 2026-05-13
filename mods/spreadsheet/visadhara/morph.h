#pragma once

// Wave-shape morph helper: continuous blend through sin → tri → saw → sq
// per the BIA Morph control. Position 0..1 maps:
//   0.000 = sine
//   0.333 = triangle
//   0.667 = saw
//   1.000 = square
// Linear interpolation between adjacent anchor shapes.
//
// Implementation: analytic per-shape evaluation. Sine via polynomial
// approximation (no libm trig — feedback_package_trig_lut). All shapes
// computed per-sample, crossfaded by morph position.
//
// CPU note: 4 shape evals per voice per sample × 6 voices = 24 evals/sample.
// At 48 kHz that's ~1.15M shape ops/sec. Acceptable for v1; if profile
// shows hotspot, swap for 4×256 LUT gather (matches JF CURVE pattern).

#include <math.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#else
#include "../jf/neon_shim.h"
#endif

namespace stolmine
{
  namespace visadhara_morph
  {
    // Polynomial sine: Bhaskara approximation, accurate to ~1.5% over half
    // cycle. Input phase in [0, 1) (one cycle); output in [-1, +1].
    static inline float poly_sin(float phase)
    {
      // Map phase [0, 1) to t [-1, +1] then sin(πt).
      const float t = 2.0f * phase - 1.0f;
      const float t2 = t * t;
      // Polynomial approximation to sin(π*t)/(π*t) * πt = sin(πt).
      // Bhaskara I: sin(πt) ≈ 16t(1-|t|) / (5 - 4|t|(1-|t|))
      const float at = t < 0.0f ? -t : t;
      const float p = at * (1.0f - at);
      // Rearranged for the [-1,1] symmetry of sine:
      // sin curve is anti-symmetric about t=0
      const float n = 16.0f * p;
      const float d = 5.0f - 4.0f * p;
      const float mag = n / d;
      return t < 0.0f ? -mag : mag;
    }

    // Triangle: peaks at phase=0.25 (+1) and 0.75 (-1), zero crossings at
    // 0.0 and 0.5 — same phase alignment as sine. [-1, +1].
    static inline float tri(float phase)
    {
      // First half [0, 0.5): rises 0 → +1 → 0
      // Second half [0.5, 1): falls 0 → -1 → 0
      if (phase < 0.25f) return 4.0f * phase;
      if (phase < 0.75f) return 2.0f - 4.0f * phase;
      return 4.0f * phase - 4.0f;
    }

    // Saw: rises -1 → +1 across the cycle. To match sine's rising zero-crossing
    // at phase=0, offset so that saw also crosses zero at phase=0.5 (going
    // from -1 to +1 with the discontinuity).
    static inline float saw(float phase)
    {
      // Standard saw: 2*phase - 1 (rises -1 to +1 across [0, 1)).
      // Discontinuity at phase=1→0 wraparound.
      return 2.0f * phase - 1.0f;
    }

    // Square: ±1 stepped at phase 0.5. Phase-aligned with sine zero-crossings.
    static inline float sq(float phase)
    {
      return phase < 0.5f ? 1.0f : -1.0f;
    }

    // Per-shape RMS normalization. With unit peak amplitude:
    //   sine     RMS = 1/√2   ≈ 0.7071
    //   triangle RMS = 1/√3   ≈ 0.5774
    //   saw      RMS = 1/√3   ≈ 0.5774
    //   square   RMS = 1
    // Scale each shape so its RMS matches the lowest (triangle / saw).
    // Peak amplitudes end up ≤ 1 for all shapes. Equal-power crossfade
    // (compute_weights below) handles the cold-spot issue across
    // shape transitions; perceived-loudness disparity from harmonic
    // content (saw / sq sound brighter at equal RMS) is preserved as
    // a deliberate "harmonic build-up" character of the morph sweep.
    static const float kSinScale = 0.8165f;   // √(2/3) — bring sine down to tri RMS
    static const float kTriScale = 1.0f;
    static const float kSawScale = 1.0f;
    static const float kSqScale  = 0.5774f;   // 1/√3 — bring square down to tri RMS

    // Precomputed crossfade weights for one block of samples. Computed
    // once at block rate via compute_weights(); the per-sample sample_w()
    // call is then branchless (no per-sample dispatch).
    //
    // Weights use equal-power (sqrt) crossfade rather than linear to
    // keep RMS roughly constant across the morph sweep. The shapes
    // adjacent in the sweep are not all positively correlated — saw is
    // anti-correlated with both triangle and square — so a linear
    // crossfade produces audible RMS dips ("cold spots") at the
    // midpoints of those transitions. sqrt-equal-power crossfade
    // maintains RMS exactly for uncorrelated pairs and only mildly
    // overshoots for correlated pairs (sin↔tri).
    //
    // Three of the four weights are 0 in any given segment.
    struct Weights
    {
      float w_sin;
      float w_tri;
      float w_saw;
      float w_sq;
    };

    static inline Weights compute_weights(float morph)
    {
      if (morph < 0.0f) morph = 0.0f;
      if (morph > 1.0f) morph = 1.0f;

      Weights w;
      w.w_sin = 0.0f;
      w.w_tri = 0.0f;
      w.w_saw = 0.0f;
      w.w_sq  = 0.0f;

      if (morph < 0.333333f)
      {
        const float a = morph * 3.0f;
        w.w_sin = sqrtf(1.0f - a) * kSinScale;
        w.w_tri = sqrtf(a)        * kTriScale;
      }
      else if (morph < 0.666667f)
      {
        const float a = (morph - 0.333333f) * 3.0f;
        w.w_tri = sqrtf(1.0f - a) * kTriScale;
        w.w_saw = sqrtf(a)        * kSawScale;
      }
      else
      {
        const float a = (morph - 0.666667f) * 3.0f;
        w.w_saw = sqrtf(1.0f - a) * kSawScale;
        w.w_sq  = sqrtf(a)        * kSqScale;
      }
      return w;
    }

    // Per-sample evaluation against precomputed weights. Branchless —
    // computes all four shapes and weights them. CPU cost is roughly 2×
    // the segmented dispatch but the per-sample loop is straight-line
    // safe per feedback_runtime_branched_dsp_dispatch.
    static inline float sample_w(float phase, const Weights &w)
    {
      const float s_sin = poly_sin(phase);
      const float s_tri = tri(phase);
      const float s_saw = saw(phase);
      const float s_sq  = sq(phase);
      return s_sin * w.w_sin
           + s_tri * w.w_tri
           + s_saw * w.w_saw
           + s_sq  * w.w_sq;
    }

    // Legacy linear-crossfade entry point (Phase 1-4). Kept for
    // compatibility while migration to sample_w / compute_weights
    // proceeds. Prefer sample_w going forward.
    static inline float sample(float phase, float morph)
    {
      const Weights w = compute_weights(morph);
      return sample_w(phase, w);
    }

    // ---- NEON 4-lane vectorized sample_w ----
    //
    // Evaluates morph for 4 phases in parallel, returning a 4-lane
    // float32x4_t. Inlined so the four shape evaluators (poly_sin, tri,
    // saw, sq) and four weight broadcasts stay in NEON registers — no
    // function-call boundary that could trigger live-across-call register
    // spills per feedback_neon_hint_surfaces.
    //
    // Branchless via vbslq_f32 masks for the piecewise shape functions
    // (tri, sq). Bhaskara sine uses reciprocal-estimate divide; ~10-bit
    // precision on hardware is well below the polynomial's ~1.5% error.

    __attribute__((always_inline))
    static inline float32x4_t poly_sin_4(float32x4_t phase)
    {
      // t = 2*phase - 1, then sin(πt) via Bhaskara.
      const float32x4_t two   = vdupq_n_f32(2.0f);
      const float32x4_t one   = vdupq_n_f32(1.0f);
      const float32x4_t four  = vdupq_n_f32(4.0f);
      const float32x4_t five  = vdupq_n_f32(5.0f);
      const float32x4_t sixteen = vdupq_n_f32(16.0f);
      const float32x4_t zero  = vdupq_n_f32(0.0f);

      float32x4_t t = vsubq_f32(vmulq_f32(phase, two), one);
      float32x4_t at = vabsq_f32(t);
      float32x4_t oneMinusAt = vsubq_f32(one, at);
      float32x4_t p = vmulq_f32(at, oneMinusAt);            // |t|*(1-|t|)
      float32x4_t num = vmulq_f32(sixteen, p);              // 16*p
      float32x4_t den = vsubq_f32(five, vmulq_f32(four, p));// 5 - 4*p
      float32x4_t mag = vmulq_f32(num, vrecpeq_f32(den));   // num/den
      // Sign: if t<0 negate.
      uint32x4_t negMask = vcltq_f32(t, zero);
      float32x4_t magNeg = vnegq_f32(mag);
      return vbslq_f32(negMask, magNeg, mag);
    }

    __attribute__((always_inline))
    static inline float32x4_t tri_4(float32x4_t phase)
    {
      // Piecewise:
      //   p < 0.25:        4*p
      //   p < 0.75:        2 - 4*p
      //   p >= 0.75:       4*p - 4
      const float32x4_t four  = vdupq_n_f32(4.0f);
      const float32x4_t two   = vdupq_n_f32(2.0f);
      const float32x4_t qtr   = vdupq_n_f32(0.25f);
      const float32x4_t three_qtr = vdupq_n_f32(0.75f);

      float32x4_t fourP = vmulq_f32(four, phase);           // 4*p
      float32x4_t v1 = fourP;                                // segment A
      float32x4_t v2 = vsubq_f32(two, fourP);                // 2 - 4*p, segment B
      float32x4_t v3 = vsubq_f32(fourP, four);               // 4*p - 4, segment C
      uint32x4_t m1 = vcltq_f32(phase, qtr);
      uint32x4_t m2 = vcltq_f32(phase, three_qtr);
      // if p < 0.25 -> v1, else if p < 0.75 -> v2, else v3
      return vbslq_f32(m1, v1, vbslq_f32(m2, v2, v3));
    }

    __attribute__((always_inline))
    static inline float32x4_t saw_4(float32x4_t phase)
    {
      // 2*phase - 1
      const float32x4_t two = vdupq_n_f32(2.0f);
      const float32x4_t one = vdupq_n_f32(1.0f);
      return vsubq_f32(vmulq_f32(phase, two), one);
    }

    __attribute__((always_inline))
    static inline float32x4_t sq_4(float32x4_t phase)
    {
      const float32x4_t half = vdupq_n_f32(0.5f);
      const float32x4_t one  = vdupq_n_f32(1.0f);
      const float32x4_t negOne = vdupq_n_f32(-1.0f);
      uint32x4_t mask = vcltq_f32(phase, half);
      return vbslq_f32(mask, one, negOne);
    }

    // Weighted blend of all four shapes for 4 phases in parallel.
    // Returns the per-lane morph output. Weights are broadcast inside
    // each shape's multiply so block-rate scalar weights don't have to
    // remain live across the four shape evaluations.
    //
    // always_inline is load-bearing: without it GCC's size heuristic
    // emits this as a real function call from Visadhara::process(),
    // creating a live-across-call NEON register surface that spills
    // quads to `[sp :64]` per feedback_neon_hint_surfaces. Inlining
    // eliminates the function-call boundary entirely so the compiler
    // register-allocates across the whole inner-loop body.
    __attribute__((always_inline))
    static inline float32x4_t sample_w_4(float32x4_t phase, const Weights &w)
    {
      float32x4_t acc = vmulq_f32(poly_sin_4(phase), vdupq_n_f32(w.w_sin));
      acc = vmlaq_f32(acc, tri_4(phase), vdupq_n_f32(w.w_tri));
      acc = vmlaq_f32(acc, saw_4(phase), vdupq_n_f32(w.w_saw));
      acc = vmlaq_f32(acc, sq_4(phase),  vdupq_n_f32(w.w_sq));
      return acc;
    }
  }
}
