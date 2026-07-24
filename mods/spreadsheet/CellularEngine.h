#pragma once

// CellularEngine -- the shared DSP core of the 1D elementary cellular-automaton
// wavetable-noise voice. The CA row IS the wavetable; a read head scans it at
// f0 and the CA advances one generation at each wavetable-pass BOUNDARY
// (phase-continuous, click-free) every NClk passes -- NClk high = a static
// structured tone, NClk=1 = a new generation every cycle = aperiodic
// rule-structured NOISE. Grain overlap layers recent generations at decorrelated
// phase offsets (equal-power); a pitch-tracked comb adds resonance.
//
// Extracted from the standalone Vivary unit so BOTH Vivary (full per-parameter
// control) and Rauschen (the CA folded onto its X/Y macros) drive the same core.
// setup() takes the SAME normalized 0..1 parameters as Vivary's controls; each
// caller resolves its own controls/macros down to these. No od::Object here --
// plain struct, all methods inline, no virtuals.
//
// am335x: the CA is integer/bitwise, run once per pass (branch-wrapped edges, no
// division); per-sample work is float interp reads + a VFP comb. No trig.

#include <od/config.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

namespace stolmine
{
  static const int kCAMaxCells = 256;
  static const int kCAGenHist  = 4;    // generations kept for grain overlap
  static const int kCAFbBufLen = 8192; // feedback comb delay buffer (pow2)
  static const int kCAFbMask   = kCAFbBufLen - 1;

  // Cubic soft clipper for the feedback loop: identity-slope at 0, bounded to
  // +/-2/3 -> the comb can't run away and self-limits (adds saturation when
  // pushed). No trig, all VFP.
  static inline float caSoftClip(float x)
  {
    if (x > 1.0f) return 0.66666667f;
    if (x < -1.0f) return -0.66666667f;
    return x - x * x * x * 0.33333333f;
  }

  // --- Value-noise field (for callers that map macros to an emergent per-
  // instance parameter landscape; see Rauschen's CA algorithm). Integer hash +
  // smoothstep bilinear over a small grid -> a smooth, seeded 2D field in [0,1).
  // No trig, no division; block-rate only. ---
  static inline uint32_t caHashU(uint32_t a)
  {
    a ^= a >> 16; a *= 0x7feb352du;
    a ^= a >> 15; a *= 0x846ca68bu;
    a ^= a >> 16; return a;
  }
  static inline float caGridVal(int i, int j, uint32_t seed)
  {
    uint32_t h = caHashU((uint32_t)i * 374761393u + (uint32_t)j * 668265263u + seed);
    return (float)(h >> 8) * (1.0f / 16777216.0f); // [0,1)
  }
  // grid = knots across the [0,1] macro square (a few islands per axis).
  static inline float caVNoise(float x, float y, uint32_t seed)
  {
    const float G = 3.0f;
    float fx = x * G, fy = y * G;
    int i = (int)fx, j = (int)fy;
    float tx = fx - (float)i, ty = fy - (float)j;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    float v00 = caGridVal(i, j, seed),     v10 = caGridVal(i + 1, j, seed);
    float v01 = caGridVal(i, j + 1, seed), v11 = caGridVal(i + 1, j + 1, seed);
    float a = v00 + tx * (v10 - v00);
    float b = v01 + tx * (v11 - v01);
    return a + ty * (b - a);
  }

  // Curated elementary rules: the sonically interesting (Wolfram class III/IV)
  // rules, DEDUPLICATED by symmetry class (mirror/complement twins removed) and
  // ranked by complexity = spatial-entropy x row-variety, from an offline sweep
  // of all 256 (planning/ca-rule-analysis.py). Every index is a distinct, lively
  // texture -- no dead rules.
  static const int kCANumRules = 48;
  static const uint8_t kCARules[kCANumRules] = {
    86, 163, 73, 101, 193, 57, 214, 129, 145, 188, 169, 65, 67, 150, 105, 107,
    81, 43, 212, 213, 209, 174, 185, 166, 3, 226, 241, 83, 240, 115, 85, 147,
    176, 122, 7, 88, 38, 167, 20, 151, 187, 175, 18, 146, 231, 144, 2, 199
  };

