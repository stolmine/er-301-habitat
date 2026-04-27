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
#include <string.h>

namespace stolmine
{

  static const float kTwoPi = 6.28318530718f;
  static const float kLn2 = 0.69314718056f;
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
  static void fillPhase3Presets(float (&t)[64][29])
  {
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

    memset(mPhaseBank, 0, sizeof(mPhaseBank));
    memset(mPrevOutBank, 0, sizeof(mPrevOutBank));
    memset(mPhaseArgBank, 0, sizeof(mPhaseArgBank));
    memset(mSineBank, 0, sizeof(mSineBank));
    memset(mMatrixFlat, 0, sizeof(mMatrixFlat));
    memset(mRatioFlat, 0, sizeof(mRatioFlat));
    memset(mDetuneFlat, 0, sizeof(mDetuneFlat));
    memset(mLevelFlat, 0, sizeof(mLevelFlat));
    fillPhase3Presets(mPresetTable);
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
      // Detach -> revert to Phase 3 placeholder gradient.
      fillPhase3Presets(mPresetTable);
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
      out[0] = out[1] = out[2] = out[3] = out[4] = out[5] = 0.0f;
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

    // Stash for next frame's flux computation.
    for (int k = 0; k < 128; k++) prevMag[k] = scratchMag[k];
  }

  // Squared L2 distance between two 6-dim feature vectors (4 audio +
  // 2 binary-domain).
  static inline float l2Distance6sq(const float *a, const float *b)
  {
    const float d0 = a[0] - b[0];
    const float d1 = a[1] - b[1];
    const float d2 = a[2] - b[2];
    const float d3 = a[3] - b[3];
    const float d4 = a[4] - b[4];
    const float d5 = a[5] - b[5];
    return d0 * d0 + d1 * d1 + d2 * d2 + d3 * d3 + d4 * d4 + d5 * d5;
  }

