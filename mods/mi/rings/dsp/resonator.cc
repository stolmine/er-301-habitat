// NEON-vectorized Resonator for Cortex-A8
// Original by Émilie Gillet, MIT License
//
// Processes 4 SVF bandpass filters per NEON iteration instead of 1.
// SoA layout allows float32x4_t loads/stores on coefficient and state arrays.

#include "rings/dsp/resonator.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/cosine_oscillator.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "rings/resources.h"

#ifdef __ARM_NEON__
#include <arm_neon.h>
#endif

namespace rings {

using namespace std;
using namespace stmlib;

void Resonator::Init() {
  for (int32_t i = 0; i < kMaxModes; ++i) {
    g_[i] = 0.0f;
    r_[i] = 0.0f;
    h_[i] = 0.0f;
    state_1_[i] = 0.0f;
    state_2_[i] = 0.0f;
  }
  amp_scratch_[0] = amp_scratch_[1] = amp_scratch_[2] = amp_scratch_[3] = 0.0f;

  set_frequency(220.0f / kSampleRate);
  set_structure(0.25f);
  set_brightness(0.5f);
  set_damping(0.3f);
  set_position(0.999f);
  previous_position_ = 0.0f;
  set_resolution(kMaxModes);
}

int32_t Resonator::ComputeFilters() {
  float stiffness = Interpolate(lut_stiffness, structure_, 256.0f);
  float harmonic = frequency_;
  float stretch_factor = 1.0f;
  float q = 500.0f * Interpolate(
      lut_4_decades,
      damping_,
      256.0f);
  float brightness_attenuation = 1.0f - structure_;
  brightness_attenuation *= brightness_attenuation;
  brightness_attenuation *= brightness_attenuation;
  brightness_attenuation *= brightness_attenuation;
  float brightness = brightness_ * (1.0f - 0.2f * brightness_attenuation);
  float q_loss = brightness * (2.0f - brightness) * 0.85f + 0.15f;
  float q_loss_damping_rate = structure_ * (2.0f - structure_) * 0.1f;
  int32_t num_modes = 0;
  for (int32_t i = 0; i < min(kMaxModes, resolution_); ++i) {
    float partial_frequency = harmonic * stretch_factor;
    if (partial_frequency >= 0.49f) {
      partial_frequency = 0.49f;
    } else {
      num_modes = i + 1;
    }

    // Compute SVF coefficients directly into SoA arrays
    // tan approximation (FREQUENCY_FAST): f + f*f*f/3
    float f = partial_frequency;
    float g = f * (1.0f + f * f * 0.333333333f);
    float resonance = 1.0f + partial_frequency * q;
    float r = 1.0f / resonance;
    float h = 1.0f / (1.0f + r * g + g * g);

    g_[i] = g;
    r_[i] = r;
    h_[i] = h;

    stretch_factor += stiffness;
    if (stiffness < 0.0f) {
      stiffness *= 0.93f;
    } else {
      stiffness *= 0.98f;
    }
    q_loss += q_loss_damping_rate * (1.0f - q_loss);
    harmonic += frequency_;
    q *= q_loss;
  }

  // Pad to next multiple of 4 (kMaxModes=64 is already a multiple, so
  // no risk of going past the array bound). Zero coefficients AND state
  // for padding lanes: g=0 alone freezes state but doesn't zero an
  // already-non-zero state from a previous larger num_modes — we need
  // bp=s1=0 at the SVF output. Belt + suspenders: zero both.
  int32_t num_modes_padded = (num_modes + 3) & ~3;
  if (num_modes_padded > kMaxModes) num_modes_padded = kMaxModes;
  for (int32_t i = num_modes; i < num_modes_padded; ++i) {
    g_[i] = 0.0f;
    r_[i] = 0.0f;
    h_[i] = 0.0f;
    state_1_[i] = 0.0f;
    state_2_[i] = 0.0f;
  }

  return num_modes_padded;
}

void Resonator::Process(const float* in, float* out, float* aux, size_t size) {
  // ComputeFilters returns num_modes_padded — already a multiple of 4
  // (kMaxModes is 64). Padding lanes have g=r=h=0 and zeroed state, so
  // they contribute bp=0 to the NEON inner loop. No scalar tail needed.
  int32_t num_modes_padded = ComputeFilters();

  ParameterInterpolator position(&previous_position_, position_, size);
  while (size--) {
    CosineOscillator amplitudes;
    amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(position.Next());

    float input = *in++ * 0.125f;
    amplitudes.Start();
    float odd, even;

#ifdef __ARM_NEON__
    float32x4_t v_input = vdupq_n_f32(input);
    // Single accumulator quad — holds amplitude·bp contributions across
    // all 4 lanes. Replaces the prior per-iter vst1q_f32(bp_vals,…) +
    // 4 scalar reads + 4 scalar mla (which drained the NEON pipeline
    // every quad). At end-of-sample we extract: odd = lane0+lane2,
    // even = lane1+lane3. NEON→FPU crossings drop from O(num_modes)
    // per sample to 4 per sample.
    float32x4_t v_acc = vdupq_n_f32(0.0f);

    for (int32_t i = 0; i < num_modes_padded; i += 4) {
      // Load coefficients
      float32x4_t v_g = vld1q_f32(&g_[i]);
      float32x4_t v_r = vld1q_f32(&r_[i]);
      float32x4_t v_h = vld1q_f32(&h_[i]);
      float32x4_t v_s1 = vld1q_f32(&state_1_[i]);
      float32x4_t v_s2 = vld1q_f32(&state_2_[i]);

      // TPT SVF bandpass — 4 filters in parallel.
      // hp = (in - r·s1 - g·s1 - s2) · h
      float32x4_t v_hp = vmulq_f32(
          vsubq_f32(
              vsubq_f32(
                  vsubq_f32(v_input, vmulq_f32(v_r, v_s1)),
                  vmulq_f32(v_g, v_s1)),
              v_s2),
          v_h);
      // bp = g·hp + s1;  s1' = g·hp + bp
      float32x4_t v_bp     = vmlaq_f32(v_s1, v_g, v_hp);
      float32x4_t v_new_s1 = vmlaq_f32(v_bp, v_g, v_hp);
      // lp = g·bp + s2;  s2' = g·bp + lp
      float32x4_t v_lp     = vmlaq_f32(v_s2, v_g, v_bp);
      float32x4_t v_new_s2 = vmlaq_f32(v_lp, v_g, v_bp);
      vst1q_f32(&state_1_[i], v_new_s1);
      vst1q_f32(&state_2_[i], v_new_s2);

      // Accumulate amp·bp into v_acc, staying in NEON. Fill the
      // class-member scratch (heap-allocated, NEON-safe per
      // feedback_neon_intrinsics_drumvoice — no stack-local :64 trap).
      amp_scratch_[0] = amplitudes.Next();
      amp_scratch_[1] = amplitudes.Next();
      amp_scratch_[2] = amplitudes.Next();
      amp_scratch_[3] = amplitudes.Next();
      float32x4_t v_amp = vld1q_f32(amp_scratch_);
      v_acc = vmlaq_f32(v_acc, v_amp, v_bp);
    }

    // Extract odd (lanes 0+2) and even (lanes 1+3). Four NEON→FPU
    // crossings per output sample, total, vs the prior ~num_modes
    // per-iter crossings.
    odd  = vgetq_lane_f32(v_acc, 0) + vgetq_lane_f32(v_acc, 2);
    even = vgetq_lane_f32(v_acc, 1) + vgetq_lane_f32(v_acc, 3);
#else
    // Scalar fallback (linux x86, macOS, anywhere __ARM_NEON__ is undef).
    // Same single-accumulator pattern, scalar arithmetic.
    float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int32_t i = 0; i < num_modes_padded; i += 4) {
      for (int32_t k = 0; k < 4; ++k) {
        const float gv = g_[i+k], rv = r_[i+k], hv = h_[i+k];
        const float s1 = state_1_[i+k], s2 = state_2_[i+k];
        const float hp = (input - rv*s1 - gv*s1 - s2) * hv;
        const float bp = s1 + gv*hp;
        state_1_[i+k]  = bp + gv*hp;
        const float lp = s2 + gv*bp;
        state_2_[i+k]  = lp + gv*bp;
        acc[k] += amplitudes.Next() * bp;
      }
    }
    odd  = acc[0] + acc[2];
    even = acc[1] + acc[3];
#endif

    *out++ = odd;
    *aux++ = even;
  }
}

}  // namespace rings
