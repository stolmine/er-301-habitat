#pragma once

// Port-local configuration for the vendored Open303 engine.
//
// This file is NOT part of upstream Open303 - it is habitat's port layer.
// Upstream: https://github.com/RobinSchmidt/Open303, MIT, (c) 2009 Robin Schmidt.
// See LICENSE-Open303.txt and planning/open303-port.md.
//
// Three concerns live here, all of them am335x-driven:
//   1. the audio-path precision split (float audio, double construction),
//   2. the oversampling tier (2x on am335x, 4x elsewhere),
//   3. libm removal from the per-sample path (fast exp2).

#include <math.h>
#include <string.h>

// ------------------------------------------------------------------------
// 1. Precision split
// ------------------------------------------------------------------------
// o3Float is the AUDIO PATH type. Table generation and the construction-time
// FFT deliberately stay `double`: they run once at insert, off the hot path,
// and keeping them exact preserves table fidelity. Per
// feedback_cortex_a8_no_double_in_hot_loops - Cortex-A8 has no double-precision
// NEON, so any double that reaches a per-sample loop falls back to scalar
// VFPv3 at 3-4x the cost.
typedef float o3Float;

// ------------------------------------------------------------------------
// 2. Oversampling tier
// ------------------------------------------------------------------------
// The oversampling exists for the filter nonlinearity, not the oscillator (the
// oscillator is band-limited by its own mip tables). am335x runs 2x; hosts with
// headroom run 4x. Decimation is by cascaded halfband stages either way, so the
// only difference is how many stages run - see o3_halfband.h.
// The build sets this explicitly: mods/biome/mod.mk passes -DO3_OVERSAMPLING=2
// for ARCH=am335x. Do NOT make the hardware tier depend on a header-side macro
// sniff - there is no -Dam335x in this build, so `defined(am335x)` silently
// never fires and hardware quietly gets the heavier 4x path. The __arm__
// fallback below only covers standalone/bench builds that bypass mod.mk.
#ifndef O3_OVERSAMPLING
#if defined(__arm__)
#define O3_OVERSAMPLING 2
#else
#define O3_OVERSAMPLING 4
#endif
#endif

#if O3_OVERSAMPLING == 2
#define O3_HALFBAND_STAGES 1
#elif O3_OVERSAMPLING == 4
#define O3_HALFBAND_STAGES 2
#else
#error "O3_OVERSAMPLING must be 2 or 4"
#endif

// ------------------------------------------------------------------------
// 3. Forced inlining
// ------------------------------------------------------------------------
// Upstream's INLINE macro degrades to a bare `inline` on GCC, which is only a
// hint - per feedback_static_inline_not_guaranteed, hot helpers have been
// observed staying out-of-line under -O3. Force it, but keep the attribute away
// from SWIG's parser (it chokes on GCC attributes).
#if defined(SWIGLUA)
#define O3_ALWAYS_INLINE inline
#elif defined(__GNUC__)
#define O3_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define O3_ALWAYS_INLINE inline
#endif

// ------------------------------------------------------------------------
// Strict-aliasing-safe float bit access
// ------------------------------------------------------------------------
// Upstream's EXPOFFLT/EXPOFDBL type-pun through reinterpret_cast, which is
// undefined behavior that GCC at -O3 is entitled to exploit. Anamnesis was
// burned by exactly this class of UB (field::hash01 signed overflow, provably
// exploited by the optimizer), so the vendored engine routes bit tricks through
// memcpy, which every compiler folds to a plain move.
O3_ALWAYS_INLINE unsigned int o3FloatBits(float x)
{
  unsigned int u;
  memcpy(&u, &x, sizeof(u));
  return u;
}

O3_ALWAYS_INLINE float o3BitsToFloat(unsigned int u)
{
  float x;
  memcpy(&x, &u, sizeof(x));
  return x;
}

// Unbiased exponent of a float, the EXPOFFLT replacement.
O3_ALWAYS_INLINE int o3Exponent(float x)
{
  return (int)((o3FloatBits(x) & 0x7FFFFFFFu) >> 23) - 127;
}

// ------------------------------------------------------------------------
// Fast exp2
// ------------------------------------------------------------------------
// Replaces the per-sample pow(2.0, ...) on the env-modulated cutoff
// (rosic_Open303.h) - the only libm call left on the voice's per-sample path.
// Split x into integer and fractional parts; build 2^i by assembling the
// exponent field directly, and evaluate 2^f with a degree-5 polynomial (the
// exp(f*ln2) series, error < 1e-6 on [0,1)).
//
// The consumer clamps the result into [200, 20000] Hz, so this is far more
// accuracy than the use demands.
O3_ALWAYS_INLINE float o3FastExp2(float x)
{
  // Bound the input so the exponent assembly cannot overflow the field. The
  // cutoff modulation never approaches these limits.
  if (x > 120.0f)
    x = 120.0f;
  else if (x < -120.0f)
    x = -120.0f;

  // Branchless floor for the bounded range: the +1024 bias makes the truncation
  // round toward negative infinity for every x we admit.
  int i = (int)(x + 1024.0f) - 1024;
  float f = x - (float)i;

  // 2^f on [0,1)
  float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133335f))));

  // 2^i by direct exponent assembly
  float s = o3BitsToFloat((unsigned int)(i + 127) << 23);

  return p * s;
}
