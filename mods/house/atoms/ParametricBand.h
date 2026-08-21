// house::ParametricBand
//
// Component atom: ONE parametric EQ band - bell or shelf, with a
// level-dependent nonlinearity. Component-only per
// feedback_atoms_as_components; a single band has no standalone
// value, so there is no od::Object wrapper and no Lua unit. The
// consumers instantiate it directly in C++.
//
// Consumers: `ochre-character-eq` (four of these) and
// `strata-channel-strip`'s EQ section (three or four). Habitat had
// NO EQ atom at all before this - only biome's one-knob Tilt EQ and
// the firmware's EQ3.
//
// SOURCE, and what was and was not taken. The level-dependent
// nonlinearity is Airwindows BiquadStack (Chris Johnson, MIT):
//
//   dis = fabs(a0 * (1.0 + out*nonlin));  if (dis > 1.0) dis = 1.0;
//
// i.e. the filter's INPUT coefficient is modulated by instantaneous
// level and saturates by clamping. No tanh, no transcendentals in
// the sample loop. That idea is lifted; the surrounding structure is
// NOT.
//
// BiquadStack is a stack of three BANDPASSES with a Butterworth Q
// ladder. That cannot make a shelf, and a four-band EQ needs shelves
// for LF and HF, so lifting the kernel wholesale would have solved
// half the problem. Instead the core here is a TPT state-variable
// filter, which:
//   - yields LP, BP and HP from ONE structure, so bell and shelf are
//     the same code with a different tap,
//   - is unconditionally stable at high Q, where a direct-form
//     biquad is not,
//   - is already proven on am335x in this tree (Breccia's per-slice
//     filter uses the same kernel).
//
// BYPASS IS EXACT BY CONSTRUCTION. The band is additive:
//
//   out = in + gain * tap(in)
//
// so gain == 0 returns `in` bit-for-bit without needing a branch or
// a comparison. The ochre design note calls for exactly this and
// warns to check it rather than assume it; tools/parametric-band-test
// asserts it.
//
// COEFFICIENTS IN DOUBLE, RECURSION IN FLOAT. The aw-batch2-ports
// finding: float is safe except for the lowest-frequency biquads,
// and this band reaches 30 Hz. tan() and the divide run at block
// rate in double; the per-sample recursion is float.
#pragma once

#include <od/config.h>
#include <math.h>
#include <stdint.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define PARAM_BAND_NEON 1
#endif

namespace house
{

  // Which tap the band uses. Bell adds a bandpass, so the curve is
  // symmetric about the centre and unity away from it. Shelves add a
  // lowpass or highpass, giving 6 dB/octave transitions per the SSL
  // 611 spec the ochre note cites.
  enum ParametricBandShape
  {
    PARAM_BAND_BELL = 0,
    PARAM_BAND_LOW_SHELF = 1,
    PARAM_BAND_HIGH_SHELF = 2
  };

  // Q law. The ochre note makes this the job of the Colour switch,
  // and it is a coefficient decision rather than new DSP.
  enum ParametricBandQLaw
  {
    // Constant-octave bandwidth: Q is whatever the caller asked for,
    // independent of gain. The "good-natured" behaviour.
    PARAM_BAND_Q_CONSTANT = 0,
    // Proportional-Q: Q tightens as |gain| rises, so a big boost is
    // narrower than a small one. The more aggressive behaviour.
    PARAM_BAND_Q_PROPORTIONAL = 1,
    // SKIRT-PINNED proportional, the API 550 behaviour. Q rises with
    // gain like PROPORTIONAL, but at the specific rate that leaves the
    // curve's SKIRT where it is: the frequency at which the response
    // first departs from flat stays put as gain increases, while the
    // peak narrows around it.
    //
    // SOLVED BY MEASUREMENT, not guessed. Sweeping the exponent p in
    // q *= 10^(|dB|*p/20) and measuring where the response first
    // exceeds +0.5 dB, across +3/+6/+12/+18 dB:
    //     p = 0.0 (constant Q)  skirt moves 3.09x
    //     p = 0.4               1.58x
    //     p = 0.7               1.13x   <- best
    //     p = 1.0               1.34x
    // At 0.7 the peak still lands exactly (3.00 / 6.00 / 12.00 / 17.99
    // dB) and the half-gain width still narrows 1.10 -> 0.34 octaves,
    // so it tightens as pushed without the skirt walking inward. That
    // is what lets an API tolerate aggressive settings.
    PARAM_BAND_Q_PINNED = 2,
    // Fixed BANDWIDTH rather than fixed Q - the Neve 1073 mid. Shape is
    // constant and only amplitude scales. Not yet used by a position;
    // kept because the research names it as a third distinct law.
    PARAM_BAND_Q_FIXED_BW = 3
  };

