// Filterbank -- parallel resonant fixed filter bank for ER-301

#include "Filterbank.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

// Must define TEST to avoid stmlib ARM inline asm on Cortex-A8
#ifndef TEST
#define TEST
#endif
#include "stmlib/dsp/filter.h"   // only for stmlib::OnePole::tan; SVF state
                                  // is hand-rolled SoA here (NEON-friendly).

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

// File-level guard against the auto-vec init trap
// (feedback_neon_hint_surfaces): scalar init loops over our new SoA
// arrays would otherwise auto-vectorize into [reg :64] hints that trap
// on Cortex-A8. Hand-written NEON intrinsics in the hot loop are not
// gated by tree-vectorize, so this pragma costs them nothing.
#pragma GCC optimize("no-tree-vectorize")

namespace stolmine
{

  // --- Built-in scale tables (cents, 1200 = octave) ---

  struct ScaleData
  {
    const float *degrees;
    int count;
  };

  static const float kChromatic[] = {0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100};
  static const float kMajor[] = {0, 200, 400, 500, 700, 900, 1100};
  static const float kNaturalMinor[] = {0, 200, 300, 500, 700, 800, 1000};
  static const float kHarmonicMinor[] = {0, 200, 300, 500, 700, 800, 1100};
  static const float kMajorPent[] = {0, 200, 400, 700, 900};
  static const float kMinorPent[] = {0, 300, 500, 700, 1000};
  static const float kWholeTone[] = {0, 200, 400, 600, 800, 1000};
  static const float kDorian[] = {0, 200, 300, 500, 700, 900, 1000};
  static const float kPhrygian[] = {0, 100, 300, 500, 700, 800, 1000};
  static const float kLydian[] = {0, 200, 400, 600, 700, 900, 1100};
  static const float kMixolydian[] = {0, 200, 400, 500, 700, 900, 1000};
  static const float kLocrian[] = {0, 100, 300, 500, 600, 800, 1000};

  static const ScaleData kScales[SCALE_COUNT] = {
      {kChromatic, 12},
      {kMajor, 7},
      {kNaturalMinor, 7},
      {kHarmonicMinor, 7},
      {kMajorPent, 5},
      {kMinorPent, 5},
      {kWholeTone, 6},
      {kDorian, 7},
      {kPhrygian, 7},
      {kLydian, 7},
      {kMixolydian, 7},
      {kLocrian, 7},
  };

  // --- Internal struct ---

  struct Filterbank::Internal
  {
    float freqHz[kMaxBands];
    float gain[kMaxBands];
    int filterType[kMaxBands];

    // SoA SVF bank (mirror of mods/mi/rings/dsp/resonator.cc layout).
    // Coefficients are written block-rate from updateFilterCoefficients;
    // state evolves per sample inside the NEON kernel in process().
    float svfG[kMaxBands];        // tan(pi * f_normalized)
    float svfR[kMaxBands];        // 1 / Q
    float svfH[kMaxBands];        // 1 / (1 + r*g + g*g)
    float svfState1[kMaxBands];   // SVF s1 state
    float svfState2[kMaxBands];   // SVF s2 state
    // Branchless mode dispatch — bake at block-rate, apply per-sample
    // (per feedback_runtime_branched_dsp_dispatch — no switch per band
    // per sample). FTYPE_LP: bpGain=0, lpGain=gain[b]; PEAK/RESON:
    // bpGain=gain[b], lpGain=0 (PEAK and RESON are byte-identical
    // per-sample, differing only in Q-floor at coeff setup).
    float bpGain[kMaxBands];
    float lpGain[kMaxBands];
    float lpMask[kMaxBands];      // 1.0 if FTYPE_LP else 0.0 (energy path)

    float bandQValues[kMaxBands]; // stored for BPF gain compensation
    float bandEnergy[kMaxBands];  // per-band RMS energy follower

    float targetFreq[kMaxBands]; // normalized (Hz / sampleRate)
    float currentFreq[kMaxBands];

    // Candidate buffer for scale distribution (avoid stack allocation)
    float candidates[256];
    float logCandidates[256];
    bool used[256];
    float selected[kMaxBands];
    float selectedLog[kMaxBands];
    float rotated[kMaxBands];
    int candidateCount;

