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

      memset(mCells, 0, sizeof(mCells));
      memset(mNext, 0, sizeof(mNext));
      memset(mGray, 0, sizeof(mGray));
      memset(mRuleLut, 0, sizeof(mRuleLut));
      mPhase = 0.0f;
      mPassCount = 0;
      mResetCount = 0;
      mLastRule = -1;
      mSeeded = false;
    }

    virtual ~Vivary() {}

#ifndef SWIGLUA
    // Reseed the row to a single live center cell (deterministic).
    void reseed(int res)
    {
      for (int i = 0; i < res; i++) mCells[i] = 0;
      mCells[res / 2] = 1;
    }

    // All-0 or all-1 fixed point = silence/DC -> the die-out watchdog.
    bool caIsDead(int res)
    {
      int sum = 0;
      for (int i = 0; i < res; i++) sum += mCells[i];
      return sum == 0 || sum == res;
    }

    // Advance one elementary CA generation over `res` cells (toroidal;
    // branch-wrapped edges, no integer division). Also updates the per-cell
    // grayscale EMA (a smoothed history of the +/-1 cell states) that Smooth
    // blends toward -> softer waveforms that bake in the time structure.
    void caStep(int res)
    {
      for (int i = 0; i < res; i++)
      {
        const int lIdx = (i == 0) ? (res - 1) : (i - 1);
        const int rIdx = (i == res - 1) ? 0 : (i + 1);
        const int l = mCells[lIdx];
        const int c = mCells[i];
        const int r = mCells[rIdx];
        mNext[i] = mRuleLut[(l << 2) | (c << 1) | r];
      }
      for (int i = 0; i < res; i++)
      {
        mCells[i] = mNext[i];
        const float cv = mCells[i] ? 1.0f : -1.0f;
        mGray[i] += 0.35f * (cv - mGray[i]);   // ~3-4 generation EMA
      }
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

      // Rule 0..1 indexes the curated interesting-rule table (distinct textures,
      // no dead rules) rather than the raw 0..255 space.
      float ruleN = mRule.value();
      if (!(ruleN >= 0.0f)) ruleN = 0.0f;
      else if (ruleN > 1.0f) ruleN = 1.0f;
      int ruleIdx = (int)(ruleN * (float)(kVivaryNumRules - 1) + 0.5f);
      if (ruleIdx < 0) ruleIdx = 0;
      else if (ruleIdx >= kVivaryNumRules) ruleIdx = kVivaryNumRules - 1;
      int ruleNum = kVivaryRules[ruleIdx];
      if (ruleNum != mLastRule)
      {
        for (int b = 0; b < 8; b++) mRuleLut[b] = (uint8_t)((ruleNum >> b) & 1);
        mLastRule = ruleNum;
      }

      float resN = mRes.value();
      if (!(resN >= 0.0f)) resN = 0.0f;
      else if (resN > 1.0f) resN = 1.0f;
      int res = 2 + (int)(resN * (float)(kVivaryMaxCells - 2) + 0.5f);

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

      // Smooth: 0 = binary +/-1 (harsh), 1 = grayscale EMA (soft, time-baked).
      float smooth = mSmooth.value();
      if (!(smooth >= 0.0f)) smooth = 0.0f;
      else if (smooth > 1.0f) smooth = 1.0f;

      if (!mSeeded)
      {
        reseed(res);
        mSeeded = true;
      }

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
            caStep(res);
            // Reset watchdog: reseed every rClk updates (0 = off) OR on die-out
            // (so a converged rule never leaves it silent).
            const bool dead = caIsDead(res);
            if ((rClk > 0 && ++mResetCount >= rClk) || dead)
            {
              mResetCount = 0;
              reseed(res);
            }
          }
        }

        // Linear-interp read; Smooth blends each cell from binary +/-1 toward
        // its grayscale EMA.
        const float p = mPhase * (float)res;
        int i0 = (int)p;
        if (i0 >= res) i0 = res - 1;
        int i1 = i0 + 1;
        if (i1 >= res) i1 = 0;
        const float frac = p - (float)i0;
        const float b0 = mCells[i0] ? 1.0f : -1.0f;
        const float b1 = mCells[i1] ? 1.0f : -1.0f;
        const float v0 = b0 + smooth * (mGray[i0] - b0);
        const float v1 = b1 + smooth * (mGray[i1] - b1);
        out[n] = v0 + frac * (v1 - v0);
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

  private:
    uint8_t mCells[kVivaryMaxCells];
    uint8_t mNext[kVivaryMaxCells];
    float mGray[kVivaryMaxCells];   // per-cell grayscale EMA (Smooth target)
    uint8_t mRuleLut[8];
    float mPhase;
    int mPassCount;
    int mResetCount;
    int mLastRule;
    bool mSeeded;
#endif
  };

} // namespace stolmine
