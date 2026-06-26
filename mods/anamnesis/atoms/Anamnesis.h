// anamnesis::Anamnesis
//
// Spatial-glitch instrument (CM4-only): a short-buffer micro-looper fused
// with a continuously-morphing spatial field, cross-fed and Spiral-governed.
// Internal-stereo (one object, shared coherent L/R field).
//
// Phase 4.2 (0.2.0.20): the ROUTER. Source (field reverberates input 0 .. loop
//   1), DirectLoop (clean ZOH-held glitched loop blended to output), Spread
//   (mid/side stereo width). Defaults (1 / 0 / 0.5) keep the prior sound.
// Phase 4.1 (0.2.0.19): looper<->field CROSS-FEEDBACK (Regen). The governed
//   field wet re-enters the looper input, so the reverb tail is re-looped /
//   re-glitched / re-reverbed -- the controllable-runaway fusion. DC-blocked +
//   Spiral governor. Petrichor-informed depth: drive past unity (kRegenMax) and
//   a low-density Spiral (kFbGovD) that stays LINEAR until hot then soft-bounds,
//   so the loop can build toward self-oscillation without blowing up. (Pauses
//   while frozen: a held loop takes no new feedback.) Builds on:
// Phase 3.3 (0.2.0.17): smooth-glide CLOCK. A ClockMode option (Steps / Smooth):
//   Steps snaps R to the harmonized LUT (crisp detents); Smooth uses a continuous
//   R = 16^(1-Clock), glided per block, so sweeping Clock glides reverb / grit /
//   loop-length instead of stepping. Builds on:
// Phase 3.2 (0.2.0.16): clean<->broken GRIT axis. Grit crossfades the wet
//   reconstruction clean linear-interp (0) -> broken ZOH (0.5), then adds
//   bit-crush (0.5->1, to ~6-bit). Default 0.5 = ZOH only = the pre-Grit
//   sound (bit-identical at Clock=1). Builds on:
// Phase 3.1 (0.2.0.13): the global CLOCK. The looper+field sub-engine advances
//   at a reduced internal rate Fc = Fs/R (R snaps to musical steps 1..16) via a
//   fire-gate; the wet is ZOH-held between steps. One knob -> reverb + loop +
//   glides all lengthen together, plus lo-fi grit; dry + Mix stay full-rate.
//   R=1 (Clock max) = full rate. Clean<->broken + smooth glide are 3.2 / 3.3.
// Phase 2.4c (0.2.0.12): MOMENTARY CAPTURE on trigger -- a fire (manual Trig
//   or Env onset) briefly holds recording (~one slice, auto-release) so the
//   re-triggered window is a clean FROZEN snapshot that loops -> crisp
//   stutter / beat-repeat instead of a moving-target judder. Builds on:
// Phase 2.4 (0.2.0.11): Trig capture/re-trigger gate (two-head crossfade
//   declick) + ENV mode -- a fast/slow peak-follower transient detector
//   auto-fires the re-trigger on input onsets (Sense = threshold), so the
//   loop re-slices itself to playing dynamics. Builds on:
// Phase 2.3 (0.2.0.6): adds STRETCH mode (granular time-stretch) + a Mode
//   selector (Tape / Stretch). In Stretch the source playhead moves at the
//   TIME rate (Speed) while grains replay at UNITY pitch -> time and pitch
//   decouple (slow / reverse a held fragment without repitching it). 4-grain
//   Hann overlap-add (LUT). Builds on:
// Phase 2.2 (0.2.0.3): FREEZE as a toggle gate + smooth Speed/Length.
//   Freeze is a 0/1 gate inlet (Comparator toggle / CV) with a fast
//   declick ramp: fz=0 live replace (Tape), fz=1 frozen hold. Speed and
//   Length now GLIDE per-sample (REPITCH-style) -- stepping Speed slides
//   the pitch, changing Length resizes the loop smoothly, no zipper.
//   Raised-cosine loop-seam declick so the frozen loop does not tick.
//   Builds on:
// Phase 2.1 (0.2.0.1): the micro-LOOPER front-end (Tape mode core).
//   Always-listening circular capture; Hermite variable-speed playback
//   with a discrete musical Speed LUT (reverse / stalled / forward); a
//   dynamic anti-alias LP for fast reads. The looper output FEEDS the
//   field. Stretch = 2.3, Env = 2.4 (99-build-order.md).
//
// Phase 1 (complete) -- the spatial field:
//   STAGE 1 sparse FEEDFORWARD early-reflection taps (addressable "glitch"
//     pole); STAGE 2 unitary N=8 FDN tail, per-line Jot T60 (size-indep.,
//     rig-validated) + Schroeder input diffuser; DENSITY = the plexus macro
//     (tap<->FDN crossfade + Erbe-Verb A(a)=I-a(2/N)11^T matrix morph);
//     per-line delay MOD (de-metallic). Size glides per-sample (REPITCH).
//
// DESIGN LAW (postmortem): the FDN loop stays UNITARY; sparse taps are
// FEEDFORWARD only. No third-party branding per feedback_no_third_party_branding.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <od/objects/Option.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace anamnesis
{

  static const float kPi = 3.14159265358979f;

  // ---- Looper ----
  static const int   kLoopBufLen = 96000;   // 2 s @ 48k (mono capture)
  static const float kLoopMinSec = 0.02f;
  static const float kLoopMaxSec = 2.0f;
  static const float kSeamXf = 64.0f;       // loop-seam declick window (samples)

  // ---- Stretch (granular time-stretch: time and pitch decoupled) ----
  static const int   kStretchGrains = 4;
  static const float kGrainDur = 3000.0f;   // grain length ~62.5 ms @48k
  static const float kGrainHop = 1500.0f;   // spawn interval = 50% overlap
  static const int   kHannLutN = 1024;
  static const float kSpeedMax = 2.0f;       // bipolar Speed range +/-2x

  // ---- global CLOCK: reduced internal rate Fc = Fs/R, R snaps to musical
  // steps. R=1 = full rate (no effect). Lower = longer + lower + grittier. ----
  static const int kClockSteps = 8;
  static const int kClockR[kClockSteps] = {1, 2, 3, 4, 6, 8, 12, 16};

  // Cross-feedback: drive headroom past unity + a low-density Spiral so the
  // loop stays LINEAR until genuinely hot, then soft-bounds (Petrichor-style:
  // build freely, clip only when hot). Bound = +/-1/kFbGovD.
  static const float kRegenMax = 1.8f;
  static const float kFbGovD   = 0.4f;
  static const float kRetrigFadeMs = 4.0f;   // re-trigger crossfade length (ms)
  static const float kEnvFloor = 0.003f;     // Env-mode auto-trigger floor (~-50 dB)

  // ---- Stage 2 FDN ----
  static const int kFdnN = 8;
  static const int kFdnBase[kFdnN] = {1669, 1987, 2311, 2833, 3299, 3671, 4049, 4447};
  static const float kSizeMin   = 0.25f;
  static const float kSizeMax   = 2.0f;
  static const int   kFdnBufLen = 9000;

  static const int kApN = 4;
  static const int kApLen[kApN] = {113, 211, 337, 449};
  static const int kApMax = 449;

  // ---- Stage 1 sparse taps ----
  static const int kTapN = 12;
  static const int kTapBase[kTapN] =
    {960, 1597, 2311, 3001, 4099, 5273, 6571, 8089, 9743, 12101, 15307, 19211};
  static const int kTapBufLen = 39000;

  class Anamnesis : public od::Object
  {
  public:
    Anamnesis()
    {
      addInput(mInL);
      addInput(mInR);
      addInput(mFreeze);
      addInput(mTrig);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mLength);
      addParameter(mSpeed);
      addParameter(mSense);
      addParameter(mSize);
      addParameter(mDecay);
      addParameter(mDiffusion);
      addParameter(mDensity);
      addParameter(mMod);
      addParameter(mRegen);
      addParameter(mClock);
      addParameter(mGrit);
      addParameter(mMix);
      addParameter(mSource);
      addParameter(mDirectLoop);
      addParameter(mSpread);
      addOption(mMode);
      mMode.set(1);                 // default Tape (1=Tape, 2=Stretch)
      mMode.enableSerialization();
      addOption(mClockMode);
      mClockMode.set(1);            // 1 = Steps (harmonized), 2 = Smooth (glide)
      mClockMode.enableSerialization();

      memset(mLoopBuf, 0, sizeof(mLoopBuf));
      memset(mLine, 0, sizeof(mLine));
      memset(mAp, 0, sizeof(mAp));
      memset(mApWr, 0, sizeof(mApWr));
      memset(mTapBuf, 0, sizeof(mTapBuf));
      mLoopWr = 0; mLoopReadPos = 0.0f; mLoopLpZ = 0.0f;
      mSpeedZ = 1.0f; mLoopLenZ = 24000.0f; mFreezeZ = 0.0f;
      mStretchHead = 0.0f; mGrainSpawnCtr = 0; mRetrigPhase = 1.0f; mOldReadPos = 0.0f;
      mEnvFast = 0.0f; mEnvSlow = 0.0f; mEnvRefractory = 0;
      mCaptureHold = false; mCaptureHoldZ = 0.0f; mCaptureTimer = 0;
      mClockPhase = 0.0f; mWetL = 0.0f; mWetR = 0.0f; mRecRateRef = 1.0f;
      mWetPrevL = 0.0f; mWetPrevR = 0.0f; mRcurZ = 1.0f;
      mWetFb = 0.0f; mFbDcX1 = 0.0f; mFbDcY1 = 0.0f;
      mSourceZ = 1.0f; mDirectLoopZ = 0.0f; mSpreadZ = 0.5f; mLoopOutHeld = 0.0f;
      for (int i = 0; i < kStretchGrains; i++) { mGrainActive[i] = false; mGrainPos[i] = 0.0f; mGrainEnvPh[i] = 0.0f; }
      for (int i = 0; i < kHannLutN; i++)
        mHannLut[i] = 0.5f - 0.5f * cosf(2.0f * kPi * (float)i / (float)(kHannLutN - 1));
      mWr = 0; mTapWr = 0;
      mSizeScaleZ = 1.0f; mT60Z = 2.0f; mDiffGZ = 0.4f; mDensityZ = 0.5f; mModZ = 0.3f;
      mInit = false;

      for (int i = 0; i < kTapN; i++)
      {
        float side = (i & 1) ? 1.0f : -1.0f;
        float spread = 0.30f + 0.70f * (float)i / (float)(kTapN - 1);
        float pan = side * spread;
        float ang = (pan * 0.5f + 0.5f) * (kPi * 0.5f);
        mPanL[i] = cosf(ang);
        mPanR[i] = sinf(ang);
        mTapGain[i] = expf(-1.8f * (float)i / (float)(kTapN - 1));
        mTapLfoPhase[i] = (float)i * 0.37f;
        mTapLfoHz[i] = 0.30f + 0.08f * (float)i;
      }
      for (int i = 0; i < kFdnN; i++)
      {
        mFdnLfoPhase[i] = (float)i * 0.61f;
        mFdnLfoHz[i] = 0.50f + 0.13f * (float)i;
      }
    }

    virtual ~Anamnesis() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Inlet     mFreeze{"Freeze"};        // gate/toggle: hold the loop
    od::Inlet     mTrig{"Trig"};            // trigger: re-anchor playback to "now"
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};

    od::Parameter mLength{"Length", 0.4f};
    od::Parameter mSpeed{"Speed", 1.0f};    // bipolar -2..2x rate; 1.0 = +1x
    od::Parameter mSense{"Sense", 0.5f};    // Env-mode transient sensitivity
    od::Parameter mSize{"Size", 0.5f};
    od::Parameter mDecay{"Decay", 0.5f};
    od::Parameter mDiffusion{"Diffusion", 0.6f};
    od::Parameter mDensity{"Density", 0.5f};
    od::Parameter mMod{"Mod", 0.3f};
    od::Parameter mRegen{"Regen", 0.0f};    // cross-feedback: field wet -> looper input
    od::Parameter mClock{"Clock", 1.0f};    // 1 = full rate; down = slower/lower/grittier
    od::Parameter mGrit{"Grit", 0.5f};      // clean(0) <- interp | ZOH -> broken(1) bitcrush
    od::Parameter mMix{"Mix", 0.4f};
    // Router
    od::Parameter mSource{"Source", 1.0f};      // field source: 0 input .. 1 loop
    od::Parameter mDirectLoop{"DirectLoop", 0.0f}; // clean loop blended to output
    od::Parameter mSpread{"Spread", 0.5f};      // width: 0 mono .. 0.5 normal .. 1 wide
    od::Option    mMode{"Mode"};            // 1=Tape (var-speed), 2=Stretch (granular)
    od::Option    mClockMode{"ClockMode"};  // 1=Steps (harmonized), 2=Smooth (glide)

    inline void ensureFlushToZero()
    {
#if defined(__aarch64__)
      if (!mFzSet)
      {
        uint64_t fpcr;
        __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
        fpcr |= (1ull << 24);
        __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
        mFzSet = true;
      }
#endif
    }

    // 4-point Hermite (Catmull-Rom) read of the loop buffer.
    inline float readLoopHermite(float rp, int L)
    {
      int i1 = (int)rp;
      float frac = rp - (float)i1;
      int i0 = i1 - 1; if (i0 < 0) i0 += L;
      int i2 = i1 + 1; if (i2 >= L) i2 -= L;
      int i3 = i1 + 2; if (i3 >= L) i3 -= L;
      float xm1 = mLoopBuf[i0], x0 = mLoopBuf[i1], x1 = mLoopBuf[i2], x2 = mLoopBuf[i3];
      float c = (x1 - xm1) * 0.5f;
      float v = x0 - x1;
      float w = c + v;
      float a = w + v + (x2 - x0) * 0.5f;
      float bneg = w + a;
      return ((a * frac - bneg) * frac + c) * frac + x0;
    }

    inline float readLine(int i, float d)
    {
      float rp = (float)mWr - d;
      while (rp < 0.0f) rp += (float)kFdnBufLen;
      int i0 = (int)rp; float fr = rp - (float)i0;
      int i1 = i0 + 1; if (i1 >= kFdnBufLen) i1 -= kFdnBufLen;
      return mLine[i][i0] + (mLine[i][i1] - mLine[i][i0]) * fr;
    }

    inline float readTap(float d)
    {
      float rp = (float)mTapWr - d;
      while (rp < 0.0f) rp += (float)kTapBufLen;
      int i0 = (int)rp; float fr = rp - (float)i0;
      int i1 = i0 + 1; if (i1 >= kTapBufLen) i1 -= kTapBufLen;
      return mTapBuf[i0] + (mTapBuf[i1] - mTapBuf[i0]) * fr;
    }

    virtual void process()
    {
      ensureFlushToZero();

      const float *inL = mInL.buffer();
      const float *inR = mInR.buffer();
      float *outL = mOutL.buffer();
      float *outR = mOutR.buffer();

      const float fs = (float)globalConfig.sampleRate;

      // ---- controls ----
      const float *fzBuf = mFreeze.buffer();
      const float *trigBuf = mTrig.buffer();
      const int    mode = mMode.value();
      const bool   stretchMode = (mode == 2);
      const bool   envMode = (mode == 3);
      float lengthN = clampf(mLength.value(), 0.0f, 1.0f);
      float speedN  = clampf(mSpeed.value(), -kSpeedMax, kSpeedMax);  // bipolar rate
      float senseN  = clampf(mSense.value(), 0.0f, 1.0f);
      float sizeN   = clampf(mSize.value(), 0.0f, 1.0f);
      float decayN  = clampf(mDecay.value(), 0.0f, 1.0f);
      float diffN   = clampf(mDiffusion.value(), 0.0f, 1.0f);
      float density = clampf(mDensity.value(), 0.0f, 1.0f);
      float modN    = clampf(mMod.value(), 0.0f, 1.0f);
      float regenN  = clampf(mRegen.value(), 0.0f, 1.0f);
      float mix     = clampf(mMix.value(), 0.0f, 1.0f);
      float sourceN = clampf(mSource.value(), 0.0f, 1.0f);
      float directN = clampf(mDirectLoop.value(), 0.0f, 1.0f);
      float spreadN = clampf(mSpread.value(), 0.0f, 1.0f);

      // Looper targets -- smoothed PER-SAMPLE below so Speed/Length glide
      // (no zipper; a tape-speed slide between the discrete steps).
      float targetLen = (kLoopMinSec + lengthN * (kLoopMaxSec - kLoopMinSec)) * fs;
      if (targetLen < 64.0f) targetLen = 64.0f;
      if (targetLen > (float)kLoopBufLen) targetLen = (float)kLoopBufLen;
      // The Speed value IS the rate multiplier: -2..+2x. Tape -> pitch,
      // Stretch -> time; 0 = stall. Continuous (dial clicks in 0.25 steps,
      // so 0.5x / 1x / 2x land on detents).
      const float targetSpeed = speedN;
      const float aLoopGlide = 1.0f - expf(-1.0f / (fs * 0.040f)); // Speed/Length glide
      const float aFreeze    = 1.0f - expf(-1.0f / (fs * 0.015f)); // toggle declick ramp
      const float retrigInc  = 1.0f / (kRetrigFadeMs * 0.001f * fs); // re-trigger attack
      const float aCapture   = 1.0f - expf(-1.0f / (fs * 0.004f));   // capture-hold ramp
      const int   captureMin = (int)(0.2f * fs);                     // min hold (~200 ms)
      // Env-mode transient detector: fast peak vs slow average; trigger when
      // fast exceeds slow * threshMult. Sense raises sensitivity (lowers it).
      const float envFastDecay = expf(-1.0f / (fs * 0.015f));        // 15 ms
      const float envSlowA     = 1.0f - expf(-1.0f / (fs * 0.150f)); // 150 ms
      const float threshMult   = 4.0f - senseN * 2.8f;               // sens 0->4x, 1->1.2x
      const int   envRefr      = (int)(0.05f * fs);                  // 50 ms refractory
      // dynamic anti-alias LP from the (steady-state) target speed
      float aspd = targetSpeed < 0.0f ? -targetSpeed : targetSpeed;
      float aLoopLp = 1.0f;
      if (aspd > 1.0f) { float fc = 0.5f / aspd; aLoopLp = 1.0f - expf(-2.0f * kPi * fc); }

      const float sizeScaleTgt = kSizeMin + sizeN * (kSizeMax - kSizeMin);
      const float t60Tgt = 0.2f * powf(100.0f, decayN);
      const float diffGTgt = diffN * 0.75f;

      if (!mInit) { mSizeScaleZ = sizeScaleTgt; mT60Z = t60Tgt; mDiffGZ = diffGTgt; mDensityZ = density; mModZ = modN; mLoopLenZ = targetLen; mSpeedZ = targetSpeed; mInit = true; }
      const float aBlk = 1.0f - expf(-(float)FRAMELENGTH / (fs * 0.030f));
      mT60Z     += aBlk * (t60Tgt - mT60Z);
      mDiffGZ   += aBlk * (diffGTgt - mDiffGZ);
      mDensityZ += aBlk * (density - mDensityZ);
      mModZ     += aBlk * (modN - mModZ);
      mSourceZ     += aBlk * (sourceN - mSourceZ);
      mDirectLoopZ += aBlk * (directN - mDirectLoopZ);
      mSpreadZ     += aBlk * (spreadN - mSpreadZ);
      const float aSize = 1.0f - expf(-1.0f / (fs * 0.040f));

      float g[kFdnN];
      for (int i = 0; i < kFdnN; i++)
      {
        float L = (float)kFdnBase[i] * mSizeScaleZ;
        if (L < 1.0f) L = 1.0f;
        if (L > (float)(kFdnBufLen - 2)) L = (float)(kFdnBufLen - 2);
        float gi = powf(10.0f, -3.0f * L / (fs * mT60Z));
        if (gi > 0.9999f) gi = 0.9999f;
        g[i] = gi;
      }
      const float apG = mDiffGZ;
      const float fMixA = mDensityZ * 2.0f / (float)kFdnN;
      const float kFdnWetGain = 0.5f;
      const float kTapWetGain = 0.35f;

      float lfoInc[kTapN];
      for (int i = 0; i < kTapN; i++) lfoInc[i] = 2.0f * kPi * mTapLfoHz[i] / fs;
      const float kJitDepth = 2.5f;
      float fdnLfoInc[kFdnN];
      for (int i = 0; i < kFdnN; i++) fdnLfoInc[i] = 2.0f * kPi * mFdnLfoHz[i] / fs;
      const float modDepth = mModZ * 18.0f;

      // global CLOCK decimation factor R. Steps mode snaps to the harmonized
      // LUT (crisp detents); Smooth mode uses a continuous R, glided per block.
      const float clockN = clampf(mClock.value(), 0.0f, 1.0f);
      if (mClockMode.value() == 2)
      {
        const float targetR = powf(16.0f, 1.0f - clockN);   // continuous 1..16
        const float aClock = 1.0f - expf(-(float)FRAMELENGTH / (fs * 0.040f));
        mRcurZ += aClock * (targetR - mRcurZ);
      }
      else
      {
        int clockIdx = (int)((1.0f - clockN) * (kClockSteps - 1) + 0.5f);
        if (clockIdx < 0) clockIdx = 0; else if (clockIdx >= kClockSteps) clockIdx = kClockSteps - 1;
        mRcurZ = (float)kClockR[clockIdx];
      }
      const float clockInc = 1.0f / mRcurZ;
      const float Rcur = mRcurZ;
      // Grit: 0..0.5 crossfades clean linear-interp -> broken ZOH reconstruction;
      // 0.5..1 adds bit-crush. 0.5 = ZOH only (matches pre-Grit behavior).
      const float gritN = clampf(mGrit.value(), 0.0f, 1.0f);
      float reconBroken = gritN * 2.0f;        if (reconBroken > 1.0f) reconBroken = 1.0f;
      float crushAmt    = gritN * 2.0f - 1.0f; if (crushAmt < 0.0f)    crushAmt = 0.0f;
      const float crushLevels = powf(2.0f, 16.0f - crushAmt * 10.0f); // 16-bit .. 6-bit
      const float crushInv = 1.0f / crushLevels;

      for (int n = 0; n < FRAMELENGTH; n++)
      {
        const float dryL = inL[n];
        const float dryR = inR[n];

        // ---- global CLOCK: advance the looper+field sub-engine once every R
        // output samples, so reverb/loop/glides all lengthen together; the wet
        // is ZOH-held between steps (bit-identical at R=1, lo-fi grit below). ----
        mClockPhase += clockInc;
        if (mClockPhase >= 1.0f)
        {
          mClockPhase -= 1.0f;
          mWetPrevL = mWetL; mWetPrevR = mWetR;   // previous step (for clean interp)
          mSizeScaleZ += aSize * (sizeScaleTgt - mSizeScaleZ);

          // cross-feedback: the governed field wet re-enters the looper input,
          // so the reverb tail gets re-looped / re-glitched / re-reverbed. DC-
          // blocked + Spiral-governed -> sings into saturation, not runaway.
          float fbDC = mWetFb - mFbDcX1 + 0.999f * mFbDcY1;
          mFbDcX1 = mWetFb; mFbDcY1 = fbDC;
          float fb = spiralSat(fbDC * (regenN * kRegenMax), kFbGovD);

        // ================= MICRO-LOOPER (Tape): capture + var-speed read =====
        const float rawIn = (dryL + dryR) * 0.5f;
        const float xIn = rawIn + fb;

        // envelope follower (drives Env-mode auto-trigger)
        float ax = xIn < 0.0f ? -xIn : xIn;
        mEnvFast = ax > mEnvFast ? ax : mEnvFast * envFastDecay;
        mEnvSlow += envSlowA * (ax - mEnvSlow);

        // per-sample Speed/Length glide (REPITCH-style; no zipper)
        mLoopLenZ += aLoopGlide * (targetLen - mLoopLenZ);
        mSpeedZ   += aLoopGlide * (targetSpeed - mSpeedZ);
        const float Lf = mLoopLenZ;              // >= 64 (glides between clamped targets)
        int Lint = (int)Lf;
        if (Lint < 64) Lint = 64; else if (Lint > kLoopBufLen) Lint = kLoopBufLen;

        // Freeze TOGGLE gate (0/1) + sound-on-sound capture (both modes).
        float fzTarget = (fzBuf[n] >= 0.5f) ? 1.0f : 0.0f;
        mFreezeZ += aFreeze * (fzTarget - mFreezeZ);
        // momentary capture: a trigger holds recording for ~one slice so the
        // re-triggered window is a CLEAN frozen snapshot (crisp stutter, not a
        // moving-target judder); auto-releases unless re-fired. Combines with
        // the manual Freeze toggle.
        if (mCaptureTimer > 0 && --mCaptureTimer == 0) mCaptureHold = false;
        mCaptureHoldZ += aCapture * ((mCaptureHold ? 1.0f : 0.0f) - mCaptureHoldZ);
        float effFreeze = 1.0f - (1.0f - mFreezeZ) * (1.0f - mCaptureHoldZ);
        // Clock-invariant freeze: lock a reference to the rate the buffer is
        // recorded at. While recording (effFreeze < 0.5) hard-set it to the
        // current clock; while frozen, HARD-HOLD it (no leak -- a one-pole here
        // asymptotes and slowly slews the pitch back). The read advance is
        // scaled by Rcur/recordedRate so a held sound keeps its CAPTURED pitch
        // wherever the clock moves (live -> factor 1, identical); the clock then
        // only morphs grit + reverb around it.
        if (effFreeze < 0.5f) mRecRateRef = Rcur;
        const float clockComp = Rcur / mRecRateRef;
        if (mLoopWr >= Lint) mLoopWr %= Lint;
        mLoopBuf[mLoopWr] = effFreeze * mLoopBuf[mLoopWr] + (1.0f - effFreeze) * xIn;
        mLoopWr++; if (mLoopWr >= Lint) mLoopWr = 0;

        // Capture/re-trigger: a rising edge restarts playback from "now"
        // (read = the live write head -> plays the last Length window from
        // its start). Clock it for rhythmic stutter.
        bool trigHigh = trigBuf[n] >= 0.5f;
        bool fire = (trigHigh && !mTrigPrev);
        if (envMode && mEnvRefractory <= 0 && mEnvFast > mEnvSlow * threshMult && ax > kEnvFloor)
        {
          fire = true;
          mEnvRefractory = envRefr;             // auto-trigger from input transient
        }
        if (mEnvRefractory > 0) mEnvRefractory--;
        if (fire)
        {
          mOldReadPos = mLoopReadPos;            // old Tape head keeps its trajectory
          mLoopReadPos = (float)mLoopWr;         // new head jumps to "now"
          mStretchHead = (float)mLoopWr;         // re-anchor the Stretch source
          mGrainSpawnCtr = 0;                    // spawn a fresh grain now; DON'T kill
                                                 // old grains -> they fade out (overlap)
          mRetrigPhase = stretchMode ? 1.0f : 0.0f; // Tape crossfades; Stretch self-overlaps
          mCaptureHold = true;                   // hold the snapshot (momentary capture)
          mCaptureTimer = Lint > captureMin ? Lint : captureMin;
        }
        mTrigPrev = trigHigh;

        float looperOut;
        if (stretchMode)
        {
          // ---- STRETCH: granular time-stretch. The source playhead moves
          // at the TIME rate (mSpeedZ); each grain replays at UNITY pitch,
          // so time and pitch decouple (slow without dropping pitch). ----
          mStretchHead += mSpeedZ * clockComp;
          if (!(mStretchHead >= 0.0f && mStretchHead < Lf))
          {
            if (!isfinitef(mStretchHead)) mStretchHead = 0.0f;
            else { mStretchHead = fmodf(mStretchHead, Lf); if (mStretchHead < 0.0f) mStretchHead += Lf; }
          }
          if (--mGrainSpawnCtr <= 0)
          {
            int g = 0; bool found = false;
            for (int i = 0; i < kStretchGrains; i++) if (!mGrainActive[i]) { g = i; found = true; break; }
            if (!found) { float mx = -1.0f; for (int i = 0; i < kStretchGrains; i++) if (mGrainEnvPh[i] > mx) { mx = mGrainEnvPh[i]; g = i; } }
            mGrainPos[g] = mStretchHead;
            mGrainEnvPh[g] = 0.0f;
            mGrainActive[g] = true;
            mGrainSpawnCtr = (int)kGrainHop;
          }
          float out = 0.0f;
          const float envInc = 1.0f / kGrainDur;
          for (int g = 0; g < kStretchGrains; g++)
          {
            if (!mGrainActive[g]) continue;
            int hi = (int)(mGrainEnvPh[g] * (float)(kHannLutN - 1));
            if (hi < 0) hi = 0; else if (hi >= kHannLutN) hi = kHannLutN - 1;
            out += readLoopHermite(mGrainPos[g], Lint) * mHannLut[hi];
            mGrainPos[g] += clockComp;            // unity pitch (clock-compensated)
            if (mGrainPos[g] >= Lf) mGrainPos[g] -= Lf;
            mGrainEnvPh[g] += envInc;
            if (mGrainEnvPh[g] >= 1.0f) mGrainActive[g] = false;
          }
          looperOut = out;
        }
        else
        {
          // ---- TAPE: variable-speed head, with a two-head CROSSFADE on
          // re-trigger -- the old head keeps its trajectory and fades out
          // while the new head fades in (complementary gains -> constant
          // amplitude, no onset dip; the Clouds freeze-loop structure). ----
          if (!(mLoopReadPos >= 0.0f && mLoopReadPos < Lf))
          {
            if (!isfinitef(mLoopReadPos)) mLoopReadPos = 0.0f;
            else { mLoopReadPos = fmodf(mLoopReadPos, Lf); if (mLoopReadPos < 0.0f) mLoopReadPos += Lf; }
          }
          float newS = readLoopHermite(mLoopReadPos, Lint);
          float dseam = mLoopReadPos < (Lf - mLoopReadPos) ? mLoopReadPos : (Lf - mLoopReadPos);
          if (dseam < kSeamXf) newS *= 0.5f - 0.5f * cosf(kPi * dseam / kSeamXf);
          mLoopReadPos += mSpeedZ * clockComp;

          float lp;
          if (mRetrigPhase < 1.0f)
          {
            if (!(mOldReadPos >= 0.0f && mOldReadPos < Lf))
            {
              if (!isfinitef(mOldReadPos)) mOldReadPos = 0.0f;
              else { mOldReadPos = fmodf(mOldReadPos, Lf); if (mOldReadPos < 0.0f) mOldReadPos += Lf; }
            }
            float oldS = readLoopHermite(mOldReadPos, Lint);
            mOldReadPos += mSpeedZ * clockComp;
            mRetrigPhase += retrigInc;
            if (mRetrigPhase > 1.0f) mRetrigPhase = 1.0f;
            lp = oldS + (newS - oldS) * mRetrigPhase;  // linear crossfade old->new
          }
          else lp = newS;

          mLoopLpZ += aLoopLp * (lp - mLoopLpZ);       // dynamic AA LP
          looperOut = mLoopLpZ;
        }
        if (!isfinitef(looperOut)) looperOut = 0.0f;   // finite firewall to field
        mLoopOutHeld = looperOut;                       // for the direct-loop blend
        // Router Source: the field reverberates input (0) .. loop (1).
        const float fieldIn = rawIn + mSourceZ * (looperOut - rawIn);

        // ================= STAGE 1: sparse feedforward taps =================
        mTapBuf[mTapWr] = fieldIn;
        float tapL = 0.0f, tapR = 0.0f;
        for (int i = 0; i < kTapN; i++)
        {
          mTapLfoPhase[i] += lfoInc[i];
          if (mTapLfoPhase[i] > 2.0f * kPi) mTapLfoPhase[i] -= 2.0f * kPi;
          float d = (float)kTapBase[i] * mSizeScaleZ + sinf(mTapLfoPhase[i]) * kJitDepth;
          if (d < 1.0f) d = 1.0f;
          if (d > (float)(kTapBufLen - 2)) d = (float)(kTapBufLen - 2);
          float t = readTap(d) * mTapGain[i];
          tapL += t * mPanL[i];
          tapR += t * mPanR[i];
        }
        tapL *= kTapWetGain;
        tapR *= kTapWetGain;
        mTapWr++; if (mTapWr >= kTapBufLen) mTapWr = 0;

        // ================= STAGE 2: unitary FDN tail =================
        float x = fieldIn;
        for (int k = 0; k < kApN; k++)
        {
          int idx = mApWr[k];
          float zD = mAp[k][idx];
          float v = x + apG * zD;
          float y = -apG * v + zD;
          mAp[k][idx] = v;
          idx++; if (idx >= kApLen[k]) idx = 0;
          mApWr[k] = idx;
          x = y;
        }

        float r[kFdnN], s = 0.0f;
        for (int i = 0; i < kFdnN; i++)
        {
          mFdnLfoPhase[i] += fdnLfoInc[i];
          if (mFdnLfoPhase[i] > 2.0f * kPi) mFdnLfoPhase[i] -= 2.0f * kPi;
          float L = (float)kFdnBase[i] * mSizeScaleZ + modDepth * sinf(mFdnLfoPhase[i]);
          if (L < 1.0f) L = 1.0f;
          if (L > (float)(kFdnBufLen - 2)) L = (float)(kFdnBufLen - 2);
          r[i] = readLine(i, L);
          s += r[i];
        }
        for (int i = 0; i < kFdnN; i++)
        {
          float mixed = r[i] - fMixA * s;
          float w = x + g[i] * mixed;
          if (!isfinitef(w)) w = 0.0f;
          if (w > 8.0f) w = 8.0f; else if (w < -8.0f) w = -8.0f;
          mLine[i][mWr] = w;
        }
        float fdnL = (r[0] + r[1] + r[2] + r[3]) * kFdnWetGain;
        float fdnR = (r[4] + r[5] + r[6] + r[7]) * kFdnWetGain;
        mWr++; if (mWr >= kFdnBufLen) mWr = 0;

        // ============ DENSITY crossfade: sparse taps <-> dense FDN ============
        mWetL = tapL + mDensityZ * (fdnL - tapL);
        mWetR = tapR + mDensityZ * (fdnR - tapR);
        mWetFb = (mWetL + mWetR) * 0.5f;             // cross-feedback tap (next step)
        if (!isfinitef(mWetFb)) mWetFb = 0.0f;
        } // ---- end CLOCK sub-engine step ----

        // reconstruct wet at Fs -- Grit crossfades clean linear-interp <-> broken
        // ZOH, then bit-crushes. t = fraction since the last sub-engine step.
        const float ct = mClockPhase;
        float wL = mWetPrevL + (mWetL - mWetPrevL) * ct;
        float wR = mWetPrevR + (mWetR - mWetPrevR) * ct;
        wL += reconBroken * (mWetL - wL);
        wR += reconBroken * (mWetR - wR);
        wL = floorf(wL * crushLevels + 0.5f) * crushInv;
        wR = floorf(wR * crushLevels + 0.5f) * crushInv;
        // Router Spread: mid/side width (0 mono, 0.5 normal, 1 wide).
        const float mid = (wL + wR) * 0.5f;
        const float side = (wL - wR) * 0.5f * (mSpreadZ * 2.0f);
        wL = mid + side; wR = mid - side;
        // Router DirectLoop: clean (ZOH-held) glitched loop blended in.
        const float dl = mDirectLoopZ * mLoopOutHeld;
        outL[n] = dryL + mix * (wL - dryL) + dl;
        outR[n] = dryR + mix * (wR - dryR) + dl;
      }
    }

    static inline float clampf(float v, float lo, float hi)
    {
      return v < lo ? lo : (v > hi ? hi : v);
    }
    static inline bool isfinitef(float v)
    {
      return (v == v) && (v <= 3.0e38f) && (v >= -3.0e38f);
    }
    // Spiral governor (sin-clipper): unity at small x, soft-bounds to +/-1/d.
    static inline float spiralSat(float x, float d)
    {
      float a = (x < 0.0f ? -x : x) * d;
      if (a > 1.57079633f) a = 1.57079633f;   // pi/2
      float s = sinf(a) / d;
      return x < 0.0f ? -s : s;
    }

    // ---- state ----
    float mLoopBuf[kLoopBufLen];
    int   mLoopWr;
    float mLoopReadPos, mLoopLpZ, mFreezeZ, mSpeedZ, mLoopLenZ;
    float mStretchHead;
    float mGrainPos[kStretchGrains], mGrainEnvPh[kStretchGrains];
    bool  mGrainActive[kStretchGrains];
    int   mGrainSpawnCtr;
    float mHannLut[kHannLutN];
    float mRetrigPhase, mOldReadPos;
    float mEnvFast, mEnvSlow;
    int   mEnvRefractory;
    bool  mTrigPrev = false;
    bool  mCaptureHold;
    float mCaptureHoldZ;
    int   mCaptureTimer;
    float mClockPhase, mWetL, mWetR, mWetPrevL, mWetPrevR;
    float mRecRateRef;   // clock R at which the buffered content was recorded
    float mRcurZ;        // current (possibly glided) clock R
    float mWetFb, mFbDcX1, mFbDcY1;   // cross-feedback signal + DC-blocker state
    float mSourceZ, mDirectLoopZ, mSpreadZ, mLoopOutHeld;   // Router
    float mLine[kFdnN][kFdnBufLen];
    float mAp[kApN][kApMax];
    int   mApWr[kApN];
    int   mWr;
    float mTapBuf[kTapBufLen];
    int   mTapWr;
    float mPanL[kTapN], mPanR[kTapN], mTapGain[kTapN];
    float mTapLfoPhase[kTapN], mTapLfoHz[kTapN];
    float mFdnLfoPhase[kFdnN], mFdnLfoHz[kFdnN];
    float mSizeScaleZ, mT60Z, mDiffGZ, mDensityZ, mModZ;
    bool  mInit = false;
    bool  mFzSet = false;
#endif
  };

} // namespace anamnesis
