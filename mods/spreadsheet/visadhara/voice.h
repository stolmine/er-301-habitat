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
    // Spread anchors. CCW (0) = harmonic series 1..8. CW (1) = prime series.
    // Per the manual: "the spread knob and CV control the frequency spacing
    // of the oscillators. This allows the overtone series to vary from a
    // purely harmonic to very inharmonic."
    //
    // 8 voices vs the BIA's 6: NEON 4×2 layout processes 8 lanes whether
    // they're active or masked, so activating voices 6 & 7 costs zero
    // additional ops and adds 7th/8th harmonics (and primes 13/17 on the
    // inharmonic end) to the bus.
    static const float kHarmonicSeries[8] = {1.0f, 2.0f, 3.0f, 4.0f,
                                              5.0f, 6.0f, 7.0f, 8.0f};
    static const float kPrimeSeries[8]    = {1.0f, 2.0f, 3.0f, 5.0f,
                                              7.0f, 11.0f, 13.0f, 17.0f};

    // Per-voice phase offset at trigger reset. Quasi-random
    // (golden-ratio derived) so voices NEVER fall into a coherent
    // alignment or a perfect-cancellation pattern.
    //
    // Previous attempt at (i+0.5)/8 evenly-spaced offsets was a
    // mathematical own-goal: those are the DFT sampling phases for
    // the fundamental, and the 8th roots of unity sum to ZERO. So
    // 8 voices at the same fundamental frequency cancel completely
    // for ALL t — total silence in the kick body at Harmonic=0.
    //
    // Golden-ratio offsets i * φ mod 1 (φ = (√5-1)/2 ≈ 0.6180) are
    // the most-irrational distribution: no integer multiple of φ
    // lands on a rational fraction, so voices can never align in
    // an evenly-spaced cancelling configuration via drift either.
    //
    // Properties:
    //   - Voice 0 at phase 0 — clean zero-crossing start for the
    //     Liquid bend gesture (the anchor character).
    //   - Voices 1-7 at golden-ratio phases — decoherent body.
    //   - Expected sum amplitude ~√8 ≈ 2.83 (RMS sum of N
    //     uncorrelated phases), about 3× less than the prior
    //     coherent peak of 8 BUT far from zero. Eliminates the
    //     splatty/aliased folder over-saturation at Harmonic=0
    //     while keeping the kick body audible.
    //   - Sum at t=0 is small but non-zero (~−0.16 for the 7
    //     non-anchor voices); inaudible as a click.
    static const float kPhaseOffset[8] = {
        0.0000f,   // voice 0 — anchor (0 · φ)
        0.6180f,   // voice 1 — 1 · φ mod 1
        0.2361f,   // voice 2 — 2 · φ mod 1
        0.8541f,   // voice 3 — 3 · φ mod 1
        0.4721f,   // voice 4
        0.0902f,   // voice 5
        0.7082f,   // voice 6
        0.3262f    // voice 7
    };

    // Per-voice fixed detune. Asymmetric ~3-cent spread emulates BIA's
    // analog oscillator drift; non-symmetric so beat patterns between
    // adjacent voices stay irregular across chord-style spread sweeps.
    // Applied once at block-rate freqMult assignment, compounds naturally
    // with Liquid mode pitchSweep.
    static const float kVoiceDetune[8] = {
        0.99885f,   // voice 0  −2.0 cents
        1.00115f,   // voice 1  +2.0 cents
        0.99942f,   // voice 2  −1.0 cents
        1.00173f,   // voice 3  +3.0 cents
        0.99827f,   // voice 4  −3.0 cents
        1.00058f,   // voice 5  +1.0 cents
        1.00144f,   // voice 6  +2.5 cents
        0.99913f    // voice 7  −1.5 cents
    };

    // Liquid-mode per-voice pitch-sweep depth. Voice 0 (fundamental)
    // carries the bend as a clear gesture; upper voices contribute
    // timbral support without participating in the sweep. Punch is
    // a perceptual figure/ground effect: ONE element bending against
    // a stable harmonic context, NOT every voice shifting in lockstep
    // (which reads as "the whole spectrum bent" → diffuse).
    //
    // Weight 0 collapses a voice's sweep to multiplier=1.0 (no bend);
    // its kSweepTauMs is therefore moot but kept 0 for documentation.
    //
    // Tuned for TIGHT character (research, 2026-05-13): short time
    // constants + deep peak give a clicky transient bend that settles
    // quickly into the clean sub-fundamental. "Depth" of the kick
    // body comes from the (separate) amplitude Decay parameter, not
    // from a sustained pitch bend. Upper-voice weights light: clicky
    // inflection only, no sustained ride.
    static const float kSweepWeight[8] = {
        1.00f,   // voice 0 — full bend depth (× kPitchSweepPeak octave)
        0.40f,   // voice 1 — companion bend
        0.15f,   // voice 2 — clicky inflection
        0.05f,   // voice 3 — micro-inflection
        0.02f,   // voice 4 — barely audible
        0.00f,   // voice 5 — silent on the gesture
        0.00f,   // voice 6
        0.00f    // voice 7
    };

    // Per-voice pitch-envelope time constants (ms). Short settle on
    // the fundamental so the bend is a percussive transient, not a
    // sustained slide. Upper voices settle even faster — they
    // contribute to the "click" character without competing with
    // voice 0's gesture. Voices with weight=0 ignore their tau.
    //
    // 25 ms τ on voice 0 → perceptible bend complete in ~75 ms
    // (3 time constants), leaving the rest of the hit as clean
    // sub-fundamental ringing under the amplitude envelope.
    static const float kSweepTauMs[8] = {
        25.0f,   // voice 0 — tight settle, percussion-appropriate
        15.0f,   // voice 1
        10.0f,   // voice 2
        6.0f,    // voice 3
        4.0f,    // voice 4
        0.0f, 0.0f, 0.0f
    };

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
    //   [0.000, 0.250):  voice 0 amp=1; voice 1 fades 0→1; voices 2..7 silent
    //   [0.250, 0.625):  voice 0+1 active; voices 2..7 decay scalars rise 0→1
    //                    (decay extends so they last longer per trigger)
    //   [0.625, 1.000]:  voice 0+1 active; voices 2..7 decay = 1; their
    //                    amplitudes fade in 0→1
    //
    // Returns per-voice (ampScalar, decayScalar). Voice 0 (fundamental) is
    // always active. Voice 1 onsets in the first segment. Voices 2..7 onset
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

      // Voices 2..7: decay scaling in [0.25, 0.625), amplitude scaling
      // in [0.625, 1.0]. Each voice has a slightly different onset point
      // so the "extension" is progressive, not all-at-once. Divisor 5
      // covers the 6 voices in this segment (voiceIdx 2..7 → voiceFrac
      // 0, 0.2, 0.4, 0.6, 0.8, 1.0).
      const float voiceFrac = (float)(voiceIdx - 2) / 5.0f;
      // Decay segment: voice 2 onset at 0.25, voice 7 onset at ~0.31.
      const float decayOnset = 0.25f + 0.0625f * voiceFrac;
      const float decayPeak  = 0.625f;
      // Amp segment: voice 2 onset at 0.625, voice 7 onset at ~0.69.
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
