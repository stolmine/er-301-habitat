#pragma once

// 6-lane voice for Visadhara. Phase 1: NEON 4-lane × 2 instances (8 lanes,
// 6 active, 2 masked). Per-voice phase, AR envelope (no sustain), shared
// trigger, per-voice frequency multiplier (Spread) and amplitude scalar
// (Harmonic). Output is sum of 6 voice signals.
//
// Following the JF/Ngoma NEON discipline:
//   - All NEON state in heap-allocated Internal struct (class member arrays
//     of float[8] for 6+2 lanes).
//   - vsetq_lane / vgetq_lane for per-lane construction + readout (no
//     stack-local NEON gather arrays).
//   - process() will be marked __attribute__((optimize("no-tree-vectorize"))).
//
// Phase 2+ extends this with: noise oscillator (7th lane via separate state),
// folder, attack tri-mode, mode crossfade.

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#else
#include "../jf/neon_shim.h"
#endif

#include <math.h>
#include <od/config.h>

namespace stolmine
{
  namespace visadhara
  {
    // Spread anchors. CCW (0) = harmonic series 1..6. CW (1) = prime series.
    // Per the manual: "the spread knob and CV control the frequency spacing
    // of the oscillators. This allows the overtone series to vary from a
    // purely harmonic to very inharmonic."
    static const float kHarmonicSeries[6] = {1.0f, 2.0f, 3.0f,  4.0f,  5.0f,  6.0f};
    static const float kPrimeSeries[6]    = {1.0f, 2.0f, 3.0f,  5.0f,  7.0f, 11.0f};

    static inline float spread_mult(int voiceIdx, float spreadPos)
    {
      // Linear interp between harmonic and prime series.
      if (spreadPos < 0.0f) spreadPos = 0.0f;
      if (spreadPos > 1.0f) spreadPos = 1.0f;
      return kHarmonicSeries[voiceIdx] +
             (kPrimeSeries[voiceIdx] - kHarmonicSeries[voiceIdx]) * spreadPos;
    }

    // Harmonic control remap. The native harmonic_voice_params() mapping
    // packs all the timbrally-interesting motion (voices 2-5 amplitudes
    // fading in) into the top ~37% of the input range [0.625, 1.0],
    // while the bottom 62% just toggles voices 0+1 and silently extends
    // decays. This remap expands the interesting region to fill the top
    // two-thirds of the user control:
    //
    //   user 0.000 → native 0.000  (voice 0 alone)
    //   user 0.333 → native 0.625  (voices 2-5 onset boundary)
    //   user 1.000 → native 1.000  (all voices active)
    //
    // Bottom 1/3 of user range compresses the old [0, 0.625] segment;
    // top 2/3 expands the old [0.625, 1.0] segment.
    static inline float remap_harmonic(float user)
    {
      if (user < 0.0f) user = 0.0f;
      if (user > 1.0f) user = 1.0f;
      const float kBoundary = 1.0f / 3.0f;
      if (user < kBoundary)
      {
        // [0, 1/3] → [0, 0.625]  (compress old bottom 2/3 into bottom 1/3)
        return user * (0.625f / kBoundary);   // ×1.875
      }
      else
      {
        // [1/3, 1] → [0.625, 1.0]  (expand old top 1/3 over top 2/3)
        const float t = (user - kBoundary) / (1.0f - kBoundary);
        return 0.625f + t * (1.0f - 0.625f);
      }
    }

    // Harmonic mapping per the manual:
    //   "Fully left the tone produced is a single harmonic tone. From there
    //    to the first quarter a second tone fades in. The remaining turning
    //    extends first the decays then the amplitudes of the other four
    //    harmonics."
    //
    // We split harmonicPos [0..1] into segments:
    //   [0.000, 0.250):  voice 0 amp=1; voice 1 fades 0→1; voices 2..5 silent
    //   [0.250, 0.625):  voice 0+1 active; voices 2..5 decay scalars rise 0→1
    //                    (decay extends so they last longer per trigger)
    //   [0.625, 1.000]:  voice 0+1 active; voices 2..5 decay = 1; their
    //                    amplitudes fade in 0→1
    //
    // Returns per-voice (ampScalar, decayScalar). Voice 0 (fundamental) is
    // always active. Voice 1 onsets in the first segment. Voices 2..5 onset
    // in segments 2 and 3. The decayScalar multiplies the global decay
    // coefficient — decayScalar=0 means "no envelope, voice silent
    // immediately" and decayScalar=1 means "use full global decay".
    static inline void harmonic_voice_params(int voiceIdx, float harmonicPos,
                                              float &amp, float &decayScale)
    {
      if (harmonicPos < 0.0f) harmonicPos = 0.0f;
      if (harmonicPos > 1.0f) harmonicPos = 1.0f;

      if (voiceIdx == 0)
      {
        amp = 1.0f;
        decayScale = 1.0f;
        return;
      }

      if (voiceIdx == 1)
      {
        // Voice 1 fades in across [0, 0.25).
        amp = harmonicPos < 0.25f ? (harmonicPos * 4.0f) : 1.0f;
        decayScale = 1.0f;
        return;
      }

      // Voices 2..5: decay scaling in [0.25, 0.625), amplitude scaling
      // in [0.625, 1.0]. Each voice has a slightly different onset point
      // so the "extension" is progressive, not all-at-once.
      const float voiceFrac = (float)(voiceIdx - 2) / 3.0f;     // 0, 0.33, 0.67, 1
      // Decay segment: voice 2 onset at 0.25, voice 5 onset at 0.5
      const float decayOnset = 0.25f + 0.0625f * voiceFrac;
      const float decayPeak  = 0.625f;
      // Amp segment: voice 2 onset at 0.625, voice 5 onset at 0.85
      const float ampOnset = 0.625f + 0.0625f * voiceFrac;
      const float ampPeak  = 1.0f;

      if (harmonicPos < decayOnset)
      {
        amp = 0.0f;
        decayScale = 0.0f;
      }
      else if (harmonicPos < decayPeak)
      {
        amp = 0.0f;
        decayScale = (harmonicPos - decayOnset) / (decayPeak - decayOnset);
      }
      else if (harmonicPos < ampOnset)
      {
        amp = 0.0f;
        decayScale = 1.0f;
      }
      else
      {
        amp = (harmonicPos - ampOnset) / (ampPeak - ampOnset);
        decayScale = 1.0f;
      }
    }
  }
}
