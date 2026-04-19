// Som -- Self-Organizing Map audio feature processor with Stuber-inspired coupled filter voice

#include "Som.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static const int kNumNodes = 64;
  static const int kNumDims = 6;

  // Fast tan approximation for SVF coefficient (Pade 3/3, accurate to ~0.01% for |x| < pi/4)
  static inline float fastTan(float x)
  {
    float x2 = x * x;
    return x * (1.0f + x2 * (1.0f / 3.0f + x2 * (2.0f / 15.0f))) /
               (1.0f + x2 * (-1.0f / 3.0f));
  }

  struct Som::Internal
  {
    float weights[kNumNodes][kNumDims];
    float activation[kNumNodes];
    float features[kNumDims];
    float prevFeatures[kNumDims];

    float sphereX[kNumNodes], sphereY[kNumNodes], sphereZ[kNumNodes];
    int scanChain[kNumNodes];
    float geodist[kNumNodes][kNumNodes];

    float rmsState;

    // Internal mod LFO
    float modPhase;
    float modPrevPhase;
    float modOutput;
    float modFbState;
    // Stateful noise shapes
    float henonX, henonY;
    float shHeld;
    float gendyPts[4];
    int gendyAge;

    // TPT SVF state (4 filters: 0,1=chA; 2,3=chB)
    float svfIc1[4], svfIc2[4];

    // Binary divider state (6-bit, /2 through /64)
    bool sqWasHigh[2];
    int dividerBits[2];     // 0-63
    int dividerJitter[2];

    // Staircase DAC output (64 levels)
    float staircase[2];

    // Temperature drift per filter
    float tempDrift[4];
    uint32_t driftCounter;

    // Mux routing tables: 64 nodes x 8 slots x 3 values (source, dest, attenuation)
    // 16 sources, 12 destinations
    float muxRouting[64][8][3];

    // Training accumulator per node (richness heuristic)
    float trainAccum[kNumNodes];

    // Current voice output for feedback
    float voiceOutA, voiceOutB;

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

      for (int n = 0; n < kNumNodes; n++)
        for (int d = 0; d < kNumDims; d++) {
          uint32_t h = (n * 374761393u + d * 668265263u) ^ 0x85ebca6bu;
          weights[n][d] = (float)((int32_t)(h >> 16) & 0xFFFF) / 65535.0f;
        }

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

      memset(activation, 0, sizeof(activation));
      memset(features, 0, sizeof(features));
      memset(prevFeatures, 0, sizeof(prevFeatures));
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

      // Generate routing tables: 16 sources, 12 destinations
      for (int n = 0; n < kNumNodes; n++) {
        float latitude = fabsf(sphereY[n]);
        float complexity = 1.0f - latitude;
        for (int slot = 0; slot < 8; slot++) {
          uint32_t h = (n * 374761393u + slot * 668265263u) ^ 0x85ebca6bu;
          int src = (int)((h >> 4) & 0xF);        // 0-15 (16 sources)
          int dst = (int)((h >> 12) & 0xF) % 12;  // 0-11 (12 destinations)
          float atten = complexity * ((float)((h >> 24) & 0xFF) / 255.0f) * 0.5f;
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

    float feedbackAmt = CLAMP(0.0f, 1.0f, mFeedback.value());
    float sumSq = 0.0f, sumAbsDiff = 0.0f, peak = 0.0f;
    int zeroCrossings = 0;
    if (feedbackAmt > 0.0f) {
      float fbMono = (s.voiceOutA + s.voiceOutB) * 0.5f;
      float dryAmt = 1.0f - feedbackAmt;
      for (int i = 0; i < FRAMELENGTH; i++) {
        float x = in[i] * dryAmt + fbMono * feedbackAmt;
        sumSq += x * x;
        float ax = (x < 0) ? -x : x;
        if (ax > peak) peak = ax;
        if (i > 0) {
          float xp = in[i-1] * dryAmt + fbMono * feedbackAmt;
          if ((x >= 0 && xp < 0) || (x < 0 && xp >= 0)) zeroCrossings++;
          float d = x - xp;
          sumAbsDiff += (d < 0) ? -d : d;
        }
      }
    } else {
      for (int i = 0; i < FRAMELENGTH; i++) {
        float x = in[i];
        sumSq += x * x;
        float ax = (x < 0) ? -x : x;
        if (ax > peak) peak = ax;
        if (i > 0) {
          float prev = in[i-1];
          if ((x >= 0 && prev < 0) || (x < 0 && prev >= 0)) zeroCrossings++;
          float d = x - prev;
          sumAbsDiff += (d < 0) ? -d : d;
        }
      }
    }
    float blockRms = sqrtf(sumSq / FRAMELENGTH);
    s.rmsState += (blockRms - s.rmsState) * 0.05f;
    float rms = s.rmsState;
    float zcr = (float)zeroCrossings / (float)FRAMELENGTH;
    float centroid = (rms > 0.001f) ? sumAbsDiff / ((FRAMELENGTH-1) * rms) : 0.0f;
    float crest = (rms > 0.001f) ? peak / rms : 1.0f;

    float flux = 0.0f;
    for (int d = 0; d < kNumDims; d++) flux += (s.features[d] - s.prevFeatures[d]) * (s.features[d] - s.prevFeatures[d]);
    flux = sqrtf(flux);

    memcpy(s.prevFeatures, s.features, sizeof(s.features));
    s.features[0] = rms;
    s.features[1] = zcr;
    s.features[2] = CLAMP(0.0f, 1.0f, centroid * 0.5f);
    s.features[3] = CLAMP(0.0f, 1.0f, peak);
    s.features[4] = CLAMP(0.0f, 1.0f, flux);
    s.features[5] = CLAMP(0.0f, 1.0f, (crest - 1.0f) * 0.2f);

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

    // Internal mod LFO (block-rate update)
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

      float lfoOut;
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
      lfoOut = w0 + (w1 - w0) * shapeFrac;

      s.modFbState = lfoOut;
      s.modOutput = lfoOut;
    }

    float modAmount = CLAMP(0.0f, 1.0f, mModAmount.value());
    float scanPos = CLAMP(0.0f, 1.0f, mScanPos.value()) + s.modOutput * modAmount * 0.5f;
    scanPos -= floorf(scanPos);
    float plasticity = CLAMP(0.0f, 1.0f, mPlasticity.value());
    float lr = CLAMP(0.01f, 1.0f, mLearningRate.value());
    float radius = CLAMP(0.05f, 0.5f, mNeighborhoodRadius.value());

    // Decay training accumulator (nodes sink back to surface over time)
    float decay = CLAMP(0.9f, 1.0f, mDecay.value());
    for (int n = 0; n < kNumNodes; n++)
      s.trainAccum[n] *= decay;

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

    float chainPos = scanPos * 63.0f;
    int idx0 = (int)chainPos;
    float frac = chainPos - (float)idx0;
    if (idx0 > 62) { idx0 = 62; frac = 1.0f; }
    int node0 = s.scanChain[idx0];
    int node1 = s.scanChain[idx0 + 1];
    mScanNode = node0;

    float vp[6];
    for (int d = 0; d < 6; d++)
      vp[d] = s.weights[node0][d] + (s.weights[node1][d] - s.weights[node0][d]) * frac;

    // Voice parameters from SOM
    float freqPrimary = 40.0f + vp[0] * 8000.0f;
    float freqRatio = 0.5f + vp[1] * 3.0f; // secondary filter ratio 0.5-3.5x
    float Q = 0.5f + vp[2] * 49.5f;
    float coupling = vp[3];
    float divBias = vp[4];
    float selfOsc = vp[5];

    int muxNode = node0;

    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float sr = globalConfig.sampleRate;

    float k = 1.0f / Q;
    float dryScale = 1.0f - selfOsc;
    float dryMix = 1.0f - mix;

    // Temperature drift update (block-rate random walk per filter)
    s.driftCounter++;
    for (int f = 0; f < 4; f++) {
      uint32_t h = (s.driftCounter * 374761393u + f * 668265263u) ^ 0x85ebca6bu;
      float step = (float)((int32_t)(h >> 16) & 0xFFFF) / 65535.0f - 0.5f;
      s.tempDrift[f] += step * 0.001f;
      s.tempDrift[f] *= 0.999f;
      s.tempDrift[f] = CLAMP(-0.02f, 0.02f, s.tempDrift[f]);
    }

    // 4 filter base coefficients with drift
    // Ch A: filter 0 = primary freq, filter 1 = primary * ratio
    // Ch B: filter 2 = primary freq, filter 3 = primary * ratio
    float filterFreq[4] = {
      freqPrimary * (1.0f + s.tempDrift[0]),
      freqPrimary * freqRatio * (1.0f + s.tempDrift[1]),
      freqPrimary * (1.0f + s.tempDrift[2]),
      freqPrimary * freqRatio * (1.0f + s.tempDrift[3])
    };
    float baseG[4];
    for (int f = 0; f < 4; f++)
      baseG[f] = fastTan(3.14159f * CLAMP(20.0f, sr * 0.49f, filterFreq[f]) / sr);

    // 6-bit divider jitter threshold
    int jitterA = 1 + (int)(divBias * 6.0f);
    int jitterB = jitterA + 3; // prime offset

    for (int i = 0; i < FRAMELENGTH; i++) {
      float dry = in[i];
      float inA = dry * dryScale;
      float inB = dry * dryScale;

      // 16-source array from 4 filters + dividers + input
      float sources[16] = {
        s.svfIc2[0], s.svfIc1[0], inA - k * s.svfIc1[0] - s.svfIc2[0],  // 0-2: Filt0 LP/BP/HP
        s.svfIc2[1], s.svfIc1[1], inA - k * s.svfIc1[1] - s.svfIc2[1],  // 3-5: Filt1 LP/BP/HP
        s.svfIc2[2], s.svfIc1[2], inB - k * s.svfIc1[2] - s.svfIc2[2],  // 6-8: Filt2 LP/BP/HP
        s.svfIc2[3], s.svfIc1[3], inB - k * s.svfIc1[3] - s.svfIc2[3],  // 9-11: Filt3 LP/BP/HP
        s.staircase[0], s.staircase[1],                                    // 12-13: Staircases
        dry, 0.0f                                                          // 14: Input, 15: Ground
      };

      // Hard-switched Sh'mance: only ONE active routing per divider state
      float dstAccum[12] = {};
      int muxState = s.dividerBits[0] & 7;
      int src = (int)s.muxRouting[muxNode][muxState][0];
      int dst = (int)s.muxRouting[muxNode][muxState][1];
      float att = s.muxRouting[muxNode][muxState][2];
      if (src < 16 && dst < 12)
        dstAccum[dst] += sources[src] * att * coupling;

      // Also apply the complementary channel's mux state for richer interaction
      int muxStateB = s.dividerBits[1] & 7;
      src = (int)s.muxRouting[muxNode][(muxStateB + 4) & 7][0]; // offset slot
      dst = (int)s.muxRouting[muxNode][(muxStateB + 4) & 7][1];
      att = s.muxRouting[muxNode][(muxStateB + 4) & 7][2];
      if (src < 16 && dst < 12)
        dstAccum[dst] += sources[src] * att * coupling * 0.5f;

      // Differential CV: verso (even) - inverso (odd) per filter
      float freqMod[4];
      freqMod[0] = dstAccum[0] - dstAccum[1];   // Filter 0 verso - inverso
      freqMod[1] = dstAccum[2] - dstAccum[3];   // Filter 1
      freqMod[2] = dstAccum[4] - dstAccum[5];   // Filter 2
      freqMod[3] = dstAccum[6] - dstAccum[7];   // Filter 3

      // Cross-coupling
      inA += dstAccum[8] * coupling;   // Coupling A→B into A
      inB += dstAccum[9] * coupling;   // Coupling B→A into B

      // Process 4 SVFs: cascade within channel
      // Channel A: input → filter 0 → filter 1
      float lpOut[4], bpOut[4];
      float filterIn = inA;
      for (int f = 0; f < 2; f++) {
        float mg = baseG[f] * (1.0f + CLAMP(-0.9f, 4.0f, freqMod[f] * 0.5f));
        float a1 = 1.0f / (1.0f + mg * (mg + k));
        float a2 = mg * a1;
        float a3 = mg * a2;
        float v3 = filterIn - s.svfIc2[f];
        float v1 = a1 * s.svfIc1[f] + a2 * v3;
        float v2 = s.svfIc2[f] + a2 * s.svfIc1[f] + a3 * v3;
        s.svfIc1[f] = 2.0f * v1 - s.svfIc1[f];
        s.svfIc2[f] = 2.0f * v2 - s.svfIc2[f];
        bpOut[f] = v1;
        lpOut[f] = v2;
        filterIn = v2; // cascade: LP output feeds next filter
      }

      // Channel B: input → filter 2 → filter 3
      filterIn = inB;
      for (int f = 2; f < 4; f++) {
        float mg = baseG[f] * (1.0f + CLAMP(-0.9f, 4.0f, freqMod[f] * 0.5f));
        float a1 = 1.0f / (1.0f + mg * (mg + k));
        float a2 = mg * a1;
        float a3 = mg * a2;
        float v3 = filterIn - s.svfIc2[f];
        float v1 = a1 * s.svfIc1[f] + a2 * v3;
        float v2 = s.svfIc2[f] + a2 * s.svfIc1[f] + a3 * v3;
        s.svfIc1[f] = 2.0f * v1 - s.svfIc1[f];
        s.svfIc2[f] = 2.0f * v2 - s.svfIc2[f];
        bpOut[f] = v1;
        lpOut[f] = v2;
        filterIn = v2;
      }

      // Squarewaveifier + 6-bit binary divider (from filter 1 and filter 3 BP)
      for (int ch = 0; ch < 2; ch++) {
        float bp = bpOut[ch * 2 + 1]; // filter 1 (chA) or filter 3 (chB)
        bool high = bp > 0.0f;
        if (high && !s.sqWasHigh[ch]) {
          s.dividerJitter[ch]++;
          // Divider rate modulated by routing matrix destinations 10-11
          float divMod = dstAccum[10 + ch] * 4.0f;
          int jThresh = (ch == 0) ? jitterA : jitterB;
          jThresh = MAX(1, jThresh + (int)divMod);
          if (s.dividerJitter[ch] >= jThresh) {
            s.dividerJitter[ch] = 0;
            s.dividerBits[ch] = (s.dividerBits[ch] + 1) & 63; // 6-bit
            s.staircase[ch] = (float)s.dividerBits[ch] / 63.0f * 2.0f - 1.0f;
          }
        }
        s.sqWasHigh[ch] = high;
      }

      // Output: last filter in each channel cascade
      float outA = tanhf(lpOut[1]);
      float outB = tanhf(lpOut[3]);
      s.voiceOutA = outA;
      s.voiceOutB = outB;

      outL[i] = dry * dryMix + outA * outputLevel * mix;
      outR[i] = dry * dryMix + outB * outputLevel * mix;

      if (++s.vizDecimCounter >= 8) {
        s.vizDecimCounter = 0;
        s.vizRing[s.vizPos] = (outL[i] + outR[i]) * 0.5f;
        s.vizPos = (s.vizPos + 1) & 127;
      }
    }
  }

} // namespace stolmine