  // RADIUS-2 binary rules (5-neighbor elementary; 32-bit rule = LUT[32]). Same
  // sharp +/-1 edges as radius-1 (so the rhythmic bite is kept) but a wider
  // neighborhood -> richer long-range structure. Curated from a 16k sample of
  // the 2^32 space by a STRUCTURE metric (spectral tonality x timbral motion,
  // NOT chaos/entropy -- which produced undifferentiated mush), symmetry-
  // deduped. planning/ca-rule-analysis.py.
  static const int kCANumRulesR2 = 48;
  static const uint32_t kCARulesR2[kCANumRulesR2] = {
    3398369426u, 759324534u, 4177033246u, 4114992578u, 3086421935u, 3910581980u,
    1544131983u, 2329997991u, 369903460u, 1690060914u, 3221231564u, 3367379758u,
    619872570u, 3222040542u, 134872461u, 3954537172u, 1820643164u, 3318187892u,
    763436670u, 1329721008u, 3398736973u, 361192849u, 390675973u, 2915329299u,
    2828958812u, 2281223153u, 1343069455u, 202170121u, 2071495672u, 3745633775u,
    738237887u, 1614717498u, 4139086520u, 1602537455u, 4119543197u, 3289435452u,
    2501213765u, 1849757627u, 2063414177u, 638294042u, 3473376867u, 1693127281u,
    2554615134u, 1007213646u, 3578051715u, 4205207733u, 842670642u, 2876285495u
  };

  // EDGE OF CHAOS (Wolfram class IV): radius-1 rules with high variety AND
  // moderate change -> coherent gliders/particles that persist and travel while
  // the pattern keeps evolving (rule 110 & kin). Curated by variety x
  // moderate-change, symmetry-deduped. planning/ca-rule-analysis.py.
  static const int kCANumRulesEOC = 40;
  static const uint8_t kCARulesEOC[kCANumRulesEOC] = {
    230, 193, 158, 126, 161, 209, 173, 52, 208, 66, 149, 2, 20, 130, 175, 73,
    195, 213, 34, 169, 212, 171, 210, 240, 75, 18, 146, 22, 82, 176, 165, 145,
    185, 15, 43, 202, 234, 184, 218, 93
  };

  struct CellularEngine
  {
    // --- persistent state ---
    uint8_t mCells[kCAMaxCells];
    uint8_t mNext[kCAMaxCells];
    uint8_t mGenRing[kCAGenHist][kCAMaxCells]; // recent generations (grain ring)
    uint8_t mRuleTable[32];                     // radius-1 LUT[8] or radius-2 LUT[32]
    float mFbLine[kCAFbBufLen];                 // feedback comb delay
    int mGenHead;                               // newest generation in the ring
    int mFbW;                                   // feedback write index
    int mPassCount;
    int mResetCount;
    int mLastFamily;
    uint32_t mLastRule;
    float mPhase;
    float mFbLP;                                // feedback one-pole damping state

    // --- resolved block-rate params (set by setup(), used by tick()) ---
    int mRes, mMode, mNClk, mRClk, mActiveGen, mFbDelay;
    float mPhInc, mOvDelta, mOvInvNorm, mFbAmt;
    float mOvW[kCAGenHist];

    void init()
    {
      memset(mCells, 0, sizeof(mCells));
      memset(mNext, 0, sizeof(mNext));
      memset(mGenRing, 0, sizeof(mGenRing));
      memset(mRuleTable, 0, sizeof(mRuleTable));
      memset(mFbLine, 0, sizeof(mFbLine));
      mGenHead = 0;
      mFbW = 0;
      mPassCount = 0;
      mResetCount = 0;
      mLastFamily = -1;         // forces reseed + table build on the first setup
      mLastRule = 0xFFFFFFFFu;
      mPhase = 0.0f;
      mFbLP = 0.0f;
      mRes = 64; mMode = 0; mNClk = 1; mRClk = 0; mActiveGen = 1; mFbDelay = 100;
      mPhInc = 0.0f; mOvDelta = 0.0f; mOvInvNorm = 1.0f; mFbAmt = 0.0f;
      mOvW[0] = 1.0f; mOvW[1] = 0.0f; mOvW[2] = 0.0f; mOvW[3] = 0.0f;
    }