  // Baked once per block by the caller, never per sample.
  struct ParametricBandCoefs
  {
    float g;        // tan(pi*fc/sr) prewarp
    float k;        // 1/Q damping
    float a1, a2, a3;
    float gain;     // linear amplitude to add; 0 == exact bypass
    float nonlin;   // 0 == clean; the AW level-dependent term
    // 1.0 = ADDITIVE, out = in + gain*tap. This is EQ: the band adds a
    //       bump or dip to the dry signal, and gain 0 is exact bypass.
    // 0.0 = REPLACING, out = gain*tap. This is a FILTER: the tap IS the
    //       output, so the same SVF yields a lowpass or a highpass with
    //       no extra DSP and the same 30-instruction stereo NEON kernel.
    // One multiply, no branch, and it keeps both behaviours in one
    // kernel rather than duplicating the filter for the Filter section.
    float passthru;
    // Tap selectors, BAKED. Exactly one is 1.0f. These used to be
    // `(shape == X) ? 1.0f : 0.0f` evaluated inside the sample loop,
    // which cost 50 vcmpe.f32 per sample across a stereo four-band
    // unit to re-decide something fixed for the whole block.
    float selBell, selLow, selHigh;

    ParametricBandCoefs()
        : g(0.0f), k(1.0f), a1(0.0f), a2(0.0f), a3(0.0f),
          gain(0.0f), nonlin(0.0f), passthru(1.0f),
          selBell(1.0f), selLow(0.0f), selHigh(0.0f) {}
  };

  // Bake coefficients. sr and the trig are DOUBLE here; this runs at
  // block rate. dB is the band gain, q the requested Q, drive 0..1
  // the nonlinearity amount.
  //
  // Never call this per sample: it contains tan() and a divide.
  static inline void parametricBandBake(ParametricBandCoefs &c,
                                        double freqHz, double dB, double q,
                                        double drive, double sr,
                                        ParametricBandShape shape,
                                        ParametricBandQLaw law)
  {
    if (sr <= 0.0) sr = 48000.0;
    // Clamp below Nyquist with margin: tan() blows up approaching
    // pi/2, and an EQ's HF band legitimately reaches 16 kHz, which is
    // already a third of the way there at 48k.
    double f = freqHz;
    if (f < 5.0) f = 5.0;
    const double fmax = sr * 0.45;
    if (f > fmax) f = fmax;

    double qq = q;
    if (qq < 0.05) qq = 0.05;
    const double mag = dB < 0.0 ? -dB : dB;
    if (law == PARAM_BAND_Q_PROPORTIONAL)
    {
      // Q rises with gain magnitude. 12 dB of boost roughly doubles
      // it, which is the audible end of the published behaviour
      // without becoming a resonator.
      qq *= (1.0 + mag * (1.0 / 12.0));
    }
    else if (law == PARAM_BAND_Q_PINNED)
    {
      // See the enum: 0.7 is the measured skirt-pinning exponent.
      qq *= pow(10.0, mag * 0.7 / 20.0);
    }
    else if (law == PARAM_BAND_Q_FIXED_BW)
    {
      // Constant bandwidth in Hz rather than in octaves: Q must scale
      // with centre frequency. Referenced to 1 kHz so the caller's Q
      // still means what it does elsewhere at that frequency.
      qq *= (f / 1000.0);
      if (qq < 0.05) qq = 0.05;
    }
    if (qq > 40.0) qq = 40.0;

    // CONSTANT-Q BELL. The naive SVF bell, out = in + (G-1)*k*BP with
    // k = 1/Q, has transfer (s^2 + s*G/Q + 1)/(s^2 + s/Q + 1). Its
    // HALF-GAIN bandwidth widens with gain - measured 1.88, 2.51 and
    // 3.27 octaves at +6, +12 and +18 dB, which is not log-symmetric
    // and is not what an analogue parametric does.
    //
    // The standard peaking form (RBJ, A = 10^(dB/40), G = A^2) is
    // (s^2 + s*A/Q + 1)/(s^2 + s/(A*Q) + 1) - same numerator shape,
    // but the DENOMINATOR damps by 1/(A*Q) rather than 1/Q. Dividing
    // the SVF damping by A gives that exactly, because the tap gain
    // (G-1)*k then makes the numerator s*G/(A*Q) = s*A/Q.
    //
    // At 0 dB, A == 1, so k is untouched and the exact-bypass
    // property is unaffected.
    const double A = pow(10.0, dB / 40.0);
    const double w = M_PI * f / sr;
    const double g = tan(w);
    const double k = 1.0 / (qq * A);
    const double a1 = 1.0 / (1.0 + g * (g + k));

    c.g = (float)g;
    c.k = (float)k;
    c.a1 = (float)a1;
    c.a2 = (float)(g * a1);
    c.a3 = (float)(g * g * a1);
    // dB to linear DELTA, not absolute gain: the band adds to the dry
    // signal, so 0 dB must map to 0.0 and not 1.0.
    c.gain = (float)(pow(10.0, dB / 20.0) - 1.0);
    c.nonlin = (float)(drive < 0.0 ? 0.0 : (drive > 1.0 ? 1.0 : drive));
    c.passthru = 1.0f;
    c.selBell = (shape == PARAM_BAND_BELL) ? 1.0f : 0.0f;
    c.selLow = (shape == PARAM_BAND_LOW_SHELF) ? 1.0f : 0.0f;
    c.selHigh = (shape == PARAM_BAND_HIGH_SHELF) ? 1.0f : 0.0f;
  }

