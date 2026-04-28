// AlembicVoice -- native 4-op phase-mod matrix voice. See AlembicVoice.h
// for architecture notes. Phase 2a: scalar per-op inner loop; only NEON
// op per sample is a single simd_sin call over the packed phase args.

#include "AlembicVoice.h"
#include "pffft.h"
#include <od/config.h>
#include <hal/constants.h>
#include <hal/ops.h>
#include <hal/simd.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace stolmine
{

  static const float kTwoPi = 6.28318530718f;
  static const float kLn2 = 0.69314718056f;
  // ln(200) for cutoff exp-mapping: cutoff_Hz = 40 * exp(fc * 5.298)
  // gives a [0,1] -> 40..8000 Hz curve identical to Som's vp[0]
  // expressed in a single fastExp call.
  static const float kCutoffExpScale = 5.298317366f;

  // Phase 5d-2 filter helpers, lifted verbatim from
  // mods/catchall/Som.cpp:31-67. Pade 3/3 tan approximation for SVF
  // coefficient + integer/fractional split exp approximation.
  static inline float fastTan(float x)
  {
    const float x2 = x * x;
    return x * (1.0f + x2 * (1.0f / 3.0f + x2 * (2.0f / 15.0f))) /
               (1.0f + x2 * (-1.0f / 3.0f));
  }

  static inline float fastExp(float x)
  {
    x *= 1.4426950f; // log2(e)
    if (x >  16.0f) x =  16.0f;
    if (x < -16.0f) x = -16.0f;
    const float xf = floorf(x);
    const float fx = x - xf;
    // 2^fx polynomial on [0, 1]
    const float poly = 1.0f + fx * (0.6931472f + fx * (0.2402265f + fx * 0.0555041f));
    union { float f; int32_t i; } u;
    int ix = (int)xf + 127;
    if (ix < 1) ix = 1;
    if (ix > 254) ix = 254;
    u.i = ix << 23;
    return poly * u.f;
  }

  // Wide soft-clip from Som.cpp:40-47. Linear pass-through in (-8, +8),
  // tanh asymptote beyond. Preserves high-Q filter swing internally
  // while bounding integrator state so feedback can't run away.
  static inline float wideSoftClip(float x)
  {
    if (!isfinite(x)) return 0.0f;
    const float ax = x < 0.0f ? -x : x;
    if (ax < 8.0f) return x;
    const float sign = x > 0.0f ? 1.0f : -1.0f;
    return sign * (8.0f + tanhf((ax - 8.0f) * 0.5f) * 2.0f);
  }

  // Cheap soft-sat (-1, +1). Used on filter routing sources so the
  // routing matrix can't compound feedback into runaway FM.
  static inline float softSat1(float x)
  {
    const float ax = x < 0.0f ? -x : x;
    return x / (1.0f + ax);
  }
  // Matches libcore SineOscillator.cpp: feedback inlet value is clamped
  // to [-1,1] then multiplied by 0.18 before combining with the phase
  // argument. AlembicRef routes diagonal matrix entries via each op's
  // Feedback inlet (picking up this scale internally); AlembicVoice
  // applies the same 0.18 to diagonal terms in the consolidated matrix
  // sum so the two units produce identical self-feedback math.
  static const float kFbScale = 0.18f;

  // Phase 3 hand-authored preset gradient. Slot 0 is a clean sine on op
  // A; slot 63 is 4-op chaos with all matrix entries ~0.45 and op detunes
  // for beating. Linear interpolation across slots 1..62. Phase 5b's
  // analyzeSample() overwrites the table with sample-derived content;
  // detach reverts to this placeholder.
  //
  // Per-slot layout (29 floats):
  //   [0..3]   ratios
  //   [4..7]   levels
  //   [8..11]  detunes (Hz)
  //   [12..27] matrix[src*4 + dst], source-major
  //   [28]     reagent flag (Phase 7+; ignored in Phase 3)
  //
  // noinline + no-tree-vectorize: GCC otherwise auto-vectorizes the
  // 64x29 init loop and emits `:64`-aligned NEON ops on the .rodata
  // endA/endB plus the destination table, which trap on Cortex-A8 at
  // construction time (feedback_neon_hint_surfaces).
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void fillPhase3Presets(float (&t)[64][43])
  {
    // Phase 3 placeholder gradient covers the original 29 trained slots
    // (ratios, levels, detunes, matrix, scalar reagent which is now the
    // wavetable blend slot). Slots [29..42] (filter base + lane attens)
    // are zeroed -- when no sample is loaded the filter is bypassed
    // (cutoffs at 0 -> filter pass-through after 5d-2 lands; in 5d-1
    // there is no filter yet so these slots are inert).
    static const float endA[29] = {
        1.0f, 2.0f, 3.0f, 5.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f};
    static const float endB[29] = {
        1.0f, 2.0f, 3.0f, 5.0f,
        0.4f, 0.3f, 0.2f, 0.1f,
        0.0f, 2.0f, 4.0f, 6.0f,
        0.45f, 0.45f, 0.45f, 0.45f,
        0.45f, 0.45f, 0.45f, 0.45f,
        0.45f, 0.45f, 0.45f, 0.45f,
        0.45f, 0.45f, 0.45f, 0.45f,
        0.0f};
    for (int slot = 0; slot < 64; slot++)
    {
      const float u = (float)slot / 63.0f;
      for (int f = 0; f < 29; f++)
      {
        t[slot][f] = endA[f] + u * (endB[f] - endA[f]);
      }
      // Phase 5d-2 placeholder filter base (gentle near-bypass with no
      // sample loaded): cutoff ~6.5 kHz, Q low, parallel topology, LP
      // out only, unity drive. Trained content overwrites in
      // derivePresetRow.
      t[slot][29] = 0.85f;  // cutoff1 in [0,1] -> ~6500 Hz via exp map
      t[slot][30] = 0.85f;  // cutoff2
      t[slot][31] = 0.0f;   // Q in [0,1] -> 0.5 (gentle)
      t[slot][32] = 0.0f;   // topoMix: parallel
      t[slot][33] = 1.0f;   // bpLpBlend: LP only
      t[slot][34] = 0.2f;   // drive in [0,1] -> driveGain ~1.0 (unity)
      // Lane attens (5d-3) stay zero in placeholder.
      for (int f = 35; f < 43; f++) t[slot][f] = 0.0f;
    }
  }

  // Phase 5d-1: identity-curve wavetable LUT. Used when no sample is
  // loaded, or as a pathological fallback when a sample is too short.
  // row[28] = 0 makes process() crossfade toward sat(pmm), bypassing
  // the LUT entirely; the identity LUT is a safety net for the
  // LUT-blend path.
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void fillIdentityWavetableLUT(float (&lut)[64][256])
  {
    for (int n = 0; n < 64; n++)
      for (int k = 0; k < 256; k++)
        lut[n][k] = ((float)k / 255.0f) * 2.0f - 1.0f;
  }

  // Phase 5d-1.6: per-node transfer-function LUTs built from 256-sample
  // multi-cycle source windows at the trained picks. 256-entry LUT
  // resolution matches source size 1:1 (no downsampling).
  //
  // Construction:
  //   1. Read 256 source samples (sum-to-mono if stereo).
  //   2. DC-remove.
  //   3. Find absolute peak position k_peak.
  //   4. Pick per-node symmetry from features: tonal/structured
  //      (low entropy + flatness) -> ODD (classical S-curve, polarity-
  //      preserving, odd harmonics); chaotic/noisy -> EVEN (hill/valley,
  //      DC + even harmonics, more aggressive). Threshold sum > 1.0.
  //   5. Fold the longer half of the window around k_peak into the
  //      LUT's positive side (128 entries); fill the negative side
  //      mirrored (EVEN: same value) or mirrored+inverted (ODD).
  //   6. RMS-normalize (target 0.4, gain cap 8x) and soft-clip per
  //      entry to (-1, 1).
  //
  // Heap-only scratch: the 256-float window buffer is on the stack
  // (~1 KB, fine for am335x stack budget) and not NEON-targeted.
  // Destination LUT is a class member.
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void buildWavetableFrames(od::Sample *s, const int picks[64],
                                   int channels, const float *coarse,
                                   int kFeatDim, float (&lut)[64][256])
  {
    if (!s) { fillIdentityWavetableLUT(lut); return; }
    const int Ns = (int)s->mSampleCount;
    const int hop = (int)s->mSampleRate / 20;
    const bool stereo = (channels > 1);
    const int kSrcLen = 256;
    const int kHalfLut = 128;

    for (int n = 0; n < 64; n++)
    {
      int frameStart = picks[n] * hop;
      if (frameStart + kSrcLen > Ns) frameStart = Ns - kSrcLen;
      if (frameStart < 0)
      {
        for (int k = 0; k < 256; k++)
          lut[n][k] = ((float)k / 255.0f) * 2.0f - 1.0f;
        continue;
      }

      float buf[256];
      for (int k = 0; k < kSrcLen; k++)
      {
        float x = s->get(frameStart + k, 0);
        if (stereo) x = (x + s->get(frameStart + k, 1)) * 0.5f;
        buf[k] = x;
      }

      // DC-remove.
      float dc = 0.0f;
      for (int k = 0; k < kSrcLen; k++) dc += buf[k];
      dc *= (1.0f / (float)kSrcLen);
      for (int k = 0; k < kSrcLen; k++) buf[k] -= dc;

      // Find absolute peak position.
      int kPeak = 0;
      float peakAbs = 0.0f;
      for (int k = 0; k < kSrcLen; k++)
      {
        const float a = buf[k] < 0.0f ? -buf[k] : buf[k];
        if (a > peakAbs) { peakAbs = a; kPeak = k; }
      }

      // Per-node symmetry choice + per-node fold drive. feat[4]=entropy,
      // feat[6]=flatness. EVEN for chaos-y, ODD for tonal/structured.
      // foldDrive scales how much the LUT entries get pushed past +/-1
      // before the triangular wavefolder wraps them back -- chaos nodes
      // get heavy fold (multiple bends), tonal nodes get gentle fold.
      const float entropy = coarse[picks[n] * kFeatDim + 4];
      const float flatness = coarse[picks[n] * kFeatDim + 6];
      const bool useEven = (entropy + flatness) > 1.0f;
      const float foldDrive = 1.0f + (entropy + flatness) * 2.0f;  // 1..5

      // Pick the longer side of the window (more usable samples).
      const int rightLen = kSrcLen - 1 - kPeak;
      const int leftLen  = kPeak;
      const bool useRight = rightLen >= leftLen;

      // Build LUT positive side (LUT[128..255]) by walking outward from
      // the peak along the longer side. If that side runs out before
      // we fill kHalfLut entries, repeat the last value.
      float pos[128];
      for (int d = 0; d < kHalfLut; d++)
      {
        int srcIdx;
        if (useRight)
        {
          srcIdx = kPeak + d;
          if (srcIdx > kSrcLen - 1) srcIdx = kSrcLen - 1;
        }
        else
        {
          srcIdx = kPeak - d;
          if (srcIdx < 0) srcIdx = 0;
        }
        pos[d] = buf[srcIdx];
      }

      // Fold into full LUT. Layout: indices 128..255 = positive side
      // (d = 0..127); indices 127..0 = negative side (d = 0..127).
      // EVEN: f(-x) = f(x) -> mirror.
      // ODD:  f(-x) = -f(x) -> mirror + invert.
      for (int d = 0; d < kHalfLut; d++)
      {
        const float p = pos[d];
        lut[n][128 + d]      = p;
        lut[n][127 - d]      = useEven ? p : -p;
      }

      // RMS-normalize the assembled LUT, then wavefold. Target RMS 0.4,
      // gain cap 8x. The wavefolder replaces the previous soft-clip:
      // soft-clip mushed the LUT extremes asymptotically toward +/-1
      // (so detail lived only in the middle and the edges were flat).
      // Triangular folding instead WRAPS large values back into range,
      // introducing discontinuous slope changes -- sharp corners that
      // generate dramatic harmonic content. Per-node foldDrive scales
      // the fold count: chaos nodes (high entropy + flatness) get
      // multiple folds across the LUT; tonal nodes get one or none.
      float sumSq = 0.0f;
      for (int k = 0; k < 256; k++) sumSq += lut[n][k] * lut[n][k];
      const float rms = sqrtf(sumSq * (1.0f / 256.0f));
      const float kTargetRms = 0.4f;
      const float kMaxGain = 8.0f;
      float gain = 0.0f;
      if (rms > 1e-4f)
      {
        gain = kTargetRms / rms;
        if (gain > kMaxGain) gain = kMaxGain;
      }
      gain *= foldDrive;
      for (int k = 0; k < 256; k++)
      {
        float x = lut[n][k] * gain;
        // Triangle wavefold: while |x| > 1, reflect back. Each iteration
        // strictly reduces |x| by at least 2, so the loop terminates in
        // O(|x|) steps -- bounded by gain * peak_lut_value, typically
        // < 8 iterations even at max gain * max foldDrive.
        while (x > 1.0f) x = 2.0f - x;
        while (x < -1.0f) x = -2.0f - x;
        lut[n][k] = x;
      }
    }
  }

  AlembicVoice::AlembicVoice()
  {
    addInput(mVOct);
    addInput(mSync);
    addOutput(mOut);

    addParameter(mF0);
    addParameter(mGlobalLevel);
    addParameter(mScanPos);
    addParameter(mScanK);

    addParameter(mRatioA);
    addParameter(mRatioB);
    addParameter(mRatioC);
    addParameter(mRatioD);

    addParameter(mLevelA);
    addParameter(mLevelB);
    addParameter(mLevelC);
    addParameter(mLevelD);

    addParameter(mDetuneA);
    addParameter(mDetuneB);
    addParameter(mDetuneC);
    addParameter(mDetuneD);

    addParameter(mMatrixAA);
    addParameter(mMatrixAB);
    addParameter(mMatrixAC);
    addParameter(mMatrixAD);
    addParameter(mMatrixBA);
    addParameter(mMatrixBB);
    addParameter(mMatrixBC);
    addParameter(mMatrixBD);
    addParameter(mMatrixCA);
    addParameter(mMatrixCB);
    addParameter(mMatrixCC);
    addParameter(mMatrixCD);
    addParameter(mMatrixDA);
    addParameter(mMatrixDB);
    addParameter(mMatrixDC);
    addParameter(mMatrixDD);
    addParameter(mReagentScan);
    addParameter(mReagent);

    addParameter(mFilterCutoff1);
    addParameter(mFilterCutoff2);
    addParameter(mFilterQ);
    addParameter(mTopoMix);
    addParameter(mBpLpBlend);
    addParameter(mDrive);

    memset(mPhaseBank, 0, sizeof(mPhaseBank));
    memset(mPrevOutBank, 0, sizeof(mPrevOutBank));
    memset(mPhaseArgBank, 0, sizeof(mPhaseArgBank));
    memset(mSineBank, 0, sizeof(mSineBank));
    memset(mMatrixFlat, 0, sizeof(mMatrixFlat));
    memset(mRatioFlat, 0, sizeof(mRatioFlat));
    memset(mDetuneFlat, 0, sizeof(mDetuneFlat));
    memset(mLevelFlat, 0, sizeof(mLevelFlat));
    fillPhase3Presets(mPresetTable);
    fillIdentityWavetableLUT(mWavetableLUT);
    mWavetableBlend = 0.0f;
    memset(mSvfIc1, 0, sizeof(mSvfIc1));
    memset(mSvfIc2, 0, sizeof(mSvfIc2));
    memset(mFilterFlat, 0, sizeof(mFilterFlat));
    memset(mLaneSrc, 0, sizeof(mLaneSrc));
    memset(mLaneDst, 0, sizeof(mLaneDst));
    mActiveEdgeCount = 0;
    memset(mRoutingSources, 0, sizeof(mRoutingSources));
    memset(mRoutingDst, 0, sizeof(mRoutingDst));
    mpSample = nullptr;

    // pffft state: lazy-allocated on first analyzeSample. Hann window
    // precomputed once here. cosf in init code is safe (not in the per-
    // frame draw path); per feedback_package_trig_lut the package-level
    // sinf/cosf miscompute hazard is graphics-draw specific.
    mFftSetup = nullptr;
    mFftIn = nullptr;
    mFftOut = nullptr;
    mFftWork = nullptr;
    for (int i = 0; i < 256; i++)
    {
      mHannWindow[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * (float)i / 255.0f));
    }
    memset(mScratchMag, 0, sizeof(mScratchMag));
    memset(mPrevMag, 0, sizeof(mPrevMag));

    mSyncWasHigh = false;
  }

  AlembicVoice::~AlembicVoice()
  {
    if (mpSample)
      mpSample->release();
    if (mFftSetup) pffft_destroy_setup(mFftSetup);
    if (mFftIn) pffft_aligned_free(mFftIn);
    if (mFftOut) pffft_aligned_free(mFftOut);
    if (mFftWork) pffft_aligned_free(mFftWork);
  }

  // setSample mirrors od::Head::setSample (er-301/od/objects/heads/Head.cpp).
  // Null-out mpSample first so the audio thread can't observe a half-
  // transitioned state; then release the old sample, attach the new, and
  // commit the pointer. On non-null, Phase 5b's analyzeSample() runs
  // synchronously to populate mPresetTable from the sample's content.
  void AlembicVoice::setSample(od::Sample *sample)
  {
    od::Sample *old = mpSample;
    mpSample = nullptr;
    if (old) old->release();
    if (sample) sample->attach();
    mpSample = sample;
    if (mpSample == nullptr)
    {
      // Detach -> revert to Phase 3 placeholder gradient + identity LUT.
      // Reset SVF state so filter ringing doesn't persist into the
      // gentle near-bypass settings, and zero the routing lane indices
      // so the matrix is inert (atten=0 in placeholder rows already
      // makes mActiveEdges empty, but explicit zero is safer).
      fillPhase3Presets(mPresetTable);
      fillIdentityWavetableLUT(mWavetableLUT);
      memset(mSvfIc1, 0, sizeof(mSvfIc1));
      memset(mSvfIc2, 0, sizeof(mSvfIc2));
      memset(mLaneSrc, 0, sizeof(mLaneSrc));
      memset(mLaneDst, 0, sizeof(mLaneDst));
    }
    else
    {
      analyzeSample();
    }
  }

  od::Sample *AlembicVoice::getSample()
  {
    return mpSample;
  }

  // ---- Phase 5b: offline feature extraction kernel ----------------------
  // Pipeline (planning/alchemy-voice.md "Multi-Modal Feature Extraction"):
  //   1. Coarse pass at 20 Hz hop computes 3 features (RMS, ZCR,
  //      brightness) per frame, adapting Som.cpp:332-379's per-block
  //      extractor to per-frame batch.
  //   2. Difference filter trims tonally-stable runs (L2 change > thresh).
  //   3. Farthest-point sampling picks 64 distinct moments in feature
  //      space (greedy O(64*N), ~77K compute for a 60s sample).
  //   4. Layer 1 derivation maps the 3-dim feature vector to the 29-float
  //      preset row: ratios (O0 direct, mean-centered), levels (O0
  //      RMS-driven), matrix off-diagonal (O1 contrast brightness*pitched).
  //
  // Phase 5 keeps it 3-dim (no FFT). Phase 8 will add spectral-centroid
  // via pffft + flux + autocorrelation pitchedness for richer features.
  // Phase 5 uses (1 - ZCR_normalized) as a pitchedness proxy: low ZCR =
  // pitched, high ZCR = noisy. ZCR is in [0, 1]; we normalize against a
  // typical noise ZCR (~0.5) to spread the proxy.

  // Per-frame extractor over [frameStart, frameStart + hop). Sums stereo
  // to mono. Writes 6 features to out[]:
  //   out[0] = RMS energy (full hop)
  //   out[1] = ZCR (full hop)
  //   out[2] = Spectral centroid in [0,1], normalized over 256-pt FFT
  //            of a Hann-windowed slice centered in the hop
  //   out[3] = Spectral flux in [0,~sqrt(2)], L2 distance from previous
  //            frame's normalized magnitude spectrum
  //   out[4] = Binary-domain bucket entropy: Shannon entropy of the
  //            16-bucket (4-bit-quantized) value distribution within
  //            the frame. 0 = single-bucket signal (DC); ~1 = uniform
  //            distribution (broadband noise / spread material).
  //   out[5] = Binary-domain mean run-length: avg consecutive samples
  //            in the same 4-bit bucket, normalized by frame length.
  //            Long runs = stationary; short runs = high-frequency or
  //            noisy material.
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void extractCoarseFeatures(od::Sample *s, int frameStart, int hop,
                                    int channels, PFFFT_Setup *fftSetup,
                                    float *fftIn, float *fftOut, float *fftWork,
                                    const float *hannWindow, float *scratchMag,
                                    float *prevMag, float *out)
  {
    int frameEnd = frameStart + hop;
    if (frameEnd > s->mSampleCount) frameEnd = s->mSampleCount;
    const int n = frameEnd - frameStart;
    if (n <= 0)
    {
      for (int i = 0; i < 7; i++) out[i] = 0.0f;
      return;
    }

    // ---- Single pass: RMS + ZCR + binary bucket stats ----
    float sumSq = 0.0f;
    int zeroCrossings = 0;
    float prevX = 0.0f;
    bool stereo = (channels > 1);

    int bucketCount[16];
    for (int b = 0; b < 16; b++) bucketCount[b] = 0;
    int prevBucket = -1;
    int runs = 0;
    int sumRunLen = 0;
    int currentRunStart = 0;

    for (int i = frameStart; i < frameEnd; i++)
    {
      float x = s->get(i, 0);
      if (stereo) x = (x + s->get(i, 1)) * 0.5f;
      sumSq += x * x;
      if (i > frameStart)
      {
        if ((x >= 0.0f && prevX < 0.0f) || (x < 0.0f && prevX >= 0.0f))
          zeroCrossings++;
      }
      prevX = x;

      // 4-bit bucketing in [-1, 1] -> 16 bins.
      int bucket = (int)((x + 1.0f) * 8.0f);
      if (bucket < 0) bucket = 0;
      else if (bucket > 15) bucket = 15;
      bucketCount[bucket]++;

      if (prevBucket < 0)
      {
        prevBucket = bucket;
        currentRunStart = i;
      }
      else if (bucket != prevBucket)
      {
        sumRunLen += (i - currentRunStart);
        runs++;
        prevBucket = bucket;
        currentRunStart = i;
      }
    }
    // Final run.
    sumRunLen += (frameEnd - currentRunStart);
    runs++;

    out[0] = sqrtf(sumSq / (float)n);
    out[1] = (float)zeroCrossings / (float)n;

    // Bucket entropy: Shannon over 16-bucket distribution, normalized
    // by max log2(16) = 4 so out is in [0, 1].
    float entropy = 0.0f;
    const float invN = 1.0f / (float)n;
    const float invLog2 = 1.0f / 0.69314718056f;
    for (int b = 0; b < 16; b++)
    {
      if (bucketCount[b] > 0)
      {
        const float p = (float)bucketCount[b] * invN;
        entropy -= p * logf(p) * invLog2;
      }
    }
    out[4] = entropy * 0.25f;

    // Mean run length normalized by frame length: 1.0 = single bucket
    // for the whole frame (DC); near 0 = every sample changes bucket.
    out[5] = ((float)sumRunLen / (float)runs) / (float)n;

    // ---- FFT-based features: Hann-windowed 256-sample slice ----
    int fftStart = frameStart + (n - 256) / 2;
    if (fftStart < 0) fftStart = 0;
    if (fftStart + 256 > s->mSampleCount) fftStart = s->mSampleCount - 256;

    if (fftStart < 0)
    {
      // Sample shorter than 256 samples? Skip FFT features.
      out[2] = 0.0f;
      out[3] = 0.0f;
      out[6] = 0.0f;
      return;
    }

    for (int i = 0; i < 256; i++)
    {
      float x = s->get(fftStart + i, 0);
      if (stereo) x = (x + s->get(fftStart + i, 1)) * 0.5f;
      fftIn[i] = x * hannWindow[i];
    }
    pffft_transform_ordered(fftSetup, fftIn, fftOut, fftWork, PFFFT_FORWARD);

    // pffft real-FFT output layout for N=256: bins 0 (DC) and 128
    // (Nyquist) are real-only and packed at indices 0,1; bins 1..127 are
    // re/im pairs at 2*k, 2*k+1. We treat as 128 magnitudes [0..127].
    scratchMag[0] = fabsf(fftOut[0]);
    for (int k = 1; k < 128; k++)
    {
      const float re = fftOut[2 * k];
      const float im = fftOut[2 * k + 1];
      scratchMag[k] = sqrtf(re * re + im * im);
    }

    // Normalize magnitude spectrum to a probability distribution. This
    // decouples the spectral features from absolute energy (already
    // captured in RMS), so centroid/flux compare spectral SHAPES.
    float sumMag = 0.0f;
    for (int k = 0; k < 128; k++) sumMag += scratchMag[k];
    if (sumMag > 1e-6f)
    {
      const float invSum = 1.0f / sumMag;
      for (int k = 0; k < 128; k++) scratchMag[k] *= invSum;
    }

    // Spectral centroid = sum(k * mag[k]) / 128, in [0, 1].
    float sumKMag = 0.0f;
    for (int k = 0; k < 128; k++) sumKMag += (float)k * scratchMag[k];
    out[2] = sumKMag / 128.0f;

    // Spectral flux = L2 distance to previous frame's normalized
    // magnitude spectrum.
    float flux = 0.0f;
    for (int k = 0; k < 128; k++)
    {
      const float d = scratchMag[k] - prevMag[k];
      flux += d * d;
    }
    out[3] = sqrtf(flux);

    // Spectral flatness = geometric mean / arithmetic mean over a sub-band
    // that excludes DC and near-Nyquist (bins 4..109). Sine -> ~0; white
    // noise -> ~1. Distinguishes tonal from broadband material more
    // cleanly than ZCR for many sources, drives the Phase 5c topology
    // picker.
    {
      float logSum = 0.0f;
      float arithSum = 0.0f;
      int validBins = 0;
      for (int k = 4; k < 110; k++)
      {
        if (scratchMag[k] > 1e-9f)
        {
          logSum += logf(scratchMag[k]);
          arithSum += scratchMag[k];
          validBins++;
        }
      }
      float flatness = 0.0f;
      if (validBins > 0 && arithSum > 1e-9f)
      {
        const float geoMean = expf(logSum / (float)validBins);
        const float arithMean = arithSum / (float)validBins;
        flatness = geoMean / (arithMean + 1e-9f);
      }
      out[6] = flatness;
    }

    // Stash for next frame's flux computation.
    for (int k = 0; k < 128; k++) prevMag[k] = scratchMag[k];
  }

  // Squared L2 distance between two 7-dim feature vectors (5 audio +
  // 2 binary-domain). Audio: rms, zcr, centroid, flux, flatness.
  // Binary: 16-bucket Shannon entropy, mean run length.
  static inline float l2Distance7sq(const float *a, const float *b)
  {
    const float d0 = a[0] - b[0];
    const float d1 = a[1] - b[1];
    const float d2 = a[2] - b[2];
    const float d3 = a[3] - b[3];
    const float d4 = a[4] - b[4];
    const float d5 = a[5] - b[5];
    const float d6 = a[6] - b[6];
    return d0*d0 + d1*d1 + d2*d2 + d3*d3 + d4*d4 + d5*d5 + d6*d6;
  }

  // Greedy farthest-point sampling: pick `count` indices from `candidates`
  // such that each pick maximizes the minimum distance to any prior pick.
  // O(count * candidates.size() * count) -- ~77K dist ops at 64 picks /
  // 1200 candidates / 60s sample. Negligible. Distance is l2Distance7sq
  // over 7-dim feature space (5 audio + 2 binary).
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void farthestPointPick(const float *coarse, const int *candidates,
                                int nCandidates, int count, int *picks)
  {
    if (nCandidates <= 0 || count <= 0) return;
    picks[0] = candidates[0];
    for (int p = 1; p < count; p++)
    {
      int best = candidates[0];
      float bestMinDist = -1.0f;
      for (int ci = 0; ci < nCandidates; ci++)
      {
        const int c = candidates[ci];
        // Skip already picked.
        bool taken = false;
        for (int q = 0; q < p; q++)
        {
          if (picks[q] == c) { taken = true; break; }
        }
        if (taken) continue;
        float minD = 1e20f;
        for (int q = 0; q < p; q++)
        {
          const float d = l2Distance7sq(&coarse[c * 7], &coarse[picks[q] * 7]);
          if (d < minD) minD = d;
        }
        if (minD > bestMinDist) { bestMinDist = minD; best = c; }
      }
      picks[p] = best;
    }
  }

  // Phase 5d-3 routing-lane training. laneAffinity scores each (src,
  // dst) pair against the pick's features -- different feature
  // signatures favor different routing topologies. selectLanesForPick
  // ranks all 60 candidates by affinity-plus-hash-noise and picks the
  // top 8. Per-pick hash provides variation across nodes with similar
  // features (avoids 64 identical lane assignments on uniform samples).
  //
  // Source / destination conventions (matched to AlembicVoice.h):
  //   src 0..3 = PMM ops A..D, src 4..6 = F1 LP/BP/HP, src 7..9 = F2
  //   dst 0..1 = F1 cutoff verso/inverso, dst 2..3 = F2 cutoff
  //   dst 4 = F1 input addAdd, dst 5 = F2 input addAdd
  static float laneAffinity(int src, int dst, const float *feat)
  {
    const float zcr        = feat[1];
    const float brightness = feat[2];
    const float fluxClamp  = feat[3] > 1.0f ? 1.0f : feat[3];
    const float entropy    = feat[4];
    const float pitched    = (zcr < 0.5f) ? (1.0f - 2.0f * zcr) : 0.0f;
    const bool srcIsPMM   = (src < 4);
    const bool srcIsFilt  = (src >= 4);
    const bool dstIsCutoff = (dst < 4);
    const bool dstIsAdd    = (dst >= 4);

    if (srcIsPMM && dstIsCutoff) return fluxClamp * 0.6f + entropy * 0.2f + brightness * 0.2f;
    if (srcIsFilt && dstIsCutoff) return entropy * 0.7f + fluxClamp * 0.2f;
    if (srcIsPMM && dstIsAdd) return pitched * 0.5f + brightness * 0.3f;
    if (srcIsFilt && dstIsAdd) return entropy * 0.4f + pitched * 0.3f;
    return 0.0f;
  }

  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void selectLanesForPick(const float *feat, uint32_t pickHash,
                                 uint8_t *outSrc, uint8_t *outDst,
                                 float *outAtten)
  {
    struct Cand { uint8_t s, d; float score; };
    Cand cands[60];
    int n = 0;
    for (int s = 0; s < 10; s++)
    {
      for (int d = 0; d < 6; d++)
      {
        float score = laneAffinity(s, d, feat);
        // Per-pick hash for variety. ~30% weight so mostly feature-driven.
        const uint32_t h = pickHash ^ (s * 374761393u) ^ (d * 668265263u);
        const float hashNoise = (float)((h >> 8) & 0xFFFF) / 65535.0f;
        score = score * 0.7f + hashNoise * 0.3f;
        cands[n].s = (uint8_t)s;
        cands[n].d = (uint8_t)d;
        cands[n].score = score;
        n++;
      }
    }
    // Partial selection sort: top 8 by score.
    for (int i = 0; i < 8; i++)
    {
      int best = i;
      for (int j = i + 1; j < n; j++)
        if (cands[j].score > cands[best].score) best = j;
      Cand tmp = cands[i]; cands[i] = cands[best]; cands[best] = tmp;
      outSrc[i] = cands[i].s;
      outDst[i] = cands[i].d;
      // Atten: score-based with floor + cap. Cap at 0.6 keeps filter-FM
      // from going wildly unstable; floor at 0.2 ensures every active
      // lane has audible contribution.
      float a = 0.2f + cands[i].score * 0.4f;
      if (a > 0.6f) a = 0.6f;
      outAtten[i] = a;
    }
  }

  // Phase 5c topology table. Bit i set => matrix path src*4+dst at
  // row[12+i] is active for this template. Active subset is ordered
  // along a "chaos" axis so adjacent buckets differ by 1-2 paths only;
  // K=4 crossfade across topology boundaries fades a small path-mask
  // delta rather than a full reset.
  //
  // Path index legend (src*4 + dst, ops A=0, B=1, C=2, D=3):
  //   0=AA  1=AB  2=AC  3=AD
  //   4=BA  5=BB  6=BC  7=BD
  //   8=CA  9=CB 10=CC 11=CD
  //  12=DA 13=DB 14=DC 15=DD
  struct AlembicTopology { uint16_t mask; };
  static const AlembicTopology kTopologies[6] = {
    // T0 SINE: no FM, pure additive sines.
    { 0x0000 },
    // T1 PARALLEL: A->B, C->D.
    { (uint16_t)((1u << 1) | (1u << 11)) },
    // T2 CHAIN: A->B, B->C, C->D.
    { (uint16_t)((1u << 1) | (1u << 6) | (1u << 11)) },
    // T3 FEEDBACK_LOOP: A->B, B->C, C->D, D->A.
    { (uint16_t)((1u << 1) | (1u << 6) | (1u << 11) | (1u << 12)) },
    // T4 SELF_FB: A->B, B->C, AA, DD (chain + endpoint feedback).
    { (uint16_t)((1u << 1) | (1u << 6) | (1u << 0) | (1u << 15)) },
    // T5 DENSE: full 9-path 5b mapping.
    { (uint16_t)((1u << 1) | (1u << 6) | (1u << 11) | (1u << 4)
               | (1u << 9) | (1u << 12) | (1u << 0) | (1u << 5)
               | (1u << 10)) },
  };

  // Phase 5c Layer 1 derivation: 7-dim feature vector + slot index ->
  // 29-float preset row. Audio features (rms, zcr, centroid, flux,
  // flatness) drive ratios / levels / detunes / matrix path strengths;
  // binary features (entropy, runLen) drive ratio integer-pull, detune
  // boost, and the topology picker. Topology selection -- a discrete
  // choice of which matrix paths are active -- gives categorical
  // discontinuity across nodes layered over the continuous strength
  // mapping (the 5b behavior was strength-only; 5c adds path-set
  // diversity so the 7 unused matrix slots become reachable).
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void derivePresetRow(const float *feat, float *row, int slot)
  {
    const float rms = feat[0];
    const float zcr = feat[1];
    const float brightness = feat[2];   // Real spectral centroid in [0,1]
    const float flux = feat[3];          // Spectral flux (onset/transient energy)
    const float entropy = feat[4];       // Binary: 16-bucket Shannon entropy
    const float runLen  = feat[5];       // Binary: mean run length normalized
    const float flatness = feat[6];      // Spectral flatness (geo/arith mean)
    // pitched in [0,1]: 1 = clean tone, 0 = noisy / broadband.
    float pitched = 1.0f - 2.0f * zcr;
    if (pitched < 0.0f) pitched = 0.0f;
    if (pitched > 1.0f) pitched = 1.0f;
    const float noisy = 1.0f - pitched;
    // Flux clamp: typical normalized spectra differ by 0..0.5 between
    // adjacent frames; transients spike higher. Clamp to [0,1] for use
    // as a mapping driver.
    float fluxClamp = flux;
    if (fluxClamp > 1.0f) fluxClamp = 1.0f;
    if (fluxClamp < 0.0f) fluxClamp = 0.0f;

    // ---- Pure feature-driven mapping ----
    // Slot index NOT used here. Discontinuity in the preset table arises
    // emergently from the sample's feature trajectory: varied samples
    // produce widely varied preset rows; tonally-uniform samples produce
    // similar preset rows (correct behavior -- chaos comes from material,
    // not from artificial slot sweep).
    //
    // Each feature drives multiple preset slots to the FULL range so
    // sample variations have macro effect on op enable/disable, FM
    // density, detune intensity, and self-feedback.
    //
    // Future meta-mapping layer (Phase 8+): non-audio features (binary-
    // domain entropy, run-length, etc.) could shape node ordering,
    // chaos vs orderliness bounds, or which feature axes get mapped
    // where. For Phase 5, the audio features alone determine the
    // mapping shape.
    const float rmsClamp = rms > 1.0f ? 1.0f : rms;
    (void)slot; // intentionally unused -- no slot-driven baseline

    // Ratios: harmonic anchor + audio-feature perturbation, with
    // runLen acting as a per-node integer-pull. High runLen means the
    // sample at this pick was stationary / structured -- pull the
    // perturbation back toward integer ratios. This is the "dappling"
    // effect: some nodes (where the source was tonal) snap close to
    // pure harmonics; others keep the inharmonic offset from features.
    const float intPull = runLen;                       // 0..1
    const float perturbScale = 1.0f - intPull * 0.7f;   // 1.0..0.3 retained
    row[0] = 1.0f;
    row[1] = 1.0f + (pitched * 1.0f + noisy * 1.5f) * perturbScale;
    row[2] = 2.0f + (brightness * 2.0f + noisy * 0.5f) * perturbScale;
    row[3] = 3.0f + (noisy * 3.0f + brightness * 1.5f + fluxClamp * 1.0f) * perturbScale;

    // Levels: features drive op enable/disable. op A always carries
    // (no silent samples); op B/C/D fully on or fully off based on
    // their feature axes. RMS scales the overall amplitude so quiet
    // samples produce quiet presets and loud samples produce loud ones.
    const float baseA = 0.5f + rmsClamp * 0.5f;            // 0.5..1.0 always
    row[4] = baseA;
    row[5] = brightness * (0.4f + rmsClamp * 0.6f);        // 0..1.0 by brightness
    row[6] = pitched   * (0.4f + rmsClamp * 0.6f);         // 0..1.0 by pitched
    row[7] = fluxClamp * (0.4f + rmsClamp * 0.6f);         // 0..1.0 by flux

    // Detunes: audio (noisy + flux) drive baseline; entropy adds extra
    // perturbation per pick. High-entropy picks (broadband / spread
    // distribution material) get bigger detunes for additional audible
    // movement. runLen reduces detune for stationary nodes (intPull
    // again -- consistent with ratio pull). Phase 5c adds row[8] (op A
    // carrier drift) driven by flatness * fluxClamp; tonal samples
    // keep op A clean, noisy/transient samples let it drift up to ~6
    // cents -- adds movement without breaking tonality.
    const float detuneScale = 1.0f - intPull * 0.6f;
    row[8]  = (1.0f - intPull) * (flatness * 4.0f + fluxClamp * 2.0f);
    row[9]  = (noisy * 8.0f  + fluxClamp * 4.0f  + entropy * 3.0f) * detuneScale;
    row[10] = (noisy * 14.0f + brightness * 6.0f + entropy * 4.0f) * detuneScale;
    row[11] = (noisy * 20.0f + fluxClamp * 10.0f + entropy * 5.0f) * detuneScale;

    // Matrix: Phase 5c topology selection. chaosScore drives a discrete
    // template choice; path strengths are still feature-driven (same
    // formulas as 5b) but only paths in the active mask are non-zero.
    // K=4 crossfade across slots straddling a topology boundary fades
    // the small mask delta -- categorical discontinuity emerges at
    // pure-slot positions, smooth blend between.
    const float chaosScore =
        (1.0f - runLen) * 0.45f
      + entropy         * 0.30f
      + flatness        * 0.25f;
    int topo;
    if      (chaosScore < 0.15f) topo = 0;  // SINE
    else if (chaosScore < 0.30f) topo = 1;  // PARALLEL
    else if (chaosScore < 0.45f) topo = 2;  // CHAIN
    else if (chaosScore < 0.60f) topo = 3;  // FEEDBACK_LOOP
    else if (chaosScore < 0.75f) topo = 4;  // SELF_FB
    else                         topo = 5;  // DENSE

    for (int i = 0; i < 16; i++) row[12 + i] = 0.0f;

    const uint16_t mask = kTopologies[topo].mask;
    const float fbScale = noisy * 0.40f;                                    // self-fb
    const float chainAB = brightness * pitched * 0.50f + fluxClamp * 0.20f; // A->B
    const float chainBC = brightness * 0.45f + pitched * 0.10f;             // B->C
    const float chainCD = pitched * 0.40f + fluxClamp * 0.15f;              // C->D
    const float chainBA = noisy * brightness * 0.40f;                       // B->A
    const float chainCB = noisy * fluxClamp * 0.35f;                        // C->B
    const float chainDA = noisy * 0.30f + fluxClamp * 0.15f;                // D->A
    const float chainBB = brightness * fluxClamp * 0.30f;                   // B self-fb
    const float chainCC = pitched * 0.25f;                                  // C self-fb

    if (mask & (1u << 0))  row[12 + 0]  = fbScale;
    if (mask & (1u << 1))  row[12 + 1]  = chainAB;
    if (mask & (1u << 4))  row[12 + 4]  = chainBA;
    if (mask & (1u << 5))  row[12 + 5]  = chainBB;
    if (mask & (1u << 6))  row[12 + 6]  = chainBC;
    if (mask & (1u << 9))  row[12 + 9]  = chainCB;
    if (mask & (1u << 10)) row[12 + 10] = chainCC;
    if (mask & (1u << 11)) row[12 + 11] = chainCD;
    if (mask & (1u << 12)) row[12 + 12] = chainDA;
    if (mask & (1u << 15)) row[12 + 15] = fbScale * 0.7f;                   // D self-fb (tamer)

    // Phase 5d-1: row[28] is now the wavetable transfer-function blend.
    // Tonal samples (high runLen, low entropy + flatness) keep blend
    // low -> output stays close to soft-saturated PMM. Noisy / chaotic
    // samples push blend high -> output is heavily shaped by the per-
    // node sample-window LUT. intPull is the same scalar used by
    // ratios + detunes earlier in this function.
    const float wblendIntensity =
        (1.0f - intPull) * (entropy * 0.5f + flatness * 0.5f);
    row[28] = wblendIntensity;

    // Phase 5d-2.1 filter base [29..34]. All values normalized [0,1];
    // process() maps to Hz / Q / etc per Som's convention. Mapping
    // taste -- cutoffs driven by ORTHOGONAL features so they move
    // independently across nodes (5d-2's both-cutoffs-from-brightness
    // mapping made them track too closely):
    //   - cutoff1 = brightness (high-freq spectral energy)
    //   - cutoff2 = (entropy + fluxClamp) * 0.5 (chaos / transient
    //     energy axis, anticorrelated with brightness for tonal sources)
    //   - Q = 1 - flatness (tonal -> resonant, noisy -> wide; flatness
    //     is post-min-max-normalized so the full [0,1] range is used,
    //     unlike the clamped-pitched mapping in 5d-2 which squashed
    //     the upper half of zcr to Q=0.5)
    //   - topoMix = runLen (stationary -> cascade, transient -> parallel)
    //   - bpLpBlend = pitched (tonal -> LP, noisy -> BP)
    //   - drive = noisy * 0.6 + fluxClamp * 0.4
    row[29] = brightness;                          // cutoff1 [0,1]
    row[30] = (entropy + fluxClamp) * 0.5f;        // cutoff2 [0,1]
    row[31] = 1.0f - flatness;                     // Q [0,1]
    row[32] = runLen;                              // topoMix [0,1]
    row[33] = pitched;                             // bpLpBlend [0,1] (tonal -> LP)
    row[34] = noisy * 0.6f + fluxClamp * 0.4f;     // drive [0,1]

    // Lane attens [35..42] stay zero (5d-3 populates).
    for (int i = 35; i < 43; i++) row[i] = 0.0f;
  }

  // Mean-center ratios across nodes per planning doc line 125: even when
  // input material is tonally uniform, mean-centering preserves whatever
  // relative variation the picks captured, recentered on 1.0.
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void meanCenterRatios(float (&t)[64][29])
  {
    for (int op = 0; op < 4; op++)
    {
      float sum = 0.0f;
      for (int n = 0; n < 64; n++) sum += t[n][op];
      const float mean = sum / 64.0f;
      const float shift = 1.0f - mean;
      for (int n = 0; n < 64; n++) t[n][op] += shift;
    }
  }

  __attribute__((noinline, optimize("no-tree-vectorize")))
  void AlembicVoice::analyzeSample()
  {
    if (!mpSample) return;

    const int sr = (int)mpSample->mSampleRate;
    const int nFrames = mpSample->mSampleCount;
    const int channels = mpSample->mChannelCount;
    if (sr <= 0 || nFrames <= 0) return;

    const int hop = sr / 20;                 // 20 Hz coarse rate
    const int nCoarse = nFrames / hop;
    if (nCoarse < 64)
    {
      // Sample too short for distinct picks. Leave placeholder gradient.
      return;
    }

    // Lazy-allocate pffft state on first analysis. Buffers must be 16-byte
    // aligned (pffft_aligned_malloc). Persisted across calls.
    if (!mFftSetup)
    {
      mFftSetup = pffft_new_setup(256, PFFFT_REAL);
      mFftIn   = (float *)pffft_aligned_malloc(256 * sizeof(float));
      mFftOut  = (float *)pffft_aligned_malloc(256 * sizeof(float));
      mFftWork = (float *)pffft_aligned_malloc(256 * sizeof(float));
    }
    // Reset previous-frame magnitude buffer so first frame's flux starts
    // from a clean baseline (otherwise the first sample's flux would
    // accidentally include the previous sample's spectral state).
    memset(mPrevMag, 0, sizeof(mPrevMag));

    // 7 features per frame: 5 audio (RMS, ZCR, centroid, flux, flatness)
    // + 2 binary (bucket entropy, mean run length). Phase 5c added
    // flatness (out[6]) for the topology picker.
    const int kFeatDim = 7;
    float *coarse = new float[nCoarse * kFeatDim];
    int *candidates = new int[nCoarse];
    int picks[64];

    // Coarse pass.
    for (int c = 0; c < nCoarse; c++)
    {
      extractCoarseFeatures(mpSample, c * hop, hop, channels,
                            mFftSetup, mFftIn, mFftOut, mFftWork,
                            mHannWindow, mScratchMag, mPrevMag,
                            &coarse[c * kFeatDim]);
    }

    // Per-dim normalization across the whole coarse buffer. Min/max
    // scaling ensures picks SPAN [0,1] in each dim regardless of the
    // sample's absolute feature distribution. Constant dims (uniform
    // sample) leave features at original values -- uniform samples
    // produce uniform preset rows by design.
    for (int dim = 0; dim < kFeatDim; dim++)
    {
      float minV = 1e20f, maxV = -1e20f;
      for (int c = 0; c < nCoarse; c++)
      {
        const float v = coarse[c * kFeatDim + dim];
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
      }
      const float span = maxV - minV;
      if (span < 1e-6f) continue; // dim effectively constant
      const float invSpan = 1.0f / span;
      for (int c = 0; c < nCoarse; c++)
      {
        coarse[c * kFeatDim + dim] = (coarse[c * kFeatDim + dim] - minV) * invSpan;
      }
    }

    // Difference filter on 6-dim features (post-normalization).
    const float kChangeThresh2 = 0.001f * 0.001f;
    int nCandidates = 0;
    for (int c = 1; c < nCoarse; c++)
    {
      if (l2Distance7sq(&coarse[c * kFeatDim], &coarse[(c - 1) * kFeatDim]) > kChangeThresh2)
      {
        candidates[nCandidates++] = c;
      }
    }
    if (nCandidates < 64)
    {
      // Fallback: use all frames.
      for (int c = 0; c < nCoarse; c++) candidates[c] = c;
      nCandidates = nCoarse;
    }

    farthestPointPick(coarse, candidates, nCandidates, 64, picks);

    // Sort picks by ascending frame index so the scan position walks
    // through the sample CHRONOLOGICALLY (smooth feature traversal).
    // Without this, farthest-point's selection-order makes adjacent
    // slot positions maximally different in feature space; the K=4
    // crossfade then averages widely-distant feature values into a
    // mush that masks all feature variation. Time-sort is a cheap
    // Hamilton-chain proxy: temporally-adjacent frames generally have
    // adjacent features.
    {
      // Insertion sort -- 64 elements, ~64*64 cmps worst case, trivial.
      for (int i = 1; i < 64; i++)
      {
        const int v = picks[i];
        int j = i - 1;
        while (j >= 0 && picks[j] > v)
        {
          picks[j + 1] = picks[j];
          j--;
        }
        picks[j + 1] = v;
      }
    }

    for (int n = 0; n < 64; n++)
    {
      derivePresetRow(&coarse[picks[n] * kFeatDim], mPresetTable[n], n);
      // Phase 5d-3 routing lane training. Per-pick hash mixes node
      // index + a constant so identical features still get distinct
      // topologies across nodes.
      const uint32_t pickHash =
          ((uint32_t)picks[n] * 374761393u) ^ ((uint32_t)n * 2246822519u);
      uint8_t src8[8], dst8[8];
      float atten8[8];
      selectLanesForPick(&coarse[picks[n] * kFeatDim], pickHash,
                         src8, dst8, atten8);
      for (int e = 0; e < 8; e++)
      {
        mLaneSrc[n][e] = src8[e];
        mLaneDst[n][e] = dst8[e];
        mPresetTable[n][35 + e] = atten8[e];
      }
    }

    // Phase 5d-1.6: build per-node 256-entry LUTs from 256-sample multi-
    // cycle source windows at the picks, folded around each window's
    // absolute peak with per-node even/odd symmetry chosen from the
    // coarse features (high entropy + flatness -> even, else odd).
    // Time-sorted picks already cluster temporally-adjacent windows
    // into adjacent slots so K=4 crossfade interpolates smoothly
    // between LUT shapes.
    buildWavetableFrames(mpSample, picks, channels, coarse, kFeatDim,
                         mWavetableLUT);

    // Mean-centering deferred: planning doc line 125 prescribes it for
    // tonally-uniform sources, but with feature-driven ratios it tends
    // to push some ratios negative. Phase 5 ships without.
    (void)meanCenterRatios;

    delete[] candidates;
    delete[] coarse;
  }

  void AlembicVoice::process()
  {
    const float sr = globalConfig.sampleRate;
    const float sp = globalConfig.samplePeriod;
    // glog2 = FULLSCALE_IN_VOLTS * ln2, matching SineOscillator.cpp:41.
    const float glog2 = FULLSCALE_IN_VOLTS * kLn2;
    const float nyquist = sr * 0.49f;

    float *out = mOut.buffer();
    float *vOct = mVOct.buffer();
    float *sync = mSync.buffer();

    const float f0 = CLAMP(0.01f, nyquist, mF0.value());
    const float lvl = mGlobalLevel.value();

    // Phase 3 K-weighted preset crossfade. Block-rate. Output buffers are
    // class members (mMatrixFlat / mRatioFlat / mDetuneFlat / mLevelFlat),
    // same arrays the per-sample NEON loop reads from in Phase 2b -- no
    // changes to that loop. Rest of process() is identical to Phase 2b.
    const float scanPos = CLAMP(0.0f, 1.0f, mScanPos.value());
    const int K = (int)CLAMP(2.0f, 6.0f, mScanK.value() + 0.5f);
    const float s = scanPos * 63.0f;
    const float halfWidth = 0.5f * (float)K;

    // Pick K nodes around s, clamping at table edges.
    int n0 = (int)(s + 0.5f) - K / 2;
    if (n0 < 0) n0 = 0;
    if (n0 + K > 64) n0 = 64 - K;

    // Triangular weights, normalize to sum=1. K up to 6.
    float w[6];
    float wSum = 0.0f;
    for (int j = 0; j < K; j++)
    {
      const float dist = fabsf((float)(n0 + j) - s);
      const float wRaw = halfWidth - dist;
      w[j] = wRaw > 0.0f ? wRaw : 0.0f;
      wSum += w[j];
    }
    if (wSum < 1e-9f)
    {
      w[0] = 1.0f;
      wSum = 1.0f;
    }
    const float wInv = 1.0f / wSum;

    // Blend the per-slot trained floats into the working voice state
    // buffers. Loop body is uniform across K (no differential dispatch
    // per feedback_runtime_branched_dsp_dispatch). Phase 5d-1 added
    // mWavetableBlend (row[28]); 5d-2/5d-3 will add filter base +
    // lane attens to this loop without restructuring it.
    for (int f = 0; f < 4; f++) mRatioFlat[f] = 0.0f;
    for (int f = 0; f < 4; f++) mLevelFlat[f] = 0.0f;
    for (int f = 0; f < 4; f++) mDetuneFlat[f] = 0.0f;
    for (int f = 0; f < 16; f++) mMatrixFlat[f] = 0.0f;
    for (int f = 0; f < 6; f++) mFilterFlat[f] = 0.0f;
    for (int j = 0; j < K; j++)
    {
      const float wj = w[j] * wInv;
      const float *p = mPresetTable[n0 + j];
      for (int f = 0; f < 4; f++) mRatioFlat[f] += wj * p[f];
      for (int f = 0; f < 4; f++) mLevelFlat[f] += wj * p[4 + f];
      for (int f = 0; f < 4; f++) mDetuneFlat[f] += wj * p[8 + f];
      for (int f = 0; f < 16; f++) mMatrixFlat[f] += wj * p[12 + f];
      for (int f = 0; f < 6; f++) mFilterFlat[f] += wj * p[29 + f];
    }

    // Phase 5d-1.5: independent reagent scan window. mReagentScan
    // selects a separate K-node neighborhood for the wavetable LUT
    // shape AND its trained row[28] blend. mReagent (amount) multiplies
    // the K-blended blend so default mReagent = 0 -> clean PM. K is
    // shared with the main scan to keep the ply count tight (one extra
    // top-level control, not two).
    const float scanPosR = CLAMP(0.0f, 1.0f, mReagentScan.value());
    const float sR = scanPosR * 63.0f;
    int n0r = (int)(sR + 0.5f) - K / 2;
    if (n0r < 0) n0r = 0;
    if (n0r + K > 64) n0r = 64 - K;
    float wR[6];
    float wSumR = 0.0f;
    for (int j = 0; j < K; j++)
    {
      const float dist = fabsf((float)(n0r + j) - sR);
      const float wRaw = halfWidth - dist;
      wR[j] = wRaw > 0.0f ? wRaw : 0.0f;
      wSumR += wR[j];
    }
    if (wSumR < 1e-9f)
    {
      wR[0] = 1.0f;
      wSumR = 1.0f;
    }
    const float wInvR = 1.0f / wSumR;
    float reagentBlend = 0.0f;
    for (int j = 0; j < K; j++)
      reagentBlend += (wR[j] * wInvR) * mPresetTable[n0r + j][28];
    const float reagentAmt = CLAMP(0.0f, 1.0f, mReagent.value());
    mWavetableBlend = reagentBlend * reagentAmt;
    // Apply the diagonal kFbScale after the blend, matching Phase 2b's
    // per-sample matrix-sum convention. Off-diagonal cross-mod stays 1x.
    mMatrixFlat[0 * 4 + 0] *= kFbScale;
    mMatrixFlat[1 * 4 + 1] *= kFbScale;
    mMatrixFlat[2 * 4 + 2] *= kFbScale;
    mMatrixFlat[3 * 4 + 3] *= kFbScale;

    // Phase 5d-1: precompute K-blend weights for the per-sample wavetable
    // shaper (K=2..6 frames). Stays on the stack but is small enough
    // not to NEON-vectorize -- gcc keeps these in scalar regs. Phase
    // 5d-1.5 splits this into reagent-scan-specific weights so the
    // wavetable LUT lookup uses the independent reagent scan window.
    float wNormalizedR[6];
    for (int j = 0; j < K; j++) wNormalizedR[j] = wR[j] * wInvR;

    // Block-rate NEON loads. Only vLevel hoists across the per-sample
    // loop -- it's used in the output-sum step *after* simd_sin so it
    // can sit in a callee-save NEON register naturally. The other
    // quads (vRow0..3, vRatioF0Sp, vDetuneSp) are loaded INSIDE the
    // loop, just before their use, and become dead before simd_sin.
    // This avoids GCC having to spill them to stack across the
    // simd_sin call -- the spill emitted `vst1.64 {dN}, [sp :64]` hints
    // which trap on misaligned stack offsets per
    // feedback_neon_intrinsics_drumvoice. Per-sample re-load cost is
    // ~6 quad ops, marginal vs the simd_sin and matrix-FMA cost.
    const float32x4_t vLevel = vld1q_f32(mLevelFlat);
    const float f0Sp = f0 * sp;
    const float wblend = mWavetableBlend;

    // Phase 5d-2 filter base resolution (block-rate). Map K-blended
    // [0,1] values to Hz / Q / etc per Som's convention. In 5d-2 there
    // is no routing FM, so freqMod = 0 -> mg is block-constant. We
    // can pre-compute mg, k, a1/a2/a3 once per block per filter; the
    // per-sample loop just runs the SVF state update. 5d-3 will move
    // mg back inside the per-sample loop where freqMod from the
    // routing matrix becomes signal-rate.
    const float fc1Norm = CLAMP(0.0f, 1.0f, mFilterFlat[0]);
    const float fc2Norm = CLAMP(0.0f, 1.0f, mFilterFlat[1]);
    const float qNorm   = CLAMP(0.0f, 1.0f, mFilterFlat[2]);
    const float topoMix = CLAMP(0.0f, 1.0f, mFilterFlat[3]);
    const float bpLpBlend = CLAMP(0.0f, 1.0f, mFilterFlat[4]);
    const float driveNrm = CLAMP(0.0f, 1.0f, mFilterFlat[5]);

    const float fc1Hz = 40.0f * fastExp(fc1Norm * kCutoffExpScale);
    const float fc2Hz = 40.0f * fastExp(fc2Norm * kCutoffExpScale);
    const float Q = 0.5f + qNorm * 49.5f;
    const float kFilterK = 1.0f / Q;
    const float driveGain = 0.5f + driveNrm * 2.5f;

    // Block-rate base coefficient. With the 5d-3 routing matrix in,
    // freqMod becomes per-sample (sources include PMM ops + filter
    // states), so mg = baseG * fastExp(freqMod * 0.8) is computed
    // per-sample inside the loop. baseG is block-rate constant.
    const float maxFc = sr * 0.499f;
    float fc1Clamp = fc1Hz; if (fc1Clamp < 5.0f) fc1Clamp = 5.0f; if (fc1Clamp > maxFc) fc1Clamp = maxFc;
    float fc2Clamp = fc2Hz; if (fc2Clamp < 5.0f) fc2Clamp = 5.0f; if (fc2Clamp > maxFc) fc2Clamp = maxFc;
    const float baseG0 = fastTan(3.14159f * fc1Clamp / sr);
    const float baseG1 = fastTan(3.14159f * fc2Clamp / sr);

    // Phase 5d-3 routing matrix: hard-cut single-slot pick. The single
    // nearest preset slot to mScanPos provides the 8 (src, dst, atten)
    // lanes -- no K-blend across slot boundaries. Routing topology
    // snaps when scan crosses slot edges (deliberate per user taste:
    // produces audible discontinuities + click character at boundaries
    // rather than smooth interpolation between routings).
    int routingSlot = (int)(s + 0.5f);
    if (routingSlot < 0) routingSlot = 0;
    if (routingSlot > 63) routingSlot = 63;
    mActiveEdgeCount = 0;
    for (int e = 0; e < 8; e++)
    {
      const float a = mPresetTable[routingSlot][35 + e];
      const float aa = a < 0.0f ? -a : a;
      if (aa > 1e-6f)
      {
        mActiveEdges[mActiveEdgeCount].src = mLaneSrc[routingSlot][e];
        mActiveEdges[mActiveEdgeCount].dst = mLaneDst[routingSlot][e];
        mActiveEdges[mActiveEdgeCount].atten = a;
        mActiveEdgeCount++;
      }
    }

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // V/Oct: scalar expf per sample. Vectorizing expf via simd_exp
      // would require a 4-sample unroll, which conflicts with the
      // sample-serial feedback dependency (each sample's matrix*prev
      // reads the previous sample's sine output). Scalar expf is ~50
      // cycles/sample; total per-sample is well under budget.
      float q = vOct[i];
      if (q > 1.0f) q = 1.0f;
      if (q < -1.0f) q = -1.0f;
      const float ex = expf(q * glog2);

      // Phase advance (NEON 4-wide):
      //   phase[op] += (f0 * ratio[op] * sp) * ex + (detune[op] * sp)
      // Build vRatioF0Sp / vDetuneSp inside the loop so they don't
      // need to survive simd_sin further down.
      const float32x4_t vRatioF0Sp = vmulq_n_f32(vld1q_f32(mRatioFlat), f0Sp);
      const float32x4_t vDetuneSp  = vmulq_n_f32(vld1q_f32(mDetuneFlat), sp);
      float32x4_t vPhase = vld1q_f32(mPhaseBank);
      vPhase = vaddq_f32(vPhase, vmlaq_n_f32(vDetuneSp, vRatioF0Sp, ex));

      // Sync: hard reset on rising edge. Threshold > 0.5f per
      // feedback_comparator_gate_threshold.
      const bool high = sync[i] > 0.5f;
      if (high && !mSyncWasHigh)
      {
        vPhase = vdupq_n_f32(0.0f);
      }
      mSyncWasHigh = high;

      // Wrap phase to [0,1). vcvtq_s32_f32 truncates toward zero; equals
      // floor only for non-negative inputs. Phases monotonically
      // increase from 0 and are only reset to 0 via sync, so the
      // non-negative invariant holds.
      const float32x4_t vInt = vcvtq_f32_s32(vcvtq_s32_f32(vPhase));
      vPhase = vsubq_f32(vPhase, vInt);
      vst1q_f32(mPhaseBank, vPhase);

      // Matrix * prevOut via column-broadcast FMA chain. Load matrix
      // rows inside the loop (block-invariant but cheap to re-load)
      // so they don't have to survive simd_sin -- avoids the
      // `[sp :64]` NEON spill trap.
      const float32x4_t vRow0 = vld1q_f32(&mMatrixFlat[0]);
      const float32x4_t vRow1 = vld1q_f32(&mMatrixFlat[4]);
      const float32x4_t vRow2 = vld1q_f32(&mMatrixFlat[8]);
      const float32x4_t vRow3 = vld1q_f32(&mMatrixFlat[12]);
      float32x4_t vArg = vmulq_n_f32(vRow0, mPrevOutBank[0]);
      vArg = vmlaq_n_f32(vArg, vRow1, mPrevOutBank[1]);
      vArg = vmlaq_n_f32(vArg, vRow2, mPrevOutBank[2]);
      vArg = vmlaq_n_f32(vArg, vRow3, mPrevOutBank[3]);
      vArg = vaddq_f32(vArg, vPhase);
      vst1q_f32(mPhaseArgBank, vArg);

      // Vectorized sine via simd_sin over 4 packed args. Unchanged from
      // phase 2a; this is the same Cephes polynomial libcore uses.
      float32x4_t vSine = simd_sin(vmulq_n_f32(vArg, kTwoPi));
      vst1q_f32(mSineBank, vSine);

      // Output sum: NEON mul then horizontal reduce to scalar. vadd(low,
      // high) + vpadd is the 4->1 reduction pattern on ARMv7 NEON (no
      // vaddvq_f32 on Cortex-A8).
      float32x4_t vProd = vmulq_f32(vSine, vLevel);
      float32x2_t vPair = vadd_f32(vget_low_f32(vProd), vget_high_f32(vProd));
      vPair = vpadd_f32(vPair, vPair);
      const float pmmSum = vget_lane_f32(vPair, 0);

      // Phase 5d-1 wavetable shaper:
      //   1. Soft-sat PMM sum to (-1, 1) cheaply.
      //   2. Map sat to LUT index [0, 31] with linear interp.
      //   3. K=4 frame blend across the active scan window.
      //   4. Crossfade identity (sat) ↔ LUT-shaped by mWavetableBlend.
      // Scalar throughout -- 4-frame LUT lookup not worth NEONing
      // and `:64` hint risk avoided.
      const float absSum = pmmSum < 0.0f ? -pmmSum : pmmSum;
      const float sat = pmmSum / (1.0f + absSum);
      // 256-entry LUT: pos in [0, 255], linear interp between idxL and
      // idxR. (sat + 1) * 127.5 maps sat=-1 -> 0, sat=+1 -> 255.
      float pos = (sat + 1.0f) * 127.5f;
      if (pos < 0.0f) pos = 0.0f;
      if (pos > 255.0f) pos = 255.0f;
      const int idxL = (int)pos;
      const int idxR = idxL + 1 > 255 ? 255 : idxL + 1;
      const float frac = pos - (float)idxL;
      float shaped = 0.0f;
      for (int j = 0; j < K; j++)
      {
        const float *frame = mWavetableLUT[n0r + j];
        const float a = frame[idxL];
        const float b = frame[idxR];
        shaped += wNormalizedR[j] * (a + frac * (b - a));
      }
      const float wave_out = sat * (1.0f - wblend) + shaped * wblend;

      // Phase 5d-3 routing + filter pair. Sources are the 4 PMM op
      // outputs (mSineBank, just computed this sample) + filter LP/BP/HP
      // outputs (LP/BP from end-of-previous-sample integrator state;
      // HP reconstructed from input - k*BP - LP). 1-sample feedback
      // delay on filter sources is fine -- standard pattern for
      // self-modulating filters.
      // mRoutingSources / mRoutingDst are CLASS MEMBERS per
      // feedback_neon_intrinsics_drumvoice -- stack-local arrays here
      // produced gcc auto-vec :64 hints under -O3 -ffast-math.
      // PMM sources already bounded by sin to [-1, +1]. Filter sources
      // pass through softSat1 (x/(1+|x|)) so the routing matrix sees
      // them bounded -- prevents feedback runaway via filter->cutoff
      // FM compounding on itself across samples.
      mRoutingSources[0] = mSineBank[0];
      mRoutingSources[1] = mSineBank[1];
      mRoutingSources[2] = mSineBank[2];
      mRoutingSources[3] = mSineBank[3];
      mRoutingSources[4] = softSat1(mSvfIc2[0]);                                // F1 LP
      mRoutingSources[5] = softSat1(mSvfIc1[0]);                                // F1 BP
      mRoutingSources[6] = softSat1(wave_out - kFilterK * mSvfIc1[0] - mSvfIc2[0]);  // F1 HP
      mRoutingSources[7] = softSat1(mSvfIc2[1]);                                // F2 LP
      mRoutingSources[8] = softSat1(mSvfIc1[1]);                                // F2 BP
      mRoutingSources[9] = softSat1(wave_out - kFilterK * mSvfIc1[1] - mSvfIc2[1]);  // F2 HP

      mRoutingDst[0] = 0.0f; mRoutingDst[1] = 0.0f; mRoutingDst[2] = 0.0f;
      mRoutingDst[3] = 0.0f; mRoutingDst[4] = 0.0f; mRoutingDst[5] = 0.0f;
      for (int e = 0; e < mActiveEdgeCount; e++)
        mRoutingDst[mActiveEdges[e].dst] += mRoutingSources[mActiveEdges[e].src] * mActiveEdges[e].atten;

      // freqMod = verso - inverso pair per filter; addAdd directly into
      // filter input. fastExp(freqMod * 0.8) follows Som's convention
      // -- multiplicative cutoff modulation, bounded by fastExp's
      // internal clamping so the filter can't go totally unstable.
      const float freqMod0 = mRoutingDst[0] - mRoutingDst[1];
      const float freqMod1 = mRoutingDst[2] - mRoutingDst[3];
      const float addF1    = mRoutingDst[4];
      const float addF2    = mRoutingDst[5];

      // Clamp mg to keep the SVF coefficient stable. fastTan(pi*fc/sr)
      // = 8 corresponds to fc just below Nyquist; beyond that the
      // SVF math diverges. Floor at 0.001 keeps mg positive when
      // freqMod drives cutoff toward DC.
      const float kMaxMg = 8.0f;
      const float kMinMg = 0.001f;
      float mg0 = baseG0 * fastExp(freqMod0 * 0.8f);
      float mg1 = baseG1 * fastExp(freqMod1 * 0.8f);
      if (mg0 > kMaxMg) mg0 = kMaxMg; else if (mg0 < kMinMg) mg0 = kMinMg;
      if (mg1 > kMaxMg) mg1 = kMaxMg; else if (mg1 < kMinMg) mg1 = kMinMg;
      const float a01 = 1.0f / (1.0f + mg0 * (mg0 + kFilterK));
      const float a02 = mg0 * a01;
      const float a03 = mg0 * a02;
      const float a11 = 1.0f / (1.0f + mg1 * (mg1 + kFilterK));
      const float a12 = mg1 * a11;
      const float a13 = mg1 * a12;

      // Filter1: input = wave_out + addF1 (additive injection from routing)
      const float f1_in = wave_out + addF1;
      float v3 = f1_in - mSvfIc2[0];
      const float v1_0 = a01 * mSvfIc1[0] + a02 * v3;
      const float v2_0 = mSvfIc2[0] + a02 * mSvfIc1[0] + a03 * v3;
      // wideSoftClip on integrator state bounds it to ~+/-8 with tanh
      // asymptote beyond -- prevents lockup from feedback amplification
      // while preserving high-Q resonance up to that range.
      mSvfIc1[0] = wideSoftClip(2.0f * v1_0 - mSvfIc1[0]);
      mSvfIc2[0] = wideSoftClip(2.0f * v2_0 - mSvfIc2[0]);
      const float bpOut0 = v1_0;
      const float lpOut0 = v2_0;

      // Filter2 input: parallel <-> cascade morph + addF2
      const float f1_bridge = bpOut0 * (1.0f - bpLpBlend) + lpOut0 * bpLpBlend;
      const float f2_in = wave_out * (1.0f - topoMix) + f1_bridge * topoMix + addF2;

      // Filter2
      v3 = f2_in - mSvfIc2[1];
      const float v1_1 = a11 * mSvfIc1[1] + a12 * v3;
      const float v2_1 = mSvfIc2[1] + a12 * mSvfIc1[1] + a13 * v3;
      mSvfIc1[1] = wideSoftClip(2.0f * v1_1 - mSvfIc1[1]);
      mSvfIc2[1] = wideSoftClip(2.0f * v2_1 - mSvfIc2[1]);
      const float lpOut1 = v2_1;

      // Drive + hard clip (5d-2.1 -- preserves transient pop from
      // routing-driven filter resonance).
      float filt = lpOut1 * driveGain;
      if (filt > 1.0f) filt = 1.0f;
      else if (filt < -1.0f) filt = -1.0f;
      out[i] = filt * lvl;

      // Prev update for next sample's matrix/feedback.
      vst1q_f32(mPrevOutBank, vSine);
    }
  }

} // namespace stolmine
