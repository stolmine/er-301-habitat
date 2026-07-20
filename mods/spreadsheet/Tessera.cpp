#include "Tessera.h"
#include "DrumVoiceSineLUT.h"   // static half-sine LUT (no runtime trig on am335x)
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

// no-tree-vectorize is load-bearing on am335x (feedback_disable_tree_vectorize_am335x).
#pragma GCC optimize("no-tree-vectorize")

namespace stolmine
{
  // ---------------------------------------------------------------------------
  // Modal engine fitted to the Trinity BLOCK mode-structure campaign (1143 hardware
  // captures: fine 1-D throws for all 8 controls + all 28 2-D pair matrices).
  // Analysis: ~/repos/trinity-midi-harness/analysis-modemap.md
  //
  // KEY STRUCTURAL FINDING: the hardware spectrum is NOT stretched odd harmonics. Every
  // capture factorizes as f(h,k) = fc*(h + k*r): a core oscillator (odd harmonics h) cross-
  // modulated by a 2nd oscillator at fc*(1+r), giving uniformly-spaced intermod sidebands.
  // ~30% of sideband amplitude sits BELOW fc (the k=-1,-2 "sub" modes) - that is the
  // hardware's body/punch, which a stretched-harmonic model cannot produce at all.
  //
  // Two decay classes (measured): "tone" modes (carrier / osc2 / folded core harmonics)
  // ring at tauRatio ~0.93-1.0; ALL sidebands are short (~0.35, weak ones 0.15-0.2).
  // That class split IS the measured brightness-decay mechanism.
  // All control interdependencies reduce to analytic forms (no 2-D tables needed).
  // ---------------------------------------------------------------------------
  static const int NM = 12;

  // mode (h,k): f = fc*(h + k*r)
  static const float kH[NM] = {1, 1, 1, 3, 1, 3, 1, 5, 3, 5, 7, 3};
  static const float kK[NM] = {0, 1, -1, 1, 2, 2, -2, 2, 0, 0, 0, -1};
  // Per-mode amplitude, least-squares fitted against all 1143 hardware captures
  // (fit_amps.py). amp = c0 + c1*dL + c2*foldN + c3*r + c4*lf + c5*g.
  // The r term is included ONLY for the modes where the stored data shows a real
  // r-dependence (3,4,5,7,8,10,11). For the carrier, osc2, both sub modes and (5,0)
  // the measured amplitude is FLAT in r (e.g. sub = 0.226/0.243/0.236 at r=0.20/0.39/
  // 0.58) - earlier fits produced a large negative r slope there purely because 370 of
  // 423 samples sit at r=0.39, and that phantom slope was collapsing the sub (the
  // body/punch) by up to 5x at mid Shape.
  static const float kAmpFit[NM][6] = {
    { 0.9935f,  0.0070f,  0.0049f,  0.0000f, -0.0122f, -0.0801f},  // h=1 k=+0 carrier
    { 0.6483f,  0.0306f, -0.0419f,  0.0000f, -0.0482f, -0.0203f},  // h=1 k=+1 osc2
    { 0.3075f,  0.0240f, -0.1494f,  0.0000f, -0.0278f, -0.0301f},  // h=1 k=-1 sub
    { 0.2088f,  0.0251f,  0.1630f,  0.0141f, -0.0347f,  0.0450f},  // h=3 k=+1
    { 0.3876f, -0.0014f,  0.2280f, -0.1182f, -0.0597f,  0.0165f},  // h=1 k=+2
    { 0.1923f,  0.0130f,  0.1098f, -0.0632f, -0.0428f,  0.1504f},  // h=3 k=+2
    { 0.2590f, -0.0566f,  0.1656f,  0.0000f, -0.1133f,  0.2749f},  // h=1 k=-2 sub
    { 0.1107f, -0.0036f,  0.0488f, -0.0136f,  0.0175f,  0.3865f},  // h=5 k=+2
    { 0.1667f,  0.0504f,  0.2011f,  0.1799f, -0.0446f,  0.0166f},  // h=3 k=+0
    { 0.1897f,  0.0267f, -0.0601f,  0.0000f,  0.0003f,  0.2468f},  // h=5 k=+0
    { 0.1559f,  0.0023f, -0.0553f, -0.0391f,  0.0055f,  0.4403f},  // h=7 k=+0
    { 0.1542f, -0.0047f, -0.1143f,  0.3891f, -0.0221f,  0.0759f},  // h=3 k=-1
  };
  // tone modes ring long, sidebands short
  static const float kTauR[NM] = {1.00f, 0.94f, 0.35f, 0.35f, 0.35f, 0.34f,
                                  0.20f, 0.17f, 0.95f, 0.95f, 0.93f, 0.35f};

