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
#include "visadhara/pmm.h"

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

      float slowAttack = 0.0f;
      float slowAttackInc = 0.0f;

      // Liquid mode (Phase 3): per-trigger pitch envelope. Decays
      // exponentially over ~30ms after each rising edge. Multiplies
      // every voice's phase-increment by (1 + liquidSweepAmt * pitchEnv)
      // when liquidAmt > 0.
      float pitchEnv = 0.0f;

      // Metal mode (Phase 4): two parallel 3-op PMM voice chains.
      // Output is sum of pair1.tick() + pair2.tick(). Always ticked
      // every sample so smooth crossfade through Metal territory works
      // without per-sample dispatch (per
      // feedback_runtime_branched_dsp_dispatch).
      visadhara_pmm::Voice pmm1;
      visadhara_pmm::Voice pmm2;

      Internal()
      {
        memset(phase, 0, sizeof(phase));
        memset(env, 0, sizeof(env));
        memset(ampScale, 0, sizeof(ampScale));
        memset(decayScale, 0, sizeof(decayScale));
        for (int i = 0; i < 6; i++) freqMult[i] = visadhara::kHarmonicSeries[i];
        freqMult[6] = 0.0f;
        freqMult[7] = 0.0f;
        visadhara_pmm::reset(pmm1);
        visadhara_pmm::reset(pmm2);
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
      // Raw user harmonic position (0..1) is used for Metal ratio
      // blending. The Skin path uses a remapped version that expands
      // the timbrally-interesting top-third of the voice-activation
      // curve to the top two-thirds of the control (voice.h).
      const float harmonicPosUser = mHarmonic.value();
      const float harmonicPos = visadhara::remap_harmonic(harmonicPosUser);
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
        s.freqMult[i] = visadhara::spread_mult(i, spreadPos) * visadhara::kVoiceDetune[i];
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

      // Block-rate morph crossfade weights. Equal-power (sqrt) curves
      // for level-flat sweep across the four shape anchors. Computed
      // once here; per-sample sample_w() is branchless.
      const visadhara_morph::Weights morphW =
        visadhara_morph::compute_weights(morphPos);

      // ---- Phase 2 block-rate setup ----
      // Folder is drive-based (Buchla 259/281 topology): fixed threshold,
      // variable input drive. Drive curve in folder.h.
      const float foldDrive    = visadhara_folder::drive_from_fold(foldPos);
      const float foldThreshold = 1.0f;
      const float postFoldGain = visadhara_folder::post_fold_gain(foldPos);

      // ---- Phase 3 block-rate setup: Mode dispatch + Liquid sweep ----
      // Mode parameter is continuous 0..2: 0=Skin, 1=Liquid, 2=Metal.
      // Block-rate dispatch builds three mode-amount masks; the Liquid
      // path's pitch-envelope-driven frequency boost is gated by
      // liquidAmt, and the bus output is gated by (skinAmt + liquidAmt)
      // so Metal territory currently silences (Phase 4 wires Metal in).
      const float modeRaw = mMode.value();
      const int modeSnapVal = mModeSnap.value();        // 1=smooth, 2=snap
      const bool snapMode = (modeSnapVal == 2);
      float skinAmt, liquidAmt, metalAmt;
      if (snapMode)
      {
        // Hard snap: nearest-mode dispatch.
        int m = (int)(modeRaw + 0.5f);
        if (m < 0) m = 0; if (m > 2) m = 2;
        skinAmt   = (m == 0) ? 1.0f : 0.0f;
        liquidAmt = (m == 1) ? 1.0f : 0.0f;
        metalAmt  = (m == 2) ? 1.0f : 0.0f;
      }
      else
      {
        // Smooth: tent function across adjacent modes.
        float c = modeRaw < 0.0f ? 0.0f : (modeRaw > 2.0f ? 2.0f : modeRaw);
        skinAmt   = (c < 1.0f) ? (1.0f - c) : 0.0f;
        liquidAmt = (c < 1.0f) ? c : ((c < 2.0f) ? (2.0f - c) : 0.0f);
        metalAmt  = (c > 1.0f) ? (c - 1.0f) : 0.0f;
      }
      // Liquid pitch-sweep peak: +1 octave (factor 2 - 1 = 1.0). Times
      // the mode-blend amount. BIA hardware has a fixed pitch sweep
      // character; this matches the audible bend depth in the manual's
      // demos / reference recordings.
      const float kPitchSweepPeak = 1.0f;
      const float liquidSweepAmt = liquidAmt * kPitchSweepPeak;
      // Pitch-envelope decay time constant: 50ms.
      const float pitchEnvCoeff = expf(-1.0f / (0.050f * globalConfig.sampleRate));
      const float skinLiquidPresence = skinAmt + liquidAmt;

      // ---- Phase 4 block-rate setup: Metal mode PMM ----
      // Two 3-op PMM pairs. Each pair's operator ratios are
      // continuously blended between a clean integer-harmonic anchor
      // (Harmonic CCW) and the natural inharmonic / metallic values
      // (Harmonic CW). Spread additionally boosts pair 2's FM indices
      // for more chaotic character at higher Spread settings.
      //
      //   Pair 1 anchors:  [1, 2, 3]      natural: [1.0, 1.5, 2.0]
      //   Pair 2 anchors:  [1, 2, 4]      natural: [1.7, 2.3, 3.5]
      const float h = harmonicPosUser;     // 0..1 raw
      const float pmm1_r1 = 1.0f + (1.0f - 1.0f) * h;       // ratio op1 (always 1)
      const float pmm1_r2 = 2.0f + (1.5f - 2.0f) * h;       // 2.0 → 1.5
      const float pmm1_r3 = 3.0f + (2.0f - 3.0f) * h;       // 3.0 → 2.0
      const float pmm2_r1 = 1.0f + (1.7f - 1.0f) * h;       // 1.0 → 1.7
      const float pmm2_r2 = 2.0f + (2.3f - 2.0f) * h;       // 2.0 → 2.3
      const float pmm2_r3 = 4.0f + (3.5f - 4.0f) * h;       // 4.0 → 3.5

      const float pmm1_inc1 = baseFreq * pmm1_r1 * invSr;
      const float pmm1_inc2 = baseFreq * pmm1_r2 * invSr;
      const float pmm1_inc3 = baseFreq * pmm1_r3 * invSr;
      const float pmm1_fb    = 0.30f;
      const float pmm1_mod12 = 0.60f;
      const float pmm1_mod23 = 0.80f;

      const float pair2Boost = 1.0f + spreadPos * 1.5f;
      const float pmm2_inc1 = baseFreq * pmm2_r1 * invSr;
      const float pmm2_inc2 = baseFreq * pmm2_r2 * invSr;
      const float pmm2_inc3 = baseFreq * pmm2_r3 * invSr;
      const float pmm2_fb    = 0.30f * pair2Boost;
      const float pmm2_mod12 = 0.60f * pair2Boost;
      const float pmm2_mod23 = 0.80f * pair2Boost;

      // Metal bus perceptual gain: PMM produces a denser waveform with
      // higher peak-to-RMS ratio than the Skin additive bus, so it
      // measures perceptually louder at the same nominal peak. Scale
      // down to match Skin/Liquid loudness.
      const float kMetalBusGain = 0.35f;

      // Attack semantic:
      //   attackPos > +0.05 → slow ramp (linear AR rise time)
      //   |attackPos| ≤ 0.05 → instant attack
      //   attackPos < -0.05 → instant attack + cross-mode injection
      //                       (Metal bus into Skin/Liquid, additive bus
      //                       into Metal), amount proportional to
      //                       |attackPos|. See injection block below.
      const bool attackSlow  = (attackPos > +0.05f);
      const float slowAttackTimeSamples =
        attackSlow ? (attackPos * 0.2f * globalConfig.sampleRate) : 0.0f;
      const float injectMix = (attackPos < 0.0f) ? -attackPos : 0.0f;

      const float useSlowMask  = (s.slowAttackInc > 0.0f) ? 1.0f : 0.0f;
      const float useDecayMask = 1.0f - useSlowMask;

      // Voice-bus perceptual gain. 6 voices sum unchecked into the bus
      // for the additive "large" character; 1/2 brings the realistic
      // 2-3 voice RMS into a useful pre-drive range while preserving
      // the additive headroom for full-harmonic configurations.
      const float voiceGain = 0.5f;

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

          // Liquid mode pitch envelope: kick to 1.0 on rising edge.
          // Modulates freq by (1 + liquidSweepAmt * pitchEnv) per voice
          // until envelope decays back to 0.
          s.pitchEnv = 1.0f;

          // Metal mode PMM phase reset — same coherent-attack discipline
          // as Skin/Liquid voices. lastOut values reset too so feedback
          // doesn't hang carryover from the previous trigger.
          visadhara_pmm::reset(s.pmm1);
          visadhara_pmm::reset(s.pmm2);
        }

        // Pitch envelope decay (per-sample, always running so smooth
        // crossfade works without per-block dispatch).
        s.pitchEnv *= pitchEnvCoeff;

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

        // Liquid pitch-sweep multiplier. Block-rate liquidSweepAmt × the
        // per-sample pitchEnv. Branchless: skinAmt path naturally gives
        // multiplier=1 (no sweep) since liquidSweepAmt=0 there.
        const float pitchSweep = 1.0f + liquidSweepAmt * s.pitchEnv;

        float sample = 0.0f;
        for (int n = 0; n < 6; n++)
        {
          const float voiceFreq = baseFreq * s.freqMult[n] * pitchSweep;
          s.phase[n] += voiceFreq * invSr;
          if (s.phase[n] >= 1.0f) s.phase[n] -= floorf(s.phase[n]);

          const float voiceCoeff = decayCoeff * s.decayScale[n];
          const float decayPath = s.env[n] * voiceCoeff;
          const float slowPath = s.slowAttack;
          s.env[n] = slowPath * useSlowMask + decayPath * useDecayMask;

          const float shaped = visadhara_morph::sample_w(s.phase[n], morphW);
          sample += shaped * s.env[n] * s.ampScale[n];
        }

        // Metal PMM tick — always run so smooth mode crossfade works
        // without per-sample dispatch (heavy work outside conditionals).
        // Morph sweeps each operator's waveshape (sine→tri→saw→sq)
        // through the FM chain — saw-FM and square-FM produce
        // dramatically different timbres than sine-FM.
        const float pmmA = visadhara_pmm::tick(s.pmm1, pmm1_inc1, pmm1_inc2, pmm1_inc3,
                                                pmm1_fb, pmm1_mod12, pmm1_mod23, morphW);
        const float pmmB = visadhara_pmm::tick(s.pmm2, pmm2_inc1, pmm2_inc2, pmm2_inc3,
                                                pmm2_fb, pmm2_mod12, pmm2_mod23, morphW);
        const float metalBus = (pmmA + pmmB) * kMetalBusGain;

        // Mode-blended source. Skin/Liquid additive bus uses voiceGain
        // to bring its 6-voice sum (peak ~6) into ~peak 2; Metal PMM
        // bus is loudness-normalized (kMetalBusGain) before mixing.
        const float skinLiquidPart = sample * voiceGain;
        const float modeBus = skinLiquidPresence * skinLiquidPart
                            + metalAmt * metalBus;

        // Cross-mode injection (negative-Attack region). Replaces the
        // simple white-noise injection: when in Skin/Liquid territory
        // we inject the Metal bus (PMM at the current panel settings);
        // when in Metal we inject the Skin/Liquid bus. Smooth blend
        // through the crossfade region. Free CPU since both buses are
        // already computed branchlessly.
        const float injectionSignal = skinLiquidPresence * metalBus
                                    + metalAmt * skinLiquidPart;

        // Mix: mode bus + cross-mode injection, then drive into folder.
        const float bussed = modeBus + injectionSignal * injectMix;
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

        // Master soft saturator: catches residual peaks at extreme
        // settings (Metal + max fold + cross-injection) without
        // introducing dynamics artifacts. Output bounded ±1.
        outBuf[i] = visadhara_folder::master_sat(postEnv * level);
      }
    }
#endif

  private:
    Internal *mpInternal = nullptr;
  };

} // namespace stolmine
