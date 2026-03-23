// Modified Resonator with NEON-vectorized SVF bank for Cortex-A8
// Original by Émilie Gillet, MIT License

#ifndef RINGS_DSP_RESONATOR_H_
#define RINGS_DSP_RESONATOR_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "rings/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/delay_line.h"

namespace rings {

const int32_t kMaxModes = 64;

class Resonator {
 public:
  Resonator() { }
  ~Resonator() { }

  void Init();
  void Process(
      const float* in,
      float* out,
      float* aux,
      size_t size);

  inline void set_frequency(float frequency) {
    frequency_ = frequency;
  }

  inline void set_structure(float structure) {
    structure_ = structure;
  }

  inline void set_brightness(float brightness) {
    brightness_ = brightness;
  }

  inline void set_damping(float damping) {
    damping_ = damping;
  }

  inline void set_position(float position) {
    position_ = position;
  }

  inline void set_resolution(int32_t resolution) {
    resolution -= resolution & 1; // Must be even!
    resolution_ = std::min(resolution, kMaxModes);
  }

 private:
  int32_t ComputeFilters();
  float frequency_;
  float structure_;
  float brightness_;
  float position_;
  float previous_position_;
  float damping_;

  int32_t resolution_;

  // SoA layout for NEON-friendly SVF processing
  float g_[kMaxModes];
  float r_[kMaxModes];
  float h_[kMaxModes];
  float state_1_[kMaxModes];
  float state_2_[kMaxModes];

  DISALLOW_COPY_AND_ASSIGN(Resonator);
};

}  // namespace rings

#endif  // RINGS_DSP_RESONATOR_H_
