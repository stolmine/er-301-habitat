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

  // Mirror knob (0..1) -> integer divisor in [1, MIRROR_DIVISOR_MAX].
  // Exponential mapping so the low end stays close to 1x (subtle
  // fold) and the high end opens to heavy fold density.
  static const int MIRROR_DIVISOR_MAX = 16;
  static inline int mirrorDivisorFromKnob(float k)
  {
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    const float maxLog = 2.77258872f;  // ln(16)
    float divf = expf(k * maxLog);
    int d = (int)(divf + 0.5f);
    if (d < 1) d = 1;
    if (d > MIRROR_DIVISOR_MAX) d = MIRROR_DIVISOR_MAX;
    return d;
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
  // Source shape: 3-shape morph (sine -> Chebyshev T3 -> 2-op self-FM).
  // Push drives nonlinearity within each shape.
  // ---------------------------------------------------------------
  static inline float sourceShape(float phase01, float morph, float push)
  {
    if (morph < 0.0f) morph = 0.0f;
    if (morph > 1.0f) morph = 1.0f;
    if (push < 0.0f) push = 0.0f;
    if (push > 1.0f) push = 1.0f;

    // Wrapped phase. neon_math::sine_poly_hq expects [0, 1).
    float p = phase01 - floorf(phase01);
    float s = neon_math::sine_poly_hq(p);

    // Shape A: sine.
    float sa = s;

    // Shape B: Chebyshev T3 (4x^3 - 3x), pre-driven by push.
    float driven = s * (1.0f + push * 0.5f);
    if (driven > 1.0f) driven = 1.0f;
    if (driven < -1.0f) driven = -1.0f;
    float sb = 4.0f * driven * driven * driven - 3.0f * driven;

    // Shape C: iterated triangle wavefolder. Drive amplifies the sine
    // beyond [-1, 1]; each fold reflects content back into range,
    // producing slope discontinuities that DOUBLE harmonic content per
    // fold. At push=1, ~6 folds happen per cycle, generating content
    // well into the Nyquist region even at low F0 (the whole point —
    // this is the bandwidth-multiplier shape that gives Mirror
    // something to fold at any perceived pitch).
    //
    // Bounded iteration count (16) is comfortably above the max folds
    // needed (drive at push=1 is 6, so ~6 folds converge).
    float drive = 1.0f + push * 5.0f;
    float folded = s * drive;
    for (int j = 0; j < 16; j++) {
      if (folded > 1.0f)       folded = 2.0f - folded;
      else if (folded < -1.0f) folded = -2.0f - folded;
      else                     break;
    }
    float sc = folded;

    // Two-segment lerp between three shapes.
    if (morph <= 0.5f) {
      float t = morph * 2.0f;
      return sa + (sb - sa) * t;
    } else {
      float t = (morph - 0.5f) * 2.0f;
      return sb + (sc - sb) * t;
    }
  }

  // ---------------------------------------------------------------
  // MirrorBlock — divider-clocked S&H with NO anti-aliasing on either
  // side. The paradigm-defining stage.
  // ---------------------------------------------------------------
  struct MirrorBlock {
    int divisor;
    int counter;
    float held;

    inline void Init() {
      divisor = 1;
      counter = 0;
      held = 0.0f;
    }

    inline float tick(float in) {
      if (counter == 0) held = in;
      counter++;
      if (counter >= divisor) counter = 0;
      return held;
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

    MirrorBlock mirror;

    // DC blocker state (one-pole HPF, 20 Hz, engaged only above 1 Hz).
    float dcX1;
    float dcY1;

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
      mirror.Init();
      dcX1 = 0.0f;
      dcY1 = 0.0f;
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
    addOutput(mClean);
    addOutput(mFold);
    addOutput(mSync);
    addOutput(mModOut);

    addParameter(mFundamental);
    addParameter(mFine);
    addParameter(mSource);
    addParameter(mPush);
    addParameter(mModDepth);
    addParameter(mSyncThreshold);
    addParameter(mMirror);
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
    float *outBuf   = mOut.buffer();
    float *cleanBuf = mClean.buffer();
    float *foldBuf  = mFold.buffer();
    float *syncBuf  = mSync.buffer();
    float *modBuf   = mModOut.buffer();

    float sr = globalConfig.sampleRate;
    float invSr = 1.0f / sr;

    // Block-rate parameter snapshots.
    float f0       = CLAMP(0.1f, sr * 0.49f, mFundamental.value());
    float fine     = CLAMP(-100.0f, 100.0f, mFine.value());
    float source   = CLAMP(0.0f, 1.0f, mSource.value());
    float push     = CLAMP(0.0f, 1.0f, mPush.value());
    float modDepth = CLAMP(0.0f, 1.0f, mModDepth.value());
    float syncKnob = CLAMP(0.0f, 1.0f, mSyncThreshold.value());
    float mirrorKnob = CLAMP(0.0f, 1.0f, mMirror.value());
    float level    = CLAMP(0.0f, 1.0f, mLevel.value());
    bool  mirrorResetOn = (mMirrorReset.value() == 1);

    // Lock ratio (cubic-around-integer-ratios mapping).
    float lockRatio = lockRatioFromKnob(syncKnob);
    s.curLockRatio = lockRatio;

    // Mirror divisor (block-rate snapshot; changes only between
    // blocks so the S&H counter doesn't get reseated mid-stream).
    int divisor = mirrorDivisorFromKnob(mirrorKnob);
    if (divisor != s.mirror.divisor) {
      s.mirror.divisor = divisor;
      // Don't reset counter here -- let it continue from where it
      // was, modulo the new divisor (avoids click).
      if (s.mirror.counter >= divisor) s.mirror.counter = 0;
    }
    s.curMirrorDivisor = divisor;

    // V/Oct: Lua wraps with 10x ConstantGain so the buffer carries
    // 1.0-per-octave (Plaits convention, mirrors Helicase).
    float pitch = voct[0];
    float fineCents = fine / 1200.0f;
    float modFreqTarget = f0 * powf(2.0f, pitch + fineCents);
    if (modFreqTarget > sr * 0.49f) modFreqTarget = sr * 0.49f;

    // Carrier rate is mod rate × lock ratio. At integer lock ratios
    // the carrier completes N cycles per mod cycle; at non-integer
    // ratios the carrier waveform gets sync-truncated producing
    // inharmonic content. Perceived pitch tracks mod rate = V/Oct.
    float carrierFreqTarget = modFreqTarget * lockRatio;
    if (carrierFreqTarget > sr * 0.49f) carrierFreqTarget = sr * 0.49f;

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
      // Mirror counter) on every mod wrap.
      bool syncEdge = modWrapped;
      if (syncEdge) {
        s.carrierPhase = 0.0f;
        if (mirrorResetOn) s.mirror.resetCounter();
        s.syncGateOut = 1.0f;
      } else {
        s.syncGateOut = 0.0f;
      }

      // Carrier phase advance with FM from mod (Mod Depth = FM index
      // weighting).
      float fmShift = modOut * modDepth * carrierInc;
      s.lastCarrierPhase = s.carrierPhase;
      s.carrierPhase += carrierInc + fmShift;
      if (s.carrierPhase >= 1.0f) s.carrierPhase -= floorf(s.carrierPhase);
      if (s.carrierPhase < 0.0f)  s.carrierPhase -= floorf(s.carrierPhase);

      // Source shape (sine -> poly3 -> self-FM morph).
      float clean = sourceShape(s.carrierPhase, source, push);

      // polyBLEP on sync edge to bandlimit the carrier phase reset
      // discontinuity. Without this, every sync edge emits broadband
      // splatter that aliases as inharmonic noise floor.
      float blepDt = carrierInc + fabsf(fmShift);
      if (blepDt > 0.5f) blepDt = 0.5f;
      if (blepDt < 1e-6f) blepDt = 1e-6f;
      clean -= polyBlep(s.carrierPhase, blepDt);

      // Mirror block: divider-clocked S&H. No AA either side.
      float folded = s.mirror.tick(clean);

      // Fold = alias residual (Mirror output minus bandlimited clean).
      float foldOnly = folded - clean;

      // Main: take the Mirror output. DC HPF on the main path.
      float mainSig = folded;
      float dcOut = mainSig - s.dcX1 + dcR * s.dcY1;
      s.dcX1 = mainSig;
      s.dcY1 = dcOut;
      float hp = dcEnable ? dcOut : mainSig;
      float finalOut = hp * level;

      // Soft clip at the rails so wild Push + Source territory can't
      // peg the DAC. Use the smooth pseudo-saturate pattern (no
      // discontinuity at threshold like the old Parfait clamp).
      // f(x) = x / sqrt(sqrt(1 + (|x|/1.5)^4))
      {
        const float invK = 1.0f / 1.5f;
        float ax = (finalOut >= 0.0f) ? finalOut : -finalOut;
        float xk = ax * invK;
        float xk2 = xk * xk;
        float xk4 = xk2 * xk2;
        finalOut = finalOut / sqrtf(sqrtf(1.0f + xk4));
      }

      outBuf[i]   = finalOut;
      cleanBuf[i] = clean;
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