  // Drive scaling, CALIBRATED BY MEASUREMENT against the parabolic
  // shaper below, not carried over from another curve.
  //
  // Unscaled, full Drive measured 0.29% THD at a realistic -12 dBFS -
  // inaudible. A first attempt reused the scale calibrated for a
  // different (gain-factor) saturator and overshot to 35-71% THD,
  // which the drive grid in tools/parametric-band-test caught at once.
  //
  // CALIBRATE AT THE BAND CENTRE. A first calibration probed an octave
  // off-centre and picked 6.0; at the centre - where the bandpass peaks
  // and where a user actually listens when boosting a band - that same
  // value gives 71% THD. The operating point matters more than the
  // curve here, and the honest one is fc == probe.
  //
  // At 0.5, measured at the band centre with Q=2 and +12 dB, full Drive
  // reads:
  //     -26 dBFS  0.33%      -12 dBFS  1.75%      -1 dBFS  8.10%
  // monotonic in BOTH level and drive across the whole grid.
  static const float kDriveScale = 0.5f;

  // Parabolic soft-clip, clamped at the vertex so it cannot fold back.
  // Multiply-only. nonlin 0 returns t untouched.
  static inline float parametricBandSaturate(float t, float nonlin)
  {
    const float d = nonlin * kDriveScale;
    if (d <= 0.0f) return t;
    const float mag = t < 0.0f ? -t : t;
    float y = t - d * t * mag;
    const float lim = 0.25f / d;          // vertex of t - d*t^2
    if (y > lim) y = lim;
    else if (y < -lim) y = -lim;
    return y;
  }

  // Bake a PURE FILTER rather than an EQ band: the tap becomes the
  // output instead of being added to it. Used by a channel strip's
  // Filter section, which wants a plain highpass and lowpass for
  // removing rumble and hiss.
  //
  // WHY NOT Capacitor2, which the design note originally named for this:
  // measured on real A8 codegen, Capacitor2Mono is 356 instructions with
  // 223 DOUBLE-precision ops, and that is MONO - roughly 712 for stereo,
  // against 30 here for both channels. Doubles fall to scalar VFP on A8
  // (feedback_cortex_a8_no_double_in_hot_loops) and its per-sample
  // coefficient modulation makes nothing hoistable. It is a fine
  // character filter and a poor utility filter; the character belongs in
  // the Drive section.
  static inline void parametricBandBakeFilter(ParametricBandCoefs &c,
                                              double freqHz, double q,
                                              double sr, bool highpass)
  {
    parametricBandBake(c, freqHz, 0.0, q, 0.0, sr,
                       highpass ? PARAM_BAND_HIGH_SHELF : PARAM_BAND_LOW_SHELF,
                       PARAM_BAND_Q_CONSTANT);
    c.passthru = 0.0f;   // tap replaces the signal
    c.gain = 1.0f;       // at unity
  }

