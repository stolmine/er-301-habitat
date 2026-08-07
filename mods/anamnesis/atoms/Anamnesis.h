// anamnesis::Anamnesis
//
// Spatial-glitch instrument (CM4-only): a short-buffer micro-looper fused
// with a continuously-morphing spatial field, cross-fed and Spiral-governed.
// Internal-stereo (one object, shared coherent L/R field).
//
// Phase 5.1 (0.2.0.21): UI consolidation step 1 -- config MENU. Mode, ClockMode,
//   Sense (3-level Low/Med/High), Grit (3-choice Clean/Normal/Broken) become unit-
//   menu Options (Sense+Grit discretized from knobs). Trig removed -- re-trigger
//   is now Env-implicit only. Surface drops from 19 to 14 flat plies.
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
#include "AnamField.h"   // shared all-over-viz field + ripple constants

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

  // Offscreen-viz gate (the Fabula pattern, [hab:viz-offscreen-gate-all]):
  // every AnamFieldGraphic draw() pings the op; process() counts the heartbeat
  // down at BLOCK rate and skips the DSP-side viz bookkeeping (flow-phase
  // advance, rain aging/spawn, bubble physics) when it expires. Those only
  // ever feed the graphic -- audio is untouched either way. ~0.68 s grace.
  static const int kVizHeartbeatBlocks = 256;

  // Rain-on-pond ripple pool: one droplet spawns per loop cycle (read wrap),
  // drips into the all-over field, bends every ply's flow lines (07-allover-viz.md).
  static const int kVizMaxDrops = 16;
  // Bubbles (Density): outlined shapes floating up through the field, woven in
  // front/behind streamline bands by z (band z randomized per unit insertion).
  static const int kVizMaxBubbles = 12;

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
      // Pre-bake the Perlin LUT on the (32 KB) app stack at insert. Without
      // this the first noise::sample() call comes from process() on the
      // 2048-byte audio task stack, and the bake used to overflow it -- the
      // [hab:anamnesis-insert-crash] data-abort. See AnamNoise.h lut().
      anamnesis::noise::bake();

      addInput(mInL);
      addInput(mInR);
      addInput(mFreeze);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mLength);
      addParameter(mSpeed);
      addParameter(mSize);
      addParameter(mDecay);
      addParameter(mDiffusion);
      addParameter(mDensity);
      addParameter(mMod);
      addParameter(mRegen);
      addParameter(mClock);
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
      addOption(mSense);
      mSense.set(2);                // 1=Low 2=Med 3=High (Env sensitivity)
      mSense.enableSerialization();
      addOption(mGrit);
      mGrit.set(2);                 // 1=Clean(0) 2=Normal(0.5) 3=Broken(1)
      mGrit.enableSerialization();

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
      for (int i = 0; i < kVizMaxDrops; i++) { mDropAge[i] = -1.0f; mDropAmp[i] = 1.0f; }
      mDropPrevRead = 0.0f;
      // Randomize the band z-levels on insertion (Fisher-Yates) -> each unit
      // instance weaves bubbles through its bands in its own depth order.
      const int nb = anamnesis::field::kStreamN / 2;
      for (int i = 0; i < nb; i++) mBandZ[i] = i;
      for (int i = nb - 1; i > 0; i--)
      {
        mBubRng = mBubRng * 1664525u + 1013904223u;
        int j = (int)((mBubRng >> 8) % (unsigned)(i + 1));
        int t = mBandZ[i]; mBandZ[i] = mBandZ[j]; mBandZ[j] = t;
      }
      for (int i = 0; i < kVizMaxBubbles; i++) { mBubR[i] = 0.0f; mBubVx[i] = 0.0f; mBubVy[i] = 0.0f; }
      mBubSpawnT = 0.0f;

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
        mTapBaseF[i] = (float)kTapBase[i];
        mTapIncQ[i] = 0.0f; mTapS0Q[i] = 0.0f; mTapS1Q[i] = 0.0f;
        mTapFracQ[i] = 0.0f; mTapIdxQ[i] = 0;
      }
      for (int i = 0; i < kFdnN; i++)
      {
        mFdnLfoPhase[i] = (float)i * 0.61f;
        mFdnLfoHz[i] = 0.50f + 0.13f * (float)i;
        mFdnBaseF[i] = (float)kFdnBase[i];
        mFdnIncQ[i] = 0.0f; mFdnGQ[i] = 0.0f;
        mFdnS0Q[i] = 0.0f; mFdnS1Q[i] = 0.0f;
        mFdnFracQ[i] = 0.0f; mFdnRQ[i] = 0.0f; mFdnIdxQ[i] = 0;
      }
    }

    virtual ~Anamnesis() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Inlet     mFreeze{"Freeze"};        // gate/toggle: hold the loop
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};

    od::Parameter mLength{"Length", 0.4f};
    od::Parameter mSpeed{"Speed", 1.0f};    // bipolar -2..2x rate; 1.0 = +1x
    od::Parameter mSize{"Size", 0.5f};
    od::Parameter mDecay{"Decay", 0.5f};
    od::Parameter mDiffusion{"Diffusion", 0.6f};
    od::Parameter mDensity{"Density", 0.5f};
    od::Parameter mMod{"Mod", 0.3f};
    od::Parameter mRegen{"Regen", 0.0f};    // cross-feedback: field wet -> looper input
    od::Parameter mClock{"Clock", 1.0f};    // 1 = full rate; down = slower/lower/grittier
    od::Parameter mMix{"Mix", 0.4f};
    // Router
    od::Parameter mSource{"Source", 1.0f};      // field source: 0 input .. 1 loop
    od::Parameter mDirectLoop{"DirectLoop", 0.0f}; // clean loop blended to output
    od::Parameter mSpread{"Spread", 0.5f};      // width: 0 mono .. 0.5 normal .. 1 wide
    od::Option    mMode{"Mode"};            // 1=Tape, 2=Stretch, 3=Env
    od::Option    mClockMode{"ClockMode"};  // 1=Steps, 2=Smooth
    od::Option    mSense{"Sense"};          // 1=Low, 2=Med, 3=High (Env sensitivity)
    od::Option    mGrit{"Grit"};            // 1=Clean(0), 2=Normal(0.5), 3=Broken(1)

    // ---- viz getters: the "Pond of Recollection" all-over field reads these
    // (C++-only; AnamFieldGraphic holds an Anamnesis* and calls them directly --
    // not SWIG-wrapped, like the rest of this guarded block). All derived from the
    // existing smoothed state; the lone new member is mVizPhase (advanced/block).
    // planning/spatial-glitch-impl/07-allover-viz.md
    float vizPhase()       { return mVizPhase; }
    float vizPlayhead()    { return mLoopLenZ > 1.0f ? mLoopReadPos / mLoopLenZ : 0.0f; }
    float vizStretchHead() { return mLoopLenZ > 1.0f ? mStretchHead / mLoopLenZ : 0.0f; }
    float vizFreeze()      { return 1.0f - (1.0f - mFreezeZ) * (1.0f - mCaptureHoldZ); }
    float vizClockR()      { return mRcurZ; }
    float vizSize()        { float s = (mSizeScaleZ - kSizeMin) / (kSizeMax - kSizeMin);
                             return s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s); }
    float vizDensity()     { return mDensityZ; }
    float vizMod()         { return mModZ; } // Mod -> slow organic flow wander
    float vizDecay()       { float d = mDecay.value(); // Decay -> ripple persistence
                             return d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d); }
    // Diffusion -> line glow/halo. mDiffGZ is the smoothed allpass gain (= knob *
    // 0.75); recover the smoothed 0..1 knob for the viz.
    float vizDiffusion()   { float d = mDiffGZ * (1.0f / 0.75f);
                             return d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d); }
    float vizEnv()         { return mEnvFast; }
    int   vizMode()        { return mMode.value(); }
    float vizGrit()        { int v = mGrit.value(); return v <= 1 ? 0.0f : (v == 2 ? 0.5f : 1.0f); }
    float vizLoopLen()     { return mLoopLenZ; }
    float vizBuffer(int i) { if (i < 0) i = 0; else if (i >= kLoopBufLen) i = kLoopBufLen - 1;
                             return mLoopBuf[i]; }
    // Rain-on-pond droplets (age < 0 = inactive). Read by every ply's graphic to
    // bend the flow lines (the pond is shared, so a drop bends across ply seams).
    int   vizMaxDrops()       { return kVizMaxDrops; }
    float vizDropX(int i)     { return mDropX[i]; }
    float vizDropY(int i)     { return mDropY[i]; }
    float vizDropAge(int i)   { return mDropAge[i]; }
    float vizDropSpeed(int i) { return mDropSpeed[i]; }
    float vizDropPhase(int i) { return mDropPhase[i]; }
    float vizDropAmp(int i)   { return mDropAmp[i]; }
    float vizMix()            { return mMix.value(); }
    int   vizBandZ(int b)     { return mBandZ[b]; }
    int   vizMaxBubbles()     { return kVizMaxBubbles; }
    float vizBubX(int i)      { return mBubX[i]; }
    float vizBubY(int i)      { return mBubY[i]; }
    float vizBubZ(int i)      { return mBubZ[i]; }
    float vizBubR(int i)      { return mBubR[i]; }   // <= 0 = inactive
    float vizBubSeed(int i)   { return mBubSeed[i]; }

    // ---- Item 1: shared per-frame all-over-field cache -----------------------
    // The metaball field is content-space + pond-wide, but was rebuilt per-ply
    // (~6x redundant). Build it ONCE per draw frame here (keyed on mVizPhase --
    // all plies in a frame see the same phase, so the first ply builds and the
    // rest reuse) into a strip-wide grid; each AnamFieldGraphic samples its
    // window. planning/anamnesis-viz-optimization.md
    void ensureFieldFrame() { if (mVizPhase != mFcLastPhase) { mFcLastPhase = mVizPhase; buildFieldFrame(); } }
    // Called by every ply graphic on draw(): keeps the DSP-side viz sim alive.
    // When no ply is on screen the pings stop and process() skips the sim
    // (block-constant gate, never a per-sample runtime tier).
    void vizPing() { mVizHeartbeat = kVizHeartbeatBlocks; }

    // ---- Round three: shared strip RASTER ------------------------------------
    // The whole 258x64 pond is rasterized ONCE per UI frame into a member byte
    // plane (bands + metaball levels + contours composited in z, all direct
    // byte writes -- no virtual od::FrameBuffer calls, no per-ply redundancy);
    // each ply's draw() just BLITS its window. mStripMask records which pixels
    // the raster actually painted, so the blit reproduces today's exact write
    // pattern (unwritten pixels never touch the framebuffer -- framework
    // content beneath, e.g. section dividers in the 1px gaps, stays exactly as
    // it would with the old per-ply renderer).
    //
    // Frame detection without a frame hook: plies draw left-to-right within a
    // frame, so a draw whose ply index is <= the previous draw's index starts
    // a new frame. Works with any subset of plies visible (a single visible
    // ply rebuilds every frame, matching the old per-frame rendering).
    static const int kStripSlices = 4;  // columns rebuilt per frame = kStripW/4
    static const int kStripW = 258;   // field::kVizPlies * field::kStride
    // Frame start = this ply has ALREADY drawn since the last rebuild. That is
    // order-INDEPENDENT: the previous `plyIndex <= last` test rebuilt on every
    // draw if the framework walked plies in descending order (5,4,3.. each
    // satisfies <=), which would rebuild 6x per frame instead of once and cost
    // MORE than having no cache at all. A seen-mask cannot fail that way.
    void ensureStripFrame(int plyIndex)
    {
      const uint32_t bit = 1u << (plyIndex & 31);
      if (mStripSeen & bit) mStripSeen = 0;   // wrapped -> new frame
      if (mStripSeen == 0) buildStripRaster();
      mStripSeen |= bit;
    }
    const uint8_t *stripCol(int col) const { return &mStripRaster[col << 6]; }
    uint64_t stripMask(int col) const { return mStripMask[col]; }
    void buildStripRaster();   // out-of-line (Anamnesis.cpp)
    const float *vizFieldGrid(int L) { return &mFcGrid[L * field::kFieldGW * field::kFieldGH]; }
    bool vizLevelUsed(int L) { return mFcLevelUsed[L]; }

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

    // NOTE the i0 wrap guard: when rp = wr - d is a tiny negative, rp + len
    // can ROUND to exactly len, so (int)rp == len = one past the buffer
    // (feedback_multitap_idx_wrap_ulp -- guard idx0 like idx0+1). frac is 0
    // there, so the wrapped read returns the correct sample.
    inline float readLine(int i, float d)
    {
      float rp = (float)mWr - d;
      while (rp < 0.0f) rp += (float)kFdnBufLen;
      int i0 = (int)rp; float fr = rp - (float)i0;
      if (i0 >= kFdnBufLen) i0 -= kFdnBufLen;
      int i1 = i0 + 1; if (i1 >= kFdnBufLen) i1 -= kFdnBufLen;
      return mLine[i][i0] + (mLine[i][i1] - mLine[i][i0]) * fr;
    }

    inline float readTap(float d)
    {
      float rp = (float)mTapWr - d;
      while (rp < 0.0f) rp += (float)kTapBufLen;
      int i0 = (int)rp; float fr = rp - (float)i0;
      if (i0 >= kTapBufLen) i0 -= kTapBufLen;
      int i1 = i0 + 1; if (i1 >= kTapBufLen) i1 -= kTapBufLen;
      return mTapBuf[i0] + (mTapBuf[i1] - mTapBuf[i0]) * fr;
    }

    // Defined out-of-line in Anamnesis.cpp: the DSP hot path compiles at the
    // package speed profile (-O3 -ffast-math -fno-tree-vectorize), not the -Os
    // SWIG-wrapper profile this header otherwise lands in (single-TU package).
    // Out-of-line virtual process() is the established od::Object pattern
    // (DrumVoice.cpp); the graphic-virtual inline rule covers draw/notify only.
    virtual void process();

    static inline float clampf(float v, float lo, float hi)
    {
      return v < lo ? lo : (v > hi ? hi : v);
    }
    // Bit-based finiteness test. Semantically identical to the old
    // (v==v) && |v| <= 3.0e38f form for every input (NaN bit patterns exceed
    // the +Inf pattern, so the single unsigned compare rejects NaN AND Inf AND
    // the >3.0e38 band) -- but IMMUNE to -ffast-math, which folds v==v to true
    // and makes NaN ordered-compares unreliable. The hot path now compiles
    // with -ffast-math (Anamnesis.cpp), so the NaN firewalls must not rely on
    // float compares.
    static inline bool isfinitef(float v)
    {
      union { float f; uint32_t u; } b; b.f = v;
      union { float f; uint32_t u; } m; m.f = 3.0e38f;
      return (b.u & 0x7fffffffu) <= m.u;
    }
    // The poly sine kernels (polySinQ on [-pi/2,pi/2], polySin2Pi on
    // [0,2pi]) live in field:: (AnamField.h), shared with the draw-path fast
    // variants -- ONE source for the formulas. The LFO phase accumulators in
    // process() keep the shipped sinf trajectory bit-identical; only the
    // sine EVALUATION is poly (< 4e-6). A rotator recurrence was tried and
    // rejected: it tracks the true frequency more accurately than the float
    // phase accumulator, so the modulation trajectories drift apart
    // audibly-in-an-A/B over tens of seconds. Faithful > better.

    // cos(y) for y in [0, pi]: fold to [0, pi/2], even Taylor through x^10,
    // max |err| < 5e-7. Used for the loop-seam raised-cosine declick.
    static inline float polyCosPi(float y)
    {
      const float x = (y > 1.57079633f) ? (3.14159265f - y) : y;
      const float x2 = x * x;
      const float c = 1.0f + x2 * (-0.5f + x2 * (0.0416666667f
                        + x2 * (-0.00138888889f + x2 * (2.48015873e-5f
                        + x2 * -2.75573192e-7f))));
      return (y > 1.57079633f) ? -c : c;
    }
    // floor(v) exactly, branch-lite, for |v| < 2^31 -- floorf is a libm CALL
    // on am335x (no vrintm on VFPv3). Matches floorf bit-for-bit over the
    // crusher's range.
    static inline float floorf31(float v)
    {
      float fi = (float)(int)v;      // trunc toward zero
      if (fi > v) fi -= 1.0f;        // negative non-integers: down one
      return fi;
    }
    // Spiral governor (sin-clipper): unity at small x, soft-bounds to +/-1/d.
    // Same curve as before (was libm sinf); polySinQ error < 4e-6 absolute.
    static inline float spiralSat(float x, float d)
    {
      float a = (x < 0.0f ? -x : x) * d;
      if (a > 1.57079633f) a = 1.57079633f;   // pi/2
      float s = field::polySinQ(a) / d;
      return x < 0.0f ? -s : s;
    }

    // Spawn a rain droplet: free slot else oldest; random epicenter within the
    // Looper ply, with per-drop speed + phase jitter (avoids a mechanical look).
    void spawnDrop();   // out-of-line (Anamnesis.cpp)

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
    bool  mCaptureHold;
    float mCaptureHoldZ;
    int   mCaptureTimer;
    float mClockPhase, mWetL, mWetR, mWetPrevL, mWetPrevR;
    float mRecRateRef;   // clock R at which the buffered content was recorded
    float mRcurZ;        // current (possibly glided) clock R
    float mVizPhase = 0.0f;   // all-over flow-field animation phase (07-allover-viz.md)
    // Rain-on-pond ripple pool. age < 0 = inactive slot. Epicenters in content
    // pixels (x within the Looper ply's 0..42, y 0..63); age in seconds.
    float mDropX[kVizMaxDrops], mDropY[kVizMaxDrops], mDropAge[kVizMaxDrops];
    float mDropSpeed[kVizMaxDrops], mDropPhase[kVizMaxDrops];
    float mDropAmp[kVizMaxDrops];   // ripple size = loudness of that loop capture
    uint32_t mDropRng = 0x1234567u;
    float mDropPrevRead = 0.0f;   // read pos last block, for loop-wrap spawn
    // Bubble pool + the per-unit-instance random band-z permutation (depth weave).
    int   mBandZ[anamnesis::field::kStreamN / 2];
    float mBubX[kVizMaxBubbles], mBubY[kVizMaxBubbles], mBubZ[kVizMaxBubbles];
    float mBubR[kVizMaxBubbles], mBubVx[kVizMaxBubbles], mBubVy[kVizMaxBubbles]; // mBubR <= 0 = inactive
    float mBubSeed[kVizMaxBubbles];                        // stable per-bubble blob shape seed

    // ---- shared per-frame all-over-field cache (built by ensureFieldFrame) ----
    static const int kFcSBcap = kVizMaxBubbles * (1 + field::kMaxLobes); // 96
    float mFcLastPhase = -1e30f;                                         // frame guard
    bool  mFcLevelUsed[field::kBubLevels] = {};
    float mFcGrid[field::kBubLevels * field::kFieldGW * field::kFieldGH] = {}; // strip-wide, slewed
    // Build the strip-wide per-level metaball field once per frame: drift points
    // + sub-bump cluster expansion + Gaussian/FBM grid with temporal slew. Moved
    // verbatim off the per-ply draw() (was recomputed ~6x/frame).
    void buildFieldFrame();   // out-of-line (Anamnesis.cpp)
    uint32_t mBubRng = 0x9e3779b9u;
    float mBubSpawnT = 0.0f;
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
    // NEON SoA state/scratch for the per-step tap + FDN gather (Pecto's
    // 3-pass compute/gather/combine pattern, feedback_neon_delay_gather).
    // CLASS MEMBERS, never stack locals (feedback_neon_intrinsics_drumvoice).
    // Deliberately NO aligned(16) claims: plain new is only 8-aligned on
    // am335x, and an unprovable claim invites :64/:128-hinted NEON loads
    // (the A8 alignment trap); unhinted vld1 costs ~a cycle and cannot trap.
    // kTapN=12 -> 3 quads, kFdnN=8 -> 2 quads. Also used (as plain arrays)
    // by the scalar fallback so both paths share the block-rate fills.
    float   mTapBaseF[kTapN];   // kTapBase as float (ctor)
    float   mTapIncQ[kTapN];    // per-block LFO phase increments
    float   mTapS0Q[kTapN], mTapS1Q[kTapN], mTapFracQ[kTapN];
    int32_t mTapIdxQ[kTapN];
    float   mFdnBaseF[kFdnN];
    float   mFdnIncQ[kFdnN], mFdnGQ[kFdnN];
    float   mFdnS0Q[kFdnN], mFdnS1Q[kFdnN], mFdnFracQ[kFdnN], mFdnRQ[kFdnN];
    int32_t mFdnIdxQ[kFdnN];
    float mSizeScaleZ, mT60Z, mDiffGZ, mDensityZ, mModZ;
    int   mVizHeartbeat = 0;   // blocks the viz sim stays live after the last graphic draw
    // Strip raster plane (column-major, [col*64 + y]) + written-pixel mask.
    // CLASS MEMBERS (UI-thread scratch lives on the heap object, never the
    // stack). ~18.5 KB.
    // DRAW-PATH SCRATCH, on the heap with the object rather than the stack.
    // A redraw can run on the `busy` task (4096-byte stack) while the Busy
    // spinner is up during insert/delete -- NOT the 32768-byte app stack. The
    // buildStripRaster -> ensureFieldFrame -> buildFieldFrame chain needed
    // 1804 + 2484 = 4288 bytes of frame and blew it, crashing on delete.
    float    mFcBX[kVizMaxBubbles], mFcBY[kVizMaxBubbles];
    float    mFcBR[kVizMaxBubbles], mFcBSeed[kVizMaxBubbles];
    int      mFcBLvl[kVizMaxBubbles];
    float    mFcPtX[field::kNumPoints], mFcPtY[field::kNumPoints];
    float    mFcSbX[kFcSBcap], mFcSbY[kFcSBcap], mFcSbR[kFcSBcap], mFcSbAmp[kFcSBcap];
    int      mFcSbLvl[kFcSBcap];
    float    mSrDX[kVizMaxDrops], mSrDY[kVizMaxDrops], mSrDAge[kVizMaxDrops];
    float    mSrDC[kVizMaxDrops], mSrDAmp[kVizMaxDrops], mSrDRi2[kVizMaxDrops];
    float    mSrCY0[68], mSrCB0[68], mSrCY1[68], mSrCB1[68];
    int      mStripSlice = 0;
    uint8_t  mStripRaster[kStripW * 64];
    uint64_t mStripMask[kStripW];
    uint32_t mStripSeen = 0;           // plies drawn since the last rebuild
    bool  mInit = false;
    bool  mFzSet = false;
#endif
  };

} // namespace anamnesis
