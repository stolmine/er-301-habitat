// Mirror — aliasing-paradigm complex osc. See Mirror.h for the
// architectural overview; planning/mirror-unit-design.md for the full
// design rationale.

#include "Mirror.h"
#include "util/neon_math.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static const float kTwoPi = 6.28318530718f;
  static const float kPi = 3.14159265359f;

  // ---------------------------------------------------------------
  // Knob mappings (Sync Threshold cubic-around-locks, Mirror exponential)
  // ---------------------------------------------------------------

  // Sync Threshold knob (0..1) -> carrier/mod lock ratio.
  // Anchors at Fibonacci ratios: 1, 2, 3, 5, 8, 13 at knob positions
  // 0.0, 0.2, 0.4, 0.6, 0.8, 1.0 respectively.
  //
  // The high upper end (13x) is the key to producing dramatic alias
  // content at LOW F0: the carrier oscillator runs at F_mod x ratio,
  // so at F_mod = 110 Hz the carrier hits 1430 Hz, generating
  // harmonics that cross Nyquist boundaries regardless of perceived
  // pitch.
  //
  // Within each segment, a cubic squish makes lock zones near the
  // anchors sticky (d(ratio)/d(knob) -> 0 at anchors) and chaos
  // transitions near the segment midpoint smooth (max derivative at
  // midpoint). Function: f(x) = sign(x) * (1 - (1 - |x|)^3) for
  // local x in [-1, 1].
  static inline float lockRatioFromKnob(float k)
  {
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    int seg;
    if (k >= 0.8f)      seg = 4;
    else if (k >= 0.6f) seg = 3;
    else if (k >= 0.4f) seg = 2;
    else if (k >= 0.2f) seg = 1;
    else                seg = 0;

    static const float kK[6] = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
    static const float kR[6] = {1.0f, 2.0f, 3.0f, 5.0f, 8.0f, 13.0f};

    float k0 = kK[seg],  k1 = kK[seg + 1];
    float r0 = kR[seg],  r1 = kR[seg + 1];
    float midK = (k0 + k1) * 0.5f;
    float halfW = (k1 - k0) * 0.5f;
    float local = (k - midK) / halfW;  // in [-1, 1]

    float absL = (local >= 0.0f) ? local : -local;
    float oneMinus = 1.0f - absL;
    float squished = 1.0f - oneMinus * oneMinus * oneMinus;
    if (local < 0.0f) squished = -squished;

    float midR = (r0 + r1) * 0.5f;
    float halfR = (r1 - r0) * 0.5f;
    return midR + halfR * squished;
  }

  // Stereo phase offset (Δφ) derived from the Sync Threshold knob.
  // At Fibonacci lock anchors (knob = 0.0, 0.2, 0.4, 0.6, 0.8, 1.0)
  // Δφ = 0 → L = R, mono center image. At chaos midpoints (0.1, 0.3,
  // 0.5, 0.7, 0.9) Δφ = 0.25 envelope cycles (= π/2 in carrier
  // terms), the maximum stereo width. Smoothstep shape gives sticky
  // anchor zones (slope = 0 at lock) and smooth chaos transitions.
  // Result: stereo width is the chaos axis — width audibly collapses
  // back to mono as the knob finds the next lock.
  static inline float stereoOffsetFromSyncKnob(float k)
  {
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;

    // Nearest anchor at multiples of 0.2.
    int seg = (int)(k * 5.0f + 0.5f);
    if (seg > 5) seg = 5;
    if (seg < 0) seg = 0;
    float anchorK = (float)seg * 0.2f;

    // Distance from nearest anchor, normalized so midpoint = 1.
    float dist = (k > anchorK) ? (k - anchorK) : (anchorK - k);
    float t = dist * 10.0f;  // 0.1 segment half-width × 10 → t in [0, 1]
    if (t > 1.0f) t = 1.0f;

    // Smoothstep — zero slope at anchor AND at midpoint, max slope
    // in between. Sticky locks + smooth chaos transitions.
    float shaped = t * t * (3.0f - 2.0f * t);

    // Max offset = 0.25 envelope cycles (matches original π/2 design).
    return shaped * 0.25f;
  }

  // Cheap Padé tanh (matches Helicase / Parfait pattern). Used by
  // the MirrorBlock pre-saturation stage.
  static inline float mirror_fast_tanh(float x)
  {
    if (x < -4.0f) return -1.0f;
    if (x >  4.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  // ---------------------------------------------------------------
  // polyBLEP -- carry-over from Helicase, used to bandlimit the
  // sync-edge discontinuity in the carrier sine. Crucially we
  // *don't* BLEP the Mirror block's hold-and-release steps -- those
  // are the paradigm's fold sources.
  // ---------------------------------------------------------------
  static inline float polyBlep(float t, float dt)
  {
    if (t < dt) {
      t /= dt;
      return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
      t = (t - 1.0f) / dt;
      return t * t + t + t + 1.0f;
    }
    return 0.0f;
  }

  // ---------------------------------------------------------------
  // Wavetable formant envelope source — 16 frames x 256 samples, each
  // frame a one-shot envelope shape. Envelope phase advances at a
  // user-controlled rate independent of the carrier; sync (mod-wrap)
  // retriggers a fresh envelope when the prior one has completed. When
  // envelope rate < sync rate, retriggers are skipped during active
  // envelopes -> undertone series (pitch division) emerges naturally.
  //
  // Frames are ordered roughly simple -> exotic. Adjacent frames are
  // musically related so the Shape knob is a smooth timbral axis;
  // distant frames sound very different.
  // ---------------------------------------------------------------
  static const int   MIRROR_WT_FRAMES = 16;
  static const int   MIRROR_WT_LEN    = 256;
  static const float MIRROR_WT_LENF   = (float)(MIRROR_WT_LEN - 1);
  static const float MIRROR_WT_FRAMESF = (float)(MIRROR_WT_FRAMES - 1);

  static float gMirrorWavetable[MIRROR_WT_FRAMES][MIRROR_WT_LEN];

  static inline void precomputeMirrorWavetable()
  {
    for (int i = 0; i < MIRROR_WT_LEN; i++) {
      float t = (float)i / MIRROR_WT_LENF;  // [0, 1]

      // 0: Square gate (const 1.0).
      gMirrorWavetable[0][i] = 1.0f;

      // 1: Saw down (1 -> 0, linear fall).
      gMirrorWavetable[1][i] = 1.0f - t;

      // 2: Symmetric triangle (peak at 0.5).
      gMirrorWavetable[2][i] = (t < 0.5f) ? (t * 2.0f) : ((1.0f - t) * 2.0f);

      // 3: Exponential decay (1 -> ~0.05).
      gMirrorWavetable[3][i] = expf(-3.0f * t);

      // 4: Half-sine bell.
      gMirrorWavetable[4][i] = sinf(kPi * t);

      // 5: Gaussian peaked at 0.5.
      {
        float d = (t - 0.5f) / 0.15f;
        gMirrorWavetable[5][i] = expf(-d * d);
      }

      // 6: Asymmetric pluck (fast rise at 0.1, linear fall).
      gMirrorWavetable[6][i] = (t < 0.1f)
        ? t * 10.0f
        : 1.0f - (t - 0.1f) / 0.9f;

      // 7: Anti-pluck (linear rise, fast fall at 0.9).
      gMirrorWavetable[7][i] = (t < 0.9f)
        ? t / 0.9f
        : 1.0f - (t - 0.9f) * 10.0f;

      // 8: Two-peak lobed (envelope-shaped two-formant).
      gMirrorWavetable[8][i] = fabsf(sinf(kPi * 2.0f * t)) * sinf(kPi * t);

      // 9: Three-peak lobed.
      gMirrorWavetable[9][i] = fabsf(sinf(kPi * 3.0f * t)) * sinf(kPi * t);

      // 10: Damped sine (bipolar with decay, multi-cycle oscillation).
      gMirrorWavetable[10][i] = sinf(kPi * 6.0f * t) * expf(-3.0f * t);

      // 11: Inverse exp (slow start, fast end).
      gMirrorWavetable[11][i] = expf(3.0f * (t - 1.0f));

      // 12: Full sine cycle (bipolar, one period).
      gMirrorWavetable[12][i] = sinf(kPi * 2.0f * t);

      // 13: Damped square wave (several half-cycles with decay).
      {
        int   step = (int)(t * 6.0f);
        float sq   = (step & 1) ? -1.0f : 1.0f;
        gMirrorWavetable[13][i] = sq * expf(-2.0f * t);
      }

      // 14: Sinc-shaped (central peak with symmetric side lobes).
      {
        float x = (t - 0.5f) * 8.0f;
        gMirrorWavetable[14][i] = (fabsf(x) < 0.001f)
          ? 1.0f
          : sinf(kPi * x) / (kPi * x);
      }

      // 15: Triple-impulse (three narrow gaussian peaks).
      {
        float p1 = expf(-((t - 0.2f) * 25.0f) * ((t - 0.2f) * 25.0f));
        float p2 = expf(-((t - 0.5f) * 25.0f) * ((t - 0.5f) * 25.0f));
        float p3 = expf(-((t - 0.8f) * 25.0f) * ((t - 0.8f) * 25.0f));
        gMirrorWavetable[15][i] = p1 + p2 + p3;
      }
    }
  }

  // Static initializer: precompute the wavetable once at module load,
  // before any unit constructor runs.
  struct MirrorWavetableInit { MirrorWavetableInit() { precomputeMirrorWavetable(); } };
  static MirrorWavetableInit gMirrorWtInit;

  // Wavetable lookup with bi-linear interpolation across (sampleIdx,
  // frameIdx). Returns 0 when envelope phase has completed.
  static inline float wavetableLookup(float envPhase, float shape)
  {
    if (envPhase >= 1.0f) return 0.0f;
    if (envPhase < 0.0f)  envPhase = 0.0f;
    if (shape < 0.0f)     shape = 0.0f;
    if (shape > 1.0f)     shape = 1.0f;

    float sampleIdx = envPhase * MIRROR_WT_LENF;
    int   i0 = (int)sampleIdx;
    if (i0 >= MIRROR_WT_LEN - 1) i0 = MIRROR_WT_LEN - 2;
    float ifrac = sampleIdx - (float)i0;

    float frameIdx = shape * MIRROR_WT_FRAMESF;
    int   f0 = (int)frameIdx;
    if (f0 >= MIRROR_WT_FRAMES - 1) f0 = MIRROR_WT_FRAMES - 2;
    float ffrac = frameIdx - (float)f0;
    int   f1 = f0 + 1;

    float a0 = gMirrorWavetable[f0][i0];
    float a1 = gMirrorWavetable[f0][i0 + 1];
    float b0 = gMirrorWavetable[f1][i0];
    float b1 = gMirrorWavetable[f1][i0 + 1];

    float fa = a0 + (a1 - a0) * ifrac;
    float fb = b0 + (b1 - b0) * ifrac;
    return fa + (fb - fa) * ffrac;
  }

  // ---------------------------------------------------------------
  // MirrorBlock — destructive aliasing crusher driven by a single
  // knob. Four compound stages from one position:
  //
  //   1. Pre-saturation (tanh-driven harmonic generation) — pushes
  //      input bandwidth above the new Nyquist before sampling
  //   2. Divider-clocked sample-and-hold — undersamples without an
  //      anti-alias filter, folds above-Nyquist content back into
  //      band
  //   3. Bit-depth quantization on the held value — creates
  //      harmonics from any input including DC and smooth content
  //   4. Reconstruction blend — zero-order hold (alias-preserving)
  //      blended with Nyquist polarity flip (synthesizes content
  //      at SR/2 itself) for the brutal end of the knob travel
  //
  // Architectural framing follows the Airwindows undersample cycle-
  // counter pattern, but with reconstruction choices INVERTED —
  // AW smooths to avoid aliasing, Mirror does the opposite. See
  // planning/mirror-block-aw-refactor-plan.md.
  // ---------------------------------------------------------------
  struct MirrorBlock {
    int   divisor;
    int   counter;
    int   bitLevels;
    float bitScale;        // bitLevels * 0.5
    float bitInvScale;     // 1 / bitScale
    float held;
    float driveAmount;
    float flipAmount;
    float lastDriven;      // exposed for the Drive sub-out (pre-sat output captured each sample)

    inline void Init() {
      divisor      = 1;
      counter      = 0;
      bitLevels    = 65536;
      bitScale     = 32768.0f;
      bitInvScale  = 1.0f / 32768.0f;
      held         = 0.0f;
      driveAmount  = 0.0f;
      flipAmount   = 0.0f;
      lastDriven   = 0.0f;
    }

    // Recompute all stage parameters from the single knob value.
    // Called at block rate to keep per-sample cost trivial.
    inline void setKnob(float k) {
      if (k < 0.0f) k = 0.0f;
      if (k > 1.0f) k = 1.0f;

      // Stage 2: divisor — log to 64.
      float divf = expf(k * 4.158883f);  // ln(64)
      int d = (int)(divf + 0.5f);
      if (d < 1)  d = 1;
      if (d > 64) d = 64;
      if (d != divisor) {
        divisor = d;
        if (counter >= divisor) counter = 0;
      }

      // Stage 3: bit levels — log down from 65536 to 4 (16-bit to 2-bit).
      // ln(65536/4) = 9.704061
      float bitsf = expf(9.704061f * (1.0f - k));
      int b = (int)(bitsf + 0.5f);
      if (b < 4)     b = 4;
      if (b > 65536) b = 65536;
      bitLevels   = b;
      bitScale    = (float)b * 0.5f;
      bitInvScale = 1.0f / bitScale;

      // Stage 1: pre-saturation drive — linear ramp.
      driveAmount = k * 6.0f;

      // Stage 4: Nyquist-flip amount — 0 below 0.85, smoothstep to
      // 1 at k=1.0. Reserves the top 15% of knob travel for the
      // brutal Nyquist ring-mod character.
      if (k < 0.85f) {
        flipAmount = 0.0f;
      } else {
        float t = (k - 0.85f) * (1.0f / 0.15f);
        flipAmount = t * t * (3.0f - 2.0f * t);  // smoothstep
      }
    }

    inline float tick(float in) {
      // Stage 1: pre-saturation.
      float driven = (driveAmount > 0.01f)
        ? mirror_fast_tanh(in * (1.0f + driveAmount)) * (1.0f + driveAmount * 0.5f)
        : in;
      lastDriven = driven;  // captured for the Drive sub-out

      // Stage 2: cycle counter (on-cycle == counter rolls over to 0).
      bool onCycle = (counter == 0);
      counter++;
      if (counter >= divisor) counter = 0;

      // Stage 3: bit quantize + hold (on-cycle samples only).
      if (onCycle) {
        float scaled = driven * bitScale;
        held = floorf(scaled + 0.5f) * bitInvScale;
      }

      // Stage 4: reconstruction. ZOH only at low knob (fast path).
      if (flipAmount < 0.001f) return held;

      // ZOH blended with Nyquist-polarity-flip at high knob.
      float sign    = (counter & 1) ? -1.0f : 1.0f;
      float flipped = held * sign;
      return held + (flipped - held) * flipAmount;
    }

    inline void resetCounter() {
      counter = 0;
    }
  };

  // ---------------------------------------------------------------
  // Internal state.
  // ---------------------------------------------------------------
  struct Mirror::Internal {
    float carrierPhase;
    float modPhase;
    float lastModPhase;        // for wrap detection
    float lastCarrierPhase;    // for polyBLEP dt estimation on sync edge

    // Envelope phase (wavetable formant). Advances at formant rate;
    // wraps to >= 1 at end of envelope cycle; sync retrigger sets it
    // back to 0 when envelope has completed.
    //
    // Stereo: L and R pipelines run in parallel with independent
    // envelope phase + Mirror state + feedback state. At sync edge,
    // L resets to 0 and R resets to stereoOffset (derived from Sync
    // Threshold knob) — so at lock zones L = R (mono center image)
    // and at chaos midpoints R is offset by 0.25 envelope cycles
    // (max stereo width). Stereo width IS the chaos axis.
    float envPhase;
    float envPhaseR;

    // Previous-sample Mirror output, used as the feedback signal
    // for self-modulation on the envelope phase advance. The
    // 1-sample delay breaks the algebraic loop that would otherwise
    // form (Mirror output -> envInc -> wavetable lookup ->
    // Mirror tick -> Mirror output).
    float prevMirrorFeedback;
    float prevMirrorFeedbackR;

    MirrorBlock mirror;
    MirrorBlock mirrorR;

    // DC blocker state (one-pole HPF, 20 Hz, engaged only above 1 Hz).
    // Main Out has its own state pair; Clean / Drive / Held sub-outs
    // each get their own pair so the filters evolve independently
    // (each tap point has different content and shouldn't share
    // filter state).
    float dcX1, dcY1;        // Main Out (L)
    float dcX1R, dcY1R;      // Main Out R
    float cleanX1, cleanY1;
    float driveX1, driveY1;
    float heldX1, heldY1;

    // Ring buffers for viz (decimated capture).
    float outputRing[256];
    float modRing[256];
    int ringPos;
    int ringDecimCounter;
    int ringDecimRate;

    // Current state snapshots for viz pickoffs.
    float curCarrierPhase;
    float curModPhase;
    float curLockRatio;
    int   curMirrorDivisor;

    // Sync gate output state (sticky for one sample to be visible to
    // downstream comparator > 0.5 threshold).
    float syncGateOut;

    void Init() {
      carrierPhase = 0.0f;
      modPhase = 0.0f;
      lastModPhase = 0.0f;
      lastCarrierPhase = 0.0f;
      envPhase  = 1.0f;  // start completed -> first sync starts a fresh envelope
      envPhaseR = 1.0f;
      prevMirrorFeedback  = 0.0f;
      prevMirrorFeedbackR = 0.0f;
      mirror.Init();
      mirrorR.Init();
      dcX1  = 0.0f; dcY1  = 0.0f;
      dcX1R = 0.0f; dcY1R = 0.0f;
      cleanX1 = 0.0f; cleanY1 = 0.0f;
      driveX1 = 0.0f; driveY1 = 0.0f;
      heldX1  = 0.0f; heldY1  = 0.0f;
      memset(outputRing, 0, sizeof(outputRing));
      memset(modRing, 0, sizeof(modRing));
      ringPos = 0;
      ringDecimCounter = 0;
      ringDecimRate = 8;
      curCarrierPhase = 0.0f;
      curModPhase = 0.0f;
      curLockRatio = 1.0f;
      curMirrorDivisor = 1;
      syncGateOut = 0.0f;
    }
  };

  // ---------------------------------------------------------------
  Mirror::Mirror()
  {
    addInput(mVOct);
    addInput(mFM);

    addOutput(mOut);
    addOutput(mOutR);
    addOutput(mClean);
    addOutput(mDrive);
    addOutput(mHeldOut);
    addOutput(mFold);
    addOutput(mSync);
    addOutput(mModOut);

    addParameter(mFundamental);
    addParameter(mShape);
    addParameter(mFormant);
    addParameter(mModDepth);
    addParameter(mSyncThreshold);
    addParameter(mMirror);
    addParameter(mFeedback);
    addParameter(mLevel);

    addOption(mMirrorReset);
    mMirrorReset.enableSerialization();

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Mirror::~Mirror()
  {
    delete mpInternal;
  }

  // SWIG-visible getters (used by future viz; safe to call from Lua).
  float Mirror::getOutputSample(int idx) {
    if (idx < 0 || idx > 255) return 0.0f;
    return mpInternal->outputRing[(mpInternal->ringPos + idx) & 255];
  }
  float Mirror::getModulatorSample(int idx) {
    if (idx < 0 || idx > 255) return 0.0f;
    return mpInternal->modRing[(mpInternal->ringPos + idx) & 255];
  }
  float Mirror::getCarrierPhase()    { return mpInternal->curCarrierPhase; }
  float Mirror::getModPhase()        { return mpInternal->curModPhase; }
  float Mirror::getLockRatio()       { return mpInternal->curLockRatio; }
  int   Mirror::getMirrorDivisor()   { return mpInternal->curMirrorDivisor; }

  // ---------------------------------------------------------------
  void Mirror::process()
  {
    Internal &s = *mpInternal;

    float *voct  = mVOct.buffer();
    float *fm    = mFM.buffer();
    float *outBuf    = mOut.buffer();
    float *outRBuf   = mOutR.buffer();
    float *cleanBuf  = mClean.buffer();
    float *driveBuf  = mDrive.buffer();
    float *heldBuf   = mHeldOut.buffer();
    float *foldBuf   = mFold.buffer();
    float *syncBuf   = mSync.buffer();
    float *modBuf    = mModOut.buffer();

    float sr = globalConfig.sampleRate;
    float invSr = 1.0f / sr;

    // Block-rate parameter snapshots.
    float f0       = CLAMP(0.1f, sr * 0.49f, mFundamental.value());
    float shape    = CLAMP(0.0f, 1.0f, mShape.value());
    float formant0 = CLAMP(0.1f, sr * 0.49f, mFormant.value());
    float modDepth = CLAMP(0.0f, 1.0f, mModDepth.value());
    float syncKnob = CLAMP(0.0f, 1.0f, mSyncThreshold.value());
    float mirrorKnob = CLAMP(0.0f, 1.0f, mMirror.value());
    float feedback = CLAMP(0.0f, 1.0f, mFeedback.value());
    float level    = CLAMP(0.0f, 1.0f, mLevel.value());
    bool  mirrorResetOn = (mMirrorReset.value() == 1);

    // Lock ratio (cubic-around-integer-ratios mapping).
    float lockRatio = lockRatioFromKnob(syncKnob);
    s.curLockRatio = lockRatio;

    // Mirror knob drives all four stages of the MirrorBlock crusher
    // (pre-sat, divisor, bit-depth, Nyquist-flip). Block-rate update
    // so the per-sample tick stays cheap. L and R crushers run
    // identical settings — stereo differentiation comes from the
    // envelope phase offset, not from per-side crusher differences.
    s.mirror.setKnob(mirrorKnob);
    s.mirrorR.setKnob(mirrorKnob);
    s.curMirrorDivisor = s.mirror.divisor;

    // Stereo envelope phase offset (Δφ) — 0 at lock zones (mono),
    // up to 0.25 envelope cycles at chaos midpoints (wide).
    float stereoOffset = stereoOffsetFromSyncKnob(syncKnob);

    // V/Oct: Lua wraps with 10x ConstantGain so the buffer carries
    // 1.0-per-octave (Plaits convention, mirrors Helicase).
    float pitch = voct[0];
    float modFreqTarget = f0 * powf(2.0f, pitch);
    if (modFreqTarget > sr * 0.49f) modFreqTarget = sr * 0.49f;

    // Carrier rate is mod rate × lock ratio. At integer lock ratios
    // the carrier completes N cycles per mod cycle; at non-integer
    // ratios the carrier waveform gets sync-truncated producing
    // inharmonic content. Perceived pitch tracks mod rate = V/Oct.
    float carrierFreqTarget = modFreqTarget * lockRatio;
    if (carrierFreqTarget > sr * 0.49f) carrierFreqTarget = sr * 0.49f;

    // Formant (envelope) rate. FIXED-style tracking: Formant in Hz
    // at V/Oct = 0, scaled by the same 2^V/Oct factor as f0. Means
    // the envelope rate tracks pitch by default; user dials Formant
    // in Hz to set the absolute character at the V/Oct = 0
    // reference point.
    //
    // Sync Threshold knob scales the envelope rate via lockRatio
    // (cubic-around-Fibonacci anchors {1, 2, 3, 5, 8, 13}). At anchor
    // 1: envelope rate = Formant. At anchor 13: 13x faster envelopes.
    // This is the chaos/lock axis: integer ratios produce clean
    // harmonic-formant series; non-integer chaos zones produce
    // inharmonic formant landings. Drives Mirror dramatic when the
    // sharp wavetable shapes hit it at high effective formant rates.
    float formantFreqTarget = formant0 * powf(2.0f, pitch) * lockRatio;
    if (formantFreqTarget > sr * 0.49f) formantFreqTarget = sr * 0.49f;
    if (formantFreqTarget < 0.1f)       formantFreqTarget = 0.1f;

    // DC HPF coefficient (one-pole, 20 Hz, engaged only when modFreq
    // > 1 Hz so Mirror-as-LFO keeps legit sub-Hz content).
    const float dcR = 1.0f - kTwoPi * 20.0f / sr;
    bool dcEnable = (modFreqTarget > 1.0f);

    // Adapt ring buffer decimation to carrier rate (keeps viz at ~8
    // cycles of context).
    float samplesPerCycle = sr / (modFreqTarget > 0.1f ? modFreqTarget : 0.1f);
    int targetDecim = (int)(samplesPerCycle * 8.0f / 256.0f);
    if (targetDecim < 1) targetDecim = 1;
    if (targetDecim > 64) targetDecim = 64;
    s.ringDecimRate = targetDecim;

    for (int i = 0; i < FRAMELENGTH; i++) {
      // FM input (audio-rate, expo): perturbs the carrier base freq.
      // Use modest weighting to keep behavior tame at default scaling.
      float fmIn = fm[i];
      float carrierFreq = carrierFreqTarget * powf(2.0f, fmIn);
      if (carrierFreq > sr * 0.49f) carrierFreq = sr * 0.49f;
      float modFreq = modFreqTarget;

      float modInc     = modFreq * invSr;
      float carrierInc = carrierFreq * invSr;

      // Advance mod phase.
      s.lastModPhase = s.modPhase;
      s.modPhase += modInc;
      bool modWrapped = (s.modPhase >= 1.0f);
      if (modWrapped) s.modPhase -= 1.0f;

      // Mod oscillator output (clean sine, used for FM into carrier
      // and as the Mod sub-out).
      float modOut = neon_math::sine_poly_hq(s.modPhase);

      // Sync edge: mod phase wrap. Internal-mod-driven sync = no
      // external sync input. Reset carrier phase (and optionally
      // Mirror counters) on every mod wrap. Mirror reset applies
      // to BOTH L and R sides — same crusher, same reset cadence.
      bool syncEdge = modWrapped;
      if (syncEdge) {
        s.carrierPhase = 0.0f;
        if (mirrorResetOn) {
          s.mirror.resetCounter();
          s.mirrorR.resetCounter();
        }
        s.syncGateOut = 1.0f;
      } else {
        s.syncGateOut = 0.0f;
      }

      // Carrier phase advance with FM from mod. Carrier phase now
      // tracks SYNC TIMING only — the actual audio comes from the
      // wavetable envelope. Keeping carrier phase live makes the
      // existing Sync Threshold + Mirror Reset machinery continue to
      // work; carrier wrap is still the sync edge.
      float fmShift = modOut * modDepth * carrierInc;
      s.lastCarrierPhase = s.carrierPhase;
      s.carrierPhase += carrierInc + fmShift;
      if (s.carrierPhase >= 1.0f) s.carrierPhase -= floorf(s.carrierPhase);
      if (s.carrierPhase < 0.0f)  s.carrierPhase -= floorf(s.carrierPhase);

      // Envelope phase advance with audio-rate FM from mod sine and
      // self-modulation from previous-sample Mirror output.
      //
      // Mod scale: exponential ±3 octaves at depth=1 (sine source,
      // shared between L and R).
      // Feedback scale: exponential ±2 octaves at depth=1 (Mirror
      // source — already broader spectrum than sine, so slightly
      // less aggressive multiplier). Per-side feedback because L
      // and R have independent Mirror states. The 1-sample delay
      // on prevMirrorFeedback breaks the would-be algebraic loop.
      float modScale  = expf(modOut * modDepth * 3.0f);
      float fbScaleL  = expf(s.prevMirrorFeedback  * feedback * 2.0f);
      float fbScaleR  = expf(s.prevMirrorFeedbackR * feedback * 2.0f);
      float envIncBase = formantFreqTarget * invSr * modScale;
      s.envPhase  += envIncBase * fbScaleL;
      s.envPhaseR += envIncBase * fbScaleR;

      // Sync retrigger: when sync edge arrives (mod wrap), restart
      // each envelope ONLY IF its previous cycle has completed
      // (envPhase >= 1). If still active, the sync is absorbed -
      // this is what produces the undertone series naturally when
      // formant rate < sync rate.
      //
      // L resets to 0; R resets to stereoOffset (Δφ derived from
      // Sync Threshold). At lock zones stereoOffset = 0 so L = R
      // (mono); at chaos midpoints R is offset by up to 0.25
      // envelope cycles (wide stereo). Width audibly collapses as
      // the knob finds each next lock.
      if (syncEdge) {
        if (s.envPhase  >= 1.0f) s.envPhase  = 0.0f;
        if (s.envPhaseR >= 1.0f) s.envPhaseR = stereoOffset;
      }

      // Wavetable lookup (per side). Returns 0 when envelope completed.
      float cleanL = wavetableLookup(s.envPhase,  shape);
      float cleanR = wavetableLookup(s.envPhaseR, shape);

      // Mirror block: 4-stage destructive aliasing crusher (per side).
      float foldedL = s.mirror.tick(cleanL);
      float foldedR = s.mirrorR.tick(cleanR);

      // Store for next sample's feedback loop into envelope rate
      // (per side — feedback evolves independently per channel).
      s.prevMirrorFeedback  = foldedL;
      s.prevMirrorFeedbackR = foldedR;

      // L-channel signals fed forward through the rest of the
      // pipeline (sub-outs are L taps only).
      float clean   = cleanL;
      float folded  = foldedL;
      float foldOnly = foldedL - cleanL;

      // Main L: Mirror output → DC HPF → Level → soft clip.
      float dcOutL = folded - s.dcX1 + dcR * s.dcY1;
      s.dcX1 = folded; s.dcY1 = dcOutL;
      float hpL = dcEnable ? dcOutL : folded;
      float finalOut = hpL * level;

      // Main R: same pipeline with independent DC state.
      float dcOutR = foldedR - s.dcX1R + dcR * s.dcY1R;
      s.dcX1R = foldedR; s.dcY1R = dcOutR;
      float hpR = dcEnable ? dcOutR : foldedR;
      float finalOutR = hpR * level;

      // Soft clip at the rails (pseudo-saturate K=1.5, p=4) on
      // both channels independently — wild Push + Source territory
      // can't peg the DAC. Smooth transition, no discontinuity.
      {
        const float invK = 1.0f / 1.5f;
        float axL = (finalOut >= 0.0f) ? finalOut : -finalOut;
        float xkL  = axL * invK;
        float xk4L = (xkL * xkL) * (xkL * xkL);
        finalOut = finalOut / sqrtf(sqrtf(1.0f + xk4L));

        float axR = (finalOutR >= 0.0f) ? finalOutR : -finalOutR;
        float xkR  = axR * invK;
        float xk4R = (xkR * xkR) * (xkR * xkR);
        finalOutR = finalOutR / sqrtf(sqrtf(1.0f + xk4R));
      }

      outBuf[i]  = finalOut;
      outRBuf[i] = finalOutR;

      // DC-block Clean / Drive / Held sub-outs so they're symmetric
      // around 0 for downstream patch consumers (the wavetable
      // envelope shapes are positive-only, so without HPF the raw
      // tap values carry strong DC). Same 20 Hz one-pole as the
      // main Out, same LFO-mode bypass.
      if (dcEnable) {
        float cleanHp = clean - s.cleanX1 + dcR * s.cleanY1;
        s.cleanX1 = clean; s.cleanY1 = cleanHp;
        cleanBuf[i] = cleanHp;

        float driveIn = s.mirror.lastDriven;
        float driveHp = driveIn - s.driveX1 + dcR * s.driveY1;
        s.driveX1 = driveIn; s.driveY1 = driveHp;
        driveBuf[i] = driveHp;

        float heldIn = s.mirror.held;
        float heldHp = heldIn - s.heldX1 + dcR * s.heldY1;
        s.heldX1 = heldIn; s.heldY1 = heldHp;
        heldBuf[i] = heldHp;
      } else {
        cleanBuf[i] = clean;
        driveBuf[i] = s.mirror.lastDriven;
        heldBuf[i]  = s.mirror.held;
      }

      foldBuf[i]  = foldOnly;
      syncBuf[i]  = s.syncGateOut;
      modBuf[i]   = modOut;

      // Viz ring buffers (decimated).
      s.ringDecimCounter++;
      if (s.ringDecimCounter >= s.ringDecimRate) {
        s.ringDecimCounter = 0;
        s.outputRing[s.ringPos] = finalOut;
        s.modRing[s.ringPos] = modOut;
        s.ringPos = (s.ringPos + 1) & 255;
      }
    }

    s.curCarrierPhase = s.carrierPhase;
    s.curModPhase = s.modPhase;
  }

} // namespace stolmine
