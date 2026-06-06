// house::Filament
//
// Third chain-as-unit in the house package. First filter-character
// unit in the catalog. Console0-wrapped Capacitor2 LP with signal-
// voltage-modulated cutoff. The filter "breathes" in response to
// input dynamics — louder transients pop the cutoff up, quiet
// passages settle. Bowed-string / vocal-resonance feel.
//
// MONOLITHIC by precedent (consistent with TickerTape + Lacquer).
// Capacitor2's 6-cell gearbox needs tight per-sample coordination
// that's cleanest inside one Object. Console0 sat math inlined,
// Capacitor2Mono used as a helper, ChainMix logic inlined.
//
// HYBRID FLOAT / Cortex-A8 hot-loop notes:
//   - All per-sample math in DOUBLE (consistent with Capacitor2's
//     state and Console0's averaging filter state). No float→
//     double cast tax per feedback_cortex_a8_no_double_in_hot_loops.
//   - I/O as float (od::Object convention).
//   - Per-sample divides ELIMINATED via block-rate reciprocal
//     precomputes (in Capacitor2's bakeCoefs).
//
// PARAMETERS (5 plies, all continuous w/ standard coarse/fine):
//   Drive  - Console0Channel + Bus gain (symmetric, transparent
//            at 0.5). Same shape as TickerTape / Lacquer.
//   Cutoff - Capacitor2 lowpass cutoff (0 = closed/mute,
//            1 = open/bypass). Squared internally per AW: actual
//            lowpassChase = Cutoff².
//   FM     - Capacitor2 signal-voltage modulation depth. Inverted
//            mapping: 0 = weak FM (nearly static filter), 1 =
//            strong FM (filter cutoff swings wildly with input).
//            Internally: nonLin = 1 + (1 - FM)*6.
//   Bloom  - Allpass-in-feedback "ghost resonance". Drives BOTH
//            negative feedback gain AND first-order APF coefficient
//            on a coupled curve. At Bloom=0 the filter behaves
//            identically to its current form (no feedback). As
//            Bloom rises, the APF in the feedback path smears the
//            resonance peak and shifts it from the cutoff —
//            "phase-mediated resonance" rather than the canonical
//            Moog-ladder sharp peak. SPIRAL LIMITER on the
//            feedback path (mechanic #1 — Console-as-feedback-
//            governor) bounds loop signal to ±1; high Bloom +
//            high Cutoff sings into saturation rather than
//            clipping out. Caps: resonance ≤ 0.85, apfG ≤ 0.7.
//   Mix    - ChainMix dry/wet (default 1.0 full wet).
//
// The APF feedback uses a TWO-STAGE Schroeder cascade with
// Fibonacci-spaced delays. ASYMMETRIC per side for natural
// stereo spread that grows with Bloom (at Bloom=0 the peaks are
// inactive so the spread is inaudible; as Bloom rises the
// asymmetric peak placement becomes audible as stereo width):
//   L: delays 5 and 13 samples → peaks ~4.8 kHz and ~1.85 kHz
//   R: delays 8 and 21 samples → peaks ~3 kHz and ~1.1 kHz
// (5, 8, 13, 21 are Fibonacci numbers; consecutive ratios → phi)
// Non-harmonic spacing avoids comb-filter-style harmonic
// ringing → genuinely smeared resonance with peaks in distinct
// musical bands.  Per side state: 32 doubles for delay buffer 1
// + 32 doubles for buffer 2 + 2 write indices + 1 double for prev
// filter output. Hot loop cost: ~15 cycles per sample per side.
//
// AW SCALAR REMAP APPLIED (per feedback_aw_param_default_subtle):
// AW Capacitor2 defaults (A=B=1.0) give effective bypass.
// Filament defaults (Cutoff=0.6, FM=0.5) instead produce audibly
// filtered output with moderate signal-voltage breathing — the
// "characterful default" rule.
//
// LOAD-BEARING design invariants:
//   1. LP side only (HP intentionally skipped — Filament is a
//      synth-LP-filter unit, not a band-shaper)
//   2. Capacitor2 gearbox dispatch preserved verbatim
//   3. Per-sample divides → block-rate reciprocal multiplies
//      (per the Lacquer CPU lesson)
//   4. dielectricScale uses fabs (positive only — sign would
//      flip the modulation direction)
//   5. nonLinTrim gain compensation applied after filter
//   6. Console0 sat pair wraps the filter for input drive +
//      level-dependent containment
//
// Dropped per template:
//   - Per-sample 32-bit dither
//   - fpd RNG (replaced by deterministic 1.18e-17 in denormal flush)

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <string.h>