  static inline float sineLUT(float phase)   // phase in [0,1) -> sin(2*pi*phase)
  {
    phase -= floorf(phase);
    bool neg = phase >= 0.5f;
    float ph = neg ? (phase - 0.5f) : phase;
    float idx = ph * 512.0f;
    int i = (int)idx;
    float fr = idx - (float)i;
    float s = kDrumVoiceSineLUT[i] + fr * (kDrumVoiceSineLUT[i + 1] - kDrumVoiceSineLUT[i]);
    return neg ? -s : s;
  }

  static inline uint32_t lcg(uint32_t &s) { s = s * 1103515245u + 12345u; return s; }
  static inline float noise(uint32_t &s) { return (float)((lcg(s) >> 9) & 0xFFFF) / 32768.0f - 1.0f; }

  // The measured amp table holds EXTRACTOR-measured amplitudes, which for short-tau
  // modes are already partly decayed over the ~110 ms analysis window. Convert a
  // measured amp back to the true initial amp: W(tau) = (tau/T)(1-e^-T/tau) is the
  // window-average of the decay; initial = measured * W(tau_carrier)/W(tau_mode).
  // Without this the sub/sideband modes render ~2x too quiet (measured across the grid).
  static inline float winAvg(float tau_s)
  {
    const float Tw = 0.11f;
    if (tau_s < 1e-4f) return 1e-4f;
    return (tau_s / Tw) * (1.0f - expf(-Tw / tau_s));
  }

  // measured HF tau ceiling: no cap below ~600 Hz, ~500 ms @950 Hz, ~60 ms @1.4 kHz+
  static inline float hfCeilMs(float f)
  {
    if (f <= 600.0f) return 1.0e9f;
    if (f >= 1400.0f) return 60.0f;
    if (f <= 950.0f)
    {
      float t = (logf(f) - logf(600.0f)) / (logf(950.0f) - logf(600.0f));
      return 3000.0f * powf(500.0f / 3000.0f, t);
    }
    float t = (logf(f) - logf(950.0f)) / (logf(1400.0f) - logf(950.0f));
    return 500.0f * powf(60.0f / 500.0f, t);
  }

  struct Tessera::Internal
  {
    float phase[NM], env[NM], mfreq[NM], mdecay[NM];
    float pitchEnv = 0, pitchCoeff = 0.999f, startMult = 1;
    float noiseEnv = 0, noiseCoeff = 0, noiseLp = 0, burst = 0, burstCoeff = 0;
    int holdLeft = 0;
    float prevTrig = 0;
    uint32_t rng = 0x51ee7u;
    Internal() { for (int m = 0; m < NM; m++) { phase[m] = 0; env[m] = 0; mfreq[m] = 0; mdecay[m] = 0; } }
  };