    // Custom scale slots (cents per degree)
    float customDegrees[kMaxCustomScales][kMaxScaleDegrees];
    int customDegreeCounts[kMaxCustomScales];
    int numCustomScales;
    float customBuildBuf[kMaxScaleDegrees]; // temp buffer for loading
    int customBuildCount;

    void Init()
    {
      float logMin = logf(100.0f);
      float logMax = logf(10000.0f);
      for (int i = 0; i < kMaxBands; i++)
      {
        float t = (float)i / (float)(kMaxBands - 1);
        float hz = expf(logMin + t * (logMax - logMin));
        freqHz[i] = hz;
        targetFreq[i] = hz / 48000.0f;
        currentFreq[i] = targetFreq[i];
        gain[i] = 1.0f;
        filterType[i] = FTYPE_PEAK;
      }
      // SoA SVF state and coefficients init. svfG=0 keeps the SVF state
      // permanently zero until updateFilterCoefficients writes real
      // coefficients (which it does once per process() call, before the
      // band loop runs). Padding bands beyond the runtime bandCount
      // keep g=0 so their state stays frozen at zero — no contribution
      // to the wet sum, no NaN risk.
      memset(svfG, 0, sizeof(svfG));
      memset(svfR, 0, sizeof(svfR));
      memset(svfH, 0, sizeof(svfH));
      memset(svfState1, 0, sizeof(svfState1));
      memset(svfState2, 0, sizeof(svfState2));
      memset(bpGain, 0, sizeof(bpGain));
      memset(lpGain, 0, sizeof(lpGain));
      memset(lpMask, 0, sizeof(lpMask));
      memset(bandEnergy, 0, sizeof(bandEnergy));
      candidateCount = 0;
      numCustomScales = 0;
      customBuildCount = 0;
      memset(customDegreeCounts, 0, sizeof(customDegreeCounts));
    }
  };

