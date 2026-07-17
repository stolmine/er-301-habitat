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

  class Vivary : public od::Object
  {
  public:
    Vivary()
    {
      addOutput(mOut);
      addParameter(mFreq);
      addParameter(mRule);
      addParameter(mRes);
      addParameter(mEvolve);
      addParameter(mReset);

      memset(mCells, 0, sizeof(mCells));
      memset(mNext, 0, sizeof(mNext));
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
    // branch-wrapped edges, no integer division).
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
      for (int i = 0; i < res; i++) mCells[i] = mNext[i];
    }

    virtual void process()
    {
      float *out = mOut.buffer();

      // ---- Block-rate params ----
      // All user controls are normalized 0..1 so a plain 0..1 CV at unity gain
      // sweeps the full range (easy modulation from any unit); mapped to their
      // real ranges here.
      float freqN = mFreq.value();
      if (!(freqN >= 0.0f)) freqN = 0.0f;
      else if (freqN > 1.0f) freqN = 1.0f;
      float f0 = powf(8000.0f, freqN) - 1.0f;   // 0..1 -> ~0 Hz .. 8 kHz (exp)
      if (f0 < 0.0f) f0 = 0.0f;

      float ruleN = mRule.value();
      if (!(ruleN >= 0.0f)) ruleN = 0.0f;
      else if (ruleN > 1.0f) ruleN = 1.0f;
      int ruleNum = (int)(ruleN * 255.0f + 0.5f);
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

        // Linear-interp read of the current row (binary cells -> +/-1).
        const float p = mPhase * (float)res;
        int i0 = (int)p;
        if (i0 >= res) i0 = res - 1;
        int i1 = i0 + 1;
        if (i1 >= res) i1 = 0;
        const float frac = p - (float)i0;
        const float v0 = mCells[i0] ? 1.0f : -1.0f;
        const float v1 = mCells[i1] ? 1.0f : -1.0f;
        out[n] = v0 + frac * (v1 - v0);
      }
    }

    od::Outlet mOut{"Out"};
    od::Parameter mFreq{"Freq", 0.52f};    // 0..1 -> ~0 Hz .. 8 kHz (exp)
    od::Parameter mRule{"Rule", 0.12f};    // 0..1 -> rule 0..255
    od::Parameter mRes{"Res", 0.24f};      // 0..1 -> 2..256 cells
    od::Parameter mEvolve{"Evolve", 1.0f}; // 0..1 -> static tone .. per-pass noise
    od::Parameter mReset{"Reset", 0.0f};   // 0..1 -> reseed interval (0 = off)

  private:
    uint8_t mCells[kVivaryMaxCells];
    uint8_t mNext[kVivaryMaxCells];
    uint8_t mRuleLut[8];
    float mPhase;
    int mPassCount;
    int mResetCount;
    int mLastRule;
    bool mSeeded;
#endif
  };

} // namespace stolmine