  // One band, one channel. A stereo consumer instantiates two.
  class ParametricBandMono
  {
  public:
    ParametricBandMono() { reset(); }

    void reset()
    {
      mIc1 = 0.0f;
      mIc2 = 0.0f;
    }

    // Process one sample. Returns in + gain*tap(in), so gain == 0 is
    // a bit-identical bypass with no branch on the audio path.
    // BAND-MAJOR BLOCK PROCESSING. Prefer this over calling process()
    // per sample inside a per-band loop.
    //
    // Why it matters on am335x: four bands hold 4 x 8 = 32 coefficients,
    // which exceeds the 32 VFP s-registers, so a sample-major loop
    // spills and reloads every coefficient every sample. Measured 45
    // vldr per sample for four bands. Running ONE band across the whole
    // block keeps that band's 8 coefficients and 2 state words resident
    // for the entire pass, and the loads collapse to the signal itself.
    //
    // The trade is that the signal is read and written per band rather
    // than once, but that is 2 accesses against the ~11 loads per band
    // it replaces.
    void processBlock(float *io, int n, const ParametricBandCoefs &c)
    {
      // Hoist into locals so the compiler is free to keep them in
      // registers rather than re-reading the struct.
      const float a1 = c.a1, a2 = c.a2, a3 = c.a3, k = c.k;
      const float g = c.gain, nl = c.nonlin;
      const float sB = c.selBell, sL = c.selLow, sH = c.selHigh;
      const float pt = c.passthru;
      float ic1 = mIc1, ic2 = mIc2;
      for (int i = 0; i < n; i++)
      {
        const float in = io[i];
        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        float tap = sB * (v1 * k) + sL * v2 + sH * (in - k * v1 - v2);
        io[i] = in * pt + g * parametricBandSaturate(tap, nl);
      }
      mIc1 = ic1;
      mIc2 = ic2;
    }

    inline float process(float in, const ParametricBandCoefs &c)
    {
      // The SVF core runs with its BAKED coefficients, untouched.
      //
      // The nonlinearity used to modulate a2 here, directly copying
      // BiquadStack. That is WRONG in this structure and it produced
      // NaN: in AW's direct-form biquad, a0 is an INPUT coefficient,
      // so inflating it only boosts level and cannot destabilize. In a
      // TPT SVF, a2 sits in the FEEDBACK path - it appears in both v1
      // and v2 - so inflating it MOVES THE POLES. Measured: at Q=10
      // with the proportional law at +18 dB, k falls to 0.0142 and a2
      // at 100 Hz is 0.0065, while the clamp allowed dis up to 1.0.
      // That is 153x the coefficient it replaced, and the filter left
      // the unit circle. The idea does not transfer to this topology.
      const float v3 = in - mIc2;
      const float v1 = c.a1 * mIc1 + c.a2 * v3;
      const float v2 = mIc2 + c.a2 * mIc1 + c.a3 * v3;
      mIc1 = 2.0f * v1 - mIc1;
      mIc2 = 2.0f * v2 - mIc2;

      // Branchless tap select against BAKED selectors - no compare in
      // the loop. feedback_runtime_branched_dsp_dispatch records that
      // a runtime switch inside a sample loop has hung the A8.
      const float bp = v1;
      const float lp = v2;
      const float hp = in - c.k * v1 - v2;
      const float sel0 = c.selBell;
      const float sel1 = c.selLow;
      const float sel2 = c.selHigh;
      // Bell taps the bandpass scaled by k so that the peak reaches
      // the requested gain independently of Q; without the k the
      // boost would shrink as Q rises. k here already carries the
      // constant-Q 1/A factor baked above.
      float tap = sel0 * (bp * c.k) + sel1 * lp + sel2 * hp;

      // Level-dependent saturation on the band's OUTPUT TAP, outside
      // every feedback path so it cannot move a pole.
      //
      // PARABOLIC, CLAMPED AT ITS VERTEX. y = t - d*t*|t| has
      // derivative 1 - 2d|t|, which turns negative at |t| > 1/(2d), so
      // the clamp is placed at the vertex value 1/(4d) and the curve is
      // MONOTONIC by construction. The previous form, a clamped linear
      // rolloff on the gain factor, degenerated into a constant gain
      // once clamped, so measured THD FELL as drive rose past a point
      // (5.38% -> 3.66% at the loudest corner). Multiply-only either
      // way; no divide, no transcendental.
      //
      // kDriveScale exists because the un-scaled form was inaudible:
      // measured 0.29% THD at a realistic -12 dBFS with a +12 dB band,
      // where 2.5% is a musical amount. The scale puts full Drive in
      // the 0.5-5% range across normal levels.
      //
      // Drive does NOTHING when the band gain is 0 dB, and that is
      // deliberate (user decision 2026-08-20): this is a modifier on
      // the EQ's own action, so a flat EQ stays a bit-identical bypass.
      tap = parametricBandSaturate(tap, c.nonlin);

      return in * c.passthru + c.gain * tap;
    }