  Filterbank::Filterbank()
  {
    addInput(mIn);
    addOutput(mOut);
    addParameter(mMix);
    addParameter(mMacroQ);
    addParameter(mBandCount);
    addParameter(mScale);
    addParameter(mRotate);
    addParameter(mVOctOffset);
    addParameter(mSlew);
    addParameter(mInputLevel);
    addParameter(mOutputLevel);
    addParameter(mTanhAmt);
    addParameter(mEditFreq);
    addParameter(mEditGain);
    addParameter(mEditType);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Filterbank::~Filterbank()
  {
    delete mpInternal;
  }

  // --- Band data accessors ---

  float Filterbank::getBandFreq(int i)
  {
    return mpInternal->freqHz[CLAMP(0, kMaxBands - 1, i)];
  }

  void Filterbank::setBandFreq(int i, float hz)
  {
    i = CLAMP(0, kMaxBands - 1, i);
    hz = CLAMP(20.0f, 20000.0f, hz);
    mpInternal->freqHz[i] = hz;
    mpInternal->targetFreq[i] = hz / globalConfig.sampleRate;
    mpInternal->currentFreq[i] = mpInternal->targetFreq[i];
  }

  float Filterbank::getBandGain(int i)
  {
    return mpInternal->gain[CLAMP(0, kMaxBands - 1, i)];
  }

  void Filterbank::setBandGain(int i, float v)
  {
    mpInternal->gain[CLAMP(0, kMaxBands - 1, i)] = CLAMP(0.0f, 4.0f, v);
  }

  int Filterbank::getBandType(int i)
  {
    return mpInternal->filterType[CLAMP(0, kMaxBands - 1, i)];
  }

  void Filterbank::setBandType(int i, int v)
  {
    mpInternal->filterType[CLAMP(0, kMaxBands - 1, i)] = CLAMP(0, (int)FTYPE_COUNT - 1, v);
  }

  void Filterbank::loadBand(int i)
  {
    i = CLAMP(0, kMaxBands - 1, i);
    mLastLoadedBand = i;
    mEditFreq.hardSet(mpInternal->freqHz[i]);
    mEditGain.hardSet(mpInternal->gain[i]);
    mEditType.hardSet((float)mpInternal->filterType[i]);
  }

  void Filterbank::storeBand(int i)
  {
    i = CLAMP(0, kMaxBands - 1, i);
    float hz = CLAMP(20.0f, 20000.0f, mEditFreq.value());
    mpInternal->freqHz[i] = hz;
    mpInternal->targetFreq[i] = hz / globalConfig.sampleRate;
    mpInternal->currentFreq[i] = mpInternal->targetFreq[i];
    mpInternal->gain[i] = CLAMP(0.0f, 4.0f, mEditGain.value());
    mpInternal->filterType[i] = CLAMP(0, (int)FTYPE_COUNT - 1, (int)(mEditType.value() + 0.5f));
  }

  int Filterbank::getBandCount()
  {
    mCachedBandCount = CLAMP(2, kMaxBands, (int)(mBandCount.value() + 0.5f));
    return mCachedBandCount;
  }

  float Filterbank::getBandEnergy(int i)
  {
    return sqrtf(mpInternal->bandEnergy[CLAMP(0, kMaxBands - 1, i)]);
  }

  float Filterbank::getBandCurrentFreq(int i)
  {
    return mpInternal->currentFreq[CLAMP(0, kMaxBands - 1, i)] * globalConfig.sampleRate;
  }

  float Filterbank::getRotate()
  {
    return mRotate.value();
  }

  float Filterbank::getMacroQ()
  {
    return CLAMP(0.0f, 1.0f, mMacroQ.value());
  }

  // --- Scale distribution ---

  // --- Custom scale loading ---

  void Filterbank::beginCustomScale(int slot)
  {
    if (slot < 0 || slot >= kMaxCustomScales) return;
    mpInternal->customBuildCount = 0;
  }

  void Filterbank::addCustomDegree(float cents)
  {
    Internal &s = *mpInternal;
    if (s.customBuildCount < kMaxScaleDegrees)
    {
      s.customBuildBuf[s.customBuildCount++] = cents;
    }
  }

  void Filterbank::endCustomScale(int slot)
  {
    Internal &s = *mpInternal;
    if (slot < 0 || slot >= kMaxCustomScales) return;
    s.customDegreeCounts[slot] = s.customBuildCount;
    for (int i = 0; i < s.customBuildCount; i++)
      s.customDegrees[slot][i] = s.customBuildBuf[i];
    if (slot >= s.numCustomScales)
      s.numCustomScales = slot + 1;
    mLastScale = -1; // force dirty
  }

  int Filterbank::getCustomScaleCount()
  {
    return mpInternal->numCustomScales;
  }

  // --- Scale distribution ---

  void Filterbank::distributeFrequencies()
  {
    Internal &s = *mpInternal;
    int scaleIdx = CLAMP(0, (int)SCALE_CUSTOM + kMaxCustomScales - 1, (int)(mScale.value() + 0.5f));
    int bandCount = mCachedBandCount;
    int rotate = (int)(mRotate.value() + 0.5f);

    // Get scale degrees (built-in or custom slot)
    const float *degrees;
    int degreeCount;
    if (scaleIdx >= (int)SCALE_CUSTOM)
    {
      int slot = scaleIdx - (int)SCALE_CUSTOM;
      if (slot < s.numCustomScales && s.customDegreeCounts[slot] > 0)
      {
        degrees = s.customDegrees[slot];
        degreeCount = s.customDegreeCounts[slot];
      }
      else
      {
        return; // no valid custom scale in this slot
      }
    }
    else if (scaleIdx < (int)SCALE_CUSTOM)
    {
      degrees = kScales[scaleIdx].degrees;
      degreeCount = kScales[scaleIdx].count;
    }
    else
    {
      return; // no valid scale
    }

    float sr = globalConfig.sampleRate;

    // Generate all candidate frequencies across 60Hz-16kHz
    float minHz = 60.0f;
    float maxHz = 16000.0f;
    float baseHz = 16.3516f; // C0
    s.candidateCount = 0;

    for (int octave = 1; octave <= 10 && s.candidateCount < 255; octave++)
    {
      float octaveHz = baseHz * (float)(1 << octave);
      for (int d = 0; d < degreeCount && s.candidateCount < 255; d++)
      {
        float hz = octaveHz * powf(2.0f, degrees[d] / 1200.0f);
        if (hz >= minHz && hz <= maxHz)
        {
          s.candidates[s.candidateCount++] = hz;
        }
      }
    }

    if (s.candidateCount == 0 || bandCount == 0)
      return;

    // Convert to log for distance calculations
    for (int i = 0; i < s.candidateCount; i++)
      s.logCandidates[i] = logf(s.candidates[i]);

    // Greedy selection: maximize minimum log-distance
    memset(s.used, 0, sizeof(s.used));

    // Start near geometric center
    float logMin = logf(minHz);
    float logMax = logf(maxHz);
    float logCenter = (logMin + logMax) * 0.5f;

    int bestIdx = 0;
    float bestDist = 1e10f;
    for (int i = 0; i < s.candidateCount; i++)
    {
      float d = fabsf(s.logCandidates[i] - logCenter);
      if (d < bestDist)
      {
        bestDist = d;
        bestIdx = i;
      }
    }
    s.selected[0] = s.candidates[bestIdx];
    s.used[bestIdx] = true;

    int numSelected = 1;
    s.selectedLog[0] = s.logCandidates[bestIdx];

    // Pick remaining bands
    for (int n = 1; n < bandCount && n < s.candidateCount; n++)
    {
      int bestCandidate = -1;
      float bestMinDist = -1.0f;

      for (int i = 0; i < s.candidateCount; i++)
      {
        if (s.used[i])
          continue;
        float minDist = 1e10f;
        for (int j = 0; j < numSelected; j++)
        {
          float d = fabsf(s.logCandidates[i] - s.selectedLog[j]);
          if (d < minDist)
            minDist = d;
        }
        if (minDist > bestMinDist)
        {
          bestMinDist = minDist;
          bestCandidate = i;
        }
      }

      if (bestCandidate >= 0)
      {
        s.selected[numSelected] = s.candidates[bestCandidate];
        s.selectedLog[numSelected] = s.logCandidates[bestCandidate];
        s.used[bestCandidate] = true;
        numSelected++;
      }
    }

    // Sort ascending
    for (int i = 0; i < numSelected - 1; i++)
    {
      for (int j = i + 1; j < numSelected; j++)
      {
        if (s.selected[j] < s.selected[i])
        {
          float tmp = s.selected[i];
          s.selected[i] = s.selected[j];
          s.selected[j] = tmp;
        }
      }
    }

    // Apply rotate (circular shift)
    if (rotate != 0 && numSelected > 1)
    {
      int n = numSelected;
      for (int i = 0; i < n; i++)
      {
        int src = ((i + rotate) % n + n) % n;
        s.rotated[i] = s.selected[src];
      }
      memcpy(s.selected, s.rotated, sizeof(float) * n);
    }

    // Set target frequencies (slew handles transition)
    for (int i = 0; i < numSelected && i < bandCount; i++)
    {
      s.freqHz[i] = s.selected[i];
      s.targetFreq[i] = s.selected[i] / sr;
    }

    // Reload edit buffer so readouts stay in sync
    loadBand(mLastLoadedBand);
  }

  void Filterbank::checkDistributionDirty()
  {
    int scaleIdx = CLAMP(0, (int)SCALE_CUSTOM + kMaxCustomScales - 1, (int)(mScale.value() + 0.5f));
    int rotate = (int)(mRotate.value() + 0.5f);
    int bandCount = mCachedBandCount;

    if (scaleIdx != mLastScale || rotate != mLastRotate ||
        bandCount != mLastBandCount)
    {
      mLastScale = scaleIdx;
      mLastRotate = rotate;
      mLastBandCount = bandCount;
      distributeFrequencies();
    }
  }

  // --- Filter coefficient update ---

  void Filterbank::updateFilterCoefficients()
  {
    Internal &s = *mpInternal;
    int bandCount = mCachedBandCount;
    float macroQ = CLAMP(0.0f, 1.0f, mMacroQ.value());
    float slewTime = CLAMP(0.0f, 5.0f, mSlew.value());

    // Q: 1-100 range, quadratic scaling for fine control at low end
    float baseQ = 1.0f + 99.0f * macroQ * macroQ;
    float q_loss = macroQ * (2.0f - macroQ) * 0.85f + 0.15f;

    // Slew: direct seconds, exponential smoothing per frame
    float slewCoeff = 0.0f;
    if (slewTime > 0.001f)
    {
      float framesPerSec = globalConfig.sampleRate / (float)FRAMELENGTH;
      float slewFrames = slewTime * framesPerSec;
      slewCoeff = 1.0f - 1.0f / slewFrames;
      if (slewCoeff < 0.0f)
        slewCoeff = 0.0f;
    }

    // V/Oct offset: shift all band frequencies by the same musical interval
    float voctOffset = CLAMP(-2.0f, 2.0f, mVOctOffset.value());
    float freqMul = powf(2.0f, voctOffset);

    float q = baseQ;
    for (int i = 0; i < bandCount; i++)
    {
      float shiftedTarget = CLAMP(0.0001f, 0.49f, s.targetFreq[i] * freqMul);
      if (slewCoeff > 0.0f)
        s.currentFreq[i] += (shiftedTarget - s.currentFreq[i]) * (1.0f - slewCoeff);
      else
        s.currentFreq[i] = shiftedTarget;

      float freq = CLAMP(0.0001f, 0.49f, s.currentFreq[i]);
      // Q increases slightly with frequency for even-sounding resonance
      float bandQ = q * (0.5f + freq * 2.0f);
      if (bandQ < 0.5f) bandQ = 0.5f;
      // LP mode: moderate Q floor for audible resonance peak
      if (s.filterType[i] == FTYPE_LP && bandQ < 5.0f)
        bandQ = 5.0f;
      // Resonator mode: hard Q floor so bands always ring
      if (s.filterType[i] == FTYPE_RESON && bandQ < 20.0f)
        bandQ = 20.0f;
      s.bandQValues[i] = bandQ;
      // Inlined from stmlib::Svf::set_f_q<FREQUENCY_FAST> (filter.h:222-227).
      // Writing direct to SoA arrays — the per-sample NEON kernel reads
      // these as quads. (PEAK and RESON differ only in the bandQ floor
      // above; both emit BP from the per-sample SVF update — dispatch is
      // entirely encoded in the per-mode gain bake below.)
      const float g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(freq);
      const float r = 1.0f / bandQ;
      s.svfG[i] = g;
      s.svfR[i] = r;
      s.svfH[i] = 1.0f / (1.0f + r * g + g * g);

      // Branchless per-mode bake (no `switch` per band per sample).
      const bool isLP = (s.filterType[i] == FTYPE_LP);
      s.lpMask[i] = isLP ? 1.0f : 0.0f;
      s.bpGain[i] = isLP ? 0.0f : s.gain[i];
      s.lpGain[i] = isLP ? s.gain[i] : 0.0f;

      q *= q_loss;
    }

    // Zero out padding bands beyond the runtime bandCount, up to the
    // next multiple of 4 (the NEON kernel processes bands in quads).
    // g=0 freezes their SVF state at zero — no contribution to wet sum,
    // no NaN risk. bpGain/lpGain=0 also zero them from the output sum.
    // svfState1/svfState2 reset to 0 so a band that becomes active later
    // starts clean instead of resuming stale state.
    const int bandsPadded = (bandCount + 3) & ~3;
    for (int i = bandCount; i < bandsPadded; i++)
    {
      s.svfG[i] = 0.0f;
      s.svfR[i] = 1.0f;   // harmless any-finite value
      s.svfH[i] = 1.0f;   // harmless any-finite value
      s.svfState1[i] = 0.0f;
      s.svfState2[i] = 0.0f;
      s.bpGain[i] = 0.0f;
      s.lpGain[i] = 0.0f;
      s.lpMask[i] = 0.0f;
    }
  }

  // --- Evaluate composite response ---

  float Filterbank::evaluateResponse(float normalizedFreq)
  {
    Internal &s = *mpInternal;
    int bandCount = mCachedBandCount;

    float logMin = logf(20.0f);
    float logMax = logf(20000.0f);
    float hz = expf(logMin + normalizedFreq * (logMax - logMin));
    float w = hz / globalConfig.sampleRate;

    float macroQ = CLAMP(0.0f, 1.0f, mMacroQ.value());
    float baseQ = 1.0f + 99.0f * macroQ * macroQ;
    float q_loss = macroQ * (2.0f - macroQ) * 0.85f + 0.15f;

    float totalResponse = 0.0f;
    float q = baseQ;
    for (int i = 0; i < bandCount; i++)
    {
      float fc = s.currentFreq[i];
      if (fc < 0.0001f)
      {
        q *= q_loss;
        continue;
      }

      float bandQ = q * (0.5f + fc * 2.0f);
      if (bandQ < 0.5f) bandQ = 0.5f;
      float wSq = w * w;
      float fcSq = fc * fc;
      float diff = wSq - fcSq;
      float bw = w * fc / bandQ;
      float denom = diff * diff + bw * bw;
      float mag = (denom > 0.000001f) ? bw / sqrtf(denom) : 1.0f;

      totalResponse += mag * s.gain[i];
      q *= q_loss;
    }

    return totalResponse;
  }

  float Filterbank::evaluateResponseAtBand(int band)
  {
    Internal &s = *mpInternal;
    int bandCount = mCachedBandCount;
    band = CLAMP(0, bandCount - 1, band);

    float fc = s.currentFreq[band];
    if (fc < 0.0001f)
      return 0.0f;

    float w = fc; // already normalized (Hz/sampleRate)

    float macroQ = CLAMP(0.0f, 1.0f, mMacroQ.value());
    float baseQ = 1.0f + 99.0f * macroQ * macroQ;
    float q_loss = macroQ * (2.0f - macroQ) * 0.85f + 0.15f;

    float totalResponse = 0.0f;
    float q = baseQ;
    for (int i = 0; i < bandCount; i++)
    {
      float fci = s.currentFreq[i];
      if (fci < 0.0001f)
      {
        q *= q_loss;
        continue;
      }

      float bandQ = q * (0.5f + fci * 2.0f);
      if (bandQ < 0.5f) bandQ = 0.5f;
      float wSq = w * w;
      float fcSq = fci * fci;
      float diff = wSq - fcSq;
      float bw = w * fci / bandQ;
      float denom = diff * diff + bw * bw;
      float mag = (denom > 0.000001f) ? bw / sqrtf(denom) : 1.0f;

      totalResponse += mag * s.gain[i];
      q *= q_loss;
    }

    return totalResponse;
  }

  // --- Process ---

  void Filterbank::process()
  {
    Internal &s = *mpInternal;

    float *in = mIn.buffer();
    float *out = mOut.buffer();

    int bandCount = CLAMP(2, kMaxBands, (int)(mBandCount.value() + 0.5f));
    mCachedBandCount = bandCount;

    // Auto-detect scale/rotate/skew/bandCount changes and redistribute
    checkDistributionDirty();

    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float inputLevel = CLAMP(0.0f, 4.0f, mInputLevel.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float tanhAmt = CLAMP(0.0f, 1.0f, mTanhAmt.value());

    updateFilterCoefficients();

    // Normalize parallel sum by 1/sqrt(bandCount)
    float sumNorm = 1.0f / sqrtf((float)bandCount);

    // Band loop bound — padded to next multiple of 4 so the NEON kernel
    // never has a scalar tail (Rings-style: padding bands have
    // g=0, gain=0, state=0 so they're a no-op contribution).
    const int bandsPadded = (bandCount + 3) & ~3;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i] * inputLevel;
      float wet;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
      // NEON SVF bank — TPT update, 4 bands at a time. Transcribed from
      // mods/mi/rings/dsp/resonator.cc:107-142 (the canonical SVF NEON
      // kernel on Cortex-A8). Mode dispatch is encoded entirely in the
      // bpGain/lpGain bake at block-rate — no per-sample switch on
      // filterType[b] (per feedback_runtime_branched_dsp_dispatch).
      const float32x4_t inV     = vdupq_n_f32(x);
      const float32x4_t energyA = vdupq_n_f32(0.001f);
      float32x4_t wetV          = vdupq_n_f32(0.0f);

      for (int b = 0; b < bandsPadded; b += 4)
      {
        float32x4_t s1 = vld1q_f32(&s.svfState1[b]);
        float32x4_t s2 = vld1q_f32(&s.svfState2[b]);
        float32x4_t g  = vld1q_f32(&s.svfG[b]);
        float32x4_t r  = vld1q_f32(&s.svfR[b]);
        float32x4_t h  = vld1q_f32(&s.svfH[b]);

        // TPT SVF update — exact transcription of
        // stmlib::Svf::Process (filter.h:232-236):
        //   hp = (in - r*s1 - g*s1 - s2) * h
        //   bp = s1 + g*hp;  s1' = bp + g*hp
        //   lp = s2 + g*bp;  s2' = lp + g*bp
        float32x4_t rs1   = vmulq_f32(r, s1);
        float32x4_t gs1   = vmulq_f32(g, s1);
        float32x4_t inner = vsubq_f32(vsubq_f32(vsubq_f32(inV, rs1), gs1), s2);
        float32x4_t hp    = vmulq_f32(inner, h);
        float32x4_t bp    = vmlaq_f32(s1, g, hp);
        float32x4_t s1New = vmlaq_f32(bp, g, hp);
        float32x4_t lp    = vmlaq_f32(s2, g, bp);
        float32x4_t s2New = vmlaq_f32(lp, g, bp);
        vst1q_f32(&s.svfState1[b], s1New);
        vst1q_f32(&s.svfState2[b], s2New);

        // Output: wet += bp*bpGain + lp*lpGain (mode dispatch baked in)
        float32x4_t bpG = vld1q_f32(&s.bpGain[b]);
        float32x4_t lpG = vld1q_f32(&s.lpGain[b]);
        wetV = vmlaq_f32(wetV, bp, bpG);
        wetV = vmlaq_f32(wetV, lp, lpG);

        // Per-band energy follower (~20 ms @ 48 kHz):
        //   bandOut = bp + lpMask*(lp - bp)
        //   energy += (bandOut*bandOut - energy) * 0.001
        float32x4_t lpMaskV = vld1q_f32(&s.lpMask[b]);
        float32x4_t bandOut = vmlaq_f32(bp, lpMaskV, vsubq_f32(lp, bp));
        float32x4_t e       = vmulq_f32(bandOut, bandOut);
        float32x4_t en      = vld1q_f32(&s.bandEnergy[b]);
        en                  = vmlaq_f32(en, vsubq_f32(e, en), energyA);
        vst1q_f32(&s.bandEnergy[b], en);
      }

      // Horizontal sum of the 4-lane wet accumulator (vpadd cascade —
      // Cortex-A8 has no vaddvq_f32, that's A7+).
      float32x2_t pair = vadd_f32(vget_low_f32(wetV), vget_high_f32(wetV));
      wet = vget_lane_f32(vpadd_f32(pair, pair), 0);
#else
      // Scalar fallback (linux x86, macOS) — same SoA TPT math, no
      // intrinsics. Padding bands have g=0/gain=0/state=0 so they
      // contribute nothing and stay stable.
      wet = 0.0f;
      for (int b = 0; b < bandsPadded; b++)
      {
        const float rs1 = s.svfR[b] * s.svfState1[b];
        const float gs1 = s.svfG[b] * s.svfState1[b];
        const float hp  = (x - rs1 - gs1 - s.svfState2[b]) * s.svfH[b];
        const float bp  = s.svfState1[b] + s.svfG[b] * hp;
        s.svfState1[b]  = bp + s.svfG[b] * hp;
        const float lp  = s.svfState2[b] + s.svfG[b] * bp;
        s.svfState2[b]  = lp + s.svfG[b] * bp;
        wet += bp * s.bpGain[b] + lp * s.lpGain[b];
        const float bandOut = bp + s.lpMask[b] * (lp - bp);
        const float e = bandOut * bandOut;
        s.bandEnergy[b] += (e - s.bandEnergy[b]) * 0.001f;
      }
#endif

      wet *= sumNorm;

      float mixed = x * (1.0f - mix) + wet * mix;

      if (tanhAmt > 0.001f)
      {
        float drive = 1.0f + tanhAmt * 3.0f;
        mixed = mixed * (1.0f - tanhAmt) + tanhf(mixed * drive) * tanhAmt;
      }

      out[i] = mixed * outputLevel;
    }
  }

} // namespace stolmine
