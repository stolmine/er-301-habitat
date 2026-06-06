// house::Carriage
//
// Fourth chain-as-unit in the house package. First dynamics-
// character unit in the catalog. Inverse-threshold engagement
// detector drives transient injection (Point) and orthogonal air
// absorption (Distance2). Self-balancing: the unit gets MORE
// active on flat material and LESS active on already-dynamic
// material.
//
// Identity in one sentence: gets less active the more the input
// does on its own.
//
// MONOLITHIC per chain-unit convention (TickerTape, Lacquer,
// Filament). Console0 sat math inlined, PointMono and
// Distance2Mono used as helpers, engagement detector + Form
// blend + competition feedback all inline.
//
// PARAMETERS (5 plies, all continuous w/ standard coarse/fine):
//   Drive  - Console0Channel + Buss gain (symmetric, transparent
//            at 0.5). Standard for chain units.
//   Reach  - Engagement amount. At 0: Point near identity and
//            Distance2 inert regardless of engagement. At 1:
//            full inverse-compressor depth (~16× nob/nib disparity
//            → strong leading-edge bite, Console0Buss catches the
//            peaks as natural saturation). Couples Point's
//            transient-boost bias AND Point's time scale C on a
//            shared curve.
//   Form   - Engagement direction (bipolar around 0.5). Controls
//            WHEN the unit engages.
//              Form=0:   anti-compressor — engages on flat material,
//                        backs off on transients. (Original Carriage
//                        identity.)
//              Form=0.5: constant 50% engagement regardless of input.
//                        Always-on, neutral character.
//              Form=1:   regular compressor — engages on transients,
//                        idle on flat. Point fires hot on attacks,
//                        Distance2 darkens the loud peaks. Classic
//                        dynamics processing.
//            Genuinely orthogonal to Air (which is spectral) and
//            interacts strongly with Reach: the same Reach value
//            produces totally different sonic context depending on
//            whether transients are being injected into stillness
//            (Form=0) or processed on motion (Form=1).
//   Air    - Distance2 wet amount AND filter darkness. Engagement
//            modulates this internally: at high engagement (flat
//            input being aggressively reshaped) → MORE active
//            air; at low engagement (dynamic input riding
//            through) → LESS active air. The orthogonal axis.
//   Mix    - ChainMix dry/wet (default 1.0 full wet).
//
// ENGAGEMENT MECHANIC (the novel core):
//   Two asymmetric envelope followers (per
//   feedback_asymmetric_envelope_follower):
//     - peakEnv:  ~5ms attack, ~50ms release  (transient tracker)
//     - levelEnv: ~50ms attack, ~500ms release (program-level tracker)
//   ratio = peakEnv / (levelEnv + eps), clamped ≥ 1
//   rs = 4 * (ratio - 1)
//   rawEng = rs / (1 + rs)
//     ∈ [0, 1): rises on transients, 0 on flat.
//   engagement = formInv * (1 - rawEng) + formKnob * rawEng
//     - Form=0: engagement = 1 - rawEng (anti-compressor — high on flat)
//     - Form=1: engagement = rawEng     (compressor — high on transient)
//     - Form=0.5: engagement = 0.5 constant (neutral, no dynamics dep.)
//   Bounded [0, 1] by construction at any Form.
//
// INPUT-OUTPUT COMPETITION FEEDBACK (default-on):
//   The envelope source is |envSrc - delayedOutput| where
//   delayedOutput is the unit's own post-processed signal
//   delayed 8 samples and Spiral-wrapped (mechanic #1 per
//   feedback_spiral_feedback_governor). When the unit's
//   manufactured transients match input character, the delta is
//   small → engagement stays high → keeps working. When natural
//   transients arrive in the input that the unit didn't predict,
//   the delta spikes → engagement drops → backs off. Pushes
//   harder when losing the race.
//
// INVISIBLE PARAMETER CROSS-COUPLING (per
// feedback_invisible_param_cross_coupling):
//   - Point B-boost bias = engagement × Reach × 0.47 (0 = identity,
//                         where engagement is post-Form-direction-
//                         flip — see ENGAGEMENT MECHANIC below)
//   - Point C-knob       = 0.3 + Reach × 0.4         (slow → fast)
//   - Distance2 wet      = Air × smoothedEngagement
//     (smoothedEngagement = one-pole-smoothed engagement, ~30ms
//      TC — lingers slightly so the air dimension hangs around
//      briefly after dynamics arrive)
//
// AW SCALAR REMAPS APPLIED (per feedback_aw_param_default_subtle):
//   - Point's bipolar B (0.5 = identity) NEVER exposed; only
//     positive boost bias driven by engagement × Reach. User
//     sees no identity zone.
//   - Point's A (input gain trim) fixed at unity — Drive already
//     handles input level.
//   - Distance2's B (filter darken) fixed at 0.4 internally —
//     gives characterful HF rolloff without exposing a separate
//     knob inside the air axis.
//
// TONALLY-RELEVANT GOVERNORS (per feedback_no_paths_of_least_resistance):
//   1. Spiral wrap on competition feedback path — runaway becomes
//      saturating ring instead of clipping
//   2. Engagement self-limit by construction — successful injection
//      drops engagement, no external limiter needed
//   3. Distance2's slew cascade IS the air mechanic, not a band-aid
//   4. Console0 pair wraps whole chain — level-dependent containment
//
// CPU PROJECTION: ~100 cycles/sample/side, ~5-8% stereo on
// Cortex-A8. Comparable to Lacquer and Filament.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <string.h>