    // Reseed the row to a single live center cell (deterministic); fill the
    // generation-history ring so grain overlap has no stale generations.
    void reseed(int res)
    {
      for (int i = 0; i < res; i++) mCells[i] = 0;
      mCells[res / 2] = 1;
      for (int g = 0; g < kCAGenHist; g++)
        for (int i = 0; i < res; i++) mGenRing[g][i] = mCells[i];
    }

    // Uniform row (all cells equal) = static/DC -> the die-out watchdog.
    bool caIsUniform(int res)
    {
      const uint8_t v = mCells[0];
      for (int i = 1; i < res; i++)
        if (mCells[i] != v) return false;
      return true;
    }

    // Advance one binary CA generation (toroidal; branch-wrapped edges, no
    // division). mode 0 = radius-1 (3-neighbor LUT[8]); mode 1 = radius-2
    // (5-neighbor LUT[32]). Push the new generation into the grain ring.
    void caStep(int res, int mode)
    {
      if (mode == 1)
      {
        for (int i = 0; i < res; i++)
        {
          int l2 = i - 2; if (l2 < 0) l2 += res;
          int l1 = i - 1; if (l1 < 0) l1 += res;
          int r1 = i + 1; if (r1 >= res) r1 -= res;
          int r2 = i + 2; if (r2 >= res) r2 -= res;
          const int idx = (mCells[l2] << 4) | (mCells[l1] << 3) |
                          (mCells[i] << 2) | (mCells[r1] << 1) | mCells[r2];
          mNext[i] = mRuleTable[idx];
        }
      }
      else
      {
        for (int i = 0; i < res; i++)
        {
          const int l = mCells[(i == 0) ? (res - 1) : (i - 1)];
          const int c = mCells[i];
          const int r = mCells[(i == res - 1) ? 0 : (i + 1)];
          mNext[i] = mRuleTable[(l << 2) | (c << 1) | r];
        }
      }
      for (int i = 0; i < res; i++) mCells[i] = mNext[i];
      mGenHead = (mGenHead + 1) & (kCAGenHist - 1);
      for (int i = 0; i < res; i++) mGenRing[mGenHead][i] = mCells[i];
    }

    // Block-rate setup. Takes the SAME normalized 0..1 params as Vivary's
    // controls: family (int 0..2), and ruleN/resN/evolveN/resetN/overlapN/fbN in
    // 0..1; plus f0 (Hz) and sr. Resolves them into the members tick() reads.
    void setup(int family, float ruleN, float resN, float evolveN,
               float resetN, float overlapN, float fbN, float f0, float sr)
    {
      if (!(resN >= 0.0f)) resN = 0.0f; else if (resN > 1.0f) resN = 1.0f;
      mRes = 2 + (int)(resN * (float)(kCAMaxCells - 2) + 0.5f);

      if (family < 0) family = 0; else if (family > 2) family = 2;
      if (!(ruleN >= 0.0f)) ruleN = 0.0f; else if (ruleN > 1.0f) ruleN = 1.0f;

      // Pick the family's caStep mode, rule table, and rule number.
      // 0 = radius-1 chaos, 1 = radius-2 structure, 2 = edge-of-chaos (r1).
      int nbits = 8;
      uint32_t ruleNum;
      if (family == 1)
      {
        mMode = 1; nbits = 32;
        int idx = (int)(ruleN * (float)(kCANumRulesR2 - 1) + 0.5f);
        if (idx >= kCANumRulesR2) idx = kCANumRulesR2 - 1;
        ruleNum = kCARulesR2[idx];
      }
      else
      {
        mMode = 0;
        const uint8_t *tbl = (family == 0) ? kCARules : kCARulesEOC;
        const int count = (family == 0) ? kCANumRules : kCANumRulesEOC;
        int idx = (int)(ruleN * (float)(count - 1) + 0.5f);
        if (idx >= count) idx = count - 1;
        ruleNum = tbl[idx];
      }
      if (family != mLastFamily || ruleNum != mLastRule)
      {
        if (family != mLastFamily) reseed(mRes);
        for (int b = 0; b < nbits; b++)
          mRuleTable[b] = (uint8_t)((ruleNum >> b) & 1u);
        mLastFamily = family;
        mLastRule = ruleNum;
      }

      // Evolve: 1 = a new generation every pass (noise), 0 = frozen (static
      // tone). Maps to the CA clock interval nClk = 1..64 passes/update.
      if (!(evolveN >= 0.0f)) evolveN = 0.0f; else if (evolveN > 1.0f) evolveN = 1.0f;
      mNClk = 1 + (int)((1.0f - evolveN) * 63.0f + 0.5f);

      if (!(resetN >= 0.0f)) resetN = 0.0f; else if (resetN > 1.0f) resetN = 1.0f;
      mRClk = (int)(resetN * 256.0f + 0.5f); // 0 = off

      // Overlap: layer the last activeGen generations, each read at a DIFFERENT
      // phase offset so their sharp edges DON'T align and average (that averaging
      // was the lowpass) -> a decorrelated mashup. Equal-power sum.
      if (!(overlapN >= 0.0f)) overlapN = 0.0f; else if (overlapN > 1.0f) overlapN = 1.0f;
      mActiveGen = 1 + (int)(overlapN * (float)(kCAGenHist - 1) + 0.999f);
      if (mActiveGen < 1) mActiveGen = 1;
      else if (mActiveGen > kCAGenHist) mActiveGen = kCAGenHist;
      mOvDelta = overlapN * 0.5f;
      float ovW2 = 0.0f;
      for (int m = 0; m < mActiveGen; m++)
      {
        mOvW[m] = 1.0f / (1.0f + (float)m);
        ovW2 += mOvW[m] * mOvW[m];
      }
      mOvInvNorm = 1.0f / sqrtf(ovW2);

      // Feedback: pitch-tracked comb (delay = one wavetable period) -> resonant,
      // Karplus-like sustained tail. Damped + soft-clipped so it can't run away.
      if (!(fbN >= 0.0f)) fbN = 0.0f; else if (fbN > 1.0f) fbN = 1.0f;
      mFbAmt = fbN * 0.9f;
      mFbDelay = (f0 > 1.0f) ? (int)(sr / f0) : kCAFbMask;
      if (mFbDelay < 4) mFbDelay = 4;
      else if (mFbDelay > kCAFbMask) mFbDelay = kCAFbMask;

      mPhInc = f0 / sr;
    }

