// Per-band AA + SVF vectorized to 4-lane NEON SoA on Cortex-A8. The
// waveshaper stays scalar per band (8 shaper types with per-band
// dispatch); AA filter + SVF + morph crossfade are identical math
// across bands and packed lane 0..2 with lane 3 zero-padded. Branchless
// morph mix via per-band-baked lpGain/bpGain/hpGain/useSvfMask.
//
// no-tree-vectorize per feedback_neon_hint_surfaces: keeps GCC from
// auto-vectorizing the SoA brace-init and emitting :64 hints that trap
// on Cortex-A8.
#pragma GCC optimize("no-tree-vectorize")

// FFT viz update period (in audio process() blocks). At 48k/128 = 375
// blocks/sec, 4 blocks = 94 Hz, 6 blocks = 62 Hz, 8 blocks = 47 Hz.
// Must stay above the 55 Hz viz framerate floor. To roll back, set to 4
// and restore peakDecay=0.92f, rmsSmooth=0.3f below.
// Constants below are calibrated for the chosen period:
//   peakDecay^(period/4) preserves the original 4-block decay rate.
//   1 - (1-rmsSmooth)^(period/4) preserves the original smoothing.
// For period=6: peakDecay = 0.92^1.5 ≈ 0.882, rmsSmooth = 1-0.7^1.5 ≈ 0.414
// For period=4: peakDecay = 0.92, rmsSmooth = 0.3
#define FFT_BLOCKS_PER_UPDATE 6
#define FFT_PEAK_DECAY 0.882f
#define FFT_RMS_SMOOTH 0.414f

