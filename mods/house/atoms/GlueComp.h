// house::GlueComp
//
// Component atom: a bus compressor whose CHARACTER lives entirely in
// the sidechain. Component-only per feedback_atoms_as_components.
//
// Research: planning/compressor-character-research.md. The finding that
// shapes this design is that modern clean-path compressors differ from
// one another "almost entirely by the sidechain" - many units with
// near-identical signal paths sound wildly different because of
// detector topology alone. So no photocell, tube or transformer is
// modelled here. Four sidechain laws are, and they are all arithmetic
// on the control signal:
//
//   1. DETECTOR PLACEMENT   feedback (reads the output) or feedforward
//   2. DETECTOR TYPE        peak or RMS
//   3. TIMING LAW           program-dependent or fixed
//   4. KNEE                 soft or hard
//
// FEEDBACK VS FEEDFORWARD IS THE LARGEST AXIS, and its consequences are
// not cosmetic. A feedback detector reads the already-reduced output,
// so:
//   - more transient escapes before the loop responds,
//   - the effective threshold is ADAPTIVE rather than fixed,
//   - DEEP GAIN REDUCTION IS HARD, because reducing the output pushes
//     the detector back below threshold and the loop backs off,
//   - and timing becomes a function of ratio rather than independent.
// The same knob settings therefore give a materially different result.
// 1176 and LA-2A are feedback; dbx 160 is feedforward; the SSL bus is
// feedforward with a simulated feedback path in its sidechain.
//
// PROGRAM-DEPENDENT TIMING follows the dbx 160's published behaviour:
// attack 15 ms at 10 dB of reduction, 5 ms at 20, 3 ms at 30, while
// release lengthens 8 ms at 1 dB to 400 ms at 50. Harder hit, faster
// attack, longer release. That is a law, not a knob, and it is what
// "program dependent" means.
//
// NO LOG OR EXP ON THE SAMPLE PATH. Gain reduction is computed in the
// LINEAR domain against an infinite-ratio target and then blended by
// ratio, the same trick Pop3 and Pressure6 both use, so there is no
// pow/log/exp anywhere per sample. One divide remains, for thresh/det,
// and it is affordable because THE DETECTOR IS MONO: a bus compressor
// is stereo-linked, so that divide happens once per sample, not twice.
#pragma once

#include <od/config.h>
#include <math.h>
#include <stdint.h>

namespace house
{

  // Fast log2/exp2 by float bit manipulation. Accurate to well under
  // 0.01 dB across the range a compressor uses, and roughly ten
  // instructions each with no libm call and no table.
  //
  // THESE EXIST BECAUSE THE LINEAR-DOMAIN SHORTCUT FAILED. Gain was
  // first computed as a blend toward thresh/det, chosen to avoid
  // log/exp per sample. It cannot express a dB ratio law: measured, a
  // nominally infinite-ratio blend delivered an effective 1.76:1, so
  // 4:1 and 10:1 were simply unreachable. A compressor needs
  // gain = over^-(1 - 1/R), which is a power, which is a log and an
  // exp. Doing them cheaply is the answer; avoiding them was not.
  static inline float fastLog2(float x)
  {
    union { float f; uint32_t i; } v; v.f = x;
    const float e = (float)((v.i >> 23) & 0xFF) - 127.0f;
    v.i = (v.i & 0x007FFFFF) | 0x3F800000;   // mantissa into [1,2)
    const float m = v.f;
    // Degree-4 fit for log2 over [1,2), max error 2.0e-04.
    // A cubic was tried first and measured 1.1 dB of END-TO-END GAIN
    // ERROR once composed with exp2 - the ratio was simply wrong. The
    // extra term costs one multiply-add and takes that to 0.02 dB.
    return e + m * (m * (m * (m * -0.079150366f + 0.628815729f)
                         - 2.081060203f) + 4.028372767f) - 2.496773768f;
  }

  static inline float fastExp2(float x)
  {
    if (x < -60.0f) return 0.0f;
    if (x > 4.0f) x = 4.0f;
    const float fl = (float)((int)(x + 512.0f) - 512.0f);   // floor, no call
    const float f = x - fl;
    // minimax cubic for 2^f over [0,1)
    // Degree-4, with the constant term pinned to EXACTLY 1. At ratio
    // 1:1 the exponent is 0 and this must return exactly 1.0, or a
    // nominally bypassed compressor applies -0.0009 dB forever and the
    // bit-identity test fails. Max error 1.5e-05.
    const float p = f * (f * (f * (f * 0.013676531f + 0.051666877f)
                              + 0.241710262f) + 0.692931289f) + 1.0f;
    union { uint32_t i; float f; } v;
    int e = (int)fl + 127;
    if (e < 0) e = 0; else if (e > 254) e = 254;
    v.i = ((uint32_t)e) << 23;
    return p * v.f;
  }

