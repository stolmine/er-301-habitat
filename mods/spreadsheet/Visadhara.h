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
      // Harmonic remap: expand the timbrally-interesting top-third of
      // the native mapping to cover the top two-thirds of the user
      // control. See voice.h::remap_harmonic.
      const float harmonicPos = visadhara::remap_harmonic(mHarmonic.value());
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
      // Folder is drive-based (Buchla 259/281 topology): fixed threshold,
      // variable input drive. Drive curve in folder.h.
      const float foldDrive    = visadhara_folder::drive_from_fold(foldPos);
      const float foldThreshold = 1.0f;
      const float postFoldGain = visadhara_folder::post_fold_gain(foldPos);

      // Attack semantic:
      //   attackPos > +0.05 → slow ramp (linear AR rise time)
      //   |attackPos| ≤ 0.05 → instant attack
      //   attackPos < -0.05 → instant attack + continuous white-noise mix,
      //                       proportional to |attackPos|
      const bool attackSlow  = (attackPos > +0.05f);
      const float slowAttackTimeSamples =
        attackSlow ? (attackPos * 0.2f * globalConfig.sampleRate) : 0.0f;
      const float noiseMix = (attackPos < 0.0f) ? -attackPos : 0.0f;

      const float useSlowMask  = (s.slowAttackInc > 0.0f) ? 1.0f : 0.0f;
      const float useDecayMask = 1.0f - useSlowMask;

      // Voice-bus perceptual gain. 6 voices sum unchecked into the bus
      // for the additive "large" character; this 1/3 keeps the bus
      // peak (~6 worst case) in a useful pre-drive range.
      const float voiceGain = 1.0f / 3.0f;

      // ---- Per-sample inner loop ----
      for (int i = 0; i < frames; i++)
      {
        const float trigNow = trigBuf[i];
        const bool risingEdge = (trigNow > 0.5f) && (s.prevTrig <= 0.5f);
        s.prevTrig = trigNow;

        if (risingEdge)
        {
          // Phase reset: snap all 6 voices to phase=0 so they start
          // coherent. For sine/triangle morph positions this is a
          // clean zero crossing (output=0 at phase=0). For saw/square
          // there's still a residual step discontinuity; mitigation
          // for those corners is queued for Phase 5 polish.
          for (int n = 0; n < 6; n++) s.phase[n] = 0.0f;

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
        }

        // Slow-attack ramp: always advance, clamp at 1, reset Inc on hit
        // (single one-shot store; not differential heavy work). Once Inc
        // is 0 the next block flips useSlowMask to 0 and the env
        // transitions cleanly to the decay path.
        s.slowAttack += s.slowAttackInc;
        if (s.slowAttack > 1.0f)
        {
          s.slowAttack = 1.0f;
          s.slowAttackInc = 0.0f;
        }

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

        // Per-sample white noise: simple LCG, no S&H decimation. Mixed
        // proportional to noiseMix (block-rate constant from |attackPos|
        // when attackPos < 0). At attackPos ≥ 0 noiseMix is 0 so noise
        // is silent regardless of LCG state.
        s.noiseLcg = visadhara_noise::lcg_step(s.noiseLcg);
        const float whiteNoise = visadhara_noise::lcg_sample(s.noiseLcg);

        // Mix: voice bus + white-noise injection, then drive into the
        // folder (drive ramps with fold). The unattenuated voice sum is
        // what gives the additive "large" character.
        const float bussed = sample * voiceGain + whiteNoise * noiseMix;
        const float preFold = bussed * foldDrive;

        const float folded = visadhara_folder::fold(preFold, foldThreshold);
        const float pulse = visadhara_folder::pulse_mix(folded, foldPos, foldThreshold);
        const float foldedSig = folded + pulse;

        // Post-fold compensation gain: brings unfolded signal up to
        // perceived parity with folded signal (gentle, max 2.5× at
        // fold=0, unity at fold=1).
        const float compensated = foldedSig * postFoldGain;

        // Re-apply global envelope (voice-0 envelope as Phase 2 proxy
        // per the BIA designer note about restoring post-fold dynamics).
        const float finalEnv = s.env[0];
        const float postEnv = compensated * finalEnv;

        outBuf[i] = postEnv * level;
      }
    }
#endif

  private:
    Internal *mpInternal = nullptr;
  };

} // namespace stolmine
