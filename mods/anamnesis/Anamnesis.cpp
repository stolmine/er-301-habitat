// anamnesis -- out-of-line hot paths.
//
// This package used to be single-TU: ALL code (DSP process() included) was
// emitted inside the SWIG wrapper TU, which compiles at CFLAGS.size (-Os, no
// -ffast-math). Measured on the offline bench that costs ~4x on the raster
// path and leaves per-sample libm calls un-optimized. This TU carries the hot
// bodies at the package speed profile (-O3 -ffast-math -fno-tree-vectorize on
// am335x, mod.mk CFLAGS), exactly the DrumVoice.cpp pattern.
//
// RULES HONORED:
//  - AnamFieldGraphic::draw() (framework virtual) stays inline in its header;
//    only the non-virtual drawImpl lives here (no key-function vtable shift
//    for the graphic; Anamnesis::process() out-of-line matches every other
//    od::Object unit in the repo).
//  - NEON intrinsics only in the per-step tap/FDN gather (round two, Pecto's
//    3-pass pattern): hand-written, class-member SoA storage only, no
//    alignment claims, whole-.o hint scan gates the build.
//    -fno-tree-vectorize stays load-bearing on am335x (no AUTO-vectorization).

#include <od/config.h>
#include "atoms/Anamnesis.h"
#include "AnamFieldGraphic.h"
#include <math.h>
#include <string.h>

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace anamnesis
{

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
// 4-lane field::polySin2Pi on phases in [0, 2pi]: the quadrant select uses
// the P(pi - y) fold, bit-identical per lane to the scalar form (P is odd and
// IEEE subtraction is exactly anti-symmetric). vmlaq rounds the product then
// the sum -- the same two roundings as the scalar's separate mul/add on
// armv7 (no fused contraction there), so lanes match the scalar fallback.
static inline float32x4_t polySin2PiQ(float32x4_t y)
{
  const float32x4_t vPi    = vdupq_n_f32(3.14159265f);
  const float32x4_t vTwoPi = vdupq_n_f32(6.28318531f);
  uint32x4_t m1 = vcltq_f32(y, vdupq_n_f32(1.57079633f));
  uint32x4_t m2 = vcltq_f32(y, vdupq_n_f32(4.71238898f));
  float32x4_t x = vbslq_f32(m1, y, vbslq_f32(m2, vsubq_f32(vPi, y), vsubq_f32(y, vTwoPi)));
  float32x4_t x2 = vmulq_f32(x, x);
  float32x4_t p = vmlaq_f32(vdupq_n_f32(-1.98412698e-4f), x2, vdupq_n_f32(2.75573192e-6f));
  p = vmlaq_f32(vdupq_n_f32(0.00833333333f), x2, p);
  p = vmlaq_f32(vdupq_n_f32(-0.16666667f), x2, p);
  p = vmlaq_f32(vdupq_n_f32(1.0f), x2, p);
  return vmulq_f32(x, p);
}
#endif

void Anamnesis::process()
{
    ensureFlushToZero();

    const float *inL = mInL.buffer();
    const float *inR = mInR.buffer();
    float *outL = mOutL.buffer();
    float *outR = mOutR.buffer();

    const float fs = (float)globalConfig.sampleRate;

    // ---- controls ----
    const float *fzBuf = mFreeze.buffer();
    const int    mode = mMode.value();
    const bool   stretchMode = (mode == 2);
    const bool   envMode = (mode == 3);
    float lengthN = clampf(mLength.value(), 0.0f, 1.0f);
    float speedN  = clampf(mSpeed.value(), -kSpeedMax, kSpeedMax);  // bipolar rate
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
    int senseV = mSense.value();
    const float threshMult   = (senseV <= 1) ? 4.0f : (senseV == 2 ? 2.5f : 1.5f); // Low/Med/High
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

    // Block-rate fills into the SoA members (shared by the NEON and scalar
    // per-step paths; values identical to the old stack lfoInc arrays).
    for (int i = 0; i < kTapN; i++) mTapIncQ[i] = 2.0f * kPi * mTapLfoHz[i] / fs;
    const float kJitDepth = 2.5f;
    for (int i = 0; i < kFdnN; i++) { mFdnIncQ[i] = 2.0f * kPi * mFdnLfoHz[i] / fs; mFdnGQ[i] = g[i]; }
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

    // Offscreen-viz gate ([hab:viz-offscreen-gate-all], the Fabula pattern):
    // the graphics ping the heartbeat on every draw; it counts down here at
    // BLOCK rate. When no Anamnesis ply is on screen the pings stop and the
    // whole viz sim below (flow phase, rain, bubble physics -- the flow/
    // rippleEval/noise transcendental load) is skipped. It only ever feeds
    // the graphic; the audio path never reads it. Block-constant bool, so no
    // per-sample runtime tier (feedback_runtime_branched_dsp_dispatch).
    const bool vizActive = (mVizHeartbeat > 0);
    if (mVizHeartbeat > 0) mVizHeartbeat--;

    // Viz animation phase: the flow-field's "current". Advances per block;
    // speed ~ 1/R so a slower CLOCK slows the whole pond. Shared by every ply's
    // AnamFieldGraphic so the all-over image stays in sync. Wrapped to bound float.
    // Freeze STOPS the flow motion only (the lines hold their shape) -- droplet
    // instancing + influence continue, so a frozen pond keeps being rained on.
    const float vizFreeze = 1.0f - (1.0f - mFreezeZ) * (1.0f - mCaptureHoldZ);
    if (vizActive)
    {
      mVizPhase += (1.0f - vizFreeze) * (float)FRAMELENGTH / fs * (6.2831853f * 0.20f) / Rcur;
      if (mVizPhase > 6.2831853f * 1024.0f) mVizPhase -= 6.2831853f * 1024.0f;
    }

    // Rain: age the active droplets (real seconds), retire the expired, and
    // spawn one per completed loop cycle (read-pointer wrap since last block).
    if (vizActive)
    {
      const float vizDt = (float)FRAMELENGTH / fs;
      // Decay -> longer ripple life (= 3 * persistence-scaled tau).
      const float dropLife = 3.0f * anamnesis::field::rippleTauOf(vizDecay());
      for (int i = 0; i < kVizMaxDrops; i++)
      {
        if (mDropAge[i] >= 0.0f)
        {
          mDropAge[i] += vizDt;
          if (mDropAge[i] > dropLife) mDropAge[i] = -1.0f;
        }
      }
      // A completed loop cycle = the active read head jumping by more than half
      // the loop (works in any mode/direction: Tape/Env use mLoopReadPos,
      // Stretch the source head; a normal per-block advance is tiny).
      const float curRead = stretchMode ? mStretchHead : mLoopReadPos;
      if (mLoopLenZ > 2.0f && fabsf(curRead - mDropPrevRead) > 0.5f * mLoopLenZ)
        spawnDrop();
    }
    // Track the read head even when gated (2 ops) so coming back on screen
    // does not fake a loop-wrap and spawn a phantom droplet.
    mDropPrevRead = stretchMode ? mStretchHead : mLoopReadPos;

    // Bubbles: float up + drift. SPEED tied to the CLOCK (vdt) like the rest of
    // the sim; COUNT tied to Density; a new bubble sometimes CALVES off an
    // existing one (born beside it, drifting away) so split-offs live on their own.
    if (vizActive)
    {
      const float vdt = (float)FRAMELENGTH / fs / mRcurZ; // clock-scaled time step
      const float colH = anamnesis::field::kVizColH;
      const float stripW = anamnesis::field::kVizStripW;
      const float vizFreeze = 1.0f - (1.0f - mFreezeZ) * (1.0f - mCaptureHoldZ);
      const float vsize = vizSize(); // Size -> flow feature scale (same as graphic)
      const float vmod  = vizMod();  // Mod -> slow flow wander (same as graphic)
      const float rtau  = anamnesis::field::rippleTauOf(vizDecay()); // Decay -> ripple persistence
      int activeB = 0;
      for (int i = 0; i < kVizMaxBubbles; i++)
      {
        if (mBubR[i] <= 0.0f) continue;
        // --- PHYSICS: the bubble is CARRIED by the flow current and SHOVED by
        // passing ripple fronts. Velocity (mBubVx/Vy) relaxes with inertia
        // toward a TARGET = buoyant rise + flow-carry + ripple-shove, so shapes
        // get thrown off course over the weighted field, then drift back. ---
        const float bx = mBubX[i], by = mBubY[i];
        // Flow advection: treat flow() as a streamfunction -> incompressible
        // swirling velocity (d/dy, -d/dx) so bubbles ride eddies, not slide off.
        const float e = anamnesis::field::kPushEps;
        const float fdx = anamnesis::field::flow(bx + e, by, mVizPhase, vsize, vmod)
                        - anamnesis::field::flow(bx - e, by, mVizPhase, vsize, vmod);
        const float fdy = anamnesis::field::flow(bx, by + e, mVizPhase, vsize, vmod)
                        - anamnesis::field::flow(bx, by - e, mVizPhase, vsize, vmod);
        const float inv2e = anamnesis::field::kFlowAdvect / (2.0f * e);
        const float tvx =  fdy * inv2e;                              // carried-x
        const float tvy =  anamnesis::field::kBubRise - fdx * inv2e; // rise + carried-y
        // Relax velocity toward the flow-carry target (inertia). This relaxation
        // also damps the ripple impulse below, so kicks fade over ~1/kBubResp s.
        const float resp = anamnesis::field::kBubResp * vdt;
        mBubVx[i] += resp * (tvx - mBubVx[i]);
        mBubVy[i] += resp * (tvy - mBubVy[i]);
        // Ripple IMPULSE (Stokes drift): each active drop's passing crest imparts
        // an accumulating radial kick (toward the crater, then out on jet/rings),
        // scaled by loudness. Unlike a velocity target, an impulse leaves NET
        // outward drift as a ring sweeps past -> expanding rings shove the whole
        // field, not just the epicenter.
        for (int d = 0; d < kVizMaxDrops; d++)
        {
          if (mDropAge[d] < 0.0f) continue;
          const float ddx = bx - mDropX[d], ddy = by - mDropY[d];
          const anamnesis::field::RippleHit h =
            anamnesis::field::rippleEval(ddx, ddy, mDropAge[d], mDropSpeed[d], rtau);
          if (h.push == 0.0f) continue;
          const float rr = sqrtf(ddx * ddx + ddy * ddy) + anamnesis::field::kRippleEps;
          const float k = anamnesis::field::kRipplePush * mDropAmp[d] * h.push / rr;
          mBubVx[i] += vdt * k * ddx;
          mBubVy[i] += vdt * k * ddy;
        }
        // Safety clamp (impulses accumulate; keep velocity bounded).
        const float vcap = anamnesis::field::kBubVMax;
        if (mBubVx[i] >  vcap) mBubVx[i] =  vcap; else if (mBubVx[i] < -vcap) mBubVx[i] = -vcap;
        if (mBubVy[i] >  vcap) mBubVy[i] =  vcap; else if (mBubVy[i] < -vcap) mBubVy[i] = -vcap;
        const float fa = mBubSeed[i] * 0.41f;
        const float rvx = cosf(fa) * anamnesis::field::kFreezeDrift;
        const float rvy = sinf(fa) * anamnesis::field::kFreezeDrift;
        const float vx = (1.0f - vizFreeze) * mBubVx[i] + vizFreeze * rvx;
        const float vy = (1.0f - vizFreeze) * mBubVy[i] + vizFreeze * rvy;
        mBubX[i] += vdt * vx;
        mBubY[i] += vdt * vy;
        if (mBubY[i] > colH + mBubR[i] + 2.0f || mBubY[i] < -mBubR[i] - 12.0f ||
            mBubX[i] < -20.0f || mBubX[i] > stripW + 20.0f) { mBubR[i] = 0.0f; continue; }
        activeB++;
      }
      const int target = (int)(density * (float)kVizMaxBubbles + 0.5f);
      mBubSpawnT -= vdt;
      if (activeB < target && mBubSpawnT <= 0.0f)
      {
        int slot = -1;
        for (int i = 0; i < kVizMaxBubbles; i++) if (mBubR[i] <= 0.0f) { slot = i; break; }
        if (slot >= 0)
        {
          mBubRng = mBubRng * 1664525u + 1013904223u; float ux = (float)((mBubRng >> 9) & 0x7fffff) / 8388607.0f;
          mBubRng = mBubRng * 1664525u + 1013904223u; float uz = (float)((mBubRng >> 9) & 0x7fffff) / 8388607.0f;
          mBubRng = mBubRng * 1664525u + 1013904223u; float ur = (float)((mBubRng >> 9) & 0x7fffff) / 8388607.0f;
          mBubRng = mBubRng * 1664525u + 1013904223u; float uv = (float)((mBubRng >> 9) & 0x7fffff) / 8388607.0f;
          mBubRng = mBubRng * 1664525u + 1013904223u; float uc = (float)((mBubRng >> 9) & 0x7fffff) / 8388607.0f;
          // Maybe calve off a mature parent.
          int parent = -1;
          if (activeB > 0 && uc < anamnesis::field::kCalveProb)
          {
            mBubRng = mBubRng * 1664525u + 1013904223u;
            int pick = (int)((mBubRng >> 9) % (unsigned)kVizMaxBubbles);
            for (int t = 0; t < kVizMaxBubbles; t++)
            { int q = (pick + t) % kVizMaxBubbles; if (q != slot && mBubR[q] > 5.0f) { parent = q; break; } }
          }
          if (parent >= 0)
          {
            const float ang = ux * 6.2831853f;
            const float off = mBubR[parent] * 1.3f + 3.0f;
            mBubX[slot] = mBubX[parent] + cosf(ang) * off;
            mBubY[slot] = mBubY[parent] + sinf(ang) * off;
            mBubZ[slot] = mBubZ[parent];                       // same z-level (can interact)
            mBubR[slot] = 3.0f + ur * (mBubR[parent] * 0.5f);  // smaller child
            mBubVx[slot] = mBubVx[parent] + cosf(ang) * 8.0f;  // drift apart
            mBubVy[slot] = mBubVy[parent] + sinf(ang) * 8.0f;  // (in 2D)
            mBubR[parent] *= 0.82f;                             // parent gives off mass
          }
          else
          {
            mBubX[slot] = ux * anamnesis::field::kVizStripW;
            mBubY[slot] = -2.0f;                                       // just below the bottom
            int lv = (int)(uz * (float)anamnesis::field::kBubLevels);
            if (lv >= anamnesis::field::kBubLevels) lv = anamnesis::field::kBubLevels - 1;
            mBubZ[slot] = (float)lv;                                    // metaball z-LEVEL
            mBubR[slot] = 3.0f + ur * 12.0f;                           // radius 3..15 (varied, +1.5x max)
            mBubVx[slot] = (uv - 0.5f) * 6.0f;                         // small drift px/s
            mBubVy[slot] = anamnesis::field::kBubRise;                 // start rising (physics takes over)
          }
          mBubRng = mBubRng * 1664525u + 1013904223u;
          mBubSeed[slot] = (float)((mBubRng >> 9) & 0x7fffff) / 8388607.0f * 16.0f; // blob seed
          mBubSpawnT = anamnesis::field::kBubSpawnInt;
        }
      }
    }
    // Grit: 0..0.5 crossfades clean linear-interp -> broken ZOH reconstruction;
    // 0.5..1 adds bit-crush. 0.5 = ZOH only (matches pre-Grit behavior).
    int gritV = mGrit.value();
    const float gritN = (gritV <= 1) ? 0.0f : (gritV == 2 ? 0.5f : 1.0f); // Clean/Normal/Broken
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
      bool fire = false;
      if (envMode && mEnvRefractory <= 0 && mEnvFast > mEnvSlow * threshMult && ax > kEnvFloor)
      {
        fire = true;
        mEnvRefractory = envRefr;             // Env auto-trigger from input transient
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

      float looperOut;
      if (stretchMode)
      {
        // ---- STRETCH: granular time-stretch. The source playhead moves
        // at the TIME rate (mSpeedZ); each grain replays at UNITY pitch,
        // so time and pitch decouple (slow without dropping pitch). ----
        mStretchHead += mSpeedZ * clockComp;
        // NaN-safe wrap: test finiteness FIRST via the bit-based check (a
        // float range-compare on NaN is unreliable under -ffast-math), then
        // range-wrap. Identical behavior for every finite value.
        if (!isfinitef(mStretchHead)) mStretchHead = 0.0f;
        else if (mStretchHead < 0.0f || mStretchHead >= Lf)
        { mStretchHead = fmodf(mStretchHead, Lf); if (mStretchHead < 0.0f) mStretchHead += Lf; }
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
        if (!isfinitef(mLoopReadPos)) mLoopReadPos = 0.0f;   // NaN-safe wrap (see Stretch)
        else if (mLoopReadPos < 0.0f || mLoopReadPos >= Lf)
        { mLoopReadPos = fmodf(mLoopReadPos, Lf); if (mLoopReadPos < 0.0f) mLoopReadPos += Lf; }
        float newS = readLoopHermite(mLoopReadPos, Lint);
        float dseam = mLoopReadPos < (Lf - mLoopReadPos) ? mLoopReadPos : (Lf - mLoopReadPos);
        // raised-cosine seam declick; polyCosPi matches cosf to < 5e-7 on [0, pi)
        if (dseam < kSeamXf) newS *= 0.5f - 0.5f * polyCosPi(kPi * dseam / kSeamXf);
        mLoopReadPos += mSpeedZ * clockComp;

        float lp;
        if (mRetrigPhase < 1.0f)
        {
          if (!isfinitef(mOldReadPos)) mOldReadPos = 0.0f;   // NaN-safe wrap (see Stretch)
          else if (mOldReadPos < 0.0f || mOldReadPos >= Lf)
          { mOldReadPos = fmodf(mOldReadPos, Lf); if (mOldReadPos < 0.0f) mOldReadPos += Lf; }
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
      // Pecto's 3-pass gather (feedback_neon_delay_gather): Pass A computes
      // the 12 modulated delays / read indices 4-wide, Pass B is the scalar
      // scatter-gather (no NEON gather load on A8), Pass C is the NEON
      // interp x gain x pan accumulate. Per-lane math mirrors the scalar
      // fallback below exactly; the only difference is the L/R accumulation
      // ORDER (lane-parallel + pairwise vs sequential), a ~1 ulp
      // reassociation of a 12-term sum.
      mTapBuf[mTapWr] = fieldIn;
      float tapL = 0.0f, tapR = 0.0f;
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
      {
        // NOTE: the vdupq constants are constructed INSIDE the loop bodies,
        // not hoisted. Hoisting them made 8+ quads live across the whole
        // region and gcc spilled q-regs to [sp :64] -- the documented A8
        // trap (feedback_neon_hint_surfaces, AlembicVoice phase 3a). In-loop
        // vdup rematerializes in registers; the loops run 3x/2x per step.
        for (int q = 0; q < kTapN; q += 4)   // ---- Pass A: delays -> idx/frac
        {
          const float32x4_t vTwoPi = vdupq_n_f32(2.0f * kPi);
          float32x4_t ph = vaddq_f32(vld1q_f32(&mTapLfoPhase[q]), vld1q_f32(&mTapIncQ[q]));
          uint32x4_t wm = vcgtq_f32(ph, vTwoPi);
          ph = vsubq_f32(ph, vreinterpretq_f32_u32(vandq_u32(wm, vreinterpretq_u32_f32(vTwoPi))));
          vst1q_f32(&mTapLfoPhase[q], ph);
          float32x4_t sn = polySin2PiQ(ph);
          float32x4_t d = vmlaq_f32(vmulq_f32(vld1q_f32(&mTapBaseF[q]), vdupq_n_f32(mSizeScaleZ)),
                                    sn, vdupq_n_f32(kJitDepth));
          d = vminq_f32(vmaxq_f32(d, vdupq_n_f32(1.0f)), vdupq_n_f32((float)(kTapBufLen - 2)));
          float32x4_t rp = vsubq_f32(vdupq_n_f32((float)mTapWr), d);
          uint32x4_t neg = vcltq_f32(rp, vdupq_n_f32(0.0f));
          rp = vaddq_f32(rp, vreinterpretq_f32_u32(vandq_u32(neg, vreinterpretq_u32_f32(vdupq_n_f32((float)kTapBufLen)))));
          int32x4_t idx = vcvtq_s32_f32(rp);            // trunc == floor, rp >= 0
          vst1q_f32(&mTapFracQ[q], vsubq_f32(rp, vcvtq_f32_s32(idx)));
          vst1q_s32(&mTapIdxQ[q], idx);
        }
        for (int t = 0; t < kTapN; t++)      // prefetch pre-pass (misses overlap)
          __builtin_prefetch(&mTapBuf[mTapIdxQ[t]], 0, 1);
        for (int t = 0; t < kTapN; t++)      // ---- Pass B: scalar gather
        {
          int i0 = mTapIdxQ[t];
          if (i0 >= kTapBufLen) i0 -= kTapBufLen; // ulp wrap edge (frac==0 there)
          int i1 = i0 + 1; if (i1 >= kTapBufLen) i1 -= kTapBufLen;
          mTapS0Q[t] = mTapBuf[i0];
          mTapS1Q[t] = mTapBuf[i1];
        }
        float32x4_t accL = vdupq_n_f32(0.0f), accR = vdupq_n_f32(0.0f);
        for (int q = 0; q < kTapN; q += 4)   // ---- Pass C: interp+gain+pan MAC
        {
          float32x4_t s0 = vld1q_f32(&mTapS0Q[q]);
          float32x4_t s1 = vld1q_f32(&mTapS1Q[q]);
          float32x4_t t = vmlaq_f32(s0, vsubq_f32(s1, s0), vld1q_f32(&mTapFracQ[q]));
          t = vmulq_f32(t, vld1q_f32(&mTapGain[q]));
          accL = vmlaq_f32(accL, t, vld1q_f32(&mPanL[q]));
          accR = vmlaq_f32(accR, t, vld1q_f32(&mPanR[q]));
        }
        float32x2_t l2 = vpadd_f32(vadd_f32(vget_low_f32(accL), vget_high_f32(accL)),
                                   vadd_f32(vget_low_f32(accR), vget_high_f32(accR)));
        tapL = vget_lane_f32(l2, 0);
        tapR = vget_lane_f32(l2, 1);
      }
#else
      for (int i = 0; i < kTapN; i++)
      {
        // phase accumulation kept BIT-IDENTICAL to the shipped build; only
        // the sine evaluation is poly (< 4e-6, no libm call per step)
        mTapLfoPhase[i] += mTapIncQ[i];
        if (mTapLfoPhase[i] > 2.0f * kPi) mTapLfoPhase[i] -= 2.0f * kPi;
        float d = (float)kTapBase[i] * mSizeScaleZ + field::polySin2Pi(mTapLfoPhase[i]) * kJitDepth;
        if (d < 1.0f) d = 1.0f;
        if (d > (float)(kTapBufLen - 2)) d = (float)(kTapBufLen - 2);
        float t = readTap(d) * mTapGain[i];
        tapL += t * mPanL[i];
        tapR += t * mPanR[i];
      }
#endif
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

      // FDN reads: same 3-pass shape as the taps. The feedback sum s and the
      // L/R output sums are accumulated SCALAR in index order from the lane
      // store, so they stay bit-identical to the old sequential loop; only
      // the per-line delay/interp/write math is lane-parallel (per-lane
      // identical ops).
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
      float fdnL, fdnR;
      {
        for (int q = 0; q < kFdnN; q += 4)   // ---- Pass A: delays -> idx/frac
        {
          const float32x4_t vTwoPi = vdupq_n_f32(2.0f * kPi);
          float32x4_t ph = vaddq_f32(vld1q_f32(&mFdnLfoPhase[q]), vld1q_f32(&mFdnIncQ[q]));
          uint32x4_t wm = vcgtq_f32(ph, vTwoPi);
          ph = vsubq_f32(ph, vreinterpretq_f32_u32(vandq_u32(wm, vreinterpretq_u32_f32(vTwoPi))));
          vst1q_f32(&mFdnLfoPhase[q], ph);
          float32x4_t sn = polySin2PiQ(ph);
          float32x4_t d = vmlaq_f32(vmulq_f32(vld1q_f32(&mFdnBaseF[q]), vdupq_n_f32(mSizeScaleZ)),
                                    vdupq_n_f32(modDepth), sn);
          d = vminq_f32(vmaxq_f32(d, vdupq_n_f32(1.0f)), vdupq_n_f32((float)(kFdnBufLen - 2)));
          float32x4_t rp = vsubq_f32(vdupq_n_f32((float)mWr), d);
          uint32x4_t neg = vcltq_f32(rp, vdupq_n_f32(0.0f));
          rp = vaddq_f32(rp, vreinterpretq_f32_u32(vandq_u32(neg, vreinterpretq_u32_f32(vdupq_n_f32((float)kFdnBufLen)))));
          int32x4_t idx = vcvtq_s32_f32(rp);
          vst1q_f32(&mFdnFracQ[q], vsubq_f32(rp, vcvtq_f32_s32(idx)));
          vst1q_s32(&mFdnIdxQ[q], idx);
        }
        for (int i = 0; i < kFdnN; i++)      // prefetch pre-pass
          __builtin_prefetch(&mLine[i][mFdnIdxQ[i]], 0, 1);
        for (int i = 0; i < kFdnN; i++)      // ---- Pass B: scalar gather
        {
          int i0 = mFdnIdxQ[i];
          if (i0 >= kFdnBufLen) i0 -= kFdnBufLen; // ulp wrap edge (frac==0 there)
          int i1 = i0 + 1; if (i1 >= kFdnBufLen) i1 -= kFdnBufLen;
          mFdnS0Q[i] = mLine[i][i0];
          mFdnS1Q[i] = mLine[i][i1];
        }
        for (int q = 0; q < kFdnN; q += 4)   // ---- Pass C: interp -> r
        {
          float32x4_t s0 = vld1q_f32(&mFdnS0Q[q]);
          float32x4_t s1 = vld1q_f32(&mFdnS1Q[q]);
          vst1q_f32(&mFdnRQ[q], vmlaq_f32(s0, vsubq_f32(s1, s0), vld1q_f32(&mFdnFracQ[q])));
        }
        // feedback sum in the OLD sequential order (bit-identical)
        float s = 0.0f;
        for (int i = 0; i < kFdnN; i++) s += mFdnRQ[i];
        // ---- Pass D: mixed/write, lane-parallel; guard + clamp branchless
        union { float f; uint32_t u; } mfin; mfin.f = 3.0e38f;
        for (int q = 0; q < kFdnN; q += 4)
        {
          float32x4_t rq = vld1q_f32(&mFdnRQ[q]);
          float32x4_t mixed = vsubq_f32(rq, vmulq_f32(vdupq_n_f32(fMixA), vdupq_n_f32(s)));
          float32x4_t w = vmlaq_f32(vdupq_n_f32(x), vld1q_f32(&mFdnGQ[q]), mixed);
          // isfinitef bit test per lane (same unsigned compare as the scalar)
          uint32x4_t fin = vcleq_u32(vandq_u32(vreinterpretq_u32_f32(w), vdupq_n_u32(0x7fffffffu)),
                                     vdupq_n_u32(mfin.u));
          w = vreinterpretq_f32_u32(vandq_u32(fin, vreinterpretq_u32_f32(w)));
          w = vminq_f32(vmaxq_f32(w, vdupq_n_f32(-8.0f)), vdupq_n_f32(8.0f));
          // scattered per-line stores (8 distinct arrays) -- lane extracts
          mLine[q + 0][mWr] = vgetq_lane_f32(w, 0);
          mLine[q + 1][mWr] = vgetq_lane_f32(w, 1);
          mLine[q + 2][mWr] = vgetq_lane_f32(w, 2);
          mLine[q + 3][mWr] = vgetq_lane_f32(w, 3);
        }
        fdnL = (mFdnRQ[0] + mFdnRQ[1] + mFdnRQ[2] + mFdnRQ[3]) * kFdnWetGain;
        fdnR = (mFdnRQ[4] + mFdnRQ[5] + mFdnRQ[6] + mFdnRQ[7]) * kFdnWetGain;
      }
#else
      float r[kFdnN], s = 0.0f;
      for (int i = 0; i < kFdnN; i++)
      {
        mFdnLfoPhase[i] += mFdnIncQ[i];
        if (mFdnLfoPhase[i] > 2.0f * kPi) mFdnLfoPhase[i] -= 2.0f * kPi;
        float L = (float)kFdnBase[i] * mSizeScaleZ + modDepth * field::polySin2Pi(mFdnLfoPhase[i]);
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
#endif
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
      wL = floorf31(wL * crushLevels + 0.5f) * crushInv;
      wR = floorf31(wR * crushLevels + 0.5f) * crushInv;
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

void Anamnesis::spawnDrop()
{
    int slot = 0; float oldest = -1.0f;
    for (int i = 0; i < kVizMaxDrops; i++)
    {
      if (mDropAge[i] < 0.0f) { slot = i; oldest = -1.0f; break; }
      if (mDropAge[i] > oldest) { oldest = mDropAge[i]; slot = i; }
    }
    mDropRng = mDropRng * 1664525u + 1013904223u; float ux = (float)((mDropRng >> 9) & 0x7fffff) / 8388607.0f;
    mDropRng = mDropRng * 1664525u + 1013904223u; float uy = (float)((mDropRng >> 9) & 0x7fffff) / 8388607.0f;
    mDropRng = mDropRng * 1664525u + 1013904223u; float uc = (float)((mDropRng >> 9) & 0x7fffff) / 8388607.0f;
    mDropX[slot] = ux * anamnesis::field::kVizStripW; // anywhere across the whole strip
    mDropY[slot] = 6.0f + uy * 52.0f;      // within the 64px column
    mDropAge[slot] = 0.0f;
    mDropSpeed[slot] = 22.0f + uc * 20.0f; // 22..42 px/s: slow rings -> traceable expansion
    mDropPhase[slot] = ux * 6.2831853f;
    // Ripple size tracks the loudness of this loop capture (fast peak), with a
    // baseline so quiet loops still drip.
    float lvl = mEnvFast * 4.0f; if (lvl > 1.0f) lvl = 1.0f;
    mDropAmp[slot] = 0.5f + 0.5f * lvl;
  }

void Anamnesis::buildFieldFrame()
{
    const float phase = mVizPhase;
    // Active bubbles (content-x / column-y / radius / seed / level).
    int nb = 0;
    float *bX = mFcBX, *bY = mFcBY, *bR = mFcBR, *bSeed = mFcBSeed;
    int *bLvl = mFcBLvl;
    for (int L = 0; L < field::kBubLevels; L++) mFcLevelUsed[L] = false;
    for (int i = 0; i < kVizMaxBubbles; i++)
    {
      const float br = mBubR[i];
      if (br <= 0.0f) continue;
      bX[nb] = mBubX[i]; bY[nb] = mBubY[i]; bR[nb] = br; bSeed[nb] = mBubSeed[i];
      int L = (int)(mBubZ[i] + 0.5f);
      if (L < 0) L = 0; else if (L >= field::kBubLevels) L = field::kBubLevels - 1;
      bLvl[nb] = L; mFcLevelUsed[L] = true; nb++;
    }
    // Drifting point layer (content-space, shared).
    const int NP = field::kNumPoints;
    float *ptX = mFcPtX, *ptY = mFcPtY;
    const float ptt = phase * field::kPointDriftRate;
    const float reacht = phase * field::kReachRate;
    for (int p = 0; p < NP; p++)
    {
      ptX[p] = field::hash01(p, 1) * field::kVizStripW + field::kPointDrift * anamnesis::noise::sample((float)p * 0.37f, ptt);
      ptY[p] = 6.0f + field::hash01(p, 2) * 52.0f + field::kPointDrift * anamnesis::noise::sample((float)p * 0.37f + 40.0f, ptt + 7.0f);
    }
    // Expand each bubble into a core bump + latched lobe sub-bumps.
    float *sbX = mFcSbX, *sbY = mFcSbY, *sbR = mFcSbR, *sbAmp = mFcSbAmp;
    int *sbLvl = mFcSbLvl;
    int nsb = 0;
    for (int b = 0; b < nb; b++)
    {
      if (nsb < kFcSBcap) { sbX[nsb] = bX[b]; sbY[nsb] = bY[b]; sbR[nsb] = bR[b] * field::kCoreK; sbAmp[nsb] = 1.0f; sbLvl[nsb] = bLvl[b]; nsb++; }
      const float rnz = anamnesis::noise::sample(bX[b] * field::kReachFreq, bY[b] * field::kReachFreq + reacht);
      float reach = (bR[b] * field::kLatchK + field::kLatchBase) * (1.0f + field::kReachVar * rnz);
      if (reach < field::kLatchBase) reach = field::kLatchBase;
      const float reach2 = reach * reach;
      int lobes = 0;
      for (int p = 0; p < NP && lobes < field::kMaxLobes && nsb < kFcSBcap; p++)
      {
        const float dx = ptX[p] - bX[b], dy = ptY[p] - bY[b];
        const float d2 = dx * dx + dy * dy;
        if (d2 >= reach2) continue;
        const float w0 = 1.0f - field::smooth01(reach * field::kLatchFull, reach, sqrtf(d2));
        float aff = 0.5f + 0.5f * anamnesis::noise::sample(bSeed[b] * 0.7f + (float)p * 0.13f, (float)p * 0.31f + 5.0f);
        aff = (aff - field::kAffBias) / (1.0f - field::kAffBias);
        if (aff <= 0.0f) continue;
        const float w = w0 * aff;
        if (w < 0.05f) continue;
        const float str = field::kPointStrMin + (1.0f - field::kPointStrMin) * field::hash01(p, 5);
        sbX[nsb] = ptX[p]; sbY[nsb] = ptY[p]; sbR[nsb] = field::kLobeR * (0.6f + 0.9f * str);
        sbAmp[nsb] = w; sbLvl[nsb] = bLvl[b]; nsb++; lobes++;
      }
    }
    // Strip-wide per-level metaball grid (content-space) with temporal slew.
    //
    // Restructured cells->all-bumps into bumps->SPLAT (Item 2 of
    // planning/anamnesis-viz-optimization.md): each Gaussian bump only touches
    // the cells within 4.5 sigma of its center (beyond that the term is
    // < 4.2e-5 of the bump amp -- two orders below anything a 4-bit pixel or
    // the 0.5 iso-threshold can see; quantified in the bench A/B). Per-cell
    // accumulation order is preserved (b ascending), so inside the boxes the
    // sums match the old loop.
    //
    // The 3-octave fbm is only evaluated where the bump field is nonzero:
    // f = bumps * (1 + gain*nz) is EXACTLY zero when bumps is zero, whatever
    // the noise -- so skipping fbm there does not change the field at all.
    // Zero-bump cells just decay (same slew law with f = 0) and SNAP to exact
    // zero below 1e-7, which the raster pass exploits (zero cells draw
    // nothing) and which is invisible (1e-7 against a 0.5 threshold and a
    // >= 0 bloom floor).
    const int C = field::kMetaCell;
    const float bMorph = phase * field::kMetaMorph;
    const int GW = field::kFieldGW, GH = field::kFieldGH;
    static float sBump[field::kFieldGW * field::kFieldGH]; // UI-thread scratch (.bss, not audio stack)
    for (int L = 0; L < field::kBubLevels; L++)
    {
      float *G = &mFcGrid[L * GW * GH];
      memset(sBump, 0, sizeof(sBump));
      for (int b = 0; b < nsb; b++)
      {
        if (sbLvl[b] != L) continue;
        float s = sbR[b] * field::kMetaSigmaK; if (s < 1.0f) s = 1.0f;
        const float amp = sbAmp[b] * field::kMetaBumpAmp;
        const float inv2s2 = 1.0f / (2.0f * s * s);
        const float cut = 4.5f * s;
        int i0 = (int)((sbX[b] - cut) / (float)C);     if (i0 < 0) i0 = 0;
        int i1 = (int)((sbX[b] + cut) / (float)C) + 1; if (i1 > GW - 1) i1 = GW - 1;
        int j0 = (int)((sbY[b] - cut) / (float)C);     if (j0 < 0) j0 = 0;
        int j1 = (int)((sbY[b] + cut) / (float)C) + 1; if (j1 > GH - 1) j1 = GH - 1;
        for (int j = j0; j <= j1; j++)
        {
          const float dy = (float)(j * C) - sbY[b];
          float *row = &sBump[j * GW];
          for (int i = i0; i <= i1; i++)
          {
            const float dx = (float)(i * C) - sbX[b];
            // fast exp (draw path; < 3.1e-4 relative on a field tested
            // against a 0.5 threshold -> sub-millicell contour shift)
            row[i] += amp * field::fastExpNeg(-(dx * dx + dy * dy) * inv2s2);
          }
        }
      }
      for (int j = 0; j < GH; j++)
      {
        const float cy = (float)(j * C);
        for (int i = 0; i < GW; i++)
        {
          const int idx = j * GW + i;
          const float bumps = sBump[idx];
          if (bumps != 0.0f)
          {
            const float cx = (float)(i * C);
            const float nz = anamnesis::noise::fbm(cx * field::kMetaNoiseFreq + (float)L * 3.1f, cy * field::kMetaNoiseFreq - bMorph);
            const float f = bumps * (1.0f + field::kMetaNoiseGain * nz);
            G[idx] += field::kMetaSlew * (f - G[idx]);
          }
          else
          {
            G[idx] += field::kMetaSlew * (0.0f - G[idx]);
            if (G[idx] > -1e-7f && G[idx] < 1e-7f) G[idx] = 0.0f;
          }
        }
      }
    }
  }

// ---- Round three: strip raster + per-ply blit --------------------------------
//
// The old drawImpl rasterized each ply's window independently (6x per frame),
// through virtual od::FrameBuffer calls per pixel. Now the WHOLE 258x64 pond
// is composited once per UI frame into Anamnesis::mStripRaster (direct byte
// writes + a written-pixel mask), and each ply's drawImpl blits its window.
// The raster is a faithful port of the per-ply renderer: per-column math,
// z-order compositing, per-ply prev-reset at each 43px boundary, and clip
// unions all reproduce the old per-window output bit-for-bit (verified by the
// framebuffer A/B harness). New beyond the port, all output-safe:
//  - band control grid computed once, strip-wide (was ~19% duplicated);
//  - marching-squares contour walked once (was 6 overlapping windows);
//  - per-drop INNER radius: a mature ring is a thin annulus (crest span +
//    4.5 sigma); points deep inside it get h < ~4e-5 -> bend < 1e-3 px, so
//    they skip the crest train entirely (A/B-proven invisible);
//  - per-CELL classification in the metaball fill: all-zero and
//    all-below-bloom cells skip (bilinear <= max corner, exact), cells with
//    min corner >= T + kEdgeSoft write solid 0 (cov >= 1, exact);
//  - the raster read in interior/bloom blending is a direct byte read
//    (was a virtual readPixel per pixel).

namespace
{
  struct StripR
  {
    uint8_t *buf;     // column-major [col*64 + y]
    uint64_t *mask;   // bit y of mask[col] = pixel painted this frame
    int c0, c1;       // active TIME SLICE: only columns [c0,c1) are rebuilt
  };
  static inline void srPix(StripR &S, int col, int y, int c)
  {
    if (col < S.c0 || col >= S.c1) return;   // outside this frame's slice
    S.buf[(col << 6) + y] = (uint8_t)c;
    S.mask[col] |= 1ull << y;
  }
  static inline int srRead(const StripR &S, int col, int y)
  {
    return S.buf[(col << 6) + y];
  }
  // Port of AnamFieldGraphic::drawLinePix into raster space (h = 64).
  static inline void srLinePix(StripR &S, int col, float y, float glow, float baseB, float &prevY)
  {
    float bf = baseB + glow * anamnesis::field::kGlowGain;
    if (bf < 0.0f) bf = 0.0f; else if (bf > 15.0f) bf = 15.0f;
    const int bri = (int)(bf + 0.5f);
    const int yi = (int)y;
    const float f = y - (float)yi;
    srPix(S, col, yi, (int)(bri * sqrtf(1.0f - f) + 0.5f));
    if (yi + 1 < 64) srPix(S, col, yi + 1, (int)(bri * sqrtf(f) + 0.5f));
    if (prevY > -999.0f)
    {
      const int a = (int)(prevY < y ? prevY : y);
      const int b = (int)(prevY < y ? y : prevY);
      for (int yy = a + 1; yy < b; yy++) srPix(S, col, yy, bri);
    }
    prevY = y;
  }
  // Port of aaPlot / drawAALineClip. Clip = the strip columns the old per-ply
  // windows covered (0..256; col 257 was never drawn) x rows 0..63.
  static inline void srAAPlot(StripR &S, int col, int y, float cov, int color)
  {
    if (cov <= 0.0f || col < 0 || col >= 257 || y < 0 || y >= 64) return;
    int c = (int)((float)color * cov + 0.5f);
    if (c > 15) c = 15;
    if (c > 0) srPix(S, col, y, c);
  }
  static inline void srAALine(StripR &S, float x0, float y0, float x1, float y1, int color)
  {
    float dx = x1 - x0, dy = y1 - y0;
    const bool steep = (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx);
    if (steep) { float t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    dx = x1 - x0; dy = y1 - y0;
    const float grad = (dx == 0.0f) ? 0.0f : dy / dx;
    const int ix0 = (int)(x0 + 0.5f), ix1 = (int)(x1 + 0.5f);
    float y = y0 + grad * ((float)ix0 - x0);
    for (int x = ix0; x <= ix1; x++)
    {
      int iy = (int)y; if ((float)iy > y) iy--; // exact floor, no libm call
      const float fy = y - (float)iy;
      if (steep)
      {
        srAAPlot(S, iy, x, 1.0f - fy, color);
        srAAPlot(S, iy + 1, x, fy, color);
      }
      else
      {
        srAAPlot(S, x, iy, 1.0f - fy, color);
        srAAPlot(S, x, iy + 1, fy, color);
      }
      y += grad;
    }
  }
} // anonymous namespace

void Anamnesis::buildStripRaster()
{
    StripR S;
    S.buf = mStripRaster;
    S.mask = mStripMask;
    // TIME SLICE (feedback_viz_encoder_capture_architectural, move 3). Rebuild
    // only 1/kStripSlices of the columns per frame; the rest KEEP last frame's
    // pixels, so no single draw() runs the whole recompute and the UI thread
    // gets back to its event pump. Total work per frame drops by the same
    // factor. Clears must be slice-scoped or the persisted columns are lost.
    const int sw = (kStripW + kStripSlices - 1) / kStripSlices;
    S.c0 = mStripSlice * sw;
    S.c1 = S.c0 + sw; if (S.c1 > kStripW) S.c1 = kStripW;
    if (S.c0 >= kStripW) { S.c0 = 0; S.c1 = sw; }
    mStripSlice = (mStripSlice + 1) % kStripSlices;
    memset(&mStripRaster[S.c0 << 6], 0, (size_t)(S.c1 - S.c0) << 6);
    memset(&mStripMask[S.c0], 0, (size_t)(S.c1 - S.c0) * sizeof(uint64_t));

    const int h = 64;
    const int n = field::kStreamN;
    const float phase = vizPhase();
    const float mixN = vizMix();
    const float baseB = field::kBaseDim + (field::kBaseBright - field::kBaseDim) * mixN;
    const float size = vizSize();
    const float diffuse = vizDiffusion();
    const float vmod = vizMod();
    const float rtau = field::rippleTauOf(vizDecay());

    ensureFieldFrame(); // metaball grid (still keyed on vizPhase, as shipped)

    // Cache the active rain droplets once, strip-wide (was per ply). Also
    // precompute each drop's INNER skip radius: the innermost active crest of
    // either train, minus 4.5 sigma -- inside that disc the crest train sums
    // to < ~4e-5, i.e. bend < 1e-3 px / glow < 1e-3 gray. During the impact
    // transient (age < kImpactT) the crater sits at r=0, so no inner skip.
    int nd = 0;
    float *dX = mSrDX, *dY = mSrDY, *dAge = mSrDAge;
    float *dC = mSrDC, *dAmp = mSrDAmp, *dRi2 = mSrDRi2;
    for (int i = 0; i < kVizMaxDrops; i++)
    {
      const float age = mDropAge[i];
      if (age < 0.0f) continue;
      dX[nd] = mDropX[i];
      dY[nd] = mDropY[i];
      dAge[nd] = age;
      dC[nd] = mDropSpeed[i];
      dAmp[nd] = mDropAmp[i];
      float rin = 0.0f;
      if (age >= field::kImpactT)
      {
        // innermost crest radius of a train = R - lam0 * sum(1 + spread*i)
        // over its (nc-1) inter-crest gaps -- mirrors crestTrainT's loop.
        auto innermost = [](float R, float age2, float lam0) {
          int nc = 2 + (int)(age2 * field::kRippleFan);
          if (nc > field::kRippleMaxCrests) nc = field::kRippleMaxCrests;
          float rc = R;
          for (int k = 0; k < nc - 1; k++) rc -= lam0 * (1.0f + field::kRippleSpread * (float)k);
          return rc;
        };
        rin = innermost(dC[nd] * age, age, field::kRippleLambda);
        if (age > field::kSecDelay)
        {
          const float a2 = age - field::kSecDelay;
          const float rin2 = innermost(dC[nd] * a2, a2, field::kRippleLambda * field::kSecLam);
          if (rin2 < rin) rin = rin2;
        }
        rin -= 4.5f * field::kRippleSigma;
        if (rin < 0.0f) rin = 0.0f;
      }
      dRi2[nd] = rin * rin;
      nd++;
    }

    // ---- band control grid, computed ONCE for the whole strip ----
    // Global grid indices g in [-1 .. 66] (68 points): the old per-ply windows
    // used g0 = x0/cstep - 1 .. (x0+w)/cstep + 2, whose union over the six
    // plies is exactly this range, with identical per-point values.
    const int cstep = field::kCtrlStep;
    const int gFirst = -1;
    const int gCount = (kStripW - 2) / cstep + 4; // 68: covers cols 0..256 (+margins)

    auto renderBand = [&](int b)
    {
      const int s0 = 2 * b, s1 = 2 * b + 1;
      const float yb0 = ((float)s0 + 0.5f) * (float)h / (float)n;
      const float yb1 = ((float)s1 + 0.5f) * (float)h / (float)n;
      float *cY0 = mSrCY0, *cB0 = mSrCB0, *cY1 = mSrCY1, *cB1 = mSrCB1;
      for (int i = 0; i < gCount; i++)
      {
        const float cx = (float)((gFirst + i) * cstep);
        float y0 = yb0 + field::flowFast(cx, yb0, phase, size, vmod);
        float y1 = yb1 + field::flowFast(cx, yb1, phase, size, vmod);
        float gl0 = 0.0f, gl1 = 0.0f;
        for (int d = 0; d < nd; d++)
        {
          const float ddx = cx - dX[d];
          const float ddy0 = y0 - dY[d];
          if (ddx * ddx + ddy0 * ddy0 > dRi2[d])
          {
            const field::RippleHit a0 = field::rippleEvalFast(ddx, ddy0, dAge[d], dC[d], rtau);
            y0 += dAmp[d] * a0.bend; gl0 += dAmp[d] * a0.glow;
          }
          const float ddy1 = y1 - dY[d];
          if (ddx * ddx + ddy1 * ddy1 > dRi2[d])
          {
            const field::RippleHit a1 = field::rippleEvalFast(ddx, ddy1, dAge[d], dC[d], rtau);
            y1 += dAmp[d] * a1.bend; gl1 += dAmp[d] * a1.glow;
          }
        }
        cY0[i] = y0; cB0[i] = gl0; cY1[i] = y1; cB1[i] = gl1;
      }
      float prev0 = -1000.0f, prev1 = -1000.0f;
      for (int cx = S.c0; cx < kStripW - 1 && cx < S.c1; cx++)  // slice only
      {
        // per-ply gap-fill reset: the old renderer reset prevY at each ply's
        // first column, so steep jumps never gap-fill across a 43px seam.
        if (cx % field::kStride == 0) { prev0 = -1000.0f; prev1 = -1000.0f; }
        const int seg = cx / cstep;
        const int idx = seg - gFirst;
        const float t = (float)(cx - seg * cstep) / (float)cstep;
        float y0 = field::catmull(cY0[idx - 1], cY0[idx], cY0[idx + 1], cY0[idx + 2], t);
        float y1 = field::catmull(cY1[idx - 1], cY1[idx], cY1[idx + 1], cY1[idx + 2], t);
        float gl0 = field::catmull(cB0[idx - 1], cB0[idx], cB0[idx + 1], cB0[idx + 2], t);
        float gl1 = field::catmull(cB1[idx - 1], cB1[idx], cB1[idx + 1], cB1[idx + 2], t);
        if (y0 < 0.0f) y0 = 0.0f; else if (y0 > (float)(h - 1)) y0 = (float)(h - 1);
        if (y1 < 0.0f) y1 = 0.0f; else if (y1 > (float)(h - 1)) y1 = (float)(h - 1);
        const int flo = (int)(y0 < y1 ? y0 : y1) + 1;
        const int fhi = (int)(y0 < y1 ? y1 : y0);
        for (int yy = flo; yy < fhi; yy++) srPix(S, cx, yy, 0); // occlusion fill
        srLinePix(S, cx, y0, gl0, baseB, prev0);
        srLinePix(S, cx, y1, gl1, baseB, prev1);
      }
    };

    int bubB = (int)(baseB + 2.5f);
    if (bubB > 15) bubB = 15;

    static const int kSeg[16][4] = {
        {-1, -1, -1, -1}, // 0
        {0, 3, -1, -1},   // 1  TL
        {0, 1, -1, -1},   // 2  TR
        {1, 3, -1, -1},   // 3  TL+TR
        {2, 3, -1, -1},   // 4  BL
        {0, 2, -1, -1},   // 5  TL+BL
        {0, 1, 2, 3},     // 6  TR+BL (saddle)
        {1, 2, -1, -1},   // 7  TL+TR+BL
        {1, 2, -1, -1},   // 8  BR
        {0, 3, 1, 2},     // 9  TL+BR (saddle)
        {0, 2, -1, -1},   // 10 TR+BR
        {2, 3, -1, -1},   // 11 TL+TR+BR
        {1, 3, -1, -1},   // 12 BL+BR
        {0, 1, -1, -1},   // 13 TL+BL+BR
        {0, 3, -1, -1},   // 14 TR+BL+BR
        {-1, -1, -1, -1}};// 15
    const int C = field::kMetaCell;
    const int GW = field::kFieldGW, GH = field::kFieldGH;
    const float T = field::kMetaThresh;
    const float bloomBand = field::kBloomBandMax * powf(diffuse, field::kBloomExp);
    const bool  bloomOn   = bloomBand > 0.0001f;
    const float bloomLo   = T - bloomBand;
    const float bloomPeak = (float)bubB * field::kBloomGain;
    const float invEdge   = 1.0f / field::kEdgeSoft;
    // A cell can only light a pixel if its bilinear max (= max corner) clears
    // the lowest lighting threshold; solid-interior cells (min corner past the
    // AA knee) write hard 0 without any per-pixel math. Both tests are exact.
    const float liteLo    = bloomOn ? bloomLo : T;
    const float solidHi   = T + field::kEdgeSoft;

    auto renderBubbleLevel = [&](int L)
    {
      const float *G = &mFcGrid[L * GW * GH];
      // FILL, walked per grid CELL (corners loaded once per cell run).
      for (int gi = S.c0 / C; gi * C < kStripW - 1 && gi < GW - 1; gi++)
      {
        int colA = gi * C;
        if (colA >= S.c1) break;                                        // past the slice
        int colB = colA + C; if (colB > kStripW - 1) colB = kStripW - 1; // cols < 257
        for (int gj = 0; gj < GH - 1; gj++)
        {
          const float v00 = G[gj * GW + gi], v10 = G[gj * GW + gi + 1];
          const float v01 = G[(gj + 1) * GW + gi], v11 = G[(gj + 1) * GW + gi + 1];
          float vmax = v00; if (v10 > vmax) vmax = v10; if (v01 > vmax) vmax = v01; if (v11 > vmax) vmax = v11;
          if (!(vmax > liteLo)) continue;   // nothing in this cell can light (exact)
          float vmin = v00; if (v10 < vmin) vmin = v10; if (v01 < vmin) vmin = v01; if (v11 < vmin) vmin = v11;
          int pyA = gj * C;
          int pyB = pyA + C; if (pyB > h) pyB = h;
          if (vmin >= solidHi)
          {
            // deep interior: cov >= 1 for every pixel -> hard 0 (exact)
            for (int col = colA; col < colB; col++)
              for (int py = pyA; py < pyB; py++) srPix(S, col, py, 0);
            continue;
          }
          for (int col = colA; col < colB; col++)
          {
            const float gxf = (float)col / (float)C;   // same form as the old per-pixel loop
            const float fx = gxf - (float)gi;
            for (int py = pyA; py < pyB; py++)
            {
              const float gyf = (float)py / (float)C;
              const float fy = gyf - (float)gj;
              const float v = (v00 * (1.0f - fx) + v10 * fx) * (1.0f - fy) + (v01 * (1.0f - fx) + v11 * fx) * fy;
              if (v > T)
              {
                float cov = (v - T) * invEdge; if (cov > 1.0f) cov = 1.0f;
                const int cur = srRead(S, col, py);
                srPix(S, col, py, (int)((float)cur * (1.0f - cov) + 0.5f));
              }
              else if (bloomOn && v > bloomLo)
              {
                float ign = 0.06711056f * (float)col + 0.00583715f * (float)py;
                ign -= (float)(int)ign; ign = 52.9829189f * ign; ign -= (float)(int)ign;
                const int bv = (int)(bloomPeak * (v - bloomLo) / (T - bloomLo) + ign);
                if (bv > 0 && bv > srRead(S, col, py)) srPix(S, col, py, bv);
              }
            }
          }
        }
      }
      // Marching-squares contour, walked ONCE over the global cells (the old
      // per-ply windows re-walked overlapping margin cells with identical
      // results; the union equals this single pass).
      for (int j = 0; j < GH - 1; j++)
        for (int i = 0; i < GW - 1; i++)
        {
          const float v00 = G[j * GW + i], v10 = G[j * GW + i + 1];
          const float v01 = G[(j + 1) * GW + i], v11 = G[(j + 1) * GW + i + 1];
          int cfg = 0;
          if (v00 > T) cfg |= 1;
          if (v10 > T) cfg |= 2;
          if (v01 > T) cfg |= 4;
          if (v11 > T) cfg |= 8;
          if (cfg == 0 || cfg == 15) continue;
          const int *sg = kSeg[cfg];
          float ex[4], ey[4];
          if (sg[0] == 0 || sg[1] == 0 || sg[2] == 0 || sg[3] == 0) { float t = (T - v00) / (v10 - v00); ex[0] = (float)i + t; ey[0] = (float)j; }
          if (sg[0] == 1 || sg[1] == 1 || sg[2] == 1 || sg[3] == 1) { float t = (T - v10) / (v11 - v10); ex[1] = (float)(i + 1); ey[1] = (float)j + t; }
          if (sg[0] == 2 || sg[1] == 2 || sg[2] == 2 || sg[3] == 2) { float t = (T - v01) / (v11 - v01); ex[2] = (float)i + t; ey[2] = (float)(j + 1); }
          if (sg[0] == 3 || sg[1] == 3 || sg[2] == 3 || sg[3] == 3) { float t = (T - v00) / (v01 - v00); ex[3] = (float)i; ey[3] = (float)j + t; }
          for (int e = 0; e < 4; e += 2)
          {
            if (sg[e] < 0 || sg[e + 1] < 0) continue;
            srAALine(S, ex[sg[e]] * (float)C, ey[sg[e]] * (float)C,
                     ex[sg[e + 1]] * (float)C, ey[sg[e + 1]] * (float)C, bubB);
          }
        }
    };

    // Z-ORDER COMPOSITE, once for the strip (identical order to the old
    // per-ply sorts -- the z inputs are global).
    const int nBands = n / 2;
    struct ZItem { float z; int type; int idx; };
    ZItem items[field::kStreamN / 2 + field::kBubLevels];
    int ni = 0;
    for (int b = 0; b < nBands; b++) { items[ni].z = (float)mBandZ[b]; items[ni].type = 0; items[ni].idx = b; ni++; }
    for (int L = 0; L < field::kBubLevels; L++)
      if (mFcLevelUsed[L]) { items[ni].z = ((float)L + 0.5f) * (float)nBands / (float)field::kBubLevels; items[ni].type = 1; items[ni].idx = L; ni++; }
    for (int a = 1; a < ni; a++) { ZItem key = items[a]; int j = a - 1; while (j >= 0 && items[j].z > key.z) { items[j + 1] = items[j]; j--; } items[j + 1] = key; }
    for (int it = 0; it < ni; it++)
    {
      if (items[it].type == 0) renderBand(items[it].idx);
      else renderBubbleLevel(items[it].idx);
    }
}

void AnamFieldGraphic::drawImpl(od::FrameBuffer &fb)
{
    if (!mpOp)
      return;
    const int w = mWidth;
    const int left = mWorldLeft;
    const int bot = mWorldBottom;
    const int x0 = mIndex * field::kStride;
    const int wext = (mIndex < mCount - 1) ? 1 : 0;

    mpOp->vizPing();                 // keep the DSP-side viz sim alive (idles offscreen)
    mpOp->ensureStripFrame(mIndex);  // first ply of the frame composites the strip

    // Masked blit: only the pixels the raster actually painted are written,
    // so framework content beneath unpainted pixels is left alone -- exactly
    // the old per-ply renderer's write pattern. Two 32-bit halves per column
    // (no 64-bit ctz libcall on armv7).
    for (int lx = 0; lx < w + wext; lx++)
    {
      const int col = x0 + lx;
      const uint64_t m = mpOp->stripMask(col);
      if (!m) continue;
      const uint8_t *cp = mpOp->stripCol(col);
      const int px = left + lx;
      uint32_t lo = (uint32_t)m;
      while (lo) { const int y = __builtin_ctz(lo); lo &= lo - 1; fb.pixel(cp[y], px, bot + y); }
      uint32_t hi = (uint32_t)(m >> 32);
      while (hi) { const int y = 32 + __builtin_ctz(hi); hi &= hi - 1; fb.pixel(cp[y], px, bot + y); }
    }

    // Impact splashes (front-most): rain flecks for drops in this ply's window.
    drawLooper(fb, left, bot, w, x0);
  }

} // namespace anamnesis
