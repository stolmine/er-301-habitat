#pragma once

// Visadhara — clean-room percussion macro voice based on the public BIA
// technical manual (manuals.noiseengineering.us/bia). Six tonal + one noise
// oscillator across three modes (Skin / Liquid / Metal). Phase 2: Skin mode
// + threshold-reflection folder + tri-mode Attack + LCG noise oscillator.
//
// All virtual implementations defined inline in this header per
// feedback_no_out_of_line_virtuals — vtable must be COMDAT-linked, immune
// to firmware-vs-package vtable drift. Internal struct is fully inlined
// here as well so process() can see it.
//
// Architecture: see planning/bia-clone-scoping.md.
// Implementation plan: see planning/visadhara-initial-pass.md.

#include <od/objects/Object.h>
#include <od/config.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#include "visadhara/voice.h"
#include "visadhara/morph.h"
#include "visadhara/folder.h"
#include "visadhara/noise.h"

namespace stolmine
{

  class Visadhara : public od::Object
  {
  public:
#ifndef SWIGLUA
    struct Internal
    {
      float phase[8];
      float env[8];
      float ampScale[8];
      float decayScale[8];
      float freqMult[8];

      float prevTrig = 0.0f;

      uint32_t noiseLcg = 0xDEADBEEFu;
      int noiseCounter = 0;
      int noiseStride = 64;
      float noiseHeld = 0.0f;
      float noiseBurst = 0.0f;
      float slowAttack = 0.0f;
      float slowAttackInc = 0.0f;

      Internal()
      {
        memset(phase, 0, sizeof(phase));
        memset(env, 0, sizeof(env));
        memset(ampScale, 0, sizeof(ampScale));
        memset(decayScale, 0, sizeof(decayScale));
        for (int i = 0; i < 6; i++) freqMult[i] = visadhara::kHarmonicSeries[i];
        freqMult[6] = 0.0f;
        freqMult[7] = 0.0f;
      }
    };
#endif

    Visadhara()
    {
      addInput(mTrigger);
      addInput(mVOct);
      addOutput(mOut);
      addParameter(mHarmonic);
      addParameter(mSpread);
      addParameter(mMorph);
      addParameter(mFold);
      addParameter(mAttack);
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

    virtual ~Visadhara()
    {
      delete mpInternal;
    }

#ifndef SWIGLUA
    od::Inlet mTrigger{"Trigger"};
    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};

    od::Parameter mHarmonic{"Harmonic", 0.5f};
    od::Parameter mSpread{"Spread", 0.0f};
    od::Parameter mMorph{"Morph", 0.0f};
    od::Parameter mFold{"Fold", 0.0f};
    od::Parameter mAttack{"Attack", 0.0f};
    od::Parameter mDecay{"Decay", 0.5f};
    od::Parameter mLevel{"Level", 0.7f};
    od::Parameter mPitch{"Pitch", 110.0f};

    od::Parameter mMode{"Mode", 0.0f};
    od::Option    mModeSnap{"ModeSnap", 1};
    od::Option    mOctave{"Octave", 2};

    __attribute__((optimize("no-tree-vectorize")))
    virtual void process()
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
      const float foldPos     = mFold.value();
      const float attackPos   = mAttack.value();
      const float decayPos    = mDecay.value();
      const float level       = mLevel.value();
      const float basePitch   = mPitch.value();

      const int octIdx = mOctave.value();
      const float octShift = (octIdx == 1) ? -2.0f
                           : (octIdx == 3) ? +2.0f
                           : 0.0f;

      const float voctV = vOctBuf[0];
      const float pitchInOct = voctV + octShift;
      const float baseFreq = basePitch * powf(2.0f, pitchInOct);

      for (int i = 0; i < 6; i++)
      {
        s.freqMult[i] = visadhara::spread_mult(i, spreadPos);
        visadhara::harmonic_voice_params(i, harmonicPos, s.ampScale[i], s.decayScale[i]);
      }
      s.freqMult[6] = 0.0f;
      s.freqMult[7] = 0.0f;
      s.ampScale[6] = 0.0f;
      s.ampScale[7] = 0.0f;
      s.decayScale[6] = 0.0f;
      s.decayScale[7] = 0.0f;

