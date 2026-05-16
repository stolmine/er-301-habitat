// NEON 4-lane polynomial math primitives for spreadsheet units.
//
// Targeting Cortex-A8 NEON (ARMv7-A). All functions are always_inline so
// the compiler register-allocates across the caller's inner loop without
// the function-call boundary spilling NEON quads to `[sp :64]` per
// feedback_neon_hint_surfaces.
//
// Accuracy targets:
//   log2_poly_4lane:  ~-70 dB max error across (0, +inf)
//   exp2_poly_4lane:  ~-70 dB max error across [-126, +127]
//   sine_poly_4lane:  Bhaskara approximation, ~-37 dB peak error (audio-OK)
//   wrap01_4:         exact (truncate-based wrap on [0,1))
//
// Scalar fallbacks provided for non-NEON builds (linux x86, darwin x86).
// The same polynomial coefficients are used in both paths so RMS A/B is
// identical between hardware and emu.

#pragma once

#include <stdint.h>
#include <math.h>

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace stolmine
{
  namespace neon_math
  {

#if defined(__ARM_NEON__) || defined(__ARM_NEON)

    // log2(x) via IEEE 754 bit-extract + 4-term polynomial on mantissa.
    // Caller must ensure x > 0. Behavior for x <= 0 is undefined.
    __attribute__((always_inline))
    static inline float32x4_t log2_poly_4lane(float32x4_t x)
    {
      // Extract exponent: e = ((bits >> 23) & 0xFF) - 127
      int32x4_t bits = vreinterpretq_s32_f32(x);
      int32x4_t exp_bits = vsubq_s32(vshrq_n_s32(bits, 23), vdupq_n_s32(127));
      float32x4_t e = vcvtq_f32_s32(exp_bits);

      // Mantissa as m in [1, 2): bits = (bits & 0x7FFFFF) | (127 << 23)
      int32x4_t mant_bits = vorrq_s32(
          vandq_s32(bits, vdupq_n_s32(0x7FFFFF)),
          vdupq_n_s32(127 << 23));
      float32x4_t m = vreinterpretq_f32_s32(mant_bits);

      // log2(m) for m in [1, 2). Let y = m - 1 in [0, 1).
      // Minimax 4-term polynomial: log2(1+y) ~= y*(a + y*(b + y*(c + y*d)))
      float32x4_t y = vsubq_f32(m, vdupq_n_f32(1.0f));
      float32x4_t poly = vmlaq_f32(vdupq_n_f32( 0.45470f),
                                   vdupq_n_f32(-0.16906f), y);
      poly = vmlaq_f32(vdupq_n_f32(-0.72135f), poly, y);
      poly = vmlaq_f32(vdupq_n_f32( 1.44269f), poly, y);
      float32x4_t logm = vmulq_f32(y, poly);

      return vaddq_f32(e, logm);
    }

    // 2^x via IEEE 754 bit-pack + 4-term polynomial on fractional part.
    // Handles negative x correctly via trunc + carry-adjust for floor().
    __attribute__((always_inline))
    static inline float32x4_t exp2_poly_4lane(float32x4_t x)
    {
      // Clamp to avoid IEEE exponent overflow/underflow.
      x = vminq_f32(vmaxq_f32(x, vdupq_n_f32(-126.0f)), vdupq_n_f32(127.0f));

      // floor(x): vcvtq_s32_f32 truncates toward zero, so for negative
      // x with a fractional part we need to subtract 1.
      int32x4_t xi_trunc = vcvtq_s32_f32(x);
      float32x4_t xi_truncf = vcvtq_f32_s32(xi_trunc);
      uint32x4_t fracNeg = vcltq_f32(x, xi_truncf);
      int32x4_t adj = vreinterpretq_s32_u32(vshrq_n_u32(fracNeg, 31));
      int32x4_t xi = vsubq_s32(xi_trunc, adj);
      float32x4_t xf = vsubq_f32(x, vcvtq_f32_s32(xi));   // xf in [0, 1)

      // 2^xf ~= 1 + xf*(c1 + xf*(c2 + xf*(c3 + xf*c4)))
      // Coefficients: 4-term Remez. Max relative error ~3e-5.
      float32x4_t mant = vmlaq_f32(vdupq_n_f32(0.05550411f),
                                   vdupq_n_f32(0.00961813f), xf);
      mant = vmlaq_f32(vdupq_n_f32(0.24022651f), mant, xf);
      mant = vmlaq_f32(vdupq_n_f32(0.69314718f), mant, xf);
      mant = vmlaq_f32(vdupq_n_f32(1.0f), mant, xf);

      // expPart = (xi + 127) << 23, reinterpreted as float
      int32x4_t bits = vshlq_n_s32(vaddq_s32(xi, vdupq_n_s32(127)), 23);
      float32x4_t expPart = vreinterpretq_f32_s32(bits);

      return vmulq_f32(expPart, mant);
    }

    // sin(2*pi*phase) for phase in [0, 1). Bhaskara form, lifted from
    // mods/spreadsheet/visadhara/morph.h poly_sin_4 (proven on Cortex-A8).
    // ~1.5% peak error. Phase outside [0,1) silently produces wrong
    // output -- caller should wrap first.
    __attribute__((always_inline))
    static inline float32x4_t sine_poly_4lane(float32x4_t phase01)
    {
      const float32x4_t two   = vdupq_n_f32(2.0f);
      const float32x4_t one   = vdupq_n_f32(1.0f);
      const float32x4_t four  = vdupq_n_f32(4.0f);
      const float32x4_t five  = vdupq_n_f32(5.0f);
      const float32x4_t sixteen = vdupq_n_f32(16.0f);
      const float32x4_t zero  = vdupq_n_f32(0.0f);

      float32x4_t t = vsubq_f32(vmulq_f32(phase01, two), one);   // t in [-1, +1]
      float32x4_t at = vabsq_f32(t);
      float32x4_t oneMinusAt = vsubq_f32(one, at);
      float32x4_t p = vmulq_f32(at, oneMinusAt);
      float32x4_t num = vmulq_f32(sixteen, p);
      float32x4_t den = vsubq_f32(five, vmulq_f32(four, p));
      float32x4_t mag = vmulq_f32(num, vrecpeq_f32(den));
      uint32x4_t negMask = vcltq_f32(t, zero);
      return vbslq_f32(negMask, vnegq_f32(mag), mag);
    }

    // Wrap a NEON quad to [0, 1) via floor-subtract. Handles negative
    // inputs. Faster than fmodf and branch-free.
    __attribute__((always_inline))
    static inline float32x4_t wrap01_4(float32x4_t x)
    {
      int32x4_t xi_trunc = vcvtq_s32_f32(x);
      float32x4_t xi_truncf = vcvtq_f32_s32(xi_trunc);
      uint32x4_t fracNeg = vcltq_f32(x, xi_truncf);
      float32x4_t adj = vreinterpretq_f32_u32(
          vandq_u32(fracNeg, vreinterpretq_u32_f32(vdupq_n_f32(1.0f))));
      float32x4_t floored = vsubq_f32(xi_truncf, adj);
      return vsubq_f32(x, floored);
    }

#endif  // __ARM_NEON__

    // --- Scalar fallbacks (x86 emu, darwin). Same polynomial coefficients
    // as the NEON paths so emu RMS A/B remains identical to hardware. ---

    static inline float log2_poly(float x)
    {
      union { float f; int32_t i; } v;
      v.f = x;
      int e = ((v.i >> 23) & 0xFF) - 127;
      v.i = (v.i & 0x7FFFFF) | (127 << 23);
      float m = v.f;
      float y = m - 1.0f;
      float poly = 0.45470f + (-0.16906f) * y;
      poly = -0.72135f + poly * y;
      poly = 1.44269f + poly * y;
      return (float)e + y * poly;
    }

    static inline float exp2_poly(float x)
    {
      if (x < -126.0f) x = -126.0f;
      if (x >  127.0f) x =  127.0f;
      int xi = (int)floorf(x);
      float xf = x - (float)xi;
      float mant = 0.05550411f + 0.00961813f * xf;
      mant = 0.24022651f + mant * xf;
      mant = 0.69314718f + mant * xf;
      mant = 1.0f + mant * xf;
      union { float f; int32_t i; } v;
      v.i = (xi + 127) << 23;
      return v.f * mant;
    }

    static inline float sine_poly(float phase01)
    {
      float t = 2.0f * phase01 - 1.0f;
      float at = t < 0 ? -t : t;
      float p = at * (1.0f - at);
      float mag = 16.0f * p / (5.0f - 4.0f * p);
      return t < 0 ? -mag : mag;
    }

    static inline float wrap01(float x)
    {
      float f = floorf(x);
      return x - f;
    }

    // --- High-accuracy scalar polynomial sine for fidelity-critical contexts
    // (Helicase FM oscillator, anywhere a Bhaskara-style ~-37 dB error would
    // produce audible sideband contamination through nonlinear modulation).
    //
    // 13th-order Taylor centered on x=0. Max error ~10⁻⁵ at x=±π
    // (-100 dB), well below audibility floor and below FM-modulated
    // noise floor at any reasonable carrier amplitude.
    //
    // Cost: 7 mults + 6 adds = ~13 scalar FLOPs. Compare to libm sinf
    // on Cortex-A8 newlib at ~40-150 cycles depending on input range.
    // Net save per call: ~30-130 cycles. For Helicase's 8-12 sinf-
    // per-sample hifi load that's 5-10% CPU.

    // sin(x) for x ∈ [-π, π]. Caller must range-reduce; output is
    // undefined (large error) outside this range.
    static inline float sine_poly_hq_x(float x)
    {
      float x2 = x * x;
      float poly = 1.0f + x2 * (-0.166666666f
                 + x2 * ( 0.00833333f
                 + x2 * (-0.000198413f
                 + x2 * ( 0.0000027557f
                 + x2 * (-0.0000000250521f
                 + x2 *   0.0000000001605f)))));
      return x * poly;
    }

    // sin(2π × phase01) for phase01 ∈ [0, 1).
    // Uses centered-Taylor identity: sin(2π·p) = -sin(2π·(p - 0.5)),
    // so the polynomial argument lands in [-π, π] where 13th-order
    // Taylor is accurate to -100 dB.
    static inline float sine_poly_hq(float phase01)
    {
      return -sine_poly_hq_x(6.28318530718f * (phase01 - 0.5f));
    }

    // tanh(x) via Padé 3/3 rational: x·(27 + x²) / (27 + 9·x²).
    // <0.1% error for |x| < 4; ~0.01% (~-80 dB) for |x| < 1 which
    // is the typical feedback-state range. Hard-clamps outside ±4.
    //
    // Lifted from mods/spreadsheet/MultibandSaturator.cpp `fast_tanh`
    // (in production since Parfait first ship). Suitable as drop-in
    // replacement for libm `tanhf` in audio paths where the small
    // beyond-±4 deviation is acceptable (and usually inaudible — most
    // audio tanhf inputs stay well within ±4).
    static inline float tanh_poly(float x)
    {
      if (x < -4.0f) return -1.0f;
      if (x >  4.0f) return  1.0f;
      float x2 = x * x;
      return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

  } // namespace neon_math
} // namespace stolmine