  private:
    float mIc1;
    float mIc2;
  };

  // Stereo pair of one band. L and R are INDEPENDENT and elementwise,
  // so this is the natural 2-wide NEON axis - the same axis EQ3 uses.
  // The scalar fallback keeps x86 and the emulator building and lets
  // the harness compare the two paths for equality.
  //
  // State is a CLASS MEMBER, never a stack local:
  // feedback_neon_intrinsics_drumvoice records that NEON on A8 is safe
  // when arrays live in the object, while stack-locals emit trapping
  // vld1.32 [reg :64] hints. check-neon-hints.sh gates this.
  class ParametricBandStereo
  {
  public:
    ParametricBandStereo() { reset(); }

    void reset()
    {
      mS[0] = mS[1] = mS[2] = mS[3] = 0.0f;
    }

    // Process interleaved-by-array stereo: separate L and R buffers,
    // n samples, one band. Band-major, so this band's coefficients stay
    // resident for the whole block.
    // add == true  : SERIES. io = io + gain*tap(io). Each band sees the
    //                previous band's output, so overlapping bands stack
    //                and four +6 dB bands at one frequency give +24 dB.
    // add == false : PARALLEL. io += gain*tap(dry), reading `dry`
    //                instead of the running signal, so bands sum against
    //                the input rather than chaining. Overlapping bands
    //                then stay near the intended curve instead of
    //                compounding - the same four bands give +13.95 dB.
    //                This is what passive designs do, and sources call
    //                additive-vs-non-additive one of the most underrated
    //                reasons EQs sound different.
    void processBlockParallel(const float *dryL, const float *dryR,
                              float *l, float *r, int n,
                              const ParametricBandCoefs &c)
    {
      const float a1 = c.a1, a2 = c.a2, a3 = c.a3, k = c.k;
      const float g = c.gain, nl = c.nonlin;
      const float sB = c.selBell, sL = c.selLow, sH = c.selHigh;
      float i1l = mS[0], i1r = mS[1], i2l = mS[2], i2r = mS[3];
      for (int i = 0; i < n; i++)
      {
        const float xl = dryL[i], xr = dryR[i];
        const float v3l = xl - i2l, v3r = xr - i2r;
        const float v1l = a1 * i1l + a2 * v3l, v1r = a1 * i1r + a2 * v3r;
        const float v2l = i2l + a2 * i1l + a3 * v3l;
        const float v2r = i2r + a2 * i1r + a3 * v3r;
        i1l = 2.0f * v1l - i1l; i1r = 2.0f * v1r - i1r;
        i2l = 2.0f * v2l - i2l; i2r = 2.0f * v2r - i2r;
        const float tl = sB * (v1l * k) + sL * v2l + sH * (xl - k * v1l - v2l);
        const float tr = sB * (v1r * k) + sL * v2r + sH * (xr - k * v1r - v2r);
        l[i] += g * parametricBandSaturate(tl, nl);
        r[i] += g * parametricBandSaturate(tr, nl);
      }
      mS[0] = i1l; mS[1] = i1r; mS[2] = i2l; mS[3] = i2r;
    }

