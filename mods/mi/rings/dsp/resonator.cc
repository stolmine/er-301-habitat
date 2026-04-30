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

  return num_modes;
}

void Resonator::Process(const float* in, float* out, float* aux, size_t size) {
  int32_t num_modes = ComputeFilters();
  // Round down to multiple of 4 for NEON, must also be even for odd/even split
  int32_t num_modes_simd = num_modes & ~3;

  ParameterInterpolator position(&previous_position_, position_, size);
  while (size--) {
    CosineOscillator amplitudes;
    amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(position.Next());

    float input = *in++ * 0.125f;
    float odd = 0.0f;
    float even = 0.0f;
    amplitudes.Start();

#ifdef __ARM_NEON__
    float32x4_t v_input = vdupq_n_f32(input);
    float32x4_t v_odd = vdupq_n_f32(0.0f);
    float32x4_t v_even = vdupq_n_f32(0.0f);

    for (int32_t i = 0; i < num_modes_simd; i += 4) {
      // Load coefficients
      float32x4_t v_g = vld1q_f32(&g_[i]);
      float32x4_t v_r = vld1q_f32(&r_[i]);
      float32x4_t v_h = vld1q_f32(&h_[i]);
      float32x4_t v_s1 = vld1q_f32(&state_1_[i]);
      float32x4_t v_s2 = vld1q_f32(&state_2_[i]);

      // SVF bandpass: 4 filters in parallel
      // hp = (in - r*s1 - g*s1 - s2) * h
      float32x4_t v_hp = vmulq_f32(
          vsubq_f32(
              vsubq_f32(
                  vsubq_f32(v_input, vmulq_f32(v_r, v_s1)),
                  vmulq_f32(v_g, v_s1)),
              v_s2),
          v_h);

      // bp = g*hp + s1
      float32x4_t v_bp = vmlaq_f32(v_s1, v_g, v_hp);

      // new_s1 = g*hp + bp
      float32x4_t v_new_s1 = vmlaq_f32(v_bp, v_g, v_hp);

      // new_s2 = g*bp + (g*bp + s2)  [lp = g*bp + s2, new_s2 = g*bp + lp]
      float32x4_t v_lp = vmlaq_f32(v_s2, v_g, v_bp);
      float32x4_t v_new_s2 = vmlaq_f32(v_lp, v_g, v_bp);

      // Store updated state
      vst1q_f32(&state_1_[i], v_new_s1);
      vst1q_f32(&state_2_[i], v_new_s2);

      // Compute 4 amplitude weights
      float a0 = amplitudes.Next();
      float a1 = amplitudes.Next();
      float a2 = amplitudes.Next();
      float a3 = amplitudes.Next();

      // Weighted accumulation: odd modes (0,2) and even modes (1,3)
      // Modes i+0, i+2 go to odd; i+1, i+3 go to even
      float bp_vals[4];
      vst1q_f32(bp_vals, v_bp);

      odd  += a0 * bp_vals[0] + a2 * bp_vals[2];
      even += a1 * bp_vals[1] + a3 * bp_vals[3];
    }

    // Scalar tail for remaining modes (0-3 modes)
    for (int32_t i = num_modes_simd; i < num_modes;) {
      float a = amplitudes.Next();
      // SVF scalar
      float hp = (input - r_[i] * state_1_[i] - g_[i] * state_1_[i] - state_2_[i]) * h_[i];
      float bp = g_[i] * hp + state_1_[i];
      state_1_[i] = g_[i] * hp + bp;
      float lp = g_[i] * bp + state_2_[i];
      state_2_[i] = g_[i] * bp + lp;
      odd += a * bp;
      i++;

      if (i < num_modes) {
        a = amplitudes.Next();
        hp = (input - r_[i] * state_1_[i] - g_[i] * state_1_[i] - state_2_[i]) * h_[i];
        bp = g_[i] * hp + state_1_[i];
        state_1_[i] = g_[i] * hp + bp;
        lp = g_[i] * bp + state_2_[i];
        state_2_[i] = g_[i] * bp + lp;
        even += a * bp;
        i++;
      }
    }
#else
    // Scalar fallback for non-NEON platforms (emulator)
    for (int32_t i = 0; i < num_modes;) {
      float a = amplitudes.Next();
      float hp = (input - r_[i] * state_1_[i] - g_[i] * state_1_[i] - state_2_[i]) * h_[i];
      float bp = g_[i] * hp + state_1_[i];
      state_1_[i] = g_[i] * hp + bp;
      state_2_[i] = g_[i] * bp + (g_[i] * bp + state_2_[i]);
      odd += a * bp;
      i++;

      a = amplitudes.Next();
      hp = (input - r_[i] * state_1_[i] - g_[i] * state_1_[i] - state_2_[i]) * h_[i];
      bp = g_[i] * hp + state_1_[i];
      state_1_[i] = g_[i] * hp + bp;
      state_2_[i] = g_[i] * bp + (g_[i] * bp + state_2_[i]);
      even += a * bp;
      i++;
    }
#endif

    *out++ = odd;
    *aux++ = even;
  }
}

}  // namespace rings