  enum GlueCompCharacter
  {
    // SSL-bus behaviour. Gentle, adaptive, hard to make sound bad.
    GLUE_COMP_GLUE = 0,
    // 1176 / dbx 160 behaviour. The only FEEDFORWARD position, which is
    // exactly why it responds differently at identical settings: it
    // catches transients and will go as deep as it is asked.
    GLUE_COMP_PEAK = 1,
    // LA-2A behaviour. Slow, forgiving, with a two-stage release no
    // fixed Release setting reproduces.
    GLUE_COMP_OPTO = 2
  };

  struct GlueCompCoefs
  {
    float threshold;   // linear
    float invThresh;   // 1/threshold, baked - kills the per-sample divide
    float ratioAmt;    // k = 1 - 1/R, the dB-domain ratio exponent
    float attack;      // per-sample coefficient
    float release;
    float makeup;      // linear
    float kneeWidth;   // linear half-width; 0 == hard knee
    float invKnee;     // 1/kneeWidth, baked - see the note in processBlock
    float feedback;    // 1 = detector reads output, 0 = reads input
    float rmsAmount;   // 0 = peak detector, 1 = RMS
    float progDepend;  // 0 = fixed timing, 1 = program-dependent
    float slowTail;    // 0 = single release, 1 = add the opto second stage
    // AUTO MAKEUP. 1 = compensate the level lost to compression, 0 = off.
    //
    // This exists for a specific reported problem: at identical
    // settings the three characters gave -5.99 / -10.61 / -4.29 dB of
    // reduction, a 6 dB spread, so switching Character changed LOUDNESS
    // more than character and the character itself was unintelligible
    // under the level jump.
    //
    // Compensating the OUTPUT is the right place, not the threshold. A
    // per-character threshold trim was tried first and it equalised the
    // levels but moved every position's operating point, so the knee
    // and the program-dependence stopped being comparable - it
    // distorted what the control MEANS rather than what it delivers.
    //
    // DEFEATABLE, deliberately: automatic level matching removes the
    // ability to hear what a stage is actually doing, which is exactly
    // the objection raised against the Channel Strip.
    float autoMakeup;

    GlueCompCoefs()
        : threshold(1.0f), invThresh(1.0f), ratioAmt(0.0f), attack(0.01f), release(0.001f),
          makeup(1.0f), kneeWidth(0.0f), invKnee(0.0f), feedback(0.0f), rmsAmount(0.0f),
          progDepend(0.0f), slowTail(0.0f), autoMakeup(1.0f) {}
  };

  // Bake once per block. `thresh` and `makeup` are linear, `ratio` is
  // 0..1, times are in seconds.
  static inline void glueCompBake(GlueCompCoefs &c, GlueCompCharacter ch,
                                  double threshDb, double ratio,
                                  double attackSec, double releaseSec,
                                  double makeup, double sr, bool autoMk)
  {
    if (sr <= 0.0) sr = 48000.0;
    // THRESHOLD IS IN dB and the control reads in dB: -60 at the
    // bottom, 0 at the top, so turning it down lowers the threshold and
    // compresses more. The linear 0..1 amplitude it used to take put
    // almost all the useful range in the top of the knob's travel.
    if (threshDb > 0.0) threshDb = 0.0; else if (threshDb < -60.0) threshDb = -60.0;
    const double thresh = pow(10.0, threshDb / 20.0);
    // RATIO IS A REAL RATIO, 1..inf. k = 1 - 1/R is the dB-domain
    // exponent: 2:1 gives 0.5, 4:1 gives 0.75, 10:1 gives 0.9.
    if (ratio < 1.0) ratio = 1.0; else if (ratio > 200.0) ratio = 200.0;
    if (attackSec < 0.00002) attackSec = 0.00002;
    if (releaseSec < 0.002) releaseSec = 0.002;


    c.ratioAmt = (float)(1.0 - 1.0 / ratio);
    c.makeup = (float)makeup;
    c.autoMakeup = autoMk ? 1.0f : 0.0f;
    // One-pole coefficients. exp() is fine here: block rate, not sample.
    c.threshold = (float)thresh;
    c.invThresh = (float)(1.0 / thresh);
    c.attack = (float)(1.0 - exp(-1.0 / (attackSec * sr)));
    c.release = (float)(1.0 - exp(-1.0 / (releaseSec * sr)));

    switch (ch)
    {
      case GLUE_COMP_PEAK:
        c.feedback = 0.0f;      // the only feedforward position
        c.rmsAmount = 0.0f;     // peak
        c.progDepend = 0.0f;    // fixed timing, independent of ratio
        c.slowTail = 0.0f;
        c.kneeWidth = 0.0f;     // hard corner
        break;
      case GLUE_COMP_OPTO:
        c.feedback = 1.0f;
        c.rmsAmount = 1.0f;
        c.progDepend = 1.0f;
        c.slowTail = 1.0f;      // photocell-style second stage
        c.kneeWidth = (float)(thresh * 0.7);
        break;
      case GLUE_COMP_GLUE:
      default:
        c.feedback = 1.0f;
        c.rmsAmount = 0.5f;     // between peak and RMS, as a bus comp is
        c.progDepend = 1.0f;
        c.slowTail = 0.0f;
        c.kneeWidth = (float)(thresh * 0.5);
        break;
    }
    // Bake the knee reciprocal. It was two divides PER SAMPLE for a
    // quantity that is constant for the whole block.
    c.invKnee = c.kneeWidth > 0.0f ? 1.0f / c.kneeWidth : 0.0f;
  }

