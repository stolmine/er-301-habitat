// Copyright 2021 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// 6-operator FM synth.

// Disable GCC auto-vectorization on this file. The 2x OS render path
// inside SixOpEngine::Render contains tight loops (decimation,
// overlap-add, fill) that GCC -O3 -ffast-math will auto-vectorize
// into NEON with `:64`/`:128` alignment hints — which trap on
// Cortex-A8 because the buffers aren't guaranteed aligned to those
// boundaries. Per feedback_neon_hint_surfaces / feedback_neon_soa_svf_bank.
#pragma GCC optimize("no-tree-vectorize")

#include "plaits/dsp/engine2/six_op_engine.h"

#include <algorithm>

#include "plaits/resources.h"

namespace plaits {

using namespace fm;
using namespace std;
using namespace stmlib;

// 2x oversampling of the 6-op FM voice to tame DX7-style aliasing in
// the upper octaves. Implemented purely inside SixOpEngine::Render:
// the voice is Init'd at 2*kCorrectedSampleRate, rendered with 2x the
// sample count per block, then decimated 2:1 in place with a 2-tap
// moving average LPF. No template/inline/member-layout changes to
// fm::Voice (per planning/plaits-6op-os-rollback.md lessons — though
// those crashes are now retroactively explained by the AAPCS issue
// fixed at mi 1.0.3.13/.15, this OS approach is intentionally minimal
// to keep risk localized).
//
// kSixOpOversampling = 2 enables. Set to 1 to disable (no behavior
// difference vs upstream — for A/B comparison).
const int kSixOpOversampling = 2;

void FMVoice::Init(fm::Algorithms<6>* algorithms, float sample_rate) {
  voice_.Init(algorithms, sample_rate);
  lfo_.Init(sample_rate);
  
  parameters_.sustain = false;
  parameters_.gate = false;
  parameters_.note = 48.0f;
  parameters_.velocity = 0.5f;
  parameters_.brightness = 0.5f;
  parameters_.envelope_control = 0.5f;
  parameters_.pitch_mod = 0.0f;
  parameters_.amp_mod = 0.0f;
  
  patch_ = NULL;
}

void FMVoice::Render(float* buffer, size_t size) {
  if (!patch_) {
    return;
  }
  voice_.Render(parameters_, buffer, size);
}

void FMVoice::LoadPatch(const fm::Patch* patch) {
  if (patch == patch_) {
    return;
  }
  patch_ = patch;
  voice_.SetPatch(patch_);
  lfo_.Set(patch_->modulations);
}

const int kNumPatchesPerBank = 32;

void SixOpEngine::Init(BufferAllocator* allocator) {
  patch_index_quantizer_.Init(32, 0.005f, false);

  algorithms_.Init();
  for (int i = 0; i < kNumSixOpVoices; ++i) {
    voice_[i].Init(&algorithms_, kCorrectedSampleRate * float(kSixOpOversampling));
  }
  // temp_buffer_ is consumed by fm::Voice::Render which addresses
  // buffers[0..3] at offsets 0, size, 2*size, 2*size with each
  // renderer writing `size` floats there. Max byte offset =
  // 3 * inner_size. At OS=2 with kMaxBlockSize=24, kNumSixOpVoices=2,
  // inner_size = 24 * 2 * 2 = 96, so we need 3 * 96 = 288 floats.
  // (Original upstream allocated kMaxBlockSize * 4 = 96 floats and
  // appears to rely on adjacent arena memory for buffers[2]/[3]
  // writes — we make it explicit and safe here.)
  temp_buffer_ = allocator->Allocate<float>(
      kMaxBlockSize * kNumSixOpVoices * kSixOpOversampling * 3);
  acc_buffer_ = allocator->Allocate<float>(kMaxBlockSize * kNumSixOpVoices);
  patches_ = allocator->Allocate<fm::Patch>(kNumPatchesPerBank);
  
  active_voice_ = kNumSixOpVoices - 1;
  rendered_voice_ = 0;
}

void SixOpEngine::Reset() {
  
}

void SixOpEngine::LoadUserData(const uint8_t* user_data) {
  for (int i = 0; i < kNumPatchesPerBank; ++i) {
    patches_[i].Unpack(user_data + i * fm::Patch::SYX_SIZE);
  }
  for (int i = 0; i < kNumSixOpVoices; ++i) {
    voice_[i].UnloadPatch();
  }
}

void SixOpEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  int patch_index = patch_index_quantizer_.Process(
      parameters.harmonics * 1.02f);
  