#include "Capacitor2.h"
#include "AllpassMono.h"
#include "Spiral.h"   // spiralFastSaturate, used as feedback-loop governor for Bloom

namespace house
{

  class Filament : public od::Object
  {
  public:
    Filament()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mDrive);
      addParameter(mCutoff);
      addParameter(mFM);
      addParameter(mBloom);
      addParameter(mMix);

      mConsoleChAvgAL = mConsoleChAvgAR = mConsoleChAvgBL = mConsoleChAvgBR = 0.0;
      mConsoleBsAvgAL = mConsoleBsAvgAR = mConsoleBsAvgBL = mConsoleBsAvgBR = 0.0;
      mMonoL.reset();
      mMonoR.reset();
      mPrevLowpassChase = 0.0;
      mPrevFilterOutL = mPrevFilterOutR = 0.0;
      memset(mApfBuf1L, 0, sizeof(mApfBuf1L));
      memset(mApfBuf1R, 0, sizeof(mApfBuf1R));
      memset(mApfBuf2L, 0, sizeof(mApfBuf2L));
      memset(mApfBuf2R, 0, sizeof(mApfBuf2R));
      mApfWriteIdx1L = mApfWriteIdx1R = 0;
      mApfWriteIdx2L = mApfWriteIdx2R = 0;
    }

    virtual ~Filament() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mDrive{"Drive", 0.5f};
    od::Parameter mCutoff{"Cutoff", 0.6f};
    od::Parameter mFM{"FM", 0.5f};
    od::Parameter mBloom{"Bloom", 0.0f};   // default off — preserves prior 4-ply character
    od::Parameter mMix{"Mix", 1.0f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      // ===== Block-rate scalar baking =====

      // Drive (Console gain, unity at 0.5 — matches TickerTape/Lacquer)
      double drive = (double)mDrive.value();
      if (drive < 0.0) drive = 0.0;
      if (drive > 1.0) drive = 1.0;
      double driveGain = 0.05 + drive * 1.9;

      // Cutoff: user knob 0..1, squared internally per AW
      double cutoffKnob = (double)mCutoff.value();

      // FM: user knob 0..1, mapped to nonLin = 1 + (1-FM)*6
      // (FM=0 → nonLin=7 weak modulation, FM=1 → nonLin=1 strong)
      // PLUS envelope-boosted depth (the 0.1.0.25 addition):
      //   baseFmGain = 1..3 across the knob (base depth)
      //   envBoost   = 0..5 across the knob (envelope-driven boost)
      // dynamicFmGain = baseFmGain + envState * envBoost per sample.
      // Transients pop envState up → dynamicFmGain spikes → filter
      // swings further, addressing the "sluggish on attacks" finding.
      //
      // INVISIBLE CUTOFF↔FM COUPLING (0.1.0.30, retuned 0.1.0.31):
      // when cutoff is wide open the filter has no headroom to
      // absorb modulation, so hot FM becomes aliased exponential
      // growth at Nyquist. When cutoff is closed, there's plenty
      // of room to swing — boosting FM there makes transients pop
      // the filter open more dramatically.
      //
      // Scale FM depth by cubic cutoff headroom (1 - Cutoff³) with
      // expanded range [0.25, 1.30]:
      //   - low Cutoff (<~0.65): 100-130% FM (boost; transients
      //                                       pop closed filter open)
      //   - mid Cutoff (~0.7):    ~95% FM (near-unity)
      //   - high Cutoff (1.0):    25% FM (well below runaway)
      //
      // Cubic shape keeps the active musical middle smooth; the
      // boost and the falloff both live at the extremes where they
      // make musical sense. Parameter-driven, no audio analysis,
      // no extra knob.
      double fmKnob = (double)mFM.value();
      if (fmKnob < 0.0) fmKnob = 0.0;
      if (fmKnob > 1.0) fmKnob = 1.0;
      double nonLin = 1.0 + (1.0 - fmKnob) * 6.0;
      double cutoffHeadroom = 1.0 - cutoffKnob * cutoffKnob * cutoffKnob;
      double fmScale = 0.25 + 1.05 * cutoffHeadroom; // [0.25, 1.30]
      double baseFmGain = 1.0 + fmKnob * 2.0 * fmScale;
      double envBoost = fmKnob * 5.0 * fmScale;

      // Bake Capacitor2 coefs (precomputes invNonLin + oneOverSpeedPlusOne
      // → eliminates per-sample divides per the Cortex-A8 hot-loop memory)
      Capacitor2Coefs cox;
      capacitor2BakeCoefs(cutoffKnob, nonLin, baseFmGain, envBoost,
                          mPrevLowpassChase, cox);
      mPrevLowpassChase = cox.lowpassChase;

      // Bloom: coupled feedback amount + APF coefficient. Bumped from
      // initial conservative caps (0.6 / 0.5) to musical caps
      // (0.85 / 0.7) once the Spiral feedback limiter was in place —
      // the limiter catches runaway, so we can push into self-
      // oscillation-adjacent territory and get a singing resonance
      // instead of clipping out.
      double bloomKnob = (double)mBloom.value();
      if (bloomKnob < 0.0) bloomKnob = 0.0;
      if (bloomKnob > 1.0) bloomKnob = 1.0;
      double resonance = bloomKnob * 0.85;
      double apfG = bloomKnob * 0.7;

      // Mix
      double mix = (double)mMix.value();
      if (mix < 0.0) mix = 0.0;
      if (mix > 1.0) mix = 1.0;
      double oneMinusMix = 1.0 - mix;

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

        // ===== Stage 2: Capacitor2 LP with signal-FM cutoff +
        //                Fibonacci-cascade APF "ghost resonance" =====
        //
        // Per side: bound the previous filter output via Spiral
        // (mechanic #1 — Console-as-feedback-governor, per
        // feedback_spiral_feedback_governor), then run through TWO
        // cascaded Schroeder allpass stages with Fibonacci-spaced
        // delays (8 and 21 samples ≈ golden-ratio progression).
        // Stage 1 contributes a peak around ~3 kHz, stage 2 around
        // ~1.1 kHz at 48k. Non-harmonic spacing avoids comb-style
        // ringing.

        double boundedFbL = spiralFastSaturate(mPrevFilterOutL, 1.0);
        // L: APF Stage 1 (5-sample delay → ~4.8 kHz peak)
        int readIdx1L = (mApfWriteIdx1L - kApfDelay1L) & (kApfBufSize - 1);
        double v1DelayedL = mApfBuf1L[readIdx1L];
        double v1NewL = boundedFbL + apfG * v1DelayedL;
        double y1OutL = -apfG * v1NewL + v1DelayedL;
        mApfBuf1L[mApfWriteIdx1L] = v1NewL;
        mApfWriteIdx1L = (mApfWriteIdx1L + 1) & (kApfBufSize - 1);
        // L: APF Stage 2 (13-sample delay → ~1.85 kHz peak)
        int readIdx2L = (mApfWriteIdx2L - kApfDelay2L) & (kApfBufSize - 1);
        double v2DelayedL = mApfBuf2L[readIdx2L];
        double v2NewL = y1OutL + apfG * v2DelayedL;
        double y2OutL = -apfG * v2NewL + v2DelayedL;
        mApfBuf2L[mApfWriteIdx2L] = v2NewL;
        mApfWriteIdx2L = (mApfWriteIdx2L + 1) & (kApfBufSize - 1);
        double filterInL = inL - resonance * y2OutL;

        double boundedFbR = spiralFastSaturate(mPrevFilterOutR, 1.0);
        // R: APF Stage 1 (8-sample delay → ~3 kHz peak)
        int readIdx1R = (mApfWriteIdx1R - kApfDelay1R) & (kApfBufSize - 1);
        double v1DelayedR = mApfBuf1R[readIdx1R];
        double v1NewR = boundedFbR + apfG * v1DelayedR;
        double y1OutR = -apfG * v1NewR + v1DelayedR;
        mApfBuf1R[mApfWriteIdx1R] = v1NewR;
        mApfWriteIdx1R = (mApfWriteIdx1R + 1) & (kApfBufSize - 1);
        // R: APF Stage 2 (21-sample delay → ~1.1 kHz peak)
        int readIdx2R = (mApfWriteIdx2R - kApfDelay2R) & (kApfBufSize - 1);
        double v2DelayedR = mApfBuf2R[readIdx2R];
        double v2NewR = y1OutR + apfG * v2DelayedR;
        double y2OutR = -apfG * v2NewR + v2DelayedR;
        mApfBuf2R[mApfWriteIdx2R] = v2NewR;
        mApfWriteIdx2R = (mApfWriteIdx2R + 1) & (kApfBufSize - 1);
        double filterInR = inR - resonance * y2OutR;

        // Pass envelope SOURCE = post-Console pre-feedback signal (inL/R)
        // so APF feedback doesn't dampen the envelope-driven dielectric
        // boost. Drive setting still affects FM responsiveness because
        // inL is post-Console0Channel saturation; high Bloom no longer
        // shrinks the envelope. Two-arg overload added in 0.1.0.32.
        double filterOutL = mMonoL.process(filterInL, inL, cox) * cox.nonLinTrim;
        double filterOutR = mMonoR.process(filterInR, inR, cox) * cox.nonLinTrim;

        // Store for next sample's feedback path (will be Spiral-
        // bounded on the next iteration's entry to the APF).
        mPrevFilterOutL = filterOutL;
        mPrevFilterOutR = filterOutR;

        // ===== Stage 3: Console0Bus desat (output recovery) =====

        tempL = filterOutL;
        filterOutL = (filterOutL + mConsoleBsAvgAL) * 0.5;
        mConsoleBsAvgAL = tempL;
        tempR = filterOutR;
        filterOutR = (filterOutR + mConsoleBsAvgAR) * 0.5;
        mConsoleBsAvgAR = tempR;

        filterOutL *= driveGain;
        filterOutR *= driveGain;

        if (filterOutL > 2.8) filterOutL = 2.8;
        if (filterOutL < -2.8) filterOutL = -2.8;
        if (filterOutL > 0.0)
          filterOutL = (filterOutL * 2.0) / (3.0 - filterOutL);
        else
          filterOutL = -(filterOutL * -2.0) / (3.0 + filterOutL);

        if (filterOutR > 2.8) filterOutR = 2.8;
        if (filterOutR < -2.8) filterOutR = -2.8;
        if (filterOutR > 0.0)
          filterOutR = (filterOutR * 2.0) / (3.0 - filterOutR);
        else
          filterOutR = -(filterOutR * -2.0) / (3.0 + filterOutR);

        tempL = filterOutL;
        filterOutL = (filterOutL + mConsoleBsAvgBL) * 0.5;
        mConsoleBsAvgBL = tempL;
        tempR = filterOutR;
        filterOutR = (filterOutR + mConsoleBsAvgBR) * 0.5;
        mConsoleBsAvgBR = tempR;

        // ===== Stage 4: ChainMix dry/wet =====

        double outL = filterOutL * mix + dryL * oneMinusMix;
        double outR = filterOutR * mix + dryR * oneMinusMix;

        *out1 = (float)outL;
        *out2 = (float)outR;
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    // Console0 sat states (4 doubles per stage = 8 doubles total).
    double mConsoleChAvgAL, mConsoleChAvgAR, mConsoleChAvgBL, mConsoleChAvgBR;
    double mConsoleBsAvgAL, mConsoleBsAvgAR, mConsoleBsAvgBL, mConsoleBsAvgBR;

    // Capacitor2 per-side filter state (6 LP cells + smoothing + count).
    Capacitor2Mono mMonoL;
    Capacitor2Mono mMonoR;

    // Block-rate cutoff smoothing reference (previous block's chase value
    // for the knob-delta-driven speed calc).
    double mPrevLowpassChase;

    // APF-in-feedback state (Bloom): two-stage cascade with
    // Fibonacci-spaced delays.
    //   mPrevFilterOut*   — last sample's filter output, fed into
    //                       the cascade (Spiral-bounded on entry)
    //   mApfBuf1*[32]     — stage 1 delay buffer (delay 8 samples)
    //   mApfBuf2*[32]     — stage 2 delay buffer (delay 21 samples)
    //   mApfWriteIdx*     — circular buffer write positions
    // 32-sample buffers (power of 2) for cheap bitmask indexing,
    // even though we only use up to 21 samples back. The masked
    // wraparound is the bitwise AND with (kApfBufSize - 1) = 31.
    static const int kApfBufSize = 32;
    // Asymmetric per-side delays for stereo spread (Fibonacci pairs).
    // The asymmetry only becomes audible when Bloom drives the feedback
    // hot enough to create peaks; at low Bloom both sides sound similar.
    static const int kApfDelay1L = 5;
    static const int kApfDelay2L = 13;
    static const int kApfDelay1R = 8;
    static const int kApfDelay2R = 21;
    double mPrevFilterOutL, mPrevFilterOutR;
    double mApfBuf1L[kApfBufSize], mApfBuf1R[kApfBufSize];
    double mApfBuf2L[kApfBufSize], mApfBuf2R[kApfBufSize];
    int mApfWriteIdx1L, mApfWriteIdx1R;
    int mApfWriteIdx2L, mApfWriteIdx2R;
#endif
  };

} // namespace house
