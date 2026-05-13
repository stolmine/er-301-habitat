#pragma once

// Minimal NEON-on-scalar shim so jf/voice.h compiles + runs on x86 emu.
//
// Provides only the intrinsics jf/voice.h uses. NOT a complete NEON
// emulation — extend as needed when adding new intrinsics.
//
// On hardware (am335x), <arm_neon.h> is used directly; this file is
// guarded out via the __ARM_NEON gate in voice.h. The scalar paths here
// are not performance-tuned — emu is for development, hardware is the
// performance target.

#include <stdint.h>

struct float32x4_t {
  float v[4];
};

struct uint32x4_t {
  uint32_t v[4];
};

struct float32x2_t {
  float v[2];
};

// ----- broadcast / load -----

static inline float32x4_t vdupq_n_f32(float x) {
  float32x4_t r = { { x, x, x, x } };
  return r;
}

static inline uint32x4_t vdupq_n_u32(uint32_t x) {
  uint32x4_t r = { { x, x, x, x } };
  return r;
}

static inline float32x4_t vld1q_f32(const float *p) {
  float32x4_t r = { { p[0], p[1], p[2], p[3] } };
  return r;
}

static inline uint32x4_t vld1q_u32(const uint32_t *p) {
  uint32x4_t r = { { p[0], p[1], p[2], p[3] } };
  return r;
}

static inline void vst1q_f32(float *p, float32x4_t a) {
  p[0] = a.v[0]; p[1] = a.v[1]; p[2] = a.v[2]; p[3] = a.v[3];
}

// ----- arithmetic -----

static inline float32x4_t vaddq_f32(float32x4_t a, float32x4_t b) {
  float32x4_t r = { { a.v[0]+b.v[0], a.v[1]+b.v[1], a.v[2]+b.v[2], a.v[3]+b.v[3] } };
  return r;
}

static inline float32x4_t vsubq_f32(float32x4_t a, float32x4_t b) {
  float32x4_t r = { { a.v[0]-b.v[0], a.v[1]-b.v[1], a.v[2]-b.v[2], a.v[3]-b.v[3] } };
  return r;
}

static inline float32x4_t vmulq_f32(float32x4_t a, float32x4_t b) {
  float32x4_t r = { { a.v[0]*b.v[0], a.v[1]*b.v[1], a.v[2]*b.v[2], a.v[3]*b.v[3] } };
  return r;
}

// Fused multiply-add: acc + a*b, lane-wise.
static inline float32x4_t vmlaq_f32(float32x4_t acc, float32x4_t a, float32x4_t b) {
  float32x4_t r = { {
    acc.v[0] + a.v[0]*b.v[0],
    acc.v[1] + a.v[1]*b.v[1],
    acc.v[2] + a.v[2]*b.v[2],
    acc.v[3] + a.v[3]*b.v[3]
  } };
  return r;
}

// Absolute value, lane-wise.
static inline float32x4_t vabsq_f32(float32x4_t a) {
  float32x4_t r = { {
    a.v[0] < 0.0f ? -a.v[0] : a.v[0],
    a.v[1] < 0.0f ? -a.v[1] : a.v[1],
    a.v[2] < 0.0f ? -a.v[2] : a.v[2],
    a.v[3] < 0.0f ? -a.v[3] : a.v[3]
  } };
  return r;
}

// Reciprocal estimate, lane-wise. On hardware this is the NEON estimate
// instruction (~10-bit precision); on scalar fallback we use full 1/x
// since precision is essentially free on x86.
static inline float32x4_t vrecpeq_f32(float32x4_t a) {
  float32x4_t r = { {
    1.0f / a.v[0], 1.0f / a.v[1], 1.0f / a.v[2], 1.0f / a.v[3]
  } };
  return r;
}

static inline float32x4_t vnegq_f32(float32x4_t a) {
  float32x4_t r = { { -a.v[0], -a.v[1], -a.v[2], -a.v[3] } };
  return r;
}

static inline float32x4_t vmaxq_f32(float32x4_t a, float32x4_t b) {
  float32x4_t r = { {
    a.v[0]>b.v[0]?a.v[0]:b.v[0],
    a.v[1]>b.v[1]?a.v[1]:b.v[1],
    a.v[2]>b.v[2]?a.v[2]:b.v[2],
    a.v[3]>b.v[3]?a.v[3]:b.v[3]
  } };
  return r;
}