    void processBlock(float *l, float *r, int n, const ParametricBandCoefs &c)
    {
#ifdef PARAM_BAND_NEON
      const float32x2_t a1 = vdup_n_f32(c.a1), a2 = vdup_n_f32(c.a2);
      const float32x2_t a3 = vdup_n_f32(c.a3), kk = vdup_n_f32(c.k);
      const float32x2_t gg = vdup_n_f32(c.gain), pt = vdup_n_f32(c.passthru);
      const float32x2_t sB = vdup_n_f32(c.selBell), sL = vdup_n_f32(c.selLow);
      const float32x2_t sH = vdup_n_f32(c.selHigh);
      const float32x2_t two = vdup_n_f32(2.0f), one = vdup_n_f32(1.0f);
      const float dScalar = c.nonlin * kDriveScale;
      const float32x2_t dsc = vdup_n_f32(dScalar);
      const float32x2_t lim = vdup_n_f32(dScalar > 0.0f ? 0.25f / dScalar : 1.0e30f);
      float32x2_t ic1 = vld1_f32(&mS[0]);
      float32x2_t ic2 = vld1_f32(&mS[2]);
      for (int i = 0; i < n; i++)
      {
        float32x2_t in = vset_lane_f32(r[i], vdup_n_f32(l[i]), 1);
        const float32x2_t v3 = vsub_f32(in, ic2);
        const float32x2_t v1 = vmla_f32(vmul_f32(a1, ic1), a2, v3);
        const float32x2_t v2 = vadd_f32(ic2, vmla_f32(vmul_f32(a2, ic1), a3, v3));
        ic1 = vmls_f32(vmul_f32(two, v1), one, ic1);
        ic2 = vmls_f32(vmul_f32(two, v2), one, ic2);
        const float32x2_t hp = vsub_f32(vsub_f32(in, vmul_f32(kk, v1)), v2);
        float32x2_t tap = vmul_f32(sB, vmul_f32(v1, kk));
        tap = vmla_f32(tap, sL, v2);
        tap = vmla_f32(tap, sH, hp);
        // Parabolic soft-clip, clamped at the vertex. Matches
        // parametricBandSaturate() exactly; the harness compares the
        // scalar and NEON paths sample for sample.
        const float32x2_t shaped = vmls_f32(tap, vmul_f32(dsc, tap), vabs_f32(tap));
        const float32x2_t out = vmla_f32(vmul_f32(in, pt), gg,
            vmax_f32(vmin_f32(shaped, lim), vneg_f32(lim)));
        l[i] = vget_lane_f32(out, 0);
        r[i] = vget_lane_f32(out, 1);
      }
      vst1_f32(&mS[0], ic1);
      vst1_f32(&mS[2], ic2);
#else
      const float a1 = c.a1, a2 = c.a2, a3 = c.a3, k = c.k;
      const float g = c.gain, nl = c.nonlin;
      const float sB = c.selBell, sL = c.selLow, sH = c.selHigh;
      const float pt = c.passthru;
      float i1l = mS[0], i1r = mS[1], i2l = mS[2], i2r = mS[3];
      for (int i = 0; i < n; i++)
      {
        const float xl = l[i], xr = r[i];
        const float v3l = xl - i2l, v3r = xr - i2r;
        const float v1l = a1 * i1l + a2 * v3l, v1r = a1 * i1r + a2 * v3r;
        const float v2l = i2l + a2 * i1l + a3 * v3l;
        const float v2r = i2r + a2 * i1r + a3 * v3r;
        i1l = 2.0f * v1l - i1l; i1r = 2.0f * v1r - i1r;
        i2l = 2.0f * v2l - i2l; i2r = 2.0f * v2r - i2r;
        const float tl = sB * (v1l * k) + sL * v2l + sH * (xl - k * v1l - v2l);
        const float tr = sB * (v1r * k) + sL * v2r + sH * (xr - k * v1r - v2r);
        l[i] = xl * pt + g * parametricBandSaturate(tl, nl);
        r[i] = xr * pt + g * parametricBandSaturate(tr, nl);
      }
      mS[0] = i1l; mS[1] = i1r; mS[2] = i2l; mS[3] = i2r;
#endif
    }

  private:
    // Class member, not a stack local - see the note above.
    // [0]=ic1 L, [1]=ic1 R, [2]=ic2 L, [3]=ic2 R.
    float mS[4];
  };

} // namespace house
