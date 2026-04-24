#include "DrumVoice.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{

  static inline float fast_tanh(float x)
  {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  // IEEE 754 fast log2 / exp2 / dB helpers (from Larets/Parfait).
  static inline float fast_log2(float x)
  {
    union { float f; int32_t i; } v;
    v.f = x;
    float y = (float)(v.i);
    y *= 1.0f / (1 << 23);
    y -= 127.0f;
    return y;
  }
  static inline float fast_exp2(float x)
  {
    union { float f; int32_t i; } v;
    v.i = (int32_t)((x + 127.0f) * (1 << 23));
    return v.f;
  }
  static inline float fast_log10(float x) { return fast_log2(x) * 0.30103f; }
  static inline float fast_fromDb(float db) { return fast_exp2(db * 0.16609640474f); }

  static inline float lookupSine(const float *lut, float tri)
  {
    float absTri = fabsf(tri);
    float idx = absTri * 128.0f;
    int i = (int)idx;
    if (i >= 128) i = 127;
    float frac = idx - (float)i;
    float val = lut[i] + frac * (lut[i + 1] - lut[i]);
    return tri >= 0.0f ? val : -val;
  }

  struct DrumVoice::Internal
  {
    static float sLUT[257];
    static bool sLUTInit;

    float phase1 = 0.0f;
    float phase2 = 0.0f;
    float phaseFm = 0.0f;

    float ampEnv = 0.0f;
    float punchEnv = 0.0f;
    float shapeEnv = 0.0f;

    int envPhase = 0;
    int holdCounter = 0;

    float currentFreq = 110.0f;
    float sweepRatio = 1.0f;
    float baseFreq = 110.0f;

    float ampDecayCoeff = 0.999f;
    float shapeDecayCoeff = 0.999f;
    float punchDecayCoeff = 0.999f;
    float attackIncr = 0.0f;

    uint32_t noiseState = 12345;

    float ic1eq = 0.0f;
    float ic2eq = 0.0f;

    float compDetector = 0.0f;

    float prevTrigger = 0.0f;

    float vizEnvLevel = 0.0f;
    bool vizGateState = false;

    float cachedCharacter = 0.5f;
    float cachedShape = 0.0f;
    float cachedGrit = 0.0f;

    void initLUT()
    {
      if (!sLUTInit)
      {
        for (int i = 0; i <= 256; i++)
          sLUT[i] = sinf((float)i / 256.0f * (float)M_PI);
        sLUTInit = true;
      }
    }
  };

  float DrumVoice::Internal::sLUT[257] = {};
  bool DrumVoice::Internal::sLUTInit = false;

  DrumVoice::DrumVoice()
  {
    addInput(mTrigger);
    addInput(mVOct);
    addInput(mXformGate);
    addOutput(mOut);
    addParameter(mCharacter);
    addParameter(mShape);
    addParameter(mGrit);
    addParameter(mPunch);
    addParameter(mSweep);
    addParameter(mSweepTime);
    addParameter(mAttack);
    addParameter(mHold);
    addParameter(mDecay);
    addParameter(mClipper);
    addParameter(mEQ);
    addParameter(mLevel);
    addParameter(mCompAmt);
    addParameter(mOctave);
    addParameter(mXformDepth);
    addParameter(mXformSpread);
    addParameter(mXformTarget);
    mpInternal = new Internal();
    mpInternal->initLUT();
  }

  DrumVoice::~DrumVoice()
  {
    delete mpInternal;
  }

  float DrumVoice::getCharacter() { return mpInternal->cachedCharacter; }
  float DrumVoice::getShape()     { return mpInternal->cachedShape; }
  float DrumVoice::getGrit()      { return mpInternal->cachedGrit; }
  float DrumVoice::getEnvLevel()  { return mpInternal->vizEnvLevel; }
  bool  DrumVoice::getGateState() { return mpInternal->vizGateState; }

  void DrumVoice::fireRandomize() { mManualFire = true; }

  void DrumVoice::setTopLevelBias(int which, od::Parameter *param)
  {
    switch (which)
    {
    case 0:  mBiasCharacter = param; break;
    case 1:  mBiasShape     = param; break;
    case 2:  mBiasGrit      = param; break;
    case 3:  mBiasPunch     = param; break;
    case 4:  mBiasAttack    = param; break;
    case 5:  mBiasHold      = param; break;
    case 6:  mBiasDecay     = param; break;
    case 7:  mBiasSweep     = param; break;
    case 8:  mBiasSweepTime = param; break;
    case 9:  mBiasClipper   = param; break;
    case 10: mBiasEQ        = param; break;
    case 11: mBiasLevel     = param; break;
    case 12: mBiasComp      = param; break;
    case 13: mBiasOctave    = param; break;
    }
  }

  static uint32_t sRandState = 48271u;

  static float randomizeValue(float cur, float mn, float mx, float depth, float spread)
  {
    float range = mx - mn;
    float center = spread * (mn + mx) * 0.5f + (1.0f - spread) * cur;
    float dev = depth * range * 0.5f;
    sRandState = sRandState * 1664525u + 1013904223u;
    float r = (float)(int32_t)sRandState / 2147483648.0f;
    float v = center + r * dev;
    if (v < mn) v = mn;
    if (v > mx) v = mx;
    return v;
  }

  void DrumVoice::applyRandomize()
  {
    float depth  = CLAMP(0.0f, 1.0f, mXformDepth.value());
    float spread = CLAMP(0.0f, 1.0f, mXformSpread.value());

    auto rnd = [&](od::Parameter *p, float mn, float mx) {
      if (p) p->hardSet(randomizeValue(p->value(), mn, mx, depth, spread));
    };
    auto rndInt = [&](od::Parameter *p, float mn, float mx) {
      if (p) p->hardSet(floorf(randomizeValue(p->value(), mn, mx, depth, spread) + 0.5f));
    };

    rnd(mBiasCharacter,  0.0f,   1.0f);
    rnd(mBiasShape,      0.0f,   1.0f);
    rnd(mBiasGrit,       0.0f,   1.0f);
    rnd(mBiasPunch,      0.0f,   1.0f);
    rnd(mBiasAttack,     0.0f,   0.05f);
    rnd(mBiasHold,       0.0f,   0.5f);
    rnd(mBiasDecay,      0.01f,  2.0f);
    rnd(mBiasSweep,      0.0f,   72.0f);
    rnd(mBiasSweepTime,  0.001f, 0.5f);
    rndInt(mBiasOctave, -4.0f,   4.0f);
    // Output-chain params (Clipper, EQ, Level, Comp) deliberately NOT
    // randomized -- they're user-mix decisions, randomizing them flips
    // the perceived loudness / character on every fire which fights the
    // user's mix. Bias-pointer registration retained for future opt-in.
  }

  void DrumVoice::process()
  {
    Internal &s = *mpInternal;
    float *trig = mTrigger.buffer();
    float *voct = mVOct.buffer();
    float *xgate = mXformGate.buffer();
    float *out  = mOut.buffer();

    float sr = globalConfig.sampleRate;

    float character = CLAMP(0.0f, 1.0f, mCharacter.value());
    float shape     = CLAMP(0.0f, 1.0f, mShape.value());
    float grit      = CLAMP(0.0f, 1.0f, mGrit.value());
    float punch     = CLAMP(0.0f, 1.0f, mPunch.value());
    float sweep     = CLAMP(0.0f, 72.0f, mSweep.value());
    float sweepTime = CLAMP(0.001f, 0.5f, mSweepTime.value());
    float attack    = CLAMP(0.0f, 0.05f, mAttack.value());
    float hold      = CLAMP(0.0f, 0.5f, mHold.value());
    float decay     = CLAMP(0.01f, 2.0f, mDecay.value());
    float clipper   = CLAMP(0.0f, 1.0f, mClipper.value());
    float eq        = CLAMP(-1.0f, 1.0f, mEQ.value());
    float level     = CLAMP(0.0f, 1.0f, mLevel.value());
    float compAmt   = CLAMP(0.0f, 1.0f, mCompAmt.value());
    bool compActive = compAmt > 0.001f;

    // CPR single-band one-knob comp (matches Larets pattern). Auto makeup
    // is always on so the user gets compensated loudness as they push.
    float compThresholdDb = -compAmt * 40.0f;            // 0 dB -> -40 dB
    float compRatioI      = 1.0f / (1.0f + compAmt * 19.0f); // 1:1 -> 20:1
    float compAttackSec   = 0.010f - compAmt * 0.009f;   // 10 ms -> 1 ms
    float compReleaseSec  = 0.200f;
    float compRiseCoeff   = expf(-1.0f / (compAttackSec * sr));
    float compFallCoeff   = expf(-1.0f / (compReleaseSec * sr));
    float compMakeupGain  = compActive
        ? fast_fromDb(-compThresholdDb * (1.0f - compRatioI))
        : 1.0f;

    s.cachedCharacter = character;
    s.cachedShape = shape;
    s.cachedGrit = grit;

    // V/Oct + octave offset: block-rate
    float octave = floorf(CLAMP(-4.0f, 4.0f, mOctave.value()) + 0.5f);
    float pitch = voct[0] + octave;
    float baseFreq = 110.0f * powf(2.0f, pitch);
    baseFreq = CLAMP(10.0f, sr * 0.49f, baseFreq);

    // Clipper drive (block-rate). Simple tanh with moderate 1..10x range
    // and gain compensation (divide by tanh(drive)) so output amplitude
    // stays near unity as drive increases.
    bool clipperActive = clipper > 0.001f;
    float driveLinear = clipperActive ? (1.0f + clipper * 9.0f) : 1.0f;
    float driveNorm   = clipperActive ? fast_tanh(driveLinear) : 1.0f;

    // DJ filter coefficients (block-rate). EQ is bipolar: -1..0 = LP,
    // 0 = bypass, 0..+1 = HP. Magnitude drives cutoff sweep.
    float eqCut = eq;
    float absEqCut = fabsf(eqCut);
    bool filterActive = absEqCut >= 0.01f;
    bool isLP = (eqCut < 0.0f);
    float fA1 = 0.0f, fA2 = 0.0f, fA3 = 0.0f, fK = 1.05f;
    if (filterActive)
    {
      float filterFreq = isLP
        ? 20.0f * powf(1000.0f, 1.0f - absEqCut)
        : 20.0f * powf(1000.0f, absEqCut);
      filterFreq = CLAMP(20.0f, sr * 0.49f, filterFreq);
      float g = tanf(3.14159f * filterFreq / sr);
      fA1 = 1.0f / (1.0f + g * (g + fK));
      fA2 = g * fA1;
      fA3 = g * fA2;
    }

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Xform gate: rising edge on CV input OR manual fire flag -> apply
      // randomize. Threshold matches Pecto (0.5) to ignore comparator
      // residue between triggers.
      bool xformHigh = xgate[i] > 0.5f;
      bool xformRise = xformHigh && !mXformGateWasHigh;
      mXformGateWasHigh = xformHigh;
      if (xformRise || mManualFire)
      {
        applyRandomize();
        mManualFire = false;
      }

      // Trigger detection: rising edge
      float trigVal = trig[i];
      if (trigVal > 0.1f && s.prevTrigger <= 0.1f)
      {
        // Grit envelope coupling: high grit shortens decay
        float effectiveDecay = decay;
        if (grit > 0.75f)
          effectiveDecay *= (1.0f - (grit - 0.75f) * 4.0f * 0.7f);
        if (effectiveDecay < 0.001f) effectiveDecay = 0.001f;

        s.baseFreq = baseFreq;
        float freqStart = baseFreq * powf(2.0f, sweep / 12.0f);
        float sweepSamples = sweepTime * sr;
        if (sweepSamples < 1.0f) sweepSamples = 1.0f;
        s.currentFreq = freqStart;
        s.sweepRatio = powf(baseFreq / freqStart, 1.0f / sweepSamples);

        s.phase1 = 0.0f;
        s.phase2 = 0.0f;
        s.phaseFm = 0.0f;

        s.ampDecayCoeff   = expf(-1.0f / (effectiveDecay * sr));
        s.shapeDecayCoeff = expf(-1.0f / (effectiveDecay * 0.6f * sr));
        s.punchDecayCoeff = expf(-1.0f / (0.003f * sr));

        if (attack > 0.0001f)
        {
          s.ampEnv = 0.0f;
          s.shapeEnv = 0.0f;
          s.attackIncr = 1.0f / (attack * sr);
          s.envPhase = 1;
        }
        else
        {
          s.ampEnv = 1.0f;
          s.shapeEnv = 1.0f;
          s.attackIncr = 0.0f;
          s.envPhase = (hold > 0.0001f) ? 2 : 3;
        }

        s.holdCounter = (hold > 0.0001f) ? (int)(hold * sr) : 0;
        s.punchEnv = punch;
        s.vizGateState = true;
      }
      s.prevTrigger = trigVal;

      // Pitch sweep: converge to baseFreq
      if (s.currentFreq > s.baseFreq + 0.01f || s.currentFreq < s.baseFreq - 0.01f)
        s.currentFreq *= s.sweepRatio;
      else
        s.currentFreq = s.baseFreq;

      // Pitch droop: +1.5% at onset, decays with amplitude
      float droopFreq = s.currentFreq * (1.0f + s.ampEnv * 0.015f);
      if (s.punchEnv > 0.01f)
        droopFreq *= (1.0f + s.punchEnv * 0.05f);

      // === Oscillator section at 2x rate (anti-alias for fold + FM) ===
      //
      // Osc1 (carrier) + osc2 (Shape FM modulator) + phaseFm (metallic
      // inharmonic modulator at 2.71x ratio) all run at 2x sample rate so
      // their harmonic content and FM sidebands stay below the 2x Nyquist.
      // A 2-tap moving-average decimator (zero at 2x Nyquist) brings the
      // pair back down to sr. NEON can't usefully parallelize the serial
      // phase recurrence on a mono voice, so this is scalar x2.
      float sr2 = sr * 2.0f;
      float foldGain = 1.0f + (character > 0.5f ? (character - 0.5f) * 2.0f * 6.0f : 0.0f);
      float tMorph   = character < 0.5f ? character * 2.0f : 0.0f;
      // Shape FM ceiling raised 2.0 -> 6.0 so the fader reaches modulation
      // index ~6 at max -- 3x deeper than the prior tuning, hits hard FM
      // territory without needing extra range on the knob.
      float shapeFmDepth   = shape * shape * s.shapeEnv * 6.0f;
      if (grit > 0.5f)
        shapeFmDepth += (grit - 0.5f) * 2.0f * shape * 0.5f;

      // Grit taming: lower FM and noise-FM ceilings (3 -> 2 and 2000 -> 1000)
      // so high grit no longer blows the output up. The tail of the chain
      // also applies a gritGainComp to walk loudness back further.
      float metFmDepth     = grit * 2.0f * s.ampEnv;
      float gritNoiseFmDev = grit * grit * 1000.0f * s.ampEnv;
      if (character > 0.7f)
        gritNoiseFmDev += (character - 0.7f) * 3.3f * grit * 500.0f;

      float osSamp[2];
      for (int k = 0; k < 2; k++)
      {
        // Osc2 (Shape modulator): small detune, same character shaping
        // as osc1 so the FM source has matching timbre.
        float inc2 = droopFreq * (1.0f + shape * 0.0058f) / sr2;
        s.phase2 += inc2;
        s.phase2 -= floorf(s.phase2);
        float tri2 = 4.0f * (s.phase2 < 0.5f ? s.phase2 : 1.0f - s.phase2) - 1.0f;
        float shapeSample;
        if (character < 0.5f)
        {
          float sine2 = lookupSine(s.sLUT, tri2);
          shapeSample = tri2 + (sine2 - tri2) * tMorph;
        }
        else
        {
          shapeSample = lookupSine(s.sLUT, tri2 * foldGain);
        }

        // Metallic FM modulator: inharmonic (2.71x) sine osc. Clean sine
        // via LUT gives coherent 808-clang spectra when the grit-driven
        // modulation index climbs through ~1.5-3.
        float incFm = droopFreq * 2.71f / sr2;
        s.phaseFm += incFm;
        s.phaseFm -= floorf(s.phaseFm);
        float triFm = 4.0f * (s.phaseFm < 0.5f ? s.phaseFm : 1.0f - s.phaseFm) - 1.0f;
        float fmMod = lookupSine(s.sLUT, triFm);

        // Osc1 carrier: Shape FM + Metallic FM + broadband noise FM all
        // injected into the phase increment.
        float inc1 = droopFreq / sr2;
        inc1 += shapeSample * shapeFmDepth * droopFreq / sr2;
        inc1 += fmMod * metFmDepth * droopFreq / sr2;
        if (grit > 0.0f)
        {
          s.noiseState = s.noiseState * 1664525u + 1013904223u;
          float noise = (float)(int32_t)s.noiseState / 2147483648.0f;
          inc1 += noise * gritNoiseFmDev / sr2;
        }
        s.phase1 += inc1;
        s.phase1 -= floorf(s.phase1);
        float tri1 = 4.0f * (s.phase1 < 0.5f ? s.phase1 : 1.0f - s.phase1) - 1.0f;
        float toneSample;
        if (character < 0.5f)
        {
          float sine1 = lookupSine(s.sLUT, tri1);
          toneSample = tri1 + (sine1 - tri1) * tMorph;
        }
        else
        {
          toneSample = lookupSine(s.sLUT, tri1 * foldGain);
        }
        osSamp[k] = toneSample;
      }

      // 2-tap moving-average halfband decimator: zero at the 2x Nyquist,
      // good enough for drum-band content. Replace with a longer halfband
      // FIR later if the residual aliasing above sr/2 is audible.
      float sample = 0.5f * (osSamp[0] + osSamp[1]);

      // Direct noise mix: kicks in above grit = 0.4 and climbs toward
      // snare / hat territory at grit = 1.0. Tamed from 1.5x slope to 1.0x
      // peak (~0.6 max) so high grit doesn't dominate the output.
      float gritNoiseGain = (grit - 0.4f) * 1.0f * s.ampEnv;
      if (gritNoiseGain > 0.0f)
      {
        s.noiseState = s.noiseState * 1664525u + 1013904223u;
        float noiseSig = (float)(int32_t)s.noiseState / 2147483648.0f;
        sample += noiseSig * gritNoiseGain;
      }

      // Gain compensation for the combined Grit contributions (FM index +
      // metallic FM + noise mix). At grit = 1, drop the sample 30% so the
      // clipper doesn't immediately slam into saturation.
      sample *= (1.0f - grit * 0.3f);

      // Amp envelope state machine
      switch (s.envPhase)
      {
      case 1: // attack
        s.ampEnv   += s.attackIncr;
        s.shapeEnv += s.attackIncr;
        if (s.ampEnv >= 1.0f)
        {
          s.ampEnv   = 1.0f;
          s.shapeEnv = 1.0f;
          s.envPhase = (s.holdCounter > 0) ? 2 : 3;
        }
        break;
      case 2: // hold
        s.holdCounter--;
        if (s.holdCounter <= 0)
          s.envPhase = 3;
        break;
      case 3: // decay
        s.ampEnv   *= s.ampDecayCoeff;
        s.shapeEnv *= s.shapeDecayCoeff;
        if (s.ampEnv < 1e-5f)
        {
          // Hard reset of every internal envelope and downstream tail
          // state when the main amp envelope ends. Ensures nothing
          // (punch tail, SVF ringing, comp detector) sneaks past the
          // main amp env. Per the "amp env governs everything" rule.
          s.ampEnv = 0.0f;
          s.shapeEnv = 0.0f;
          s.punchEnv = 0.0f;
          s.envPhase = 0;
          s.vizGateState = false;
          s.ic1eq = 0.0f;
          s.ic2eq = 0.0f;
          s.compDetector = 0.0f;
        }
        break;
      default: // idle
        break;
      }

      sample *= s.ampEnv;

      // Punch
      sample *= (1.0f + s.punchEnv);
      s.punchEnv *= s.punchDecayCoeff;
      if (s.punchEnv < 1e-5f) s.punchEnv = 0.0f;

      // Clipper: simple tanh with gain compensation.
      if (clipperActive)
        sample = fast_tanh(sample * driveLinear) / driveNorm;

      // DJ filter (TPT SVF, Cytomic formulation)
      if (filterActive)
      {
        float v0 = sample;
        float v3 = v0 - s.ic2eq;
        float v1 = fA1 * s.ic1eq + fA2 * v3;
        float v2 = s.ic2eq + fA2 * s.ic1eq + fA3 * v3;
        s.ic1eq = 2.0f * v1 - s.ic1eq;
        s.ic2eq = 2.0f * v2 - s.ic2eq;
        float wet = isLP ? v2 : (v0 - fK * v1 - v2);
        sample = sample * (1.0f - absEqCut) + wet * absEqCut;
      }

      // CPR single-band one-knob comp (replaces Makeup). Auto makeup
      // built in. Bypassed when compAmt < 0.001.
      if (compActive)
      {
        float absLevel = sample < 0.0f ? -sample : sample;
        float coeff = absLevel > s.compDetector ? compRiseCoeff : compFallCoeff;
        s.compDetector = coeff * s.compDetector + (1.0f - coeff) * absLevel;
        float levelDb = 20.0f * fast_log10(s.compDetector + 1e-10f);
        float overDb = levelDb - compThresholdDb;
        if (overDb < 0.0f) overDb = 0.0f;
        float reductionDb = overDb * (1.0f - compRatioI);
        sample *= fast_fromDb(-reductionDb) * compMakeupGain;
      }

      out[i] = sample * level;
    }

    s.vizEnvLevel = s.ampEnv;
  }

} // namespace stolmine