static inline float32x4_t vminq_f32(float32x4_t a, float32x4_t b) {
  float32x4_t r = { {
    a.v[0]<b.v[0]?a.v[0]:b.v[0],
    a.v[1]<b.v[1]?a.v[1]:b.v[1],
    a.v[2]<b.v[2]?a.v[2]:b.v[2],
    a.v[3]<b.v[3]?a.v[3]:b.v[3]
  } };
  return r;
}

// ----- conversion -----

static inline int32_t f2i_trunc(float f) { return (int32_t)f; }

struct int32x4_t { int32_t v[4]; };

static inline int32x4_t vcvtq_s32_f32(float32x4_t a) {
  int32x4_t r = { { f2i_trunc(a.v[0]), f2i_trunc(a.v[1]), f2i_trunc(a.v[2]), f2i_trunc(a.v[3]) } };
  return r;
}

static inline float32x4_t vcvtq_f32_s32(int32x4_t a) {
  float32x4_t r = { { (float)a.v[0], (float)a.v[1], (float)a.v[2], (float)a.v[3] } };
  return r;
}

// ----- bitwise / logical -----

static inline uint32x4_t vandq_u32(uint32x4_t a, uint32x4_t b) {
  uint32x4_t r = { { a.v[0]&b.v[0], a.v[1]&b.v[1], a.v[2]&b.v[2], a.v[3]&b.v[3] } };
  return r;
}

static inline uint32x4_t vorrq_u32(uint32x4_t a, uint32x4_t b) {
  uint32x4_t r = { { a.v[0]|b.v[0], a.v[1]|b.v[1], a.v[2]|b.v[2], a.v[3]|b.v[3] } };
  return r;
}

static inline uint32x4_t vbicq_u32(uint32x4_t a, uint32x4_t b) {
  // a AND NOT b
  uint32x4_t r = { { a.v[0]&~b.v[0], a.v[1]&~b.v[1], a.v[2]&~b.v[2], a.v[3]&~b.v[3] } };
  return r;
}

// Bitwise select: result = (mask & a) | (~mask & b)
static inline float32x4_t vbslq_f32(uint32x4_t mask, float32x4_t a, float32x4_t b) {
  // bit-select on float bits
  union { float f; uint32_t u; } pa, pb, pr;
  float32x4_t r;
  for (int i = 0; i < 4; i++) {
    pa.f = a.v[i]; pb.f = b.v[i];
    pr.u = (mask.v[i] & pa.u) | (~mask.v[i] & pb.u);
    r.v[i] = pr.f;
  }
  return r;
}

// ----- comparison → mask -----

static inline uint32x4_t vcgeq_f32(float32x4_t a, float32x4_t b) {
  uint32x4_t r = { {
    a.v[0]>=b.v[0]?0xFFFFFFFFu:0u,
    a.v[1]>=b.v[1]?0xFFFFFFFFu:0u,
    a.v[2]>=b.v[2]?0xFFFFFFFFu:0u,
    a.v[3]>=b.v[3]?0xFFFFFFFFu:0u
  } };
  return r;
}

static inline uint32x4_t vcltq_f32(float32x4_t a, float32x4_t b) {
  uint32x4_t r = { {
    a.v[0]<b.v[0]?0xFFFFFFFFu:0u,
    a.v[1]<b.v[1]?0xFFFFFFFFu:0u,
    a.v[2]<b.v[2]?0xFFFFFFFFu:0u,
    a.v[3]<b.v[3]?0xFFFFFFFFu:0u
  } };
  return r;
}

// ----- lane access -----

static inline float32x4_t vsetq_lane_f32(float v, float32x4_t a, int idx) {
  a.v[idx] = v;
  return a;
}

static inline uint32x4_t vsetq_lane_u32(uint32_t v, uint32x4_t a, int idx) {
  a.v[idx] = v;
  return a;
}

static inline float vgetq_lane_f32(float32x4_t a, int idx) {
  return a.v[idx];
}

// ----- pairwise add (for sum_lanes) -----

static inline float32x2_t vget_low_f32(float32x4_t a) {
  float32x2_t r = { { a.v[0], a.v[1] } };
  return r;
}

static inline float32x2_t vget_high_f32(float32x4_t a) {
  float32x2_t r = { { a.v[2], a.v[3] } };
  return r;
}

static inline float32x2_t vpadd_f32(float32x2_t a, float32x2_t b) {
  float32x2_t r = { { a.v[0]+a.v[1], b.v[0]+b.v[1] } };
  return r;
}

static inline float vget_lane_f32(float32x2_t a, int idx) {
  return a.v[idx];
}
