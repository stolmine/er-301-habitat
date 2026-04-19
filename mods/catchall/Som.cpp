// Som -- Self-Organizing Map audio feature processor with Stuber-inspired coupled filter voice
//
// Voice topology (per channel): two TPT SVFs that morph between parallel and
// cascade via a SOM-learned topology dim; staircase DAC (6-bit divider) mixed
// into output; 2x halfband oversampling across the inner loop; wide softclip
// only at the output boundary so high-Q resonance actually resonates.
//
// SOM dim mapping (6 dims, post-rework):
//   vp[0] -- primary filter frequency (40..8040 Hz)
//   vp[1] -- Q (0.5..50, shared across both filters in channel)
//   vp[2] -- topoMix (0 parallel, 1 cascade)
//   vp[3] -- bpLpBlend (BP->LP feed blend in cascade mode)
//   vp[4] -- stairMix (0 pure filter, 1 pure staircase DAC at output)
//   vp[5] -- drive (0.5x..3x input gain + silence-triggered noise exciter)
// Filter ratio is fixed at 1.5x (secondary filter center = primary * 1.5).

#include "Som.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static const int kNumNodes = 64;
  static const int kNumDims = 6;
  static const float kFreqRatio = 1.5f;

  // Fast tan approximation for SVF coefficient (Pade 3/3, ~0.01% for |x| < pi/4)
  static inline float fastTan(float x)
  {
    float x2 = x * x;
    return x * (1.0f + x2 * (1.0f / 3.0f + x2 * (2.0f / 15.0f))) /
               (1.0f + x2 * (-1.0f / 3.0f));
  }

  // Wide soft-clip: linear to +/-8, smooth rolloff, asymptote at +/-10.
  // Lets high-Q filter states actually swing freely before limiting.
  static inline float wideSoftClip(float x)
  {
    if (!isfinite(x)) return 0.0f;
    float ax = x < 0.0f ? -x : x;
    if (ax < 8.0f) return x;
    float sign = x > 0.0f ? 1.0f : -1.0f;
    return sign * (8.0f + tanhf((ax - 8.0f) * 0.5f) * 2.0f);
  }

  // exp approximation via 2^(x * log2(e)) with integer/fractional split.
  // Safe across full input range (clamped to [-16, 16]); monotonic and
  // always positive, which the SVF coefficient relies on.
  static inline float fastExp(float x)
  {
    x *= 1.4426950f; // log2(e)
    if (x >  16.0f) x =  16.0f;
    if (x < -16.0f) x = -16.0f;
    float xf = floorf(x);
    float fx = x - xf;
    // 2^fx polynomial on [0, 1]
    float poly = 1.0f + fx * (0.6931472f + fx * (0.2402265f + fx * 0.0555041f));
    union { float f; int32_t i; } u;
    int ix = (int)xf + 127;
    if (ix < 1) ix = 1;
    if (ix > 254) ix = 254;
    u.i = ix << 23;
    return poly * u.f;
  }

  struct Som::Internal
  {
    // SOM state
    float weights[kNumNodes][kNumDims];
    float activation[kNumNodes];
    float features[kNumDims];
    float prevFeatures[kNumDims];

    // Sphere geometry
    float sphereX[kNumNodes], sphereY[kNumNodes], sphereZ[kNumNodes];
    int scanChain[kNumNodes];
    float geodist[kNumNodes][kNumNodes];

    // Per-node geographic constants (populated at Init from latitude)
    int mActiveSlots[kNumNodes]; // 1..6, dense at equator
    int mDivThresh[kNumNodes];   // divider jitter threshold, slow at poles

    // Feature extraction state (audio-rate one-pole LP for brightness feature)
    float brightLp;
    float rmsState;

    // Internal mod LFO
    float modPhase;
    float modPrevPhase;
    float modOutput;
    float modFbState;
    float henonX, henonY;
    float shHeld;
    float gendyPts[4];
    int gendyAge;

    // TPT SVF state (4 filters: 0/1=chA, 2/3=chB)
    float svfIc1[4], svfIc2[4];

    // Binary divider state (6-bit, /2 through /64)
    bool sqWasHigh[2];
    int dividerBits[2];
    int dividerJitter[2];
    float staircase[2];

    // Temperature drift per filter
    float tempDrift[4];
    uint32_t driftCounter;

    // Mux routing tables: 64 nodes x 8 slots x 3 values (source, dest, atten)
    float muxRouting[64][8][3];

    // Training accumulator per node (richness heuristic for viz)
    float trainAccum[kNumNodes];

    // Current voice output (for input feedback path and viz)
    float voiceOutA, voiceOutB;

    // Smoothing state: voice parameters carried from previous block so the
    // inner loop can linearly interpolate per sample (avoids block-rate
    // zipper on topoMix / bpLpBlend / stairMix / drive / freqPrimary / Q).
    float prevVp[kNumDims];
    float prevCoupling;

    // White-noise exciter LFSR
    uint32_t noiseState;

    // Viz ring
    float vizRing[128];
    int vizPos, vizDecimCounter;

    void Init()
    {
      for (int i = 0; i < kNumNodes; i++) {
        float theta = acosf(1.0f - 2.0f * (i + 0.5f) / (float)kNumNodes);
        float phi = M_PI * (1.0f + sqrtf(5.0f)) * i;
        sphereX[i] = sinf(theta) * cosf(phi);
        sphereY[i] = sinf(theta) * sinf(phi);
        sphereZ[i] = cosf(theta);
      }

      for (int i = 0; i < kNumNodes; i++)
        for (int j = 0; j < kNumNodes; j++) {
          float dx = sphereX[i]-sphereX[j], dy = sphereY[i]-sphereY[j], dz = sphereZ[i]-sphereZ[j];
          geodist[i][j] = sqrtf(dx*dx + dy*dy + dz*dz);
        }

      // Deterministic sphere-map init: latitude/longitude drive a coherent
      // voice gradient. Scanning the chain at cold-start produces a musical
      // low-south -> bright-north sweep through the turbulent equator.
      // Training personalizes from this baseline.
      for (int n = 0; n < kNumNodes; n++) {
        float lat = (sphereY[n] + 1.0f) * 0.5f;   // 0 south, 1 north
        float absLat = fabsf(sphereY[n]);         // 0 equator, 1 poles
        float lon = (atan2f(sphereX[n], sphereZ[n]) + M_PI) * (1.0f / (2.0f * M_PI));
        float alt = (sphereZ[n] + 1.0f) * 0.5f;   // 0 back, 1 front

        weights[n][0] = 0.2f + lat * 0.7f;                  // freq
        weights[n][1] = 0.15f + absLat * 0.75f;             // Q: low equator, high poles
        weights[n][2] = alt;                                 // topoMix
        weights[n][3] = lon;                                 // bpLpBlend
        weights[n][4] = (1.0f - absLat) * 0.85f + 0.05f;    // stairMix: dense at equator
        weights[n][5] = 0.3f + lat * 0.5f;                  // drive
      }

      // Hamilton-like chain via greedy nearest-neighbor
      bool visited[kNumNodes] = {};
      scanChain[0] = 0;
      visited[0] = true;
      for (int step = 1; step < kNumNodes; step++) {
        int prev = scanChain[step-1];
        float bestDist = 1e10f;
        int bestNode = 0;
        for (int n = 0; n < kNumNodes; n++) {
          if (visited[n]) continue;
          if (geodist[prev][n] < bestDist) { bestDist = geodist[prev][n]; bestNode = n; }
        }
        scanChain[step] = bestNode;
        visited[bestNode] = true;
      }

      // Per-node mux density and divider threshold (geography = complexity).
      // Equator: many simultaneous routings + fast divider; poles: clean + slow.
      for (int n = 0; n < kNumNodes; n++) {
        float absLat = fabsf(sphereY[n]);
        float complexity = 1.0f - absLat;
        mActiveSlots[n] = 1 + (int)(complexity * 5.0f);   // 1..6
        mDivThresh[n]   = 1 + (int)(absLat * 6.0f);       // 1 equator, 7 poles
      }

      memset(activation, 0, sizeof(activation));
      memset(features, 0, sizeof(features));
      memset(prevFeatures, 0, sizeof(prevFeatures));
      brightLp = 0.0f;
      rmsState = 0.0f;
      modPhase = 0.0f;
      modPrevPhase = 0.0f;
      modOutput = 0.0f;
      modFbState = 0.0f;
      henonX = 0.1f;
      henonY = 0.3f;
      shHeld = 0.0f;
      for (int i = 0; i < 4; i++) {
        uint32_t h = (i * 374761393u) ^ 0x85ebca6bu;
        gendyPts[i] = (float)((int32_t)(h >> 16) & 0xFFFF) / 32768.0f - 1.0f;
      }
      gendyAge = 0;

      for (int f = 0; f < 4; f++) { svfIc1[f] = svfIc2[f] = 0.0f; tempDrift[f] = 0.0f; }
      sqWasHigh[0] = sqWasHigh[1] = false;
      dividerBits[0] = dividerBits[1] = 0;
      dividerJitter[0] = dividerJitter[1] = 0;
      staircase[0] = staircase[1] = 0.0f;
      driftCounter = 0;
      memset(trainAccum, 0, sizeof(trainAccum));
      voiceOutA = voiceOutB = 0.0f;
      for (int d = 0; d < kNumDims; d++) prevVp[d] = 0.5f;
      prevCoupling = 0.6f;
      noiseState = 0x12345678u;

      // Routing tables: 16 sources x 12 destinations x 8 slots per node.
      // Slot atten scales with node complexity (equator louder couplings).
      for (int n = 0; n < kNumNodes; n++) {
        float absLat = fabsf(sphereY[n]);
        float complexity = 1.0f - absLat;
        for (int slot = 0; slot < 8; slot++) {
          uint32_t h = (n * 374761393u + slot * 668265263u) ^ 0x85ebca6bu;
          int src = (int)((h >> 4) & 0xF);
          int dst = (int)((h >> 12) & 0xF) % 12;
          float atten = (0.2f + complexity * 0.6f) * ((float)((h >> 24) & 0xFF) / 255.0f);
          muxRouting[n][slot][0] = (float)src;
          muxRouting[n][slot][1] = (float)dst;
          muxRouting[n][slot][2] = atten;
        }
      }

      memset(vizRing, 0, sizeof(vizRing));
      vizPos = 0;
      vizDecimCounter = 0;
    }
  };

  Som::Som()
  {
    addInput(mIn);
    addOutput(mOutL);
    addOutput(mOutR);
    addParameter(mPlasticity);
    addParameter(mScanPos);
    addParameter(mMix);
    addParameter(mOutputLevel);
    addParameter(mParallax);
    addParameter(mModAmount);
    addParameter(mModRate);
    addParameter(mModShape);
    addParameter(mModFeedback);
    addParameter(mNeighborhoodRadius);
    addParameter(mLearningRate);
    addParameter(mFeedback);
    addParameter(mDecay);
    mpInternal = new Internal();
    mpInternal->Init();
  }

  Som::~Som()
  {
    delete mpInternal;
  }

  float Som::getNodeWeight(int node, int dim)
  {
    if (node < 0 || node >= kNumNodes || dim < 0 || dim >= kNumDims) return 0.0f;
    return mpInternal->weights[node][dim];
  }

  float Som::getNodeActivation(int node)
  {
    if (node < 0 || node >= kNumNodes) return 0.0f;
    return mpInternal->activation[node];
  }

  int Som::getBMU() { return mBMU; }

  float Som::getOutputSample(int idx)
  {
    Internal &s = *mpInternal;
    return s.vizRing[(s.vizPos + idx) & 127];
  }

  float Som::getNodeX(int node)
  {
    if (node < 0 || node >= kNumNodes) return 0.0f;
    return mpInternal->sphereX[node];
  }

  float Som::getNodeY(int node)
  {
    if (node < 0 || node >= kNumNodes) return 0.0f;
    return mpInternal->sphereY[node];
  }

  float Som::getNodeZ(int node)
  {
    if (node < 0 || node >= kNumNodes) return 0.0f;
    return mpInternal->sphereZ[node];
  }

  float Som::getVoiceStateA() { return mpInternal->voiceOutA; }
  float Som::getVoiceStateB() { return mpInternal->voiceOutB; }
  int Som::getScanNode() { return mScanNode; }
  float Som::getNodeRichness(int node) { return mpInternal->trainAccum[CLAMP(0, kNumNodes - 1, node)]; }

  void Som::process()
  {
    Internal &s = *mpInternal;
    float *in = mIn.buffer();
    float *outL = mOutL.buffer();
    float *outR = mOutR.buffer();
    const bool stereo = (mChannelCount > 1);

    // ---- Feature extraction (block-rate) ----
    float feedbackAmt = CLAMP(0.0f, 1.0f, mFeedback.value());
    float fbMono = (s.voiceOutA + s.voiceOutB) * 0.5f;
    float dryAmt = 1.0f - feedbackAmt;

    float sumSq = 0.0f, hpEnergy = 0.0f, lpEnergy = 0.0f, peak = 0.0f;
    int zeroCrossings = 0;
    float prevX = 0.0f;
    for (int i = 0; i < FRAMELENGTH; i++) {
      float x = (feedbackAmt > 0.0f)
                  ? in[i] * dryAmt + fbMono * feedbackAmt
                  : in[i];
      // One-pole LP/HP split for brightness feature (~75 Hz LP at 48k).
      s.brightLp += (x - s.brightLp) * 0.01f;
      float hp = x - s.brightLp;
      lpEnergy += s.brightLp * s.brightLp;
      hpEnergy += hp * hp;

      sumSq += x * x;
      float ax = x < 0 ? -x : x;
      if (ax > peak) peak = ax;
      if (i > 0) {
        if ((x >= 0 && prevX < 0) || (x < 0 && prevX >= 0)) zeroCrossings++;
      }
      prevX = x;
    }
    float blockRms = sqrtf(sumSq / FRAMELENGTH);
    s.rmsState += (blockRms - s.rmsState) * 0.05f;
    float rms = s.rmsState;
    float zcr = (float)zeroCrossings / (float)FRAMELENGTH;
    float brightness = hpEnergy / (lpEnergy + hpEnergy + 1e-6f);
    float crest = (rms > 0.001f) ? peak / rms : 1.0f;

    float flux = 0.0f;
    for (int d = 0; d < kNumDims; d++) {
      float df = s.features[d] - s.prevFeatures[d];
      flux += df * df;
    }
    flux = sqrtf(flux);

    memcpy(s.prevFeatures, s.features, sizeof(s.features));
    // Clip to [0.05, 0.95] instead of [0, 1] so features can never saturate
    // hard -- that's what keeps weights from pinning to 0/1 under long
    // self-feedback runs, without gating the feedback path itself.
    auto featClip = [](float x) {
      if (x < 0.05f) return 0.05f;
      if (x > 0.95f) return 0.95f;
      return x;
    };
    s.features[0] = featClip(rms);
    s.features[1] = featClip(zcr * 2.0f);
    s.features[2] = featClip(brightness);
    s.features[3] = featClip(peak);
    s.features[4] = featClip(flux);
    s.features[5] = featClip((crest - 1.0f) * 0.2f);

    // ---- BMU search ----
    int bmu = 0;
    float bmuDist = 1e10f;
    for (int n = 0; n < kNumNodes; n++) {
      float dist = 0.0f;
      for (int d = 0; d < kNumDims; d++) {
        float diff = s.features[d] - s.weights[n][d];
        dist += diff * diff;
      }
      s.activation[n] = dist;
      if (dist < bmuDist) { bmuDist = dist; bmu = n; }
    }
    mBMU = bmu;

    // ---- Internal mod LFO (block-rate) ----
    {
      float modRate = CLAMP(0.001f, 20.0f, mModRate.value());
      float modShape = CLAMP(0.0f, 1.0f, mModShape.value());
      float modFb = CLAMP(0.0f, 0.95f, mModFeedback.value());

      float rateHz = modRate * (1.0f + s.modFbState * modFb * 2.0f);
      if (rateHz < 0.001f) rateHz = 0.001f;
      if (rateHz > 40.0f) rateHz = 40.0f;
      float blocksPerSec = globalConfig.sampleRate / (float)FRAMELENGTH;
      s.modPhase += rateHz / blocksPerSec;
      s.modPhase -= floorf(s.modPhase);
      float p = s.modPhase;

      bool wrapped = (s.modPhase < s.modPrevPhase);
      s.modPrevPhase = s.modPhase;

      if (wrapped) {
        uint32_t h = (uint32_t)(s.modPhase * 100000.0f + 1.0f) * 374761393u;
        s.shHeld = (float)((int32_t)(h >> 16) & 0xFFFF) / 32768.0f - 1.0f;
        float nx = 1.0f - 1.4f * s.henonX * s.henonX + s.henonY;
        float ny = 0.3f * s.henonX;
        s.henonX = CLAMP(-2.0f, 2.0f, nx);
        s.henonY = CLAMP(-2.0f, 2.0f, ny);
        s.gendyAge++;
        for (int i = 0; i < 4; i++) {
          uint32_t gh = (s.gendyAge * 2654435761u + i * 668265263u) ^ 0x85ebca6bu;
          float perturb = (float)((int32_t)(gh >> 16) & 0xFFFF) / 65535.0f - 0.5f;
          s.gendyPts[i] = CLAMP(-1.0f, 1.0f, s.gendyPts[i] + perturb * 0.4f);
        }
      }

      float shapeSel = modShape * 8.0f;
      int shapeIdx = (int)shapeSel;
      float shapeFrac = shapeSel - (float)shapeIdx;
      auto evalShape = [&](int idx, float ph) -> float {
        switch (idx) {
        case 0: return sinf(ph * 6.28318f);
        case 1: return (ph < 0.5f) ? (ph * 4.0f - 1.0f) : (3.0f - ph * 4.0f);
        case 2: return ph * 2.0f - 1.0f;
        case 3: return (ph < 0.5f) ? 1.0f : -1.0f;
        case 4: return s.shHeld;
        case 5: return sinf(ph * 6.28318f * 3.0f) * 0.7f + sinf(ph * 6.28318f * 7.0f) * 0.3f;
        case 6: return s.henonX * 0.5f;
        case 7: return sinf(ph * 6.28318f * 2.3f) * 0.5f
                     + sinf(ph * 6.28318f * 5.7f) * 0.3f
                     + sinf(ph * 6.28318f * 11.1f) * 0.2f;
        default: {
          float t = ph * 4.0f;
          int seg = (int)t;
          float frac = t - (float)seg;
          float v0 = s.gendyPts[seg & 3];
          float v1 = s.gendyPts[(seg + 1) & 3];
          return v0 + (v1 - v0) * frac * frac * (3.0f - 2.0f * frac);
        }
        }
      };
      float w0 = evalShape(shapeIdx, p);
      float w1 = evalShape((shapeIdx + 1) % 9, p);
      s.modOutput = w0 + (w1 - w0) * shapeFrac;
      s.modFbState = s.modOutput;
    }

    float modAmount = CLAMP(0.0f, 1.0f, mModAmount.value());
    float scanPos = CLAMP(0.0f, 1.0f, mScanPos.value()) + s.modOutput * modAmount * 0.5f;
    scanPos -= floorf(scanPos);
    float plasticity = CLAMP(0.0f, 1.0f, mPlasticity.value());
    float lr = CLAMP(0.01f, 1.0f, mLearningRate.value());
    float radius = CLAMP(0.05f, 0.5f, mNeighborhoodRadius.value());

    // ---- Viz richness decay + gentle weight gravity toward center ----
    float decay = CLAMP(0.9f, 1.0f, mDecay.value());
    const float kGravity = 1e-5f; // pulls weights to 0.5 on a ~20 min timescale
    for (int n = 0; n < kNumNodes; n++) {
      s.trainAccum[n] *= decay;
      for (int d = 0; d < kNumDims; d++)
        s.weights[n][d] += (0.5f - s.weights[n][d]) * kGravity;
    }

    // ---- Training (plasticity-gated) ----
    if (plasticity > 0.001f) {
      float parallax = CLAMP(-1.0f, 1.0f, mParallax.value());
      float writePos = scanPos + parallax * 0.5f;
      writePos -= floorf(writePos);
      float writeChainPos = writePos * 63.0f;
      int writeIdx = CLAMP(0, 63, (int)(writeChainPos + 0.5f));
      int writeNode = s.scanChain[writeIdx];

      float invR2 = 1.0f / (2.0f * radius * radius);
      for (int n = 0; n < kNumNodes; n++) {
        float gd = s.geodist[writeNode][n];
        if (gd > radius * 3.0f) continue;
        float h = expf(-gd * gd * invR2);
        float rate = plasticity * lr * h;
        for (int d = 0; d < kNumDims; d++)
          s.weights[n][d] += rate * (s.features[d] - s.weights[n][d]);
        s.trainAccum[n] += rate;
      }
    }

    // ---- Scan: pick voice parameters from interpolated weights ----
    float chainPos = scanPos * 63.0f;
    int idx0 = (int)chainPos;
    float frac = chainPos - (float)idx0;
    if (idx0 > 62) { idx0 = 62; frac = 1.0f; }
    int node0 = s.scanChain[idx0];
    int node1 = s.scanChain[idx0 + 1];
    mScanNode = node0;

    float vpNew[kNumDims];
    for (int d = 0; d < kNumDims; d++)
      vpNew[d] = s.weights[node0][d] + (s.weights[node1][d] - s.weights[node0][d]) * frac;

    int muxNode = node0;
    int numSlots = s.mActiveSlots[muxNode];
    int jitterA = s.mDivThresh[muxNode];
    int jitterB = jitterA + 3;
    float couplingNew = 0.4f + (1.0f - fabsf(s.sphereY[muxNode])) * 0.8f;

    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float dryMix = 1.0f - mix;
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float sr = globalConfig.sampleRate;
    float srOS = sr * 2.0f;
    float invSrOS = 1.0f / srOS;

    // ---- Per-sample interpolation setup (prev -> new across the block) ----
    // Avoids zipper from block-rate jumps in any voice parameter.
    const float invBlock = 1.0f / (float)FRAMELENGTH;
    float vpDelta[kNumDims];
    for (int d = 0; d < kNumDims; d++)
      vpDelta[d] = (vpNew[d] - s.prevVp[d]) * invBlock;
    float couplingDelta = (couplingNew - s.prevCoupling) * invBlock;

    // ---- Temperature drift (block-rate random walk per filter) ----
    s.driftCounter++;
    for (int f = 0; f < 4; f++) {
      uint32_t h = (s.driftCounter * 374761393u + f * 668265263u) ^ 0x85ebca6bu;
      float step = (float)((int32_t)(h >> 16) & 0xFFFF) / 65535.0f - 0.5f;
      s.tempDrift[f] += step * 0.001f;
      s.tempDrift[f] *= 0.999f;
      s.tempDrift[f] = CLAMP(-0.02f, 0.02f, s.tempDrift[f]);
    }

    // ---- Inner per-sample loop, 2x oversampled ----
    for (int i = 0; i < FRAMELENGTH; i++) {
      float dry = in[i];

      // Linearly interpolate voice parameters to this sample to kill zipper.
      float fi = (float)(i + 1);
      float vp[kNumDims];
      for (int d = 0; d < kNumDims; d++)
        vp[d] = s.prevVp[d] + vpDelta[d] * fi;
      float coupling    = s.prevCoupling + couplingDelta * fi;
      float freqPrimary = 40.0f + vp[0] * 8000.0f;
      float Q           = 0.5f + vp[1] * 49.5f;
      float topoMix     = CLAMP(0.0f, 1.0f, vp[2]);
      float bpLpBlend   = CLAMP(0.0f, 1.0f, vp[3]);
      float stairMix    = CLAMP(0.0f, 1.0f, vp[4]);
      float drive       = CLAMP(0.0f, 1.0f, vp[5]);
      float k           = 1.0f / Q;
      float driveGain   = 0.5f + drive * 2.5f;

      // Per-sample base coefficients (tempDrift + kFreqRatio are block-rate
      // but the tan is cheap enough to evaluate per sample).
      float baseG[4];
      {
        float f0 = freqPrimary              * (1.0f + s.tempDrift[0]);
        float f1 = freqPrimary * kFreqRatio * (1.0f + s.tempDrift[1]);
        float f2 = freqPrimary              * (1.0f + s.tempDrift[2]);
        float f3 = freqPrimary * kFreqRatio * (1.0f + s.tempDrift[3]);
        float ffs[4] = {f0, f1, f2, f3};
        for (int f = 0; f < 4; f++) {
          float ff = ffs[f];
          if (ff < 5.0f) ff = 5.0f;
          if (ff > srOS * 0.499f) ff = srOS * 0.499f;
          baseG[f] = fastTan(3.14159f * ff * invSrOS);
        }
      }

      float subOutA[2] = {0.0f, 0.0f};
      float subOutB[2] = {0.0f, 0.0f};

      for (int sub = 0; sub < 2; sub++) {
        // Drive + silence-gated noise exciter: keeps high-Q filter singing
        // even with no input, so self-oscillation is audible-by-default.
        s.noiseState = s.noiseState * 1664525u + 1013904223u;
        float noise = ((float)((int32_t)(s.noiseState >> 8) & 0xFFFF) / 32768.0f - 1.0f);
        float silenceGain = 1.0f / (1.0f + rms * 20.0f);
        float exciter = noise * drive * 0.008f * silenceGain;
        float inA = dry * driveGain + exciter;
        float inB = inA; // same driven input at filter input; topology diverges below

        // Build 16-source array from SVF ic1/ic2 (BP/HP reconstructed) + stair + input
        float sources[16] = {
          s.svfIc2[0], s.svfIc1[0], inA - k * s.svfIc1[0] - s.svfIc2[0],  // F0 LP/BP/HP
          s.svfIc2[1], s.svfIc1[1], inA - k * s.svfIc1[1] - s.svfIc2[1],  // F1 LP/BP/HP
          s.svfIc2[2], s.svfIc1[2], inB - k * s.svfIc1[2] - s.svfIc2[2],  // F2
          s.svfIc2[3], s.svfIc1[3], inB - k * s.svfIc1[3] - s.svfIc2[3],  // F3
          s.staircase[0], s.staircase[1],
          dry, 0.0f
        };

        // Sh'mance mux: iterate N active slots (1..6 by geography).
        // Primary slot = dividerBits[0] & 7; next slots walk around cyclically.
        float dstAccum[12] = {};
        int baseSlot = s.dividerBits[0] & 7;
        for (int sl = 0; sl < numSlots; sl++) {
          int slot = (baseSlot + sl) & 7;
          int src = (int)s.muxRouting[muxNode][slot][0];
          int dst = (int)s.muxRouting[muxNode][slot][1];
          float att = s.muxRouting[muxNode][slot][2];
          if (src < 16 && dst < 12) {
            float slotWeight = 1.0f / (1.0f + (float)sl * 0.5f);
            dstAccum[dst] += sources[src] * att * coupling * slotWeight;
          }
        }

        // Differential freqMod (verso - inverso) per filter
        float freqMod[4];
        freqMod[0] = dstAccum[0] - dstAccum[1];
        freqMod[1] = dstAccum[2] - dstAccum[3];
        freqMod[2] = dstAccum[4] - dstAccum[5];
        freqMod[3] = dstAccum[6] - dstAccum[7];

        // Cross-coupling additive injection into channel inputs
        inA += dstAccum[8] * coupling;
        if (stereo) inB += dstAccum[9] * coupling;

        // ---- Channel A: filter 0 -> (topoMix * bridge) -> filter 1 ----
        float lpOut[4], bpOut[4];
        {
          // Filter 0 on raw inA
          float mg = baseG[0] * fastExp(freqMod[0] * 0.8f);
          float a1 = 1.0f / (1.0f + mg * (mg + k));
          float a2 = mg * a1;
          float a3 = mg * a2;
          float v3 = inA - s.svfIc2[0];
          float v1 = a1 * s.svfIc1[0] + a2 * v3;
          float v2 = s.svfIc2[0] + a2 * s.svfIc1[0] + a3 * v3;
          s.svfIc1[0] = 2.0f * v1 - s.svfIc1[0];
          s.svfIc2[0] = 2.0f * v2 - s.svfIc2[0];
          bpOut[0] = v1;
          lpOut[0] = v2;
        }
        {
          // Filter 1 input: parallel sees inA; cascade sees BP/LP blend of F0.
          float f0_bridge = bpOut[0] * (1.0f - bpLpBlend) + lpOut[0] * bpLpBlend;
          float f1_in = inA * (1.0f - topoMix) + f0_bridge * topoMix;
          float mg = baseG[1] * fastExp(freqMod[1] * 0.8f);
          float a1 = 1.0f / (1.0f + mg * (mg + k));
          float a2 = mg * a1;
          float a3 = mg * a2;
          float v3 = f1_in - s.svfIc2[1];
          float v1 = a1 * s.svfIc1[1] + a2 * v3;
          float v2 = s.svfIc2[1] + a2 * s.svfIc1[1] + a3 * v3;
          s.svfIc1[1] = 2.0f * v1 - s.svfIc1[1];
          s.svfIc2[1] = 2.0f * v2 - s.svfIc2[1];
          bpOut[1] = v1;
          lpOut[1] = v2;
        }

        // ---- Channel B (stereo only) ----
        if (stereo) {
          {
            float mg = baseG[2] * fastExp(freqMod[2] * 0.8f);
            float a1 = 1.0f / (1.0f + mg * (mg + k));
            float a2 = mg * a1;
            float a3 = mg * a2;
            float v3 = inB - s.svfIc2[2];
            float v1 = a1 * s.svfIc1[2] + a2 * v3;
            float v2 = s.svfIc2[2] + a2 * s.svfIc1[2] + a3 * v3;
            s.svfIc1[2] = 2.0f * v1 - s.svfIc1[2];
            s.svfIc2[2] = 2.0f * v2 - s.svfIc2[2];
            bpOut[2] = v1;
            lpOut[2] = v2;
          }
          {
            float f2_bridge = bpOut[2] * (1.0f - bpLpBlend) + lpOut[2] * bpLpBlend;
            float f3_in = inB * (1.0f - topoMix) + f2_bridge * topoMix;
            float mg = baseG[3] * fastExp(freqMod[3] * 0.8f);
            float a1 = 1.0f / (1.0f + mg * (mg + k));
            float a2 = mg * a1;
            float a3 = mg * a2;
            float v3 = f3_in - s.svfIc2[3];
            float v1 = a1 * s.svfIc1[3] + a2 * v3;
            float v2 = s.svfIc2[3] + a2 * s.svfIc1[3] + a3 * v3;
            s.svfIc1[3] = 2.0f * v1 - s.svfIc1[3];
            s.svfIc2[3] = 2.0f * v2 - s.svfIc2[3];
            bpOut[3] = v1;
            lpOut[3] = v2;
          }
        }

        // ---- Squarewaveifier + 6-bit divider on F1/F3 BP ----
        int chMax = stereo ? 2 : 1;
        for (int ch = 0; ch < chMax; ch++) {
          float bp = bpOut[ch * 2 + 1];
          bool high = bp > 0.0f;
          if (high && !s.sqWasHigh[ch]) {
            s.dividerJitter[ch]++;
            float divMod = dstAccum[10 + ch] * 4.0f;
            int jThresh = (ch == 0) ? jitterA : jitterB;
            jThresh = MAX(1, jThresh + (int)divMod);
            if (s.dividerJitter[ch] >= jThresh) {
              s.dividerJitter[ch] = 0;
              s.dividerBits[ch] = (s.dividerBits[ch] + 1) & 63;
              s.staircase[ch] = (float)s.dividerBits[ch] / 63.0f * 2.0f - 1.0f;
            }
          }
          s.sqWasHigh[ch] = high;
        }

        // ---- Voice sub-output: filter LP + staircase DAC mix ----
        float voiceA = lpOut[1] * (1.0f - stairMix) + s.staircase[0] * stairMix * 0.8f;
        float voiceB = stereo
                       ? (lpOut[3] * (1.0f - stairMix) + s.staircase[1] * stairMix * 0.8f)
                       : voiceA;
        subOutA[sub] = voiceA;
        subOutB[sub] = voiceB;
      } // end 2x oversample

      // 2-tap halfband decimation
      float voiceA = 0.5f * (subOutA[0] + subOutA[1]);
      float voiceB = 0.5f * (subOutB[0] + subOutB[1]);

      // Wide soft-clip only at output boundary
      voiceA = wideSoftClip(voiceA);
      voiceB = wideSoftClip(voiceB);

      s.voiceOutA = voiceA;
      s.voiceOutB = voiceB;

      outL[i] = dry * dryMix + voiceA * outputLevel * mix;
      outR[i] = dry * dryMix + voiceB * outputLevel * mix;

      if (++s.vizDecimCounter >= 8) {
        s.vizDecimCounter = 0;
        s.vizRing[s.vizPos] = (outL[i] + outR[i]) * 0.5f;
        s.vizPos = (s.vizPos + 1) & 127;
      }
    }

    // Persist interpolation endpoints for next block.
    for (int d = 0; d < kNumDims; d++) s.prevVp[d] = vpNew[d];
    s.prevCoupling = couplingNew;
  }

} // namespace stolmine