    // Per-sample: advance the read phase, advance the CA at pass boundaries,
    // grain-overlap read + pitch-tracked feedback comb. Returns one sample.
    float tick()
    {
      mPhase += mPhInc;
      if (mPhase >= 1.0f)
      {
        mPhase -= 1.0f;
        if (mPhase >= 1.0f) mPhase = 0.0f;   // guard f0 above SR
        if (++mPassCount >= mNClk)
        {
          mPassCount = 0;
          caStep(mRes, mMode);
          const bool dead = caIsUniform(mRes);
          if ((mRClk > 0 && ++mResetCount >= mRClk) || dead)
          {
            mResetCount = 0;
            reseed(mRes);
          }
        }
      }

      // Grain overlap: each generation grain read at its own phase offset so
      // edges decorrelate instead of averaging; equal-power sum.
      float acc = 0.0f;
      for (int m = 0; m < mActiveGen; m++)
      {
        float gp = mPhase + (float)m * mOvDelta;
        gp -= (float)(int)gp;                    // frac (mod 1)
        const float pg = gp * (float)mRes;
        int j0 = (int)pg;
        if (j0 >= mRes) j0 = mRes - 1;
        int j1 = j0 + 1;
        if (j1 >= mRes) j1 = 0;
        const float fr = pg - (float)j0;
        const uint8_t *row =
          mGenRing[(mGenHead - m + kCAGenHist) & (kCAGenHist - 1)];
        const float a0 = row[j0] ? 1.0f : -1.0f;
        const float a1 = row[j1] ? 1.0f : -1.0f;
        acc += mOvW[m] * (a0 + fr * (a1 - a0));
      }
      const float dry = acc * mOvInvNorm;

      // Pitch-tracked feedback comb (damped + soft-clipped). fb=0 -> dry clean.
      const int rd = (mFbW - mFbDelay) & kCAFbMask;
      mFbLP += 0.5f * (mFbLine[rd] - mFbLP);      // mild HF damping (Karplus)
      const float o = dry + mFbAmt * mFbLP;
      mFbLine[mFbW] = caSoftClip(o);              // bound the loop
      mFbW = (mFbW + 1) & kCAFbMask;
      return o;
    }
  };

} // namespace stolmine