  // Greedy farthest-point sampling: pick `count` indices from `candidates`
  // such that each pick maximizes the minimum distance to any prior pick.
  // O(count * candidates.size() * count) -- ~77K dist ops at 64 picks /
  // 1200 candidates / 60s sample. Negligible.
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
        // Distance to nearest existing pick (6-dim feature space).
        float minD = 1e20f;
        for (int q = 0; q < p; q++)
        {
          const float d = l2Distance6sq(&coarse[c * 6], &coarse[picks[q] * 6]);
          if (d < minD) minD = d;
        }
        if (minD > bestMinDist) { bestMinDist = minD; best = c; }
      }
      picks[p] = best;
    }
  }

  // Layer 1 derivation: 3-dim feature vector + slot index -> 29-float row.
  // Features (rms, zcr, brightness) drive the per-feature mappings.
  // The slot index drives a DIAGNOSTIC ratio sweep so it's obvious from
  // scan whether the analysis pipeline is running at all (without it,
  // identical features at every pick produce 64 identical rows). The
  // sweep is small enough not to dominate the feature-driven character
  // but unmistakable on the ear.
  __attribute__((noinline, optimize("no-tree-vectorize")))
  static void derivePresetRow(const float *feat, float *row, int slot)
  {
    const float rms = feat[0];
    const float zcr = feat[1];
    const float brightness = feat[2];   // Real spectral centroid in [0,1]
    const float flux = feat[3];          // Spectral flux (onset/transient energy)
    const float entropy = feat[4];       // Binary: 16-bucket Shannon entropy
    const float runLen  = feat[5];       // Binary: mean run length normalized
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
    // movement. runLen also reduces detune for stationary nodes
    // (intPull again -- consistent with ratio pull).
    const float detuneScale = 1.0f - intPull * 0.6f;
    row[8]  = 0.0f;
    row[9]  = (noisy * 8.0f  + fluxClamp * 4.0f  + entropy * 3.0f) * detuneScale;
    row[10] = (noisy * 14.0f + brightness * 6.0f + entropy * 4.0f) * detuneScale;
    row[11] = (noisy * 20.0f + fluxClamp * 10.0f + entropy * 5.0f) * detuneScale;

    // Matrix: pure feature-driven, no slot baseline. Per-path scales
    // up to ~0.5 so extreme features can reach the Phase 3 chaos
    // endpoint naturally. Tonally-flat samples will have low matrix
    // values and produce mostly additive sines; varied/transient
    // samples produce dense feature-shaped FM topology.
    for (int i = 0; i < 16; i++) row[12 + i] = 0.0f;

    // Matrix paths: audio features only. Binary features land in
    // ratios (runLen integer-pull above) and detunes (entropy detune
    // boost above). Adding them additively to matrix entries floods
    // the FM density baseline and saturates per-node variation into
    // uniform chaos -- learned from 147->148 regression where matrix
    // baseline went from ~0.3 to ~0.45 mean and scan flattened.
    row[12 + 0 * 4 + 1] = brightness * pitched * 0.50f + fluxClamp * 0.20f;  // A->B
    row[12 + 1 * 4 + 2] = brightness * 0.45f + pitched * 0.10f;              // B->C
    row[12 + 2 * 4 + 3] = pitched * 0.40f + fluxClamp * 0.15f;               // C->D
    row[12 + 1 * 4 + 0] = noisy * brightness * 0.40f;                        // B->A chaos
    row[12 + 2 * 4 + 1] = noisy * fluxClamp * 0.35f;                         // C->B
    row[12 + 3 * 4 + 0] = noisy * 0.30f + fluxClamp * 0.15f;                 // D->A
    row[12 + 0 * 4 + 0] = noisy * 0.40f;                                     // A self-fb
    row[12 + 1 * 4 + 1] = brightness * fluxClamp * 0.30f;                    // B self-fb
    row[12 + 2 * 4 + 2] = pitched * 0.25f;                                   // C self-fb

    // Reagent flag (Phase 8 O3 will fill).
    row[28] = 0.0f;
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

    // 6 features per frame: 4 audio (RMS, ZCR, centroid, flux) + 2 binary
    // (bucket entropy, mean run length).
    const int kFeatDim = 6;
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
      if (l2Distance6sq(&coarse[c * kFeatDim], &coarse[(c - 1) * kFeatDim]) > kChangeThresh2)
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
    }

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

    // Blend the 29 per-slot floats into the working voice state buffers.
    // Loop body is uniform across K (no differential dispatch per
    // feedback_runtime_branched_dsp_dispatch).
    for (int f = 0; f < 4; f++) mRatioFlat[f] = 0.0f;
    for (int f = 0; f < 4; f++) mLevelFlat[f] = 0.0f;
    for (int f = 0; f < 4; f++) mDetuneFlat[f] = 0.0f;
    for (int f = 0; f < 16; f++) mMatrixFlat[f] = 0.0f;
    for (int j = 0; j < K; j++)
    {
      const float wj = w[j] * wInv;
      const float *p = mPresetTable[n0 + j];
      for (int f = 0; f < 4; f++) mRatioFlat[f] += wj * p[f];
      for (int f = 0; f < 4; f++) mLevelFlat[f] += wj * p[4 + f];
      for (int f = 0; f < 4; f++) mDetuneFlat[f] += wj * p[8 + f];
      for (int f = 0; f < 16; f++) mMatrixFlat[f] += wj * p[12 + f];
    }
    // Apply the diagonal kFbScale after the blend, matching Phase 2b's
    // per-sample matrix-sum convention. Off-diagonal cross-mod stays 1x.
    mMatrixFlat[0 * 4 + 0] *= kFbScale;
    mMatrixFlat[1 * 4 + 1] *= kFbScale;
    mMatrixFlat[2 * 4 + 2] *= kFbScale;
    mMatrixFlat[3 * 4 + 3] *= kFbScale;

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

      // Output sum: NEON mul then horizontal reduce to scalar, scaled by
      // the global level. vadd(low, high) + vpadd is the 4->1 reduction
      // pattern on ARMv7 NEON (no vaddvq_f32 on Cortex-A8).
      float32x4_t vProd = vmulq_f32(vSine, vLevel);
      float32x2_t vPair = vadd_f32(vget_low_f32(vProd), vget_high_f32(vProd));
      vPair = vpadd_f32(vPair, vPair);
      out[i] = vget_lane_f32(vPair, 0) * lvl;

      // Prev update for next sample's matrix/feedback.
      vst1q_f32(mPrevOutBank, vSine);
    }
  }

} // namespace stolmine