  if (parameters.trigger & TRIGGER_UNPATCHED) {
    const float t = parameters.morph;
    // LFO sample-rate is now 2x kCorrectedSampleRate, so Scrub argument
    // (which is a sample count) needs the matching 2x.
    voice_[0].mutable_lfo()->Scrub(
        2.0f * kCorrectedSampleRate * float(kSixOpOversampling) * t);

    for (int i = 0; i < kNumSixOpVoices; ++i) {
      voice_[i].LoadPatch(&patches_[patch_index]);
      Voice<6>::Parameters* p = voice_[i].mutable_parameters();
      p->sustain = i == 0 ? true : false;
      p->gate = false;
      p->note = parameters.note;
      p->velocity = parameters.accent;
      p->brightness = parameters.timbre;
      p->envelope_control = t;
      voice_[i].set_modulations(voice_[0].lfo());
    }
  } else {
    if (parameters.trigger & TRIGGER_RISING_EDGE) {
      active_voice_ = (active_voice_ + 1) % kNumSixOpVoices;
      voice_[active_voice_].LoadPatch(&patches_[patch_index]);
      voice_[active_voice_].mutable_lfo()->Reset();
    }
    Voice<6>::Parameters* p = voice_[active_voice_].mutable_parameters();
    p->note = parameters.note;
    p->velocity = parameters.accent;
    p->envelope_control = parameters.morph;
    // LFO sample-rate is now 2x kCorrectedSampleRate, so Step's
    // sample-count arg gets the matching 2x scaling.
    voice_[active_voice_].mutable_lfo()->Step(
        float(size * kSixOpOversampling));

    for (int i = 0; i < kNumSixOpVoices; ++i) {
      Voice<6>::Parameters* p = voice_[i].mutable_parameters();
      p->brightness = parameters.timbre;
      p->sustain = false;
      p->gate = (parameters.trigger & TRIGGER_HIGH) && (i == active_voice_);
      if (voice_[i].patch() != voice_[active_voice_].patch()) {
        voice_[i].mutable_lfo()->Step(float(size * kSixOpOversampling));
        voice_[i].set_modulations(voice_[i].lfo());
      } else {
        voice_[i].set_modulations(voice_[active_voice_].lfo());
      }
    }
  }

  // Naive block rendering.
  // fill(temp_buffer_[0], temp_buffer_[size], 0.0f);
  // for (int i = 0; i < kNumSixOpVoices; ++i) {
  //   voice_[i].Render(temp_buffer_, size);
  // }

  // Staggered rendering at kSixOpOversampling x native rate, then
  // 2:1 decimation in place, then staggered overlap-add at native
  // rate.
  const size_t native_block = size * kNumSixOpVoices;
  const size_t os_block = native_block * kSixOpOversampling;
  // Zero-fill the full OS region: fm::Voice renderers ADD to assigned
  // buffer slots, so a zero baseline is required for correct mixing.
  // (Upstream cleared only [(kNumSixOpVoices-1)*size .. kNumSixOpVoices*size]
  //  because the rest was pre-loaded with the staggered overlap. We
  //  do overlap-add AFTER decimation instead, so the entire OS region
  //  starts at zero.)
  fill(&temp_buffer_[0], &temp_buffer_[3 * os_block], 0.0f);

  rendered_voice_ = (rendered_voice_ + 1) % kNumSixOpVoices;
  voice_[rendered_voice_].Render(temp_buffer_, os_block);

  // 2:1 decimation in place with a 2-tap moving-average LPF.
  // H(f) = cos(pi*f/fs_os): zero at the new Nyquist (24kHz post-
  // decimation), -3dB at 12kHz. Gentle treble rolloff acceptable.
  for (size_t i = 0; i < native_block; ++i) {
    temp_buffer_[i] = 0.5f * (temp_buffer_[2 * i] + temp_buffer_[2 * i + 1]);
  }

  // Add the OTHER voice's staggered overlap (saved from previous
  // call). acc_buffer holds (kNumSixOpVoices-1)*size = size samples.
  for (size_t i = 0; i < (kNumSixOpVoices - 1) * size; ++i) {
    temp_buffer_[i] += acc_buffer_[i];
  }

  for (size_t i = 0; i < size; ++i) {
    aux[i] = out[i] = SoftClip(temp_buffer_[i] * 0.25f);
  }
  // Save the post-output tail for next-call overlap.
  copy(
      &temp_buffer_[size],
      &temp_buffer_[native_block],
      &acc_buffer_[0]);
}

}  // namespace plaits
