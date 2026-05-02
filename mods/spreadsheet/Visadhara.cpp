// Visadhara — clean-room percussion macro voice. Phase 1: Skin mode skeleton.
// See planning/visadhara-initial-pass.md.

#include "Visadhara.h"
#include "visadhara/voice.h"
#include "visadhara/morph.h"

#include <od/config.h>
#include <math.h>
#include <string.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#else
#include "jf/neon_shim.h"
#endif

namespace stolmine
{
  // 6-voice state. Lanes 6,7 of the second NEON quad are masked off (gate=0,
  // amp=0), so they don't contribute to the output. Class-member arrays per
  // feedback_neon_intrinsics_drumvoice.
  struct Visadhara::Internal
  {
    // Per-voice phase accumulator [0, 1). 8 lanes (2 NEON quads).
    float phase[8];
    // Per-voice envelope state [0, 1]. AR with no sustain: rises to 1 on
    // trigger rising edge, decays exponentially.
    float env[8];
    // Per-voice amplitude scalar (set per block from Harmonic param).
    float ampScale[8];
    // Per-voice decay scalar (multiplies global decay coefficient).
    float decayScale[8];
    // Per-voice frequency multiplier (set per block from Spread param).
    float freqMult[8];

    // Edge detector state for the trigger inlet.
    float prevTrig = 0.0f;

    Internal()
    {
      memset(phase, 0, sizeof(phase));
      memset(env, 0, sizeof(env));
      memset(ampScale, 0, sizeof(ampScale));
      memset(decayScale, 0, sizeof(decayScale));
      // Default freqMult to harmonic series in case process runs before
      // any Spread param read.
      for (int i = 0; i < 6; i++) freqMult[i] = visadhara::kHarmonicSeries[i];
      freqMult[6] = 0.0f;
      freqMult[7] = 0.0f;
    }
  };

  Visadhara::Visadhara()
  {
    addInput(mTrigger);
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mHarmonic);
    addParameter(mSpread);
    addParameter(mMorph);
    addParameter(mDecay);
    addParameter(mLevel);
    addParameter(mPitch);
    addParameter(mMode);
    addOption(mModeSnap);
    addOption(mOctave);
    mModeSnap.enableSerialization();
    mOctave.enableSerialization();

    mpInternal = new Internal();
  }

  Visadhara::~Visadhara()
  {
    delete mpInternal;
  }

  // Per-voice frequency in Hz given base pitch + octave + spread + V/Oct CV.
  // Block-rate computation; per-sample updates are just phase accumulation.
  __attribute__((optimize("no-tree-vectorize")))
  void Visadhara::process()
  {
    Internal &s = *mpInternal;
    const int frames = FRAMELENGTH;
    float *outBuf = mOut.buffer();
    float *trigBuf = mTrigger.buffer();
    float *vOctBuf = mVOct.buffer();

    // ---- Block-rate parameter reads ----
    const float harmonicPos = mHarmonic.value();
    const float spreadPos   = mSpread.value();
    const float morphPos    = mMorph.value();
    const float decayPos    = mDecay.value();
    const float level       = mLevel.value();
    const float basePitch   = mPitch.value();

    // Octave: 1=Bass(-2), 2=Alto(0), 3=Treble(+2). Stored as int via
    // od::Option (so 1/2/3 indexing works around CHOICE_UNKNOWN sentinel
    // per feedback_option_vs_parameter).
    const int octIdx = mOctave.value();
    const float octShift = (octIdx == 1) ? -2.0f
                         : (octIdx == 3) ? +2.0f
                         : 0.0f;

    // V/Oct: read at block start (per-sample tracking arrives later).
    // Standard 10x scaling pattern: buffer carries 0.1V/octave so we
    // multiply by 10 to get 1V/octave inside C++.
    const float voctV = vOctBuf[0];
    const float pitchInOct = voctV + octShift;
    const float baseFreq = basePitch * powf(2.0f, pitchInOct);

    // ---- Per-voice block-rate setup ----
    // Spread → frequency multipliers; Harmonic → amp + decay scalars.
    for (int i = 0; i < 6; i++)
    {
      s.freqMult[i] = visadhara::spread_mult(i, spreadPos);
      visadhara::harmonic_voice_params(i, harmonicPos, s.ampScale[i], s.decayScale[i]);
    }
    // Mask off lanes 6,7
    s.freqMult[6] = 0.0f;
    s.freqMult[7] = 0.0f;
    s.ampScale[6] = 0.0f;
    s.ampScale[7] = 0.0f;
    s.decayScale[6] = 0.0f;
    s.decayScale[7] = 0.0f;

    // Decay coefficient: map decayPos [0..1] to per-sample multiplier.
    // We want a perceptually-linear decay-time control: longer decay at
    // higher decayPos. exp(-1/decayTimeInSamples) is the per-sample factor.
    // decayTimeInSamples = decayPos² * 96000 + 480 (≈10ms..2s range).
    const float decayTimeSamples = decayPos * decayPos * 96000.0f + 480.0f;
    const float decayCoeff = expf(-1.0f / decayTimeSamples);

    const float invSr = 1.0f / globalConfig.sampleRate;

    // ---- Per-sample inner loop ----
    for (int i = 0; i < frames; i++)
    {
      // Trigger edge detection (per feedback_comparator_gate_threshold).
      const float trigNow = trigBuf[i];
      const bool risingEdge = (trigNow > 0.5f) && (s.prevTrig <= 0.5f);
      s.prevTrig = trigNow;

      // Reset envelope on rising edge — instant attack for Phase 1
      // (Phase 2 adds tri-mode Attack with noise/instant/slow variants).
      if (risingEdge)
      {
        for (int n = 0; n < 6; n++)
        {
          s.env[n] = 1.0f;
          // Don't reset phase — let voices freerun for harmonic-stack
          // continuity (alternative: reset to 0 for percussive pop;
          // tune by ear during Phase 1 hardware test).
        }
      }

      // Per-voice update + sum.
      float sample = 0.0f;
      for (int n = 0; n < 6; n++)
      {
        // Phase advance.
        const float voiceFreq = baseFreq * s.freqMult[n];
        s.phase[n] += voiceFreq * invSr;
        // Wrap phase to [0, 1).
        if (s.phase[n] >= 1.0f) s.phase[n] -= floorf(s.phase[n]);

        // Envelope decay. Per-voice decayScale modulates the global rate.
        const float voiceCoeff = decayCoeff * s.decayScale[n] +
                                 (1.0f - s.decayScale[n]) * 0.0f;
        // When decayScale=0, voice silenced fast. When decayScale=1, full
        // global decay. Linear interp between (decayScale=0 → coeff=0
        // → env collapses to 0 in one sample).
        s.env[n] *= voiceCoeff;

        // Voice output: shaped waveform × envelope × amp scalar.
        const float shaped = visadhara_morph::sample(s.phase[n], morphPos);
        sample += shaped * s.env[n] * s.ampScale[n];
      }

      // Final scale by Level and write out.
      // Phase 2 will add the folder + final-envelope-reapply here.
      outBuf[i] = sample * level * (1.0f / 6.0f);
    }
  }

} // namespace stolmine
