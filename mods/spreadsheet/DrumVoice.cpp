#include "DrumVoice.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{

  static inline float tanhRational(float x)
  {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

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
    addParameter(mMakeup);
    addParameter(mOctave);
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

  void DrumVoice::process()
  {
    Internal &s = *mpInternal;
    float *trig = mTrigger.buffer();
    float *voct = mVOct.buffer();
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
    float decay     = CLAMP(0.01f, 5.0f, mDecay.value());
    float clipper   = CLAMP(0.0f, 1.0f, mClipper.value());
    float eq        = CLAMP(-1.0f, 1.0f, mEQ.value());
    float level     = CLAMP(0.0f, 1.0f, mLevel.value());
    float makeup    = CLAMP(0.0f, 1.0f, mMakeup.value());
    float makeupGain = powf(10.0f, makeup * 6.0f / 20.0f);

    s.cachedCharacter = character;
    s.cachedShape = shape;
    s.cachedGrit = grit;

    // V/Oct + octave offset: block-rate
    float octave = floorf(CLAMP(-4.0f, 4.0f, mOctave.value()) + 0.5f);
    float pitch = voct[0] + octave;
    float baseFreq = 110.0f * powf(2.0f, pitch);
    baseFreq = CLAMP(10.0f, sr * 0.49f, baseFreq);

    // Clipper drive (block-rate)
    float driveLinear = (clipper > 0.01f) ? powf(10.0f, clipper * 2.0f) : 0.0f;
    float driveNorm   = (driveLinear > 0.0f) ? tanhRational(driveLinear) : 1.0f;

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

      // Phase accumulate
      float inc1 = droopFreq / sr;
      float inc2 = droopFreq * (1.0f + shape * 0.0058f) / sr;

      // FM noise into phase increment
      if (grit > 0.0f)
      {
        s.noiseState = s.noiseState * 1664525u + 1013904223u;
        float noise = (float)(int32_t)s.noiseState / 2147483648.0f;
        float fmDev = grit * grit * 500.0f * s.ampEnv;
        inc1 += noise * fmDev / sr;
      }

      s.phase1 += inc1;
      s.phase1 -= floorf(s.phase1);
      s.phase2 += inc2;
      s.phase2 -= floorf(s.phase2);

      // Triangle generation
      float tri1 = 4.0f * (s.phase1 < 0.5f ? s.phase1 : 1.0f - s.phase1) - 1.0f;
      float tri2 = 4.0f * (s.phase2 < 0.5f ? s.phase2 : 1.0f - s.phase2) - 1.0f;

      // Character morph for tone osc
      float toneSample;
      if (character < 0.5f)
      {
        float t = character * 2.0f;
        float sine1 = lookupSine(s.sLUT, tri1);
        toneSample = tri1 + (sine1 - tri1) * t;
      }
      else
      {
        float foldGain = 1.0f + (character - 0.5f) * 2.0f * 3.0f;
        toneSample = lookupSine(s.sLUT, tri1 * foldGain);
      }

      // Shape osc with same character
      float shapeSample;
      if (character < 0.5f)
      {
        float t = character * 2.0f;
        float sine2 = lookupSine(s.sLUT, tri2);
        shapeSample = tri2 + (sine2 - tri2) * t;
      }
      else
      {
        float foldGain = 1.0f + (character - 0.5f) * 2.0f * 3.0f;
        shapeSample = lookupSine(s.sLUT, tri2 * foldGain);
      }

      // Mix tone + shape osc
      float sample = toneSample + shape * shapeSample * s.shapeEnv;

      // Direct noise mix into signal
      if (grit > 0.0f)
      {
        s.noiseState = s.noiseState * 1664525u + 1013904223u;
        float noiseSig = (float)(int32_t)s.noiseState / 2147483648.0f;
        sample += noiseSig * grit * s.ampEnv * 0.3f;
      }

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
          s.ampEnv = 0.0f;
          s.shapeEnv = 0.0f;
          s.envPhase = 0;
          s.vizGateState = false;
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

      // Clipper
      if (driveLinear > 0.0f)
        sample = tanhRational(sample * driveLinear) / driveNorm;

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

      out[i] = sample * level * makeupGain;
    }

    s.vizEnvLevel = s.ampEnv;
  }

} // namespace stolmine