#include "MultibandSaturator.h"
#include "util/neon_math.h"
#include "pffft.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>
#include <new>
#include <stdlib.h>

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace stolmine
{

  static inline float fast_tanh(float x)
  {
    if (x < -4.0f) return -1.0f;
    if (x >  4.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  // --- Waveshapers (stateless, ported from Discont) ---

  static inline float fold2(float x)
  {
    while (x > 1.0f) x = 2.0f - x;
    while (x < -1.0f) x = -2.0f - x;
    return x;
  }

  // Fast ln(1+x) approximation for companding
  static inline float fastLog1p(float x)
  {
    return x / (1.0f + x * 0.5f);
  }

  // Fast sine approximation (5th order, max error ~0.0002 on [-pi,pi])
  static inline float fast_sinf(float x)
  {
    // Range reduce to [-pi, pi]
    const float itp = 1.0f / 3.14159f;
    float n = floorf(x * itp + 0.5f);
    x -= n * 3.14159f;
    float x2 = x * x;
    float r = x * (1.0f + x2 * (-0.16605f + x2 * 0.00761f));
    return ((int)n & 1) ? -r : r;
  }

  // Fast log2 via IEEE 754 bit extraction + linear refinement (~4 cycles)
  static inline float fast_log2(float x)
  {
    union { float f; int32_t i; } v;
    v.f = x;
    float exp = (float)((v.i >> 23) - 127);
    v.i = (v.i & 0x7FFFFF) | (127 << 23); // mantissa as [1,2)
    float m = v.f;
    // Minimax quadratic on [1,2): max error ~0.01
    return exp + m * (m * -0.3333f + 2.0f) - 1.667f;
  }

  // Fast exp2 via IEEE 754 bit packing (~4 cycles)
  static inline float fast_exp2(float x)
  {
    float xi = floorf(x);
    float xf = x - xi;
    // Quadratic on [0,1): max error ~0.01
    float m = 1.0f + xf * (0.6602f + xf * 0.3398f);
    union { float f; int32_t i; } v;
    v.i = ((int32_t)xi + 127) << 23;
    return v.f * m;
  }

  static inline float applyShaper(float x, int type, float amount, float bias)
  {
    if (type == 0) return x; // Off -- passthrough

    // Bias shifts input asymmetrically, drive scales with amount
    float sig = (x + bias) * (1.0f + amount * 5.0f);
    float wet;
    switch (type)
    {
    default:
    case 1: // Tube -- asymmetric soft clip (even harmonics)
    {
      float pos = fast_tanh(sig * 1.5f);
      float neg = fast_tanh(sig * 0.8f);
      wet = (sig >= 0) ? pos : neg;
      break;
    }
    case 2: // Diode -- arctan with soft knee
    {
      wet = sig / (1.0f + fabsf(sig) + 0.2f * sig * sig);
      break;
    }
    case 3: // Tri Fold -- multi-pass wavefolder
    {
      float s = sig;
      for (int j = 0; j < 3; j++)
      {
        while (s > 1.0f) s = 2.0f - s;
        while (s < -1.0f) s = -2.0f - s;
        s *= 1.2f;
      }
      wet = s;
      break;
    }
    case 4: // Half Rect -- asymmetric soft clip (even harmonics)
      wet = (sig > 0.0f) ? fast_tanh(sig) : 0.0f;
      break;
    case 5: // Crush -- bit reduction with mu-law companding
    {
      float mu = 8.0f;
      float sign = (sig >= 0) ? 1.0f : -1.0f;
      float absig = fabsf(sig);
      float compressed = sign * fastLog1p(mu * absig) / fastLog1p(mu);
      float quantized = floorf(compressed * 8.0f + 0.5f) / 8.0f;
      float absQ = fabsf(quantized);
      wet = (quantized >= 0 ? 1.0f : -1.0f) * ((1.0f + mu * absQ) - 1.0f) / mu;
      break;
    }
    case 6: // Sine Fold -- tonal wavefolder (decoupled gain, ~1-2 folds max)
    {
      float depth = 1.0f + amount * 2.0f;
      float s = (x + bias) * depth;
      wet = fast_sinf(s * 3.14159f);
      break;
    }
    case 7: // Fractal -- iterated polynomial (decoupled gain, clamped to stable region)
    {
      float depth = 0.5f + amount * 1.5f;
      float s = (x + bias) * depth;
      if (s > 1.7f) s = 1.7f;
      if (s < -1.7f) s = -1.7f;
      for (int j = 0; j < 3; j++)
      {
        s = s - (s * s * s) / 3.0f;
        s *= 1.5f;
      }
      wet = s * 0.67f;
      break;
    }
    }
    // Blend: amount=0 -> dry, amount>0 -> progressively more wet
    return x + (wet - x) * CLAMP(0.0f, 1.0f, amount);
  }

  struct MultibandSaturator::Internal
  {
    // Tilt EQ state
    float tiltLpState = 0.0f;

    // Crossover (2 split points, LR4 4-pole each for 24dB/oct)
    float crossoverHz[2];
    float xoverState[2][4];  // [crossover][cascade stage]

    // Per-band post-shaper SVF filter (inline state, not stmlib::Svf).
    // Widened to 4 lanes for NEON SoA; lane 3 is padding kept at
    // neutral (g=0, h=1) so the SVF math produces zero contribution
    // from the padding lane.
    float svfState1[4] __attribute__((aligned(16)));
    float svfState2[4] __attribute__((aligned(16)));
    float svfG[4] __attribute__((aligned(16)));
    float svfR[4] __attribute__((aligned(16)));
    float svfH[4] __attribute__((aligned(16)));

    // Block-rate-baked morph crossfade gains + active mask + combined
    // band gain. All 4-lane SoA; the per-sample NEON kernel reads
    // these to build the branchless final output.
    float lpGain[4] __attribute__((aligned(16)));
    float bpGain[4] __attribute__((aligned(16)));
    float hpGain[4] __attribute__((aligned(16)));
    float useSvfMask[4] __attribute__((aligned(16)));      // 1.0 if morph>0.01, else 0
    float bandCombinedGain[4] __attribute__((aligned(16))); // bandLevel × !mute

    // Per-sample NEON scratch (heap, never stack — :64 hint safety per
    // feedback_neon_intrinsics_drumvoice).
    float bandSigScratch[4] __attribute__((aligned(16)));

    // Compressor state
    float compDetector = 0.0f;
    float scHpState = 0.0f;

    // Anti-alias lowpass state (~18kHz one-pole per band). Widened to 4.
    float aaState[4] __attribute__((aligned(16)));

    // DC blocker state (one-pole highpass ~5Hz)
    float dcState = 0.0f;

    // Per-band energy (for graphic)
    float bandEnergy[3];

    // FFT state
    PFFFT_Setup *fftSetup;
    float *fftIn;
    float *fftOut;
    float *fftWork;
    float hannWindow[256];
    float fftPeak[128];
    float fftRms[128];
    float ringBuf[256];
    int ringPos;
    int fftFrameCount;
    bool fftReady;

    void Init()
    {
      tiltLpState = 0.0f;
      crossoverHz[0] = 200.0f;
      crossoverHz[1] = 2000.0f;
      for (int i = 0; i < 2; i++)
        for (int j = 0; j < 4; j++)
          xoverState[i][j] = 0.0f;
      for (int i = 0; i < 4; i++)
      {
        svfState1[i] = 0.0f;
        svfState2[i] = 0.0f;
        svfG[i] = (i < 3) ? 0.1f : 0.0f;   // pad lane: g=0 freezes state
        svfR[i] = 1.0f;
        svfH[i] = (i < 3) ? 0.5f : 1.0f;
        lpGain[i] = 0.0f;
        bpGain[i] = 0.0f;
        hpGain[i] = 0.0f;
        useSvfMask[i] = 0.0f;
        bandCombinedGain[i] = 0.0f;
        aaState[i] = 0.0f;
      }
      compDetector = 0.0f;
      scHpState = 0.0f;
      dcState = 0.0f;
      for (int i = 0; i < 3; i++)
        bandEnergy[i] = 0.0f;

      // FFT (deferred init -- setup on first process() call)
      fftSetup = 0;
      fftIn = 0;
      fftOut = 0;
      fftWork = 0;
      fftReady = false;
      memset(fftPeak, 0, sizeof(fftPeak));
      memset(fftRms, 0, sizeof(fftRms));
      memset(ringBuf, 0, sizeof(ringBuf));
      ringPos = 0;
      fftFrameCount = 0;
    }

    void Cleanup()
    {
      if (fftSetup) { pffft_destroy_setup(fftSetup); fftSetup = 0; }
      if (fftIn) { pffft_aligned_free(fftIn); fftIn = 0; }
      if (fftOut) { pffft_aligned_free(fftOut); fftOut = 0; }
      if (fftWork) { pffft_aligned_free(fftWork); fftWork = 0; }
    }
  };

  MultibandSaturator::MultibandSaturator()
  {
    addInput(mIn);
    addOutput(mOut);

    // Global
    addParameter(mDrive);
    addParameter(mToneAmount);
    addParameter(mToneFreq);
    addParameter(mSkew);
    addParameter(mMix);
    addParameter(mOutputLevel);
    addParameter(mCompressAmt);
    addParameter(mTanhAmt);
    addParameter(mScHpf);

    // Per-band
    addParameter(mBandLevel0);
    addParameter(mBandLevel1);
    addParameter(mBandLevel2);
    addParameter(mBandAmount0);
    addParameter(mBandAmount1);
    addParameter(mBandAmount2);
    addParameter(mBandBias0);
    addParameter(mBandBias1);
    addParameter(mBandBias2);
    addParameter(mBandType0);
    addParameter(mBandType1);
    addParameter(mBandType2);
    addParameter(mBandWeight0);
    addParameter(mBandWeight1);
    addParameter(mBandWeight2);
    addParameter(mBandFilterFreq0);
    addParameter(mBandFilterFreq1);
    addParameter(mBandFilterFreq2);
    addParameter(mBandFilterMorph0);
    addParameter(mBandFilterMorph1);
    addParameter(mBandFilterMorph2);
    addParameter(mBandFilterQ0);
    addParameter(mBandFilterQ1);
    addParameter(mBandFilterQ2);
    addParameter(mBandMute0);
    addParameter(mBandMute1);
    addParameter(mBandMute2);
    mBandMute0.enableSerialization();
    mBandMute1.enableSerialization();
    mBandMute2.enableSerialization();

    mpInternal = new Internal();
    mpInternal->Init();

    for (int i = 0; i < 3; i++)
      mLastWeight[i] = -1.0f;
    mLastSkew = -1.0f;
    for (int b = 0; b < 3; b++)
    {
      for (int p = 0; p < kBiasCount; p++)
        mBandBias[b][p] = 0;
      mBandLevelBias[b] = 0;
    }
  }

  MultibandSaturator::~MultibandSaturator()
  {
    mpInternal->Cleanup();
    delete mpInternal;
  }

  // --- Crossover frequency derivation (from Etcher weight/skew pattern) ---

  void MultibandSaturator::recomputeCrossovers()
  {
    Internal &s = *mpInternal;
    float w0 = CLAMP(0.1f, 4.0f, mBandWeight0.value());
    float w1 = CLAMP(0.1f, 4.0f, mBandWeight1.value());
    float w2 = CLAMP(0.1f, 4.0f, mBandWeight2.value());
    float skewParam = CLAMP(-1.0f, 1.0f, mSkew.value());

    float total = w0 + w1 + w2;
    float accum0 = w0 / total;
    float accum1 = (w0 + w1) / total;

    // Symmetric skew: shift boundaries in log-freq space by equal octaves
    // in each direction, capped by distance to nearer edge of [0,1].
    // Positive skew bunches crossovers low, negative bunches high.
    float f0 = (accum0 < 1.0f - accum0) ? accum0 : 1.0f - accum0;
    float f1 = (accum1 < 1.0f - accum1) ? accum1 : 1.0f - accum1;
    float b0 = accum0 - skewParam * f0;
    float b1 = accum1 - skewParam * f1;

    // Map 0-1 boundary to 20-20000 Hz (log scale)
    s.crossoverHz[0] = 20.0f * powf(1000.0f, b0);
    s.crossoverHz[1] = 20.0f * powf(1000.0f, b1);

    // Clamp to safe range
    float sr = globalConfig.sampleRate;
    s.crossoverHz[0] = CLAMP(30.0f, sr * 0.33f, s.crossoverHz[0]);
    s.crossoverHz[1] = CLAMP(s.crossoverHz[0] + 10.0f, sr * 0.33f, s.crossoverHz[1]);

    mLastWeight[0] = w0;
    mLastWeight[1] = w1;
    mLastWeight[2] = w2;
    mLastSkew = skewParam;
  }

  float MultibandSaturator::getCrossoverFreq(int band)
  {
    if (band < 0 || band > 1) return 0.0f;
    return mpInternal->crossoverHz[band];
  }

  void MultibandSaturator::setBandLevelBias(int band, od::Parameter *p)
  {
    if (band >= 0 && band < 3)
      mBandLevelBias[band] = p;
  }

  void MultibandSaturator::setBandBias(int band, int param, od::Parameter *p)
  {
    if (band >= 0 && band < 3 && param >= 0 && param < kBiasCount)
      mBandBias[band][param] = p;
  }

  float MultibandSaturator::getBandEnergy(int band)
  {
    if (band < 0 || band > 2) return 0.0f;
    return mpInternal->bandEnergy[band];
  }

  float MultibandSaturator::getFFTPeak(int bin)
  {
    if (bin < 0 || bin > 127) return 0.0f;
    return mpInternal->fftPeak[bin];
  }

  float MultibandSaturator::getFFTRms(int bin)
  {
    if (bin < 0 || bin > 127) return 0.0f;
    return mpInternal->fftRms[bin];
  }

  float MultibandSaturator::getBandLevel(int band)
  {
    if (band < 0 || band > 2) return 1.0f;
    // Read from Bias ref if available (tied params may not be scheduled)
    if (mBandLevelBias[band])
      return mBandLevelBias[band]->value();
    switch (band)
    {
    case 0: return mBandLevel0.value();
    case 1: return mBandLevel1.value();
    case 2: return mBandLevel2.value();
    }
    return 1.0f;
  }

  int MultibandSaturator::getCrossoverBin(int band)
  {
    if (band < 0 || band > 1) return 0;
    float sr = globalConfig.sampleRate;
    return (int)(mpInternal->crossoverHz[band] / (sr / 256.0f));
  }

  bool MultibandSaturator::getBandMuted(int band)
  {
    if (band < 0 || band > 2) return false;
    switch (band)
    {
    case 0: return mBandMute0.value() > 0.5f;
    case 1: return mBandMute1.value() > 0.5f;
    case 2: return mBandMute2.value() > 0.5f;
    }
    return false;
  }

  // --- Process ---

  void MultibandSaturator::process()
  {
    Internal &s = *mpInternal;
    float *in = mIn.buffer();
    float *out = mOut.buffer();

    float drive = CLAMP(0.0f, 16.0f, mDrive.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float tanhAmt = CLAMP(0.0f, 1.0f, mTanhAmt.value());
    float toneAmt = CLAMP(-1.0f, 1.0f, mToneAmount.value());
    float toneFreq = CLAMP(50.0f, 5000.0f, mToneFreq.value());
    float bandLevel[3] = {
      CLAMP(0.0f, 2.0f, mBandLevel0.value()),
      CLAMP(0.0f, 2.0f, mBandLevel1.value()),
      CLAMP(0.0f, 2.0f, mBandLevel2.value())
    };
    bool bandMute[3] = {
      mBandMute0.value() > 0.5f,
      mBandMute1.value() > 0.5f,
      mBandMute2.value() > 0.5f
    };
    // Read per-band params from Bias refs (direct from UI, bypasses unscheduled adapters)
    float bandAmount[3], bandBiasVal[3], bandFilterFreq[3], bandFilterMorph[3], bandFilterQ[3];
    int bandType[3];
    for (int b = 0; b < 3; b++)
    {
      bandAmount[b] = mBandBias[b][0] ? CLAMP(0.0f, 1.0f, mBandBias[b][0]->value()) : 0.5f;
      bandBiasVal[b] = mBandBias[b][1] ? CLAMP(-1.0f, 1.0f, mBandBias[b][1]->value()) : 0.0f;
      bandType[b] = mBandBias[b][2] ? CLAMP(0, 7, (int)(mBandBias[b][2]->value() + 0.5f)) : 0;
      bandFilterFreq[b] = mBandBias[b][4] ? CLAMP(20.0f, 20000.0f, mBandBias[b][4]->value()) : 1000.0f;
      bandFilterMorph[b] = mBandBias[b][5] ? CLAMP(0.0f, 1.0f, mBandBias[b][5]->value()) : 0.0f;
      bandFilterQ[b] = mBandBias[b][6] ? CLAMP(0.5f, 20.0f, mBandBias[b][6]->value()) : 0.5f;
    }

    // Dirty-check crossovers (read weights from Bias refs)
    float w0 = mBandBias[0][3] ? mBandBias[0][3]->value() : mBandWeight0.value();
    float w1 = mBandBias[1][3] ? mBandBias[1][3]->value() : mBandWeight1.value();
    float w2 = mBandBias[2][3] ? mBandBias[2][3]->value() : mBandWeight2.value();
    float skew = mSkew.value();
    if (w0 != mLastWeight[0] || w1 != mLastWeight[1] || w2 != mLastWeight[2] || skew != mLastSkew)
      recomputeCrossovers();

    float sr = globalConfig.sampleRate;

    // Tilt EQ coefficients
    float tiltCoeff = 1.0f / (1.0f + 1.0f / (2.0f * 3.14159f * toneFreq / sr));
    float tiltGain = powf(10.0f, toneAmt * 0.3f);
    float tiltLGain = 1.0f / tiltGain;

    // Crossover coefficients (LR4 = four cascaded one-pole filters, 24dB/oct)
    float xCoeff[2];
    for (int c = 0; c < 2; c++)
    {
      float fc = s.crossoverHz[c] / sr;
      xCoeff[c] = 1.0f / (1.0f + 1.0f / (2.0f * 3.14159f * fc));
    }

    // Set per-band SVF coefficients (ZDF topology), bake morph
    // crossfade gains + useSvfMask + bandCombinedGain into SoA arrays
    // so the per-sample NEON kernel can vld1q_f32 them.
    for (int b = 0; b < 3; b++)
    {
      // Avoid tanf (may not be available on ARM ELF loader)
      float fNorm = 3.14159f * bandFilterFreq[b] / sr;
      float sinVal = sinf(fNorm);
      float cosVal = cosf(fNorm);
      float g = (cosVal > 1e-10f) ? sinVal / cosVal : 100.0f;
      float r = 1.0f / bandFilterQ[b];
      s.svfG[b] = g;
      s.svfR[b] = r;
      s.svfH[b] = 1.0f / (1.0f + r * g + g * g);

      // Morph crossfade — same segmented mapping as the original
      // inner-loop branches, hoisted to block rate.
      float morph = bandFilterMorph[b];
      if (morph > 0.01f)
      {
        s.useSvfMask[b] = 1.0f;
        float m = (morph - 0.01f) / 0.99f;
        if (m < 0.333f)
        {
          float t = m * 3.0f;
          s.lpGain[b] = 1.0f - t; s.bpGain[b] = t; s.hpGain[b] = 0.0f;
        }
        else if (m < 0.666f)
        {
          float t = (m - 0.333f) * 3.0f;
          s.lpGain[b] = 0.0f; s.bpGain[b] = 1.0f - t; s.hpGain[b] = t;
        }
        else
        {
          float t = (m - 0.666f) * 3.0f;
          s.lpGain[b] = t; s.bpGain[b] = 0.0f; s.hpGain[b] = 1.0f;
        }
      }
      else
      {
        s.useSvfMask[b] = 0.0f;
        s.lpGain[b] = s.bpGain[b] = s.hpGain[b] = 0.0f;
      }

      // Combined band gain: bandLevel × !mute. Muted bands contribute
      // zero to the output sum and zero to the energy meter.
      s.bandCombinedGain[b] = bandMute[b] ? 0.0f : bandLevel[b];
    }
    // Lane 3 padding: neutral SVF (g=0 freezes state), zero gains, zero out
    s.svfG[3] = 0.0f;
    s.svfR[3] = 1.0f;
    s.svfH[3] = 1.0f;
    s.lpGain[3] = s.bpGain[3] = s.hpGain[3] = 0.0f;
    s.useSvfMask[3] = 0.0f;
    s.bandCombinedGain[3] = 0.0f;

    // Fast-path flag: any band with SVF morph engaged? When false the
    // SVF math is pure waste (final = aa + 0 * (svfMix - aa) = aa). Skip
    // the SVF kernel entirely and pass the AA output through. SVF state
    // is held frozen on the fast path; flushed on the edge so re-engage
    // starts clean.
    bool anySvfActive = (s.useSvfMask[0] + s.useSvfMask[1] + s.useSvfMask[2]) > 0.0f;
    if (anySvfActive && !mPrevSvfActive)
    {
      // Edge: SVF re-engaging. Zero state to avoid stale-state transient.
      for (int i = 0; i < 4; i++)
      {
        s.svfState1[i] = 0.0f;
        s.svfState2[i] = 0.0f;
      }
    }
    mPrevSvfActive = anySvfActive;

    // Compressor constants (hoisted out of sample loop)
    // Anti-alias lowpass coefficient (~18kHz)
    float aaCoeff = 18000.0f / sr;
    if (aaCoeff > 1.0f) aaCoeff = 1.0f;

    float compAmt = CLAMP(0.0f, 1.0f, mCompressAmt.value());
    bool compActive = compAmt > 0.001f;
    bool scHpEnabled = mScHpf.value() > 0.5f;
    float compAttack = 1.0f - expf(-1.0f / (0.001f * sr));
    float compRelease = 1.0f - expf(-1.0f / (0.1f * sr));
    float compThreshold = 1.0f - compAmt * 0.8f;
    float compThreshDb = 10.0f * log10f(compThreshold * compThreshold + 1e-20f);
    float compRatioFactor = 1.0f - 1.0f / (1.0f + compAmt * 7.0f);
    float scHpCoeff = 100.0f / sr;

    float energyAccum[3] = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float dry = in[i];
      float x = dry * drive;

      // Tilt EQ
      s.tiltLpState += (x - s.tiltLpState) * tiltCoeff;
      x = s.tiltLpState * tiltLGain + (x - s.tiltLpState) * tiltGain;

      // Band split: LR4 (4 cascaded one-pole) crossovers, 24dB/oct
      // Crossover 0: x -> lp0 (band0), hp0 (remainder)
      s.xoverState[0][0] += (x - s.xoverState[0][0]) * xCoeff[0];
      s.xoverState[0][1] += (s.xoverState[0][0] - s.xoverState[0][1]) * xCoeff[0];
      s.xoverState[0][2] += (s.xoverState[0][1] - s.xoverState[0][2]) * xCoeff[0];
      s.xoverState[0][3] += (s.xoverState[0][2] - s.xoverState[0][3]) * xCoeff[0];
      float band0 = s.xoverState[0][3];
      float hp0 = x - band0;

      // Crossover 1: hp0 -> lp1 (band1), hp1 (band2)
      s.xoverState[1][0] += (hp0 - s.xoverState[1][0]) * xCoeff[1];
      s.xoverState[1][1] += (s.xoverState[1][0] - s.xoverState[1][1]) * xCoeff[1];
      s.xoverState[1][2] += (s.xoverState[1][1] - s.xoverState[1][2]) * xCoeff[1];
      s.xoverState[1][3] += (s.xoverState[1][2] - s.xoverState[1][3]) * xCoeff[1];
      float band1 = s.xoverState[1][3];
      float band2 = hp0 - band1;

      // Per-band waveshaper (scalar — per-band shaper-type dispatch).
      // Skip muted bands to avoid the cost; AA + SVF still run on
      // muted bands but their output is zeroed by bandCombinedGain=0.
      // Scratch is heap-allocated in Internal — NOT stack-local — to
      // dodge :64 hint trap per feedback_neon_intrinsics_drumvoice.
      s.bandSigScratch[0] = band0;
      s.bandSigScratch[1] = band1;
      s.bandSigScratch[2] = band2;
      s.bandSigScratch[3] = 0.0f;
      for (int b = 0; b < 3; b++)
      {
        if (bandMute[b]) continue;
        if (bandAmount[b] > 0.001f)
          s.bandSigScratch[b] = applyShaper(s.bandSigScratch[b], bandType[b], bandAmount[b], bandBiasVal[b]);
      }

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
      // NEON 4-lane: AA filter (always-on for anti-aliasing) then
      // optional SVF kernel. Lane 3 is padding (g=0 freezes state).
      float32x4_t bsQ = vld1q_f32(s.bandSigScratch);

      // AA one-pole LP: state += (in - state) * aaCoeff
      float32x4_t aaStateQ = vld1q_f32(s.aaState);
      aaStateQ = vmlaq_f32(aaStateQ,
                           vsubq_f32(bsQ, aaStateQ),
                           vdupq_n_f32(aaCoeff));
      vst1q_f32(s.aaState, aaStateQ);

      float32x4_t finalQ;
      if (anySvfActive)
      {
        // SVF TPT: 4 filters in parallel.
        float32x4_t gQ  = vld1q_f32(s.svfG);
        float32x4_t rQ  = vld1q_f32(s.svfR);
        float32x4_t hQ  = vld1q_f32(s.svfH);
        float32x4_t s1Q = vld1q_f32(s.svfState1);
        float32x4_t s2Q = vld1q_f32(s.svfState2);
        // hp = (in - r·s1 - g·s1 - s2) · h
        float32x4_t hpQ = vmulq_f32(
            vsubq_f32(
                vsubq_f32(
                    vsubq_f32(aaStateQ, vmulq_f32(rQ, s1Q)),
                    vmulq_f32(gQ, s1Q)),
                s2Q),
            hQ);
        // bp = s1 + g·hp;  s1' = bp + g·hp
        float32x4_t bpQ     = vmlaq_f32(s1Q, gQ, hpQ);
        float32x4_t newS1Q  = vmlaq_f32(bpQ, gQ, hpQ);
        // lp = s2 + g·bp;  s2' = lp + g·bp
        float32x4_t lpQ     = vmlaq_f32(s2Q, gQ, bpQ);
        float32x4_t newS2Q  = vmlaq_f32(lpQ, gQ, bpQ);
        vst1q_f32(s.svfState1, newS1Q);
        vst1q_f32(s.svfState2, newS2Q);

        // Mix lp/bp/hp via block-rate-baked per-band gains
        float32x4_t lpGQ = vld1q_f32(s.lpGain);
        float32x4_t bpGQ = vld1q_f32(s.bpGain);
        float32x4_t hpGQ = vld1q_f32(s.hpGain);
        float32x4_t svfMixQ = vmlaq_f32(
            vmlaq_f32(vmulq_f32(lpQ, lpGQ), bpQ, bpGQ),
            hpQ, hpGQ);

        // Branchless morph select: final = aa + useSvfMask · (svfMix - aa)
        float32x4_t maskQ = vld1q_f32(s.useSvfMask);
        finalQ = vmlaq_f32(aaStateQ, maskQ, vsubq_f32(svfMixQ, aaStateQ));
      }
      else
      {
        // No band morphs the SVF — skip the entire SVF kernel.
        // Output is just the AA-filtered signal.
        finalQ = aaStateQ;
      }

      // Apply combined gain (bandLevel × !mute)
      float32x4_t cgQ = vld1q_f32(s.bandCombinedGain);
      finalQ = vmulq_f32(finalQ, cgQ);
      vst1q_f32(s.bandSigScratch, finalQ);
#else
      // Scalar fallback (linux x86, darwin). Mirrors the NEON dispatch:
      // AA always runs; SVF only when anySvfActive.
      for (int b = 0; b < 4; b++)
      {
        s.aaState[b] += (s.bandSigScratch[b] - s.aaState[b]) * aaCoeff;
        float aaOut = s.aaState[b];
        float fin;
        if (anySvfActive)
        {
          float g = s.svfG[b], r = s.svfR[b], h = s.svfH[b];
          float hp = (aaOut - r * s.svfState1[b] - g * s.svfState1[b] - s.svfState2[b]) * h;
          float bp = s.svfState1[b] + g * hp;
          s.svfState1[b] = bp + g * hp;
          float lp = s.svfState2[b] + g * bp;
          s.svfState2[b] = lp + g * bp;

          float svfMix = lp * s.lpGain[b] + bp * s.bpGain[b] + hp * s.hpGain[b];
          fin = aaOut + s.useSvfMask[b] * (svfMix - aaOut);
        }
        else
        {
          fin = aaOut;
        }
        s.bandSigScratch[b] = fin * s.bandCombinedGain[b];
      }
#endif

      // Sum lanes 0..2 (lane 3 is bandCombinedGain=0 zeroed)
      float wet = s.bandSigScratch[0] + s.bandSigScratch[1] + s.bandSigScratch[2];
      energyAccum[0] += s.bandSigScratch[0] * s.bandSigScratch[0];
      energyAccum[1] += s.bandSigScratch[1] * s.bandSigScratch[1];
      energyAccum[2] += s.bandSigScratch[2] * s.bandSigScratch[2];

      // DC blocker (~5Hz one-pole highpass)
      s.dcState += (wet - s.dcState) * (5.0f / sr);
      wet -= s.dcState;

      // Safety limiter (~1.5x soft clip -- transparent at normal levels)
      if (wet > 1.5f || wet < -1.5f)
        wet = fast_tanh(wet * 0.67f) * 1.5f;

      // FFT ring buffer (capture post-sum signal)
      s.ringBuf[s.ringPos] = wet;
      s.ringPos = (s.ringPos + 1) & 255;

      // Compressor
      if (compActive)
      {
        float sc = wet;
        if (scHpEnabled)
        {
          sc = wet - s.scHpState;
          s.scHpState += sc * scHpCoeff;
        }
        float energy = sc * sc;
        if (energy > s.compDetector)
          s.compDetector += (energy - s.compDetector) * compAttack;
        else
          s.compDetector += (energy - s.compDetector) * compRelease;

        float levelDb = 3.0103f * fast_log2(s.compDetector + 1e-20f);
        float overDb = levelDb - compThreshDb;
        if (overDb > 0.0f)
        {
          float gainDb = overDb * compRatioFactor;
          wet *= fast_exp2(-gainDb * 0.16609f); // 0.05 * log2(10) = 0.16609
        }
      }

      // Output saturation
      if (tanhAmt > 0.001f)
      {
        float d = 1.0f + tanhAmt * 3.0f;
        wet = wet * (1.0f - tanhAmt) + fast_tanh(wet * d) * tanhAmt;
      }

      wet *= outputLevel;

      // Dry/wet mix
      out[i] = dry + (wet - dry) * mix;
    }

    // Update band energy (smoothed)
    float energyCoeff = 0.1f;
    for (int b = 0; b < 3; b++)
    {
      float rms = sqrtf(energyAccum[b] / (float)FRAMELENGTH);
      s.bandEnergy[b] += (rms - s.bandEnergy[b]) * energyCoeff;
    }

    // Lazy FFT init (deferred from constructor)
    if (!s.fftReady)
    {
      s.fftSetup = pffft_new_setup(256, PFFFT_REAL);
      s.fftIn = (float *)pffft_aligned_malloc(256 * sizeof(float));
      s.fftOut = (float *)pffft_aligned_malloc(256 * sizeof(float));
      s.fftWork = (float *)pffft_aligned_malloc(256 * sizeof(float));
      if (s.fftSetup && s.fftIn && s.fftOut && s.fftWork)
      {
        for (int k = 0; k < 256; k++)
          s.hannWindow[k] = 0.5f * (1.0f - cosf(2.0f * 3.14159f * (float)k / 255.0f));
        s.fftReady = true;
      }
    }

    // FFT viz update — see FFT_BLOCKS_PER_UPDATE at file top for tuning.
    s.fftFrameCount++;
    if (s.fftFrameCount >= FFT_BLOCKS_PER_UPDATE && s.fftReady)
    {
      s.fftFrameCount = 0;
      for (int k = 0; k < 256; k++)
        s.fftIn[k] = s.ringBuf[(s.ringPos + k) & 255] * s.hannWindow[k];

      pffft_transform_ordered(s.fftSetup, s.fftIn, s.fftOut, s.fftWork, PFFFT_FORWARD);

      float peakDecay = FFT_PEAK_DECAY;
      float rmsSmooth = FFT_RMS_SMOOTH;
      for (int k = 0; k < 128; k++)
      {
        float re = s.fftOut[k * 2];
        float im = s.fftOut[k * 2 + 1];
        float mag = sqrtf(re * re + im * im) / 256.0f;
        if (mag > s.fftPeak[k])
          s.fftPeak[k] = mag;
        else
          s.fftPeak[k] *= peakDecay;
        s.fftRms[k] += (mag - s.fftRms[k]) * rmsSmooth;
      }
    }
  }

} // namespace stolmine