  Tessera::Tessera()
  {
    addInput(mTrigger);
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mF0);
    addParameter(mCharacter);
    addParameter(mShape);
    addParameter(mGrit);
    addParameter(mSweep);
    addParameter(mTime);
    addParameter(mHold);
    addParameter(mDecay);
    addParameter(mLevel);
    mpInternal = new Internal();
  }

  Tessera::~Tessera() { delete mpInternal; }

  float Tessera::getEnvLevel() { return mpInternal->env[0]; }

  void Tessera::process()
  {
    float *trig = mTrigger.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();
    Internal &I = *mpInternal;
    float sr = globalConfig.sampleRate;
    float nyq = sr * 0.45f;

    // knobs 0..1 -> the hardware's CC 0..127 throw (all laws fitted in CC domain)
    float ccChar = CLAMP(0.0f, 1.0f, mCharacter.value()) * 127.0f;
    float ccShape = CLAMP(0.0f, 1.0f, mShape.value()) * 127.0f;
    float ccGrit = CLAMP(0.0f, 1.0f, mGrit.value()) * 127.0f;
    float ccSweep = CLAMP(0.0f, 1.0f, mSweep.value()) * 127.0f;
    float ccTime = CLAMP(0.0f, 1.0f, mTime.value()) * 127.0f;
    float ccHold = CLAMP(0.0f, 1.0f, mHold.value()) * 127.0f;
    float ccDecay = CLAMP(0.0f, 1.0f, mDecay.value()) * 127.0f;
    float level = CLAMP(0.0f, 1.0f, mLevel.value());
    float f0 = CLAMP(8.0f, 8000.0f, mF0.value());

    // Shape -> osc2 detune ratio (measured linear, pitch-tracked)
    float r = CLAMP(0.0f, 2.0f, 0.0189f * (ccShape - 9.2f));
    // Character -> fold amount: dead zone to CC 78, then linear (when osc2 active)
    float fold = CLAMP(0.0f, 1.0f, (ccChar - 78.0f) / 42.0f);
    // Decay -> carrier tau (quadratic-in-log law, ~10 ms .. 3 s)
    float tauC = expf(2.447f + 0.0576f * ccDecay - 0.000115f * ccDecay * ccDecay);  // ms
    // Grit -> tau ceiling (harmonic sum), measured regimes
    float tauEff = tauC;
    if (ccGrit > 70.0f)
    {
      float t = CLAMP(0.0f, 1.0f, (ccGrit - 70.0f) / 40.0f);
      float tauG = 250.0f * powf(60.0f / 250.0f, t);
      tauEff = 1.0f / (1.0f / tauC + 1.0f / tauG);
    }
    float L = logf(tauC);            // spectral-reshaping driver (pre-ceiling)
    float dL = L - 5.489f;           // L0 = ln(242 ms)

    // Grit noise path (4 measured regimes)
    float noiseMix = 0.0f;
    if (ccGrit >= 115.0f) noiseMix = 0.75f;
    else if (ccGrit > 110.0f) noiseMix = 0.35f + (ccGrit - 110.0f) / 5.0f * 0.40f;
    else if (ccGrit > 25.0f) noiseMix = CLAMP(0.0f, 0.35f, 0.0074f * (ccGrit - 18.0f));
    float noiseTau = tauEff < 150.0f ? tauEff : 150.0f;
    if (ccGrit > 110.0f) noiseTau = 60.0f;

    // Sweep -> start pitch multiplier (linear in OCTAVES); Time -> pitch-env tau (exp)
    float startMult = 1.12f * powf(2.0f, ccSweep / 22.5f);
    float tauP = 0.002f * expf(0.042f * ccTime);
    float pitchCoeff = expf(-1.0f / (tauP * sr));
    // Hold -> plateau (exponential), clamp 4 s
    float holdSec = 0.001f * powf(2.0f, ccHold / 8.6f);
    if (holdSec > 4.0f) holdSec = 4.0f;
    int holdSamples = (int)(holdSec * sr);
    float noiseLpG = 1.0f - expf(-6.2832f * 4000.0f / sr);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float tv = trig[i];
      if (tv > 0.5f && I.prevTrig <= 0.5f)   // rising edge -> new hit
      {
        float fc = f0 * powf(2.0f, voct[i]);
        float foldN = fold * powf(fc / 246.5f, -0.163f);   // fold falls with pitch
        float lf = logf(fc / 246.5f);                      // pitch feature
        float gN = ccGrit / 127.0f;                        // grit feature
        // sine-dip only exists when osc2 is inactive (core-only regime)
        float sineDip = 0.0f;
        if (ccChar < 64.0f) sineDip = ccChar / 64.0f;
        else if (ccChar < 85.0f) sineDip = 1.0f - (ccChar - 64.0f) / 21.0f;

        for (int m = 0; m < NM; m++)
        {
          float f = fc * (kH[m] + kK[m] * r);
          I.mfreq[m] = f;
          const float *c = kAmpFit[m];
          float a = c[0] + c[1] * dL + c[2] * foldN + c[3] * r + c[4] * lf + c[5] * gN;
          // core h3 keeps its measured sine-dip when osc2 is inactive
          if (m == 8 && r <= 0.1f) a += 0.07f * (1.0f - sineDip) - 0.07f;
          if (a < 0.0f) a = 0.0f;
          if (a > 1.5f) a = 1.5f;
          if (f <= 15.0f || f >= nyq) a = 0.0f;           // drop out-of-range modes
          // measured-amp -> initial-amp correction (see winAvg)
          float tmS = tauEff * kTauR[m] * 0.001f;
          float ceilS = hfCeilMs(f) * 0.001f;
          if (tmS > ceilS) tmS = ceilS;
          if (tmS < 0.002f) tmS = 0.002f;
          a *= winAvg(tauEff * 0.001f) / winAvg(tmS);
          I.env[m] = a;
          // Varied (not aligned) start phases: all-aligned created an artificial
          // broadband click. Varying them matches the measured sub-mode amplitude
          // (0.23 vs HW 0.216) and punch (1.5 vs HW 1.3) while keeping the impact.
          I.phase[m] = 0.25f + 0.37f * (float)m;
          I.phase[m] -= floorf(I.phase[m]);
          float tm = tauEff * kTauR[m];
          float ceil = hfCeilMs(f);
          if (tm > ceil) tm = ceil;
          if (tm < 2.0f) tm = 2.0f;
          I.mdecay[m] = expf(-1.0f / (tm * 0.001f * sr));
        }
        I.startMult = startMult;
        I.pitchCoeff = pitchCoeff;
        I.pitchEnv = 1.0f;
        I.holdLeft = holdSamples;
        I.noiseEnv = 1.0f;
        I.noiseCoeff = expf(-1.0f / (noiseTau * 0.001f * sr));
        // regime-4 attack burst (measured punch 9.8 / atk_frac 0.41 at grit >= ~115)
        I.burst = (ccGrit >= 115.0f) ? 8.0f : 0.0f;
        I.burstCoeff = expf(-1.0f / (0.005f * sr));
      }
      I.prevTrig = tv;

      I.pitchEnv *= I.pitchCoeff;
      float pmul = 1.0f + (I.startMult - 1.0f) * I.pitchEnv;
      bool held = I.holdLeft > 0;

      float y = 0.0f;
      for (int m = 0; m < NM; m++)
      {
        float fm = I.mfreq[m] * pmul;
        I.phase[m] += fm / sr; I.phase[m] -= floorf(I.phase[m]);
        if (!held) I.env[m] *= I.mdecay[m];
        y += I.env[m] * sineLUT(I.phase[m]);
      }
      if (held) I.holdLeft--;

      // noise body (own env) + grit attack burst
      I.noiseEnv *= I.noiseCoeff;
      I.burst *= I.burstCoeff;
      I.noiseLp += noiseLpG * (noise(I.rng) - I.noiseLp);
      y = y * (1.0f - noiseMix * 0.5f) + I.noiseLp * (noiseMix * I.noiseEnv + I.burst * 0.12f);

      out[i] = y * level;
    }
  }

} // namespace stolmine
