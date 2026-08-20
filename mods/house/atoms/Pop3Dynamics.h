// house::Pop3Dynamics
//
// Component atom: a compressor and a gate, sharing one detector path.
// Component-only per feedback_atoms_as_components - the consumers
// instantiate it in C++; there is no od::Object wrapper and no Lua
// unit, because a bare dynamics stage has no standalone value next to
// Impasto and Gesso.
//
// Consumer: `strata-channel-strip`'s Comp section. Habitat had NO
// compressor or gate atom at all before this.
//
// SOURCE: Airwindows Pop3 (Chris Johnson, MIT), the ConsoleX dynamics
// section. The algorithm is preserved; three things are adapted for
// this target and are called out below. What makes it worth porting
// is how little there is: ~22 lines of actual algorithm, 8 params,
// about 32 bytes of state, and no arrays or buffers at all.
//
// WHY THE DESIGN TRAVELS WELL. The compressor's gain multiplier lives
// in the LINEAR domain, never in dB:
//   - over threshold it leaky-integrates toward compThresh/|x|, which
//     is an infinite-ratio target,
//   - under threshold it one-poles back toward 1.0,
//   - and `ratio` is a plain linear crossfade between 1.0 and that
//     multiplier, not a dB slope.
// So there is no log, no exp, and no dB conversion anywhere on the
// sample path.
//
// The gate is phase-based: crossing its threshold slams a phase value
// to `sustain`, which then decays multiplicatively, and the gain is
// sin(phase) crossfaded by `ratio`. Because sin(pi/2) == 1, the whole
// sine term is SKIPPED while phase is at or above pi/2 - so the gate
// costs nothing at all while it is open, and only computes during its
// closing stretch.
//
// THE GATE READS THE UNCOMPRESSED SIGNAL. It is deliberately
// independent of the compressor, so squashing the signal does not
// drag the gate open. Preserved here: process() takes the detector
// reading before any gain is applied.
//
// NO MAKEUP GAIN, by design. ConsoleX supplies it externally and so
// must Channel Strip's Out section. Do not add one here.
//
// THREE ADAPTATIONS, none of them character changes:
//
// 1. sin() is replaced by a bounded polynomial. The argument is only
//    ever in [0, pi/2), and libm sin costs 300-500 ns on am335x
//    (feedback_am335x_libm_sin_cost) - unaffordable during every
//    sample of a gate close. A 7th-order odd minimax over that range
//    is accurate to ~1e-4, far below audibility for an envelope.
// 2. Doubles become floats on the sample path, coefficients stay
//    double at bake time (feedback_cortex_a8_no_double_in_hot_loops).
// 3. AW's 32-bit dither is dropped. It exists to shape truncation
//    noise for a VST host writing 32-bit output; the ER-301 signal
//    path is already float and never truncates here.
//
// SAMPLE RATE: every time constant below is 44.1 kHz-calibrated in
// the original and is rescaled by sr/44100, exactly as AW's
// `overallscale` does. The strata note flags this class of bug
// (Channel9's ultrasonic biquads bypass entirely at 48 kHz) and it is
// easy to inherit by forgetting the scale.
#pragma once

#include <od/config.h>
#include <math.h>
#include <stdint.h>

namespace house
{

