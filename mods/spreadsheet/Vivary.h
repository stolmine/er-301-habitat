#pragma once

// Vivary -- a generative noise/texture source built on a 1D ELEMENTARY
// cellular automaton (working name; see planning/ca-wavetable-noise-design.md).
// The CA row IS the wavetable; a read head scans it at Freq, and the CA
// advances one generation at each wavetable-pass BOUNDARY (phase-continuous,
// click-free) every NClk passes -- so NClk high = a static structured tone,
// NClk=1 = a new generation every cycle = aperiodic rule-structured NOISE.
//   Rule (0-255) = the elementary rule = the character.
//   Res (2..256) = active cell count at constant pitch (brute square -> detail).
//   Reset       = reseed every N updates to keep converging rules alive
//                 (rules can settle to a fixed point; a die-out watchdog also
//                 reseeds so it never goes silent).
// Inspired-in-spirit by Kentaro's tonemata; the CA is public-domain math
// (clean-room), generic name ([[feedback_no_third_party_branding]]). POC
// (phase 1): mono out, binary +/-1 cells, linear-interp read.
//
// am335x: the CA is integer/bitwise, run once per pass (branch-wrapped, no
// division); per-sample is a float interp read. All virtuals inline
// (feedback_no_out_of_line_virtuals). No Vivary.cpp.

#include <od/objects/Object.h>
#include <od/config.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

namespace stolmine
{
  static const int kVivaryMaxCells = 256;
  static const int kVivaryGenHist  = 4;   // generations kept for grain overlap

  // Curated elementary rules: the sonically interesting (Wolfram class III/IV)
  // rules, DEDUPLICATED by symmetry class (mirror/complement twins removed) and
  // ranked by complexity = spatial-entropy x row-variety, from an offline sweep
  // of all 256 (planning/ca-rule-analysis.py). The Rule control indexes this
  // list so every knob position is a distinct, lively texture -- no dead rules.
  static const int kVivaryNumRules = 48;
  static const uint8_t kVivaryRules[kVivaryNumRules] = {
    86, 163, 73, 101, 193, 57, 214, 129, 145, 188, 169, 65, 67, 150, 105, 107,
    81, 43, 212, 213, 209, 174, 185, 166, 3, 226, 241, 83, 240, 115, 85, 147,
    176, 122, 7, 88, 38, 167, 20, 151, 187, 175, 18, 146, 231, 144, 2, 199
  };

