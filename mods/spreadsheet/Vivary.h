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

  // Multi-state TOTALISTIC rule families (next state from the neighborhood SUM;
  // cells 0..k-1 -> multi-LEVEL waveforms, a genuinely different palette than
  // binary +/-1). Curated + symmetry-deduped + complexity-ranked from offline
  // sweeps (planning/ca-rule-analysis.py): k=3 (sum 0..6, base-3 rule 0..2186),
  // k=4 (sum 0..9, base-4 rule 0..1048575).
  static const int kVivaryNumRules3 = 32;
  static const uint16_t kVivaryRules3[kVivaryNumRules3] = {
    926, 532, 1317, 1149, 797, 1770, 1280, 1767, 150, 1263, 1315, 1874, 794,
    2010, 289, 1281, 1017, 1504, 1600, 1990, 345, 2040, 471, 1878, 552, 939,
    538, 554, 835, 383, 936, 920
  };
  static const int kVivaryNumRules4 = 32;
  static const uint32_t kVivaryRules4[kVivaryNumRules4] = {
    709509, 214947, 317657, 809197, 603848, 706256, 160376, 58586, 924446,
    138960, 231324, 647648, 225912, 364148, 119512, 308785, 494029, 107769,
    706249, 317658, 815727, 75574, 887246, 860605, 186590, 578473, 126054,
    124137, 156110, 495174, 96562, 675403
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
      memset(mGray, 0, sizeof(mGray));
      memset(mGenRing, 0, sizeof(mGenRing));
      memset(mRuleTable, 0, sizeof(mRuleTable));
      mGenHead = 0;
      mPhase = 0.0f;
      mPassCount = 0;
      mResetCount = 0;
      mLastRule = -1;
      mLastFamily = -1;   // forces reseed + table build on the first block
    }

    virtual ~Vivary() {}

#ifndef SWIGLUA
    // Reseed the row to a single max-state center cell (deterministic); fill
    // the generation-history ring so grain overlap has no stale generations.
    void reseed(int res, int k)
    {
      for (int i = 0; i < res; i++) mCells[i] = 0;
      mCells[res / 2] = (uint8_t)(k - 1);
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

    // Advance one CA generation (toroidal; branch-wrapped edges, no division).
    // Elementary = LUT by the (l,c,r) pattern; totalistic = table by the
    // neighborhood SUM (multi-state). Then update the per-cell grayscale EMA in
    // the AMPLITUDE domain (ampScale = 2/(k-1)) that Smooth blends toward.
    void caStep(int res, bool totalistic, float ampScale)
    {
      if (totalistic)
      {
        for (int i = 0; i < res; i++)
        {
          const int lIdx = (i == 0) ? (res - 1) : (i - 1);
          const int rIdx = (i == res - 1) ? 0 : (i + 1);
          mNext[i] = mRuleTable[mCells[lIdx] + mCells[i] + mCells[rIdx]];
        }
      }
      else
      {
        for (int i = 0; i < res; i++)
        {
          const int lIdx = (i == 0) ? (res - 1) : (i - 1);
          const int rIdx = (i == res - 1) ? 0 : (i + 1);
          const int l = mCells[lIdx];
          const int c = mCells[i];
          const int r = mCells[rIdx];
          mNext[i] = mRuleTable[(l << 2) | (c << 1) | r];
        }
      }
      for (int i = 0; i < res; i++)
      {
        mCells[i] = mNext[i];
        const float amp = (float)mCells[i] * ampScale - 1.0f;
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

      // Family: 0 = elementary binary (k=2), 1 = totalistic 3-state, 2 = 4-state.
      // Multi-state cells map to multi-LEVEL waveforms (ampScale = 2/(k-1)).
      float familyN = mFamily.value();
      if (!(familyN >= 0.0f)) familyN = 0.0f;
      else if (familyN > 1.0f) familyN = 1.0f;
      int family = (int)(familyN * 2.0f + 0.5f);
      if (family < 0) family = 0;
      else if (family > 2) family = 2;
      const int k = (family == 0) ? 2 : (family == 1) ? 3 : 4;
      const bool totalistic = (family != 0);
      const float ampScale = 2.0f / (float)(k - 1);

      // Rule 0..1 indexes the curated interesting-rule table for this family.
      float ruleN = mRule.value();
      if (!(ruleN >= 0.0f)) ruleN = 0.0f;
      else if (ruleN > 1.0f) ruleN = 1.0f;
      int ruleNum;
      if (family == 0)
      {
        int idx = (int)(ruleN * (float)(kVivaryNumRules - 1) + 0.5f);
        if (idx >= kVivaryNumRules) idx = kVivaryNumRules - 1;
        ruleNum = kVivaryRules[idx];
      }
      else if (family == 1)
      {
        int idx = (int)(ruleN * (float)(kVivaryNumRules3 - 1) + 0.5f);
        if (idx >= kVivaryNumRules3) idx = kVivaryNumRules3 - 1;
        ruleNum = kVivaryRules3[idx];
      }
      else
      {
        int idx = (int)(ruleN * (float)(kVivaryNumRules4 - 1) + 0.5f);
        if (idx >= kVivaryNumRules4) idx = kVivaryNumRules4 - 1;
        ruleNum = (int)kVivaryRules4[idx];
      }
      // Rebuild the rule table when family/rule changes; reseed on a family
      // change (stale cells would be out of range for the new k). Base-k digit
      // unpack (totalistic) uses idiv but only block-rate-on-change.
      if (family != mLastFamily || ruleNum != mLastRule)
      {
        if (family != mLastFamily) reseed(res, k);
        if (family == 0)
        {
          for (int b = 0; b < 8; b++) mRuleTable[b] = (uint8_t)((ruleNum >> b) & 1);
        }
        else
        {
          const int T = 3 * (k - 1) + 1;   // 7 (k=3) or 10 (k=4)
          int rr = ruleNum;
          for (int j = 0; j < T; j++) { mRuleTable[j] = (uint8_t)(rr % k); rr /= k; }
        }
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
            caStep(res, totalistic, ampScale);
            // Reset watchdog: reseed every rClk updates (0 = off) OR on a
            // uniform row (so a converged rule never leaves it silent).
            const bool dead = caIsUniform(res);
            if ((rClk > 0 && ++mResetCount >= rClk) || dead)
            {
              mResetCount = 0;
              reseed(res, k);
            }
          }
        }

        // Grain overlap-add: sum the last `activeGen` generations read at the
        // shared pass-phase, weighted by a decaying window over the overlap
        // depth (age m weight = max(0, 1 - (m+phase)/D)), normalized. Each cell
        // maps to a multi-level amplitude (ampScale = 2/(k-1)). Then Smooth
        // blends toward the grayscale EMA. activeGen=1 -> just the current gen.
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
          const float a0 = (float)row[i0] * ampScale - 1.0f;
          const float a1 = (float)row[i1] * ampScale - 1.0f;
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
    float mGray[kVivaryMaxCells];   // per-cell grayscale EMA (Smooth target)
    uint8_t mGenRing[kVivaryGenHist][kVivaryMaxCells]; // recent generations
    int mGenHead;                   // newest generation index in the ring
    uint8_t mRuleTable[16];         // elementary LUT[8] or totalistic table[<=10]
    float mPhase;
    int mPassCount;
    int mResetCount;
    int mLastRule;
    int mLastFamily;
#endif
  };

} // namespace stolmine