  // sin(x) for x in [0, pi/2]. Odd Taylor/minimax, ~1.5e-4 worst case
  // at the top of the range, evaluated by Horner on x^2: 4 multiplies
  // and no call. Only ever invoked while the gate is closing.
  static inline float pop3Sin(float x)
  {
    const float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.00833333f + x2 * -0.00019841f)));
  }

  // Baked once per block. All eight inputs are normalized 0..1, which
  // is how the original exposes them; the powers below are AW's own
  // control laws and give the knobs their feel.
  struct Pop3Coefs
  {
    float compThresh;
    float compRatio;
    float compAttack;
    float compRelease;
    float gateThresh;
    float gateRatio;
    float gateSustain;
    float gateRelease;

    Pop3Coefs()
        : compThresh(1.0f), compRatio(0.0f), compAttack(0.0f),
          compRelease(0.0f), gateThresh(0.0f), gateRatio(0.0f),
          gateSustain(1.5707963f), gateRelease(0.0f) {}
  };

  // Never call per sample: contains pow().
  static inline void pop3Bake(Pop3Coefs &c,
                              double thresh, double ratio,
                              double attack, double release,
                              double gThresh, double gRatio,
                              double gSustain, double gRelease,
                              double sr)
  {
    if (sr <= 0.0) sr = 48000.0;
    // AW's overallscale. Every time constant below was tuned at
    // 44.1 kHz; without this they run fast at 48 k and faster still
    // at 96 k.
    const double scale = sr / 44100.0;

    c.compThresh = (float)pow(thresh, 4.0);
    c.compRatio = (float)(1.0 - pow(1.0 - ratio, 2.0));
    c.compAttack = (float)(1.0 / (((pow(attack, 3.0) * 5000.0) + 500.0) * scale));
    c.compRelease = (float)(1.0 / (((pow(release, 5.0) * 50000.0) + 500.0) * scale));

    c.gateThresh = (float)pow(gThresh, 4.0);
    c.gateRatio = (float)(1.0 - pow(1.0 - gRatio, 2.0));
    c.gateSustain = (float)(M_PI_2 * pow(gSustain + 1.0, 4.0));
    c.gateRelease = (float)(1.0 / (((pow(gRelease, 5.0) * 500000.0) + 500.0) * scale));
  }

  // One channel. A stereo consumer instantiates two and calls
  // pop3StereoLink() between them; see the note on that function,
  // because the gate in the original is genuinely SHARED rather than
  // duplicated.
  class Pop3Mono
  {
  public:
    Pop3Mono() { reset(); }

    void reset()
    {
      mComp = 1.0f;
      mGate = 0.0f;
    }

    // Advance the detector for this sample WITHOUT applying gain. Split
    // out so a stereo pair can both observe, then link, then apply -
    // which is the order the original uses.
    inline void observe(float in, const Pop3Coefs &c)
    {
      float a = in < 0.0f ? -in : in;
      // Denormal guard. am335x links -nostdlib, so the crtfastmath
      // that normally sets flush-to-zero is absent and a denormal can
      // trap into slow support code.
      if (a < 1.0e-20f) a = 0.0f;

      if (a > c.compThresh)
      {
        mComp -= mComp * c.compAttack;
        mComp += (c.compThresh / a) * c.compAttack;
      }
      else
      {
        mComp = mComp * (1.0f - c.compRelease) + c.compRelease;
      }
      if (mComp > 1.0f) mComp = 1.0f;
      else if (mComp < 0.0f) mComp = 0.0f;

      // Gate reads the UNCOMPRESSED level, deliberately.
      if (a > c.gateThresh) mGate = c.gateSustain;
      else mGate *= (1.0f - c.gateRelease);
      if (mGate < 0.0f) mGate = 0.0f;
    }

    // Apply the gain decided by the last observe(). Ratio 0 on both
    // stages returns `in` bit-for-bit: the compressor crossfade is
    // (1-0) + comp*0 == 1.0 exactly, and the gate term is skipped or
    // likewise 1.0.
    inline float apply(float in, const Pop3Coefs &c) const
    {
      float y = in * ((1.0f - c.compRatio) + (mComp * c.compRatio));
      // The sine is skipped entirely once phase reaches pi/2, because
      // sin(pi/2) is 1 and the crossfade would be transparent. This is
      // what makes an open gate free.
      if (mGate < 1.5707963f)
        y *= ((1.0f - c.gateRatio) + (pop3Sin(mGate) * c.gateRatio));
      return y;
    }

    inline float process(float in, const Pop3Coefs &c)
    {
      observe(in, c);
      return apply(in, c);
    }

    // Exposed so a stereo consumer can link. Not for general use.
    inline float compState() const { return mComp; }
    inline float gateState() const { return mGate; }
    inline void setCompState(float v) { mComp = v; }
    inline void setGateState(float v) { mGate = v; }

  private:
    float mComp;
    float mGate;
  };

  // Stereo link, called after both channels observe() and before
  // either apply(). Two distinct behaviours in the original, and they
  // are not the same thing:
  //
  //   - The COMPRESSOR link is asymmetric: whichever channel is
  //     currently less compressed gets pulled down toward the other by
  //     one attack step. It converges the pair without hard-linking
  //     them, so a hard pan still moves.
  //   - The GATE is genuinely SHARED - one variable in the original,
  //     triggered by EITHER channel crossing threshold. Duplicating it
  //     per channel would let one side gate while the other did not,
  //     which is audible as the image collapsing.
  static inline void pop3StereoLink(Pop3Mono &l, Pop3Mono &r, const Pop3Coefs &c)
  {
    float cl = l.compState(), cr = r.compState();
    if (cl > cr) cl -= cl * c.compAttack;
    if (cr > cl) cr -= cr * c.compAttack;
    l.setCompState(cl);
    r.setCompState(cr);

    const float g = l.gateState() > r.gateState() ? l.gateState() : r.gateState();
    l.setGateState(g);
    r.setGateState(g);
  }

} // namespace house