  // RADIUS-2 binary rules (5-neighbor elementary; 32-bit rule = LUT[32]). Same
  // sharp +/-1 edges as radius-1 (so the rhythmic bite is kept) but a wider
  // neighborhood -> richer long-range structure. Curated from a 16k sample of
  // the 2^32 space by a STRUCTURE metric (spectral tonality x timbral motion,
  // NOT chaos/entropy -- which produced undifferentiated mush), symmetry-
  // deduped (mirror + complement). planning/ca-rule-analysis.py.
  static const int kVivaryNumRulesR2 = 48;
  static const uint32_t kVivaryRulesR2[kVivaryNumRulesR2] = {
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
  static const int kVivaryNumRulesEOC = 40;
  static const uint8_t kVivaryRulesEOC[kVivaryNumRulesEOC] = {
    230, 193, 158, 126, 161, 209, 173, 52, 208, 66, 149, 2, 20, 130, 175, 73,
    195, 213, 34, 169, 212, 171, 210, 240, 75, 18, 146, 22, 82, 176, 165, 145,
    185, 15, 43, 202, 234, 184, 218, 93
  };

  // REVERSIBLE 2nd-order: next = f(l,c,r) XOR previous-generation. Cannot die
  // (time-symmetric) -> perpetual wave-like motion + interference. These are
  // the base rules f that give the liveliest reversible dynamics (curated by
  // variety). caStep runs the 2nd-order recurrence.
  static const int kVivaryNumRulesREV = 40;
  static const uint8_t kVivaryRulesREV[kVivaryNumRulesREV] = {
    191, 3, 6, 239, 9, 11, 207, 13, 143, 15, 25, 167, 39, 28, 30, 123, 187, 35,
    38, 235, 41, 42, 43, 203, 45, 139, 54, 56, 57, 163, 60, 62, 73, 78, 133,
    106, 110, 161, 190, 134
  };

  // ADDITIVE / LINEAR (XOR): the linear-over-GF(2) rules -> nested, self-similar
  // Sierpinski-like fractal patterns. A crystalline/recursive character.
  static const int kVivaryNumRulesFRAC = 10;
  static const uint8_t kVivaryRulesFRAC[kVivaryNumRulesFRAC] = {
    90, 60, 165, 195, 102, 153, 101, 89, 75, 45
  };

  class Vivary : public od::Object
  {
  public:
    Vivary()
    {
      addInput(mVOct);
      addOutput(mOut);
      addParameter(mFreq);
      addParameter(mRule);
      addParameter(mRes);
      addParameter(mEvolve);
      addParameter(mReset);
      addParameter(mSmooth);
      addParameter(mFamily);
      addParameter(mOverlap);

      memset(mCells, 0, sizeof(mCells));
      memset(mNext, 0, sizeof(mNext));
      memset(mPrev, 0, sizeof(mPrev));
      memset(mGray, 0, sizeof(mGray));
      memset(mGenRing, 0, sizeof(mGenRing));
      memset(mRuleTable, 0, sizeof(mRuleTable));
      mGenHead = 0;
      mPhase = 0.0f;
      mPassCount = 0;
      mResetCount = 0;
      mLastRule = 0xFFFFFFFFu;
      mLastFamily = -1;   // forces reseed + table build on the first block
    }

    virtual ~Vivary() {}

#ifndef SWIGLUA
    // Reseed the row to a single live center cell (deterministic); fill the
    // generation-history ring so grain overlap has no stale generations.
    void reseed(int res)
    {
      for (int i = 0; i < res; i++) { mCells[i] = 0; mPrev[i] = 0; }
      mCells[res / 2] = 1;
      for (int g = 0; g < kVivaryGenHist; g++)
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
    // (5-neighbor LUT[32]); mode 2 = reversible 2nd-order (next = f(l,c,r) XOR
    // previous generation -> cannot die). Then update the grayscale EMA.
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
      else if (mode == 2)
      {
        for (int i = 0; i < res; i++)
        {
          const int l = mCells[(i == 0) ? (res - 1) : (i - 1)];
          const int c = mCells[i];
          const int r = mCells[(i == res - 1) ? 0 : (i + 1)];
          mNext[i] = mRuleTable[(l << 2) | (c << 1) | r] ^ mPrev[i];
        }
        for (int i = 0; i < res; i++) mPrev[i] = mCells[i];   // current -> previous
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
      for (int i = 0; i < res; i++)
      {
        mCells[i] = mNext[i];
        const float amp = mCells[i] ? 1.0f : -1.0f;
        mGray[i] += 0.35f * (amp - mGray[i]);
      }
      // Push this generation into the history ring for grain overlap.
      mGenHead = (mGenHead + 1) & (kVivaryGenHist - 1);
      for (int i = 0; i < res; i++) mGenRing[mGenHead][i] = mCells[i];
    }

    virtual void process()
    {
      float *out = mOut.buffer();

      // ---- Block-rate params ----
      // Freq is a normal oscillator f0 (Hz, oscFreq map in Lua) with a V/Oct
      // input: f0 = fundamental * 2^(V/Oct*10) (the ER-301 pitch convention,
      // block rate). The other controls stay normalized 0..1 for easy mod.
      const float voct = mVOct.buffer()[0];
      float fund = mFreq.value();
      if (!(fund >= 0.0f)) fund = 0.0f;
      float f0 = fund * powf(2.0f, voct * 10.0f);
      if (f0 < 0.0f) f0 = 0.0f;
      const float f0Max = globalConfig.sampleRate * 0.49f;
      if (f0 > f0Max) f0 = f0Max;

      float resN = mRes.value();
      if (!(resN >= 0.0f)) resN = 0.0f;
      else if (resN > 1.0f) resN = 1.0f;
      int res = 2 + (int)(resN * (float)(kVivaryMaxCells - 2) + 0.5f);

      // Family (5): 0 = radius-1 chaos, 1 = radius-2 structure, 2 = edge-of-chaos
      // (class IV / gliders), 3 = reversible 2nd-order, 4 = additive/XOR fractal.
      // All binary. Each maps to a caStep mode + a curated rule table.
      float familyN = mFamily.value();
      if (!(familyN >= 0.0f)) familyN = 0.0f;
      else if (familyN > 1.0f) familyN = 1.0f;
      int family = (int)(familyN * 4.0f + 0.5f);
      if (family < 0) family = 0;
      else if (family > 4) family = 4;

      float ruleN = mRule.value();
      if (!(ruleN >= 0.0f)) ruleN = 0.0f;
      else if (ruleN > 1.0f) ruleN = 1.0f;

      // Pick the family's caStep mode, rule table, and rule number.
      int mode = 0;               // 0 = radius-1, 1 = radius-2, 2 = reversible
      int nbits = 8;
      uint32_t ruleNum;
      if (family == 1)            // radius-2 structure (32-bit rules)
      {
        mode = 1; nbits = 32;
        int idx = (int)(ruleN * (float)(kVivaryNumRulesR2 - 1) + 0.5f);
        if (idx >= kVivaryNumRulesR2) idx = kVivaryNumRulesR2 - 1;
        ruleNum = kVivaryRulesR2[idx];
      }
      else                        // radius-1 or reversible (8-bit rules)
      {
        const uint8_t *tbl; int count;
        if (family == 0)      { tbl = kVivaryRules;     count = kVivaryNumRules;     mode = 0; }
        else if (family == 2) { tbl = kVivaryRulesEOC;  count = kVivaryNumRulesEOC;  mode = 0; }
        else if (family == 3) { tbl = kVivaryRulesREV;  count = kVivaryNumRulesREV;  mode = 2; }
        else                  { tbl = kVivaryRulesFRAC; count = kVivaryNumRulesFRAC; mode = 0; }
        int idx = (int)(ruleN * (float)(count - 1) + 0.5f);
        if (idx >= count) idx = count - 1;
        ruleNum = tbl[idx];
      }
      // Rebuild the LUT when family/rule changes; reseed on a family change.
      if (family != mLastFamily || ruleNum != mLastRule)
      {
        if (family != mLastFamily) reseed(res);
        for (int b = 0; b < nbits; b++)
          mRuleTable[b] = (uint8_t)((ruleNum >> b) & 1u);
        mLastFamily = family;
        mLastRule = ruleNum;
      }

      // Evolve: 1 = a new generation every pass (noise), 0 = frozen (static
      // tone). Maps to the CA clock interval nClk = 1..64 passes/update.
      float evoN = mEvolve.value();
      if (!(evoN >= 0.0f)) evoN = 0.0f;
      else if (evoN > 1.0f) evoN = 1.0f;
      int nClk = 1 + (int)((1.0f - evoN) * 63.0f + 0.5f);

      float resetN = mReset.value();
      if (!(resetN >= 0.0f)) resetN = 0.0f;
      else if (resetN > 1.0f) resetN = 1.0f;
      int rClk = (int)(resetN * 256.0f + 0.5f);   // 0 = off

      // Smooth: 0 = binary/step (harsh), 1 = grayscale EMA (soft, time-baked).
      float smooth = mSmooth.value();
      if (!(smooth >= 0.0f)) smooth = 0.0f;
      else if (smooth > 1.0f) smooth = 1.0f;

      // Overlap (grain overlap): layer the last N CA generations. 0 = current
      // generation only (hard swap); up = more generations blend/morph. Window
      // depth ovD generations; only `activeGen` are summed.
      float overlapN = mOverlap.value();
      if (!(overlapN >= 0.0f)) overlapN = 0.0f;
      else if (overlapN > 1.0f) overlapN = 1.0f;
      const float ovInvD = 1.0f / (1.0f + overlapN * (float)(kVivaryGenHist - 1));
      int activeGen = 1 + (int)(overlapN * (float)(kVivaryGenHist - 1) + 0.999f);
      if (activeGen < 1) activeGen = 1;
      else if (activeGen > kVivaryGenHist) activeGen = kVivaryGenHist;

      const float phInc = f0 / globalConfig.sampleRate;

      for (int n = 0; n < FRAMELENGTH; n++)
      {
        mPhase += phInc;
        if (mPhase >= 1.0f)
        {
          mPhase -= 1.0f;
          if (mPhase >= 1.0f) mPhase = 0.0f;   // guard f0 above SR

          // Wavetable-pass boundary: advance the CA every nClk passes.
          if (++mPassCount >= nClk)
          {
            mPassCount = 0;
            caStep(res, mode);
            // Reset watchdog: reseed every rClk updates (0 = off) OR on a
            // uniform row (so a converged rule never leaves it silent).
            const bool dead = caIsUniform(res);
            if ((rClk > 0 && ++mResetCount >= rClk) || dead)
            {
              mResetCount = 0;
              reseed(res);
            }
          }
        }

        // Grain overlap-add: sum the last `activeGen` generations read at the
        // shared pass-phase, weighted by a decaying window over the overlap
        // depth (age m weight = max(0, 1 - (m+phase)/D)), normalized. Binary
        // cells -> +/-1. Then Smooth blends toward the grayscale EMA.
        // activeGen=1 -> just the current generation.
        const float p = mPhase * (float)res;
        int i0 = (int)p;
        if (i0 >= res) i0 = res - 1;
        int i1 = i0 + 1;
        if (i1 >= res) i1 = 0;
        const float frac = p - (float)i0;
        float acc = 0.0f, wsum = 0.0f;
        for (int m = 0; m < activeGen; m++)
        {
          float wt = 1.0f - ((float)m + mPhase) * ovInvD;
          if (wt < 0.0f) wt = 0.0f;
          const uint8_t *row =
            mGenRing[(mGenHead - m + kVivaryGenHist) & (kVivaryGenHist - 1)];
          const float a0 = row[i0] ? 1.0f : -1.0f;
          const float a1 = row[i1] ? 1.0f : -1.0f;
          acc += wt * (a0 + frac * (a1 - a0));
          wsum += wt;
        }
        const float rawOut = acc / (wsum + 1e-9f);
        const float gray = mGray[i0] + frac * (mGray[i1] - mGray[i0]);
        out[n] = rawOut + smooth * (gray - rawOut);
      }
    }

    od::Inlet mVOct{"V/Oct"};
    od::Outlet mOut{"Out"};
    od::Parameter mFreq{"Freq", 110.0f};   // Hz fundamental (oscFreq map in Lua)
    od::Parameter mRule{"Rule", 0.0f};    // 0..1 -> curated rule index
    od::Parameter mRes{"Res", 0.24f};      // 0..1 -> 2..256 cells
    od::Parameter mEvolve{"Evolve", 1.0f}; // 0..1 -> static tone .. per-pass noise
    od::Parameter mReset{"Reset", 0.0f};   // 0..1 -> reseed interval (0 = off)
    od::Parameter mSmooth{"Smooth", 0.0f}; // 0..1 -> binary harsh .. grayscale soft
    od::Parameter mFamily{"Family", 0.0f}; // 0..1 -> binary / 3-state / 4-state
    od::Parameter mOverlap{"Overlap", 0.0f}; // 0..1 -> grain overlap (gen layering)

  private:
    uint8_t mCells[kVivaryMaxCells];
    uint8_t mNext[kVivaryMaxCells];
    uint8_t mPrev[kVivaryMaxCells];   // previous generation (reversible mode)
    float mGray[kVivaryMaxCells];   // per-cell grayscale EMA (Smooth target)
    uint8_t mGenRing[kVivaryGenHist][kVivaryMaxCells]; // recent generations
    int mGenHead;                   // newest generation index in the ring
    uint8_t mRuleTable[32];         // radius-1 LUT[8] or radius-2 LUT[32]
    float mPhase;
    int mPassCount;
    int mResetCount;
    uint32_t mLastRule;
    int mLastFamily;
#endif
  };

} // namespace stolmine