#include "Point.h"
#include "Distance2.h"
#include "Spiral.h"

namespace house
{

  class Carriage : public od::Object
  {
  public:
    Carriage()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mDrive);
      addParameter(mReach);
      addParameter(mForm);
      addParameter(mAir);
      addParameter(mMix);

      mConsoleChAvgAL = mConsoleChAvgAR = mConsoleChAvgBL = mConsoleChAvgBR = 0.0;
      mConsoleBsAvgAL = mConsoleBsAvgAR = mConsoleBsAvgBL = mConsoleBsAvgBR = 0.0;
      mPointL.reset();
      mPointR.reset();
      mDist2L.reset();
      mDist2R.reset();
      mPeakEnvL = mPeakEnvR = 0.0;
      mLevelEnvL = mLevelEnvR = 0.0;
      mSmoothEngL = mSmoothEngR = 0.0;
      memset(mFbDelayL, 0, sizeof(mFbDelayL));
      memset(mFbDelayR, 0, sizeof(mFbDelayR));
      mFbIdxL = mFbIdxR = 0;
    }

    virtual ~Carriage() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mDrive{"Drive", 0.5f};
    od::Parameter mReach{"Reach", 0.6f};
    od::Parameter mForm{"Form", 0.4f};
    od::Parameter mAir{"Air", 0.5f};
    od::Parameter mMix{"Mix", 1.0f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      // ===== Block-rate scalar baking =====

      double drive = (double)mDrive.value();
      if (drive < 0.0) drive = 0.0;
      if (drive > 1.0) drive = 1.0;
      double driveGain = 0.05 + drive * 1.9;

      double reachKnob = (double)mReach.value();
      if (reachKnob < 0.0) reachKnob = 0.0;
      if (reachKnob > 1.0) reachKnob = 1.0;

      double formKnob = (double)mForm.value();
      if (formKnob < 0.0) formKnob = 0.0;
      if (formKnob > 1.0) formKnob = 1.0;
      double formInv = 1.0 - formKnob;

      double airKnob = (double)mAir.value();
      if (airKnob < 0.0) airKnob = 0.0;
      if (airKnob > 1.0) airKnob = 1.0;

      double mix = (double)mMix.value();
      if (mix < 0.0) mix = 0.0;
      if (mix > 1.0) mix = 1.0;
      double oneMinusMix = 1.0 - mix;

      // Bake Point coefs (C from Reach; B-boost driven per-sample by engagement).
      // pointBakeCoefs takes bBoost=0 here; the per-sample boost is applied
      // as a separate multiplier on the nob reciprocals inside the hot loop.
      double pointC = 0.3 + reachKnob * 0.4;  // range [0.3, 0.7]
      PointCoefs pcoefs;
      pointBakeCoefs(pointC, 0.0, (double)globalConfig.sampleRate, pcoefs);
      // Per-sample boost scale factor. Range chosen to give max
      // nob/nib disparity ~16× at Reach=1, engagement=1 (vs the
      // gentle ~5× of the initial 0.4 mapping). The Console0Buss
      // saturating curve catches the transient peaks naturally —
      // sharper "snap" character at high Reach, particularly when
      // Form sends only the residual into Point.
      double pointBoostScale = reachKnob * 0.47;

      // Bake Distance2 coefs (A=airKnob, B fixed at 0.4, wet driven
      // per-sample by engagementSmoothed × airKnob).
      // We bake assuming wet=1.0 — per-sample wet is applied as a
      // multiplier inside the hot loop on the wet-mix step.
      // Actually Distance2's softslew + thresholds + offsetScale +
      // levelcorrect depend only on A and B (not wet); we bake those
      // and skip wet in the bake.
      Distance2Coefs dcoefs;
      distance2BakeCoefs(airKnob, 0.4, 1.0, (double)globalConfig.sampleRate, dcoefs);

      // Envelope follower alphas (asymmetric, per
      // feedback_asymmetric_envelope_follower):
      //   peak:  attack ~5ms (alpha 0.996), release ~50ms (alpha 0.9996)
      //   level: attack ~50ms (alpha 0.9996), release ~500ms (alpha 0.99996)
      // Constants assume 48k; close enough at 44.1k/96k for these slow
      // followers that one-sample-rate-of-error is inaudible.
      const double peakAttackAlpha   = 0.996;
      const double peakReleaseAlpha  = 0.9996;
      const double levelAttackAlpha  = 0.9996;
      const double levelReleaseAlpha = 0.99996;

      // Smoothed engagement alpha (for Air linger). ~30ms TC at 48k.
      const double engSmoothAlpha = 0.9994;

      // Engagement slope. Tunable; higher = sharper falloff as ratio
      // grows above 1.
      const double engSlope = 4.0;

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        double inL = *in1;
        double inR = *in2;
        if (fabs(inL) < 1.18e-23) inL = 1.18e-17;
        if (fabs(inR) < 1.18e-23) inR = 1.18e-17;
        double dryL = inL;
        double dryR = inR;

        // ===== Stage 1: Console0Channel sat (input drive) =====
        // Lifted verbatim from Filament — same Console0Channel
        // identity-at-Drive=0.5 contract.

        double tempL = inL;
        inL = (inL + mConsoleChAvgAL) * 0.5;
        mConsoleChAvgAL = tempL;
        double tempR = inR;
        inR = (inR + mConsoleChAvgAR) * 0.5;
        mConsoleChAvgAR = tempR;

        inL *= driveGain;
        inR *= driveGain;

        if (inL > 1.4137166941154) inL = 1.4137166941154;
        if (inL < -1.4137166941154) inL = -1.4137166941154;
        if (inL > 0.0)
          inL = (inL / 2.0) * (2.8274333882308 - inL);
        else
          inL = -(inL / -2.0) * (2.8274333882308 + inL);

        if (inR > 1.4137166941154) inR = 1.4137166941154;
        if (inR < -1.4137166941154) inR = -1.4137166941154;
        if (inR > 0.0)
          inR = (inR / 2.0) * (2.8274333882308 - inR);
        else
          inR = -(inR / -2.0) * (2.8274333882308 + inR);

        tempL = inL;
        inL = (inL + mConsoleChAvgBL) * 0.5;
        mConsoleChAvgBL = tempL;
        tempR = inR;
        inR = (inR + mConsoleChAvgBR) * 0.5;
        mConsoleChAvgBR = tempR;

        // ===== Stage 2: Envelope source =====
        // Raw absolute amplitude is the envelope detector source.
        // Form no longer alters the source (was a subtle effect
        // that conflated with Air's spectral character).

        double envSrcL = fabs(inL);
        double envSrcR = fabs(inR);

        // ===== Stage 3: Input-output competition feedback =====
        //
        // Spiral-wrap the delayed output (per
        // feedback_spiral_feedback_governor). The competition
        // signal is how much the input differs from what we
        // produced ~8 samples ago.

        int fbReadIdxL = (mFbIdxL - kFbDelay) & (kFbBufSize - 1);
        int fbReadIdxR = (mFbIdxR - kFbDelay) & (kFbBufSize - 1);
        double governedFbL = spiralFastSaturate(mFbDelayL[fbReadIdxL], 1.0);
        double governedFbR = spiralFastSaturate(mFbDelayR[fbReadIdxR], 1.0);
        double competitionSrcL = fabs(envSrcL - governedFbL);
        double competitionSrcR = fabs(envSrcR - governedFbR);

        // ===== Stage 4: Dual asymmetric envelope follower =====

        double peakAlphaL = (competitionSrcL > mPeakEnvL) ? peakAttackAlpha : peakReleaseAlpha;
        mPeakEnvL = mPeakEnvL * peakAlphaL + competitionSrcL * (1.0 - peakAlphaL);
        double levelAlphaL = (competitionSrcL > mLevelEnvL) ? levelAttackAlpha : levelReleaseAlpha;
        mLevelEnvL = mLevelEnvL * levelAlphaL + competitionSrcL * (1.0 - levelAlphaL);

        double peakAlphaR = (competitionSrcR > mPeakEnvR) ? peakAttackAlpha : peakReleaseAlpha;
        mPeakEnvR = mPeakEnvR * peakAlphaR + competitionSrcR * (1.0 - peakAlphaR);
        double levelAlphaR = (competitionSrcR > mLevelEnvR) ? levelAttackAlpha : levelReleaseAlpha;
        mLevelEnvR = mLevelEnvR * levelAlphaR + competitionSrcR * (1.0 - levelAlphaR);

        // ===== Stage 5: Engagement (Form-direction-blended) =====
        //
        // ratio < 1 (signal decaying, peak dropped below level)
        // clamps to 1 → rawEng = 0 → both directions converge to
        // their flat-state values (anti-comp = 1, comp = 0).
        //
        // rawEng ∈ [0, 1): 0 on flat, → 1 on transient.
        // Form blends:
        //   formInv * (1 - rawEng) — anti-compressor (high on flat)
        //   formKnob * rawEng       — compressor (high on transient)
        // Sum is always bounded [0, 1]. At Form=0.5, engagement is
        // 0.5 × ((1-r) + r) = 0.5 constant — neutral middle.

        double ratioL = mPeakEnvL / (mLevelEnvL + 1.0e-6);
        if (ratioL < 1.0) ratioL = 1.0;
        double rsL = engSlope * (ratioL - 1.0);
        double rawEngL = rsL / (1.0 + rsL);
        double engagementL = formInv * (1.0 - rawEngL) + formKnob * rawEngL;

        double ratioR = mPeakEnvR / (mLevelEnvR + 1.0e-6);
        if (ratioR < 1.0) ratioR = 1.0;
        double rsR = engSlope * (ratioR - 1.0);
        double rawEngR = rsR / (1.0 + rsR);
        double engagementR = formInv * (1.0 - rawEngR) + formKnob * rawEngR;

        // Smoothed engagement for Air linger (one-pole LP).
        mSmoothEngL = mSmoothEngL * engSmoothAlpha + engagementL * (1.0 - engSmoothAlpha);
        mSmoothEngR = mSmoothEngR * engSmoothAlpha + engagementR * (1.0 - engSmoothAlpha);

        // ===== Stage 6: Point transient injection =====
        //
        // Per-sample boost computed from engagement × Reach.
        // bBoost ∈ [0, 0.47]. Use struct copy + targeted overwrite
        // of the nob reciprocals (computed multiplicatively from
        // the cached nib reciprocals — saves one divide per side
        // per sample vs computing nobDiv-then-its-reciprocal).
        //
        double bBoostL = engagementL * pointBoostScale;
        double bBoostR = engagementR * pointBoostScale;

        PointCoefs pcL = pcoefs;  // copy base coefs
        double denomL = 1.001 - 2.0 * bBoostL;
        pcL.oneOverNobDiv = denomL * pcoefs.oneOverNibDiv;
        pcL.oneOverNobDivPlusOne = 1.0 / (1.0 + pcL.oneOverNobDiv);
        double samPtL = mPointL.process(inL, pcL);

        PointCoefs pcR = pcoefs;
        double denomR = 1.001 - 2.0 * bBoostR;
        pcR.oneOverNobDiv = denomR * pcoefs.oneOverNibDiv;
        pcR.oneOverNobDivPlusOne = 1.0 / (1.0 + pcR.oneOverNobDiv);
        double samPtR = mPointR.process(inR, pcR);

        // ===== Stage 7: Distance2 air absorption =====
        //
        // Per-sample wet computed from airKnob × smoothedEngagement.
        // Pass as wetOverride to avoid copying the whole coefs struct.

        double wetL = airKnob * mSmoothEngL;
        if (wetL < 0.0) wetL = 0.0;
        if (wetL > 1.0) wetL = 1.0;
        double samDistL = mDist2L.process(samPtL, dcoefs, wetL);

        double wetR = airKnob * mSmoothEngR;
        if (wetR < 0.0) wetR = 0.0;
        if (wetR > 1.0) wetR = 1.0;
        double samDistR = mDist2R.process(samPtR, dcoefs, wetR);

        // ===== Stage 8: Console0Buss desat (output recovery) =====
        // Lifted verbatim from Filament.

        double outBusL = samDistL;
        double outBusR = samDistR;

        tempL = outBusL;
        outBusL = (outBusL + mConsoleBsAvgAL) * 0.5;
        mConsoleBsAvgAL = tempL;
        tempR = outBusR;
        outBusR = (outBusR + mConsoleBsAvgAR) * 0.5;
        mConsoleBsAvgAR = tempR;

        outBusL *= driveGain;
        outBusR *= driveGain;

        if (outBusL > 2.8) outBusL = 2.8;
        if (outBusL < -2.8) outBusL = -2.8;
        if (outBusL > 0.0)
          outBusL = (outBusL * 2.0) / (3.0 - outBusL);
        else
          outBusL = -(outBusL * -2.0) / (3.0 + outBusL);

        if (outBusR > 2.8) outBusR = 2.8;
        if (outBusR < -2.8) outBusR = -2.8;
        if (outBusR > 0.0)
          outBusR = (outBusR * 2.0) / (3.0 - outBusR);
        else
          outBusR = -(outBusR * -2.0) / (3.0 + outBusR);

        tempL = outBusL;
        outBusL = (outBusL + mConsoleBsAvgBL) * 0.5;
        mConsoleBsAvgBL = tempL;
        tempR = outBusR;
        outBusR = (outBusR + mConsoleBsAvgBR) * 0.5;
        mConsoleBsAvgBR = tempR;

        // ===== Stage 9: Feedback delay write (for competition) =====
        // Write post-processing wet output (pre-mix). This feeds
        // the competition envelope source on the NEXT sample.

        mFbDelayL[mFbIdxL & (kFbBufSize - 1)] = outBusL;
        mFbDelayR[mFbIdxR & (kFbBufSize - 1)] = outBusR;
        mFbIdxL = (mFbIdxL + 1) & (kFbBufSize - 1);
        mFbIdxR = (mFbIdxR + 1) & (kFbBufSize - 1);

        // ===== Stage 10: ChainMix dry/wet =====

        double outFinalL = outBusL * mix + dryL * oneMinusMix;
        double outFinalR = outBusR * mix + dryR * oneMinusMix;

        *out1 = (float)outFinalL;
        *out2 = (float)outFinalR;
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    // Console0 sat states (8 doubles).
    double mConsoleChAvgAL, mConsoleChAvgAR, mConsoleChAvgBL, mConsoleChAvgBR;
    double mConsoleBsAvgAL, mConsoleBsAvgAR, mConsoleBsAvgBL, mConsoleBsAvgBR;

    // Transient designer per side (5 doubles + bool each).
    PointMono mPointL, mPointR;

    // Air absorption per side (9 doubles each).
    Distance2Mono mDist2L, mDist2R;

    // Dual asymmetric envelope followers (2 doubles per side).
    double mPeakEnvL, mPeakEnvR;
    double mLevelEnvL, mLevelEnvR;

    // Smoothed engagement for Air linger (1 double per side).
    double mSmoothEngL, mSmoothEngR;

    // Input-output competition feedback delay buffer per side.
    // 8-sample delay is enough to break the per-sample algebraic
    // loop without introducing audible latency; 16-sample
    // power-of-2 buffer for cheap bitmask wrap.
    static const int kFbBufSize = 16;
    static const int kFbDelay = 8;
    double mFbDelayL[kFbBufSize], mFbDelayR[kFbBufSize];
    int mFbIdxL, mFbIdxR;
#endif
  };

} // namespace house