      const float decayTimeSamples = decayPos * decayPos * 96000.0f + 480.0f;
      const float decayCoeff = expf(-1.0f / decayTimeSamples);

      const float invSr = 1.0f / globalConfig.sampleRate;

      // ---- Phase 2 block-rate setup ----
      const float foldThreshold = visadhara_folder::threshold_from_fold(foldPos);
      s.noiseStride = visadhara_noise::stride_for_freq(baseFreq, globalConfig.sampleRate);
      if (s.noiseStride < 1) s.noiseStride = 1;

      const bool attackNoise = (attackPos < -0.05f);
      const bool attackSlow  = (attackPos > +0.05f);
      const float slowAttackTimeSamples =
        attackSlow ? (attackPos * 0.2f * globalConfig.sampleRate) : 0.0f;
      const float noiseBurstCoeff = expf(-1.0f / (0.015f * globalConfig.sampleRate));

      const float useSlowMask  = (s.slowAttackInc > 0.0f) ? 1.0f : 0.0f;
      const float useDecayMask = 1.0f - useSlowMask;

      // ---- Per-sample inner loop ----
      for (int i = 0; i < frames; i++)
      {
        const float trigNow = trigBuf[i];
        const bool risingEdge = (trigNow > 0.5f) && (s.prevTrig <= 0.5f);
        s.prevTrig = trigNow;

        if (risingEdge)
        {
          if (attackSlow)
          {
            for (int n = 0; n < 6; n++) s.env[n] = 0.0f;
            s.slowAttack = 0.0f;
            s.slowAttackInc = (slowAttackTimeSamples > 0.0f)
                                ? (1.0f / slowAttackTimeSamples)
                                : 1.0f;
          }
          else
          {
            for (int n = 0; n < 6; n++) s.env[n] = 1.0f;
            s.slowAttack = 1.0f;
            s.slowAttackInc = 0.0f;
          }
          if (attackNoise) s.noiseBurst = 1.0f;
        }

        s.slowAttack += s.slowAttackInc;
        if (s.slowAttack > 1.0f) s.slowAttack = 1.0f;

        float sample = 0.0f;
        for (int n = 0; n < 6; n++)
        {
          const float voiceFreq = baseFreq * s.freqMult[n];
          s.phase[n] += voiceFreq * invSr;
          if (s.phase[n] >= 1.0f) s.phase[n] -= floorf(s.phase[n]);

          const float voiceCoeff = decayCoeff * s.decayScale[n];
          const float decayPath = s.env[n] * voiceCoeff;
          const float slowPath = s.slowAttack;
          s.env[n] = slowPath * useSlowMask + decayPath * useDecayMask;

          const float shaped = visadhara_morph::sample(s.phase[n], morphPos);
          sample += shaped * s.env[n] * s.ampScale[n];
        }

        s.noiseLcg = visadhara_noise::lcg_step(s.noiseLcg);
        const float candidateNoise = visadhara_noise::lcg_sample(s.noiseLcg);
        s.noiseCounter--;
        const int roll = (s.noiseCounter <= 0);
        const float rollMask = (float)roll;
        s.noiseHeld = rollMask * candidateNoise + (1.0f - rollMask) * s.noiseHeld;
        s.noiseCounter += roll * s.noiseStride;

        const float noiseAmt = 0.1f + 0.9f * s.noiseBurst;
        sample += s.noiseHeld * noiseAmt;
        s.noiseBurst *= noiseBurstCoeff;

        const float preFold = sample * (1.0f / 7.0f);

        const float folded = visadhara_folder::fold(preFold, foldThreshold);
        const float pulse = visadhara_folder::pulse_mix(folded, foldPos, foldThreshold);
        const float foldedSig = folded + pulse;

        const float finalEnv = s.env[0];
        const float postEnv = foldedSig * finalEnv;

        outBuf[i] = postEnv * level;
      }
    }
#endif

  private:
    Internal *mpInternal = nullptr;
  };

} // namespace stolmine