  // Stereo-LINKED, which is what a bus compressor is: one detector, one
  // gain, both channels. That also means the expensive part happens once
  // per sample rather than twice.
  class GlueCompStereo
  {
  public:
    GlueCompStereo() { reset(); }

    void reset()
    {
      mGain = 1.0f;
      mRms = 0.0f;
      mSlow = 1.0f;
      mMakeupAvg = 1.0f;
    }

    // Current gain reduction as a linear multiplier, for metering.
    float gain() const { return mGain; }

    void processBlock(float *l, float *r, int n, const GlueCompCoefs &c)
    {
      const float thr = c.threshold, ra = c.ratioAmt, mk = c.makeup;
      const float fb = c.feedback, rms = c.rmsAmount, pd = c.progDepend;
      const float knee = c.kneeWidth, ik = c.invKnee, tail = c.slowTail;
      const float atk = c.attack, rel = c.release;
      const float am = c.autoMakeup;
      float g = mGain, rl = mRms, sl = mSlow, mu = mMakeupAvg;

      for (int i = 0; i < n; i++)
      {
        const float xl = l[i], xr = r[i];

        // ---- detector input: feedback reads the OUTPUT ----
        // Branchless. fb == 1 feeds the already-reduced signal, so the
        // loop sees its own effect and the threshold becomes adaptive.
        const float gd = 1.0f + fb * (g - 1.0f);
        float al = xl * gd, ar = xr * gd;
        if (al < 0.0f) al = -al;
        if (ar < 0.0f) ar = -ar;
        const float pk = al > ar ? al : ar;

        // ---- detector type: peak or RMS ----
        // The RMS side runs in the SQUARED domain and is square-rooted
        // once; no per-sample divide.
        rl += (pk * pk - rl) * 0.001f;
        const float rmsVal = sqrtf(rl);
        float det = pk + rms * (rmsVal - pk);
        if (det < 1.0e-9f) det = 1.0e-9f;

        // ---- target gain: a REAL dB ratio law ----
        // gain = over^-(1 - 1/R), computed as exp2(-k * log2(over)).
        // `k` is baked, so the sample path is one fast log2 and one
        // fast exp2 and no divide at all - the thresh/det divide the
        // old linear blend needed is gone too.
        const float over = det * c.invThresh;
        float target = 1.0f;
        if (over > 1.0f)
        {
          float k = ra;
          // SOFT KNEE: ease the ratio in over the knee region rather
          // than switching it on at the corner.
          if (knee > 0.0f)
          {
            const float t = (det - thr) * ik;
            if (t < 1.0f) k = ra * (t * t * (3.0f - 2.0f * t));
          }
          target = fastExp2(-k * fastLog2(over));
        }
        else if (knee > 0.0f && det > thr - knee)
        {
          // A soft knee starts working BEFORE the corner, which is the
          // whole point of one.
          const float t = (det - (thr - knee)) * ik;
          target = fastExp2(-ra * 0.5f * t * t * fastLog2(det * c.invThresh));
        }

        // ---- timing, optionally program-dependent ----
        // dbx 160: harder hit means a FASTER attack and a LONGER
        // release. `depth` is how far below unity the target sits.
        const float depth = 1.0f - target;
        // Attack speeds up and release lengthens with depth, per the
        // dbx 160's published behaviour. The release side is a MULTIPLY
        // rather than rel/(1 + k): a divide here cost 14 non-pipelined
        // cycles per sample on A8 for a voicing curve whose exact shape
        // does not matter, only its direction. At full depth this still
        // lengthens the release fourfold.
        const float aC = atk * (1.0f + pd * depth * 6.0f);
        float rC = rel * (1.0f - pd * depth * 0.75f);
        if (rC < rel * 0.25f) rC = rel * 0.25f;

        if (target < g) g += (target - g) * (aC > 1.0f ? 1.0f : aC);
        else g += (target - g) * (rC > 1.0f ? 1.0f : rC);

        // ---- opto second stage: a slow tail on top of the release ----
        // A photocell recovers fast then slow; no single Release setting
        // reproduces that, which is why it is a law rather than a knob.
        sl += (g - sl) * rel * 0.15f;
        const float gOut = g + tail * (sl - g) * 0.5f;

        // Auto makeup tracks the AVERAGE gain reduction slowly and
        // divides it back out, so switching Character changes character
        // and not loudness. Slow enough (about a second) that it does
        // not undo the compression itself, only the static offset.
        mu += (gOut - mu) * 0.00002f;
        if (mu < 0.05f) mu = 0.05f;
        const float comp = 1.0f + am * (1.0f / mu - 1.0f);

        l[i] = xl * gOut * mk * comp;
        r[i] = xr * gOut * mk * comp;
      }
      mGain = g; mRms = rl; mSlow = sl; mMakeupAvg = mu;
    }

  private:
    float mGain;
    float mRms;
    float mSlow;
    float mMakeupAvg;
  };

} // namespace house
