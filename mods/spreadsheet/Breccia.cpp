#include "Breccia.h"
#include <od/config.h>
#include <od/extras/Random.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  // Focus first, then symmetric pairs outward.
  const int Breccia::kLayerOffset[Breccia::kMaxLayers] = { 0, -1, 1, -2, 2, -3, 3 };

  // Octaves and fifths outward from the focus: unison, 8ve up, 8ve down, 5th
  // up, 5th down, 2 8ve up, 2 8ve down. 2^(semitones/12), compile-time so no
  // powf is involved.
  //                                                    0    +12   -12    +7
  const float Breccia::kLayerRate[Breccia::kMaxLayers] = { 1.0f, 2.0f, 0.5f, 1.498307f,
  //                                                    -5    +24   -24
                                                          0.749154f, 4.0f, 0.25f };


  Breccia::Breccia()
  {
    addInput(mShuffle);
    addInput(mSpeed);
    addOutput(mOutput);
    addParameter(mSize);
    addParameter(mGlitch);
    addParameter(mLayer);
    addParameter(mOffset);
    addParameter(mWorld);
    addParameter(mLevel);
    addOption(mSizeMode);
    // od::Option is NOT auto-serialized.
    mSizeMode.enableSerialization();

    for (int i = 0; i <= kSinLut; i++)
      mSinLut[i] = (float)sin(2.0 * M_PI * (double)i / (double)kSinLut);

    identity(kMaxSlices);
    for (int L = 0; L < kMaxLayers; L++)
    {
      identityFx(mFx[L]); identityFx(mFxPrev[L]);
      mSvfIc1[L] = 0.0f; mSvfIc2[L] = 0.0f;
      mEnvA[L] = 0.0f; mEnvD[L] = 1.0f; mRingPhase[L] = 0.0f;
    }
    mSlices = 8;
    mLastSampleRate = globalConfig.sampleRate;
    rebuildFade();
  }

  Breccia::~Breccia()
  {
  }

  void Breccia::setSample(od::Sample *sample)
  {
    Base::setSample(sample);
    mPlayPos = 0.0;
    mRateRatio = 1.0;
    if (sample && sample->mSampleRate > 0.0f)
      mRateRatio = (double)sample->mSampleRate / (double)globalConfig.sampleRate;
  }

  void Breccia::identity(int n)
  {
    for (int i = 0; i < n; i++) mPerm[i] = i;
  }

  // Fisher-Yates. A real permutation: every slice appears exactly once, so the
  // buffer's content is preserved and only the order changes.
  void Breccia::reshuffle(int n)
  {
    identity(n);
    for (int i = n - 1; i > 0; i--)
    {
      int j = (int)(od::Random::generateFloat(0.0f, 1.0f) * (float)(i + 1));
      if (j > i) j = i;
      if (j < 0) j = 0;
      const int t = mPerm[i];
      mPerm[i] = mPerm[j];
      mPerm[j] = t;
    }
  }

  // Effect order: stutter, crush, scrub, reverse, pitch, step, chop, filter,
  // comb, env, scatter, ring, freeze, sweep. Each row sums to 0.60, so at
  // Glitch = 1 exactly 40% of slices come through clean in EVERY world.
  //
  // Ordered so total-variation distance from the start rises MONOTONICALLY.
  // An arbitrary order made the knob double back on itself and revisit ground
  // it had already covered.
  const float Breccia::kWorld[Breccia::kNumWorlds][Breccia::kNumFx] = {
    // RHYTHMIC - time and gating: stutter, reverse, chop, scatter, freeze
    { 0.12f,0.02f,0.02f,0.11f,0.02f,0.03f,0.09f,0.01f,0.01f,0.06f,0.07f,0.01f,0.02f,0.01f },
    // DEGRADED - lo-fi texture: crush, scrub, ring
    { 0.05f,0.14f,0.11f,0.03f,0.02f,0.02f,0.04f,0.03f,0.01f,0.02f,0.03f,0.06f,0.03f,0.01f },
    // DIFFUSE - smeared and spatial: comb, filter, sweep, env swells
    { 0.03f,0.03f,0.06f,0.03f,0.04f,0.04f,0.02f,0.07f,0.09f,0.07f,0.02f,0.02f,0.02f,0.06f },
    // TONAL - pitched and resonant: pitch, step, filter, comb, ring, freeze
    { 0.02f,0.01f,0.02f,0.03f,0.11f,0.10f,0.01f,0.07f,0.06f,0.02f,0.01f,0.05f,0.05f,0.04f }
  };

  enum { FX_NONE=0, FX_STUTTER, FX_CRUSH, FX_SCRUB, FX_REVERSE,
         FX_PITCH, FX_STEP, FX_CHOP, FX_FILTER, FX_COMB,
         FX_ENV, FX_SCATTER, FX_RING, FX_FREEZE, FX_SWEEP };

  void Breccia::identityFx(SliceFx &fx)
  {
    fx.gain = 1.0f; fx.dir = 1.0f;
    fx.segFrac = 1.0f; fx.segAbs = 0.0f;
    fx.scrub = 0.0f; fx.scrubRate = 0.0f;
    fx.crush = 0.0f; fx.crushInv = 0.0f; fx.crushMix = 0.0f;
    fx.rate = 1.0f;
    fx.stepCount = 1;
    for (int k = 0; k < 8; k++) fx.stepRate[k] = 1.0f;
    fx.chop = 0.0f; fx.chopDuty = 0.5f;
    fx.fg = 0.0f; fx.fk = 1.0f; fx.fa1 = 0.0f; fx.fa2 = 0.0f; fx.fa3 = 0.0f;
    fx.fmLp = 0.0f; fx.fmBp = 0.0f; fx.fmHp = 0.0f;
    fx.combDelay = 0.0f; fx.combMix = 0.0f; fx.combGain = 0.0f;
    fx.fd1 = 0.0f; fx.fd2 = 0.0f; fx.fd3 = 0.0f;
    fx.envAtk = 1.0f; fx.envDec = 1.0f;
    fx.ringInc = 0.0f; fx.ringMix = 0.0f;
    fx.scatCount = 1;
    for (int k = 0; k < 8; k++) fx.scatPerm[k] = (uint8_t)k;
    fx.mode = FX_NONE; fx.h1 = 0.5f; fx.h2 = 0.5f; fx.h3 = 0.5f;
    // decim MUST be here. identityFx is the only thing that ever returns it
    // to 1: deriveFx writes it exclusively in FX_CRUSH, so a missing reset
    // latched the last crush slice's decimation factor (up to 25x) into every
    // subsequent slice AND survived the Glitch=0 hard bypass - and from the
    // constructor it was read uninitialized (fine on the emu's zero pages,
    // garbage on a recycled hardware heap block). The invariant harness in
    // tools/breccia-invariant-test measures this.
    fx.decim = 1;
  }

  // WHICH effect, plus the raw hashes. Rolled fresh at every slice entry.
  void Breccia::rollFx(int sliceIdx, const float *w, SliceFx &fx) const
  {
    identityFx(fx);

    uint32_t t[kNumFx]; uint32_t acc = 0u;
    for (int i = 0; i < kNumFx; i++)
    {
      acc += (uint32_t)(w[i] * 65535.0f);
      if (acc > 65535u) acc = 65535u;
      t[i] = acc;
    }
    if (acc == 0u) return;                 // Glitch 0: nothing can fire

    uint32_t h = mGlitchLcg ^ ((uint32_t)sliceIdx * 2654435761u + 0xDD55DD55u);
    h = h * 1103515245u + 12345u;
    const uint32_t r = (h >> 16) & 0xFFFFu;
    uint32_t hs = mGlitchLcg ^ ((uint32_t)sliceIdx * 2654435761u + 0x55555555u);
    hs = hs * 1103515245u + 12345u;
    fx.h1 = (float)((hs >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
    uint32_t hb = mGlitchLcg ^ ((uint32_t)sliceIdx * 2654435761u + 0xC3C3C3C3u);
    hb = hb * 1103515245u + 12345u;
    fx.h2 = (float)((hb >> 16) & 0xFFFFu) * (1.0f / 65535.0f);
    uint32_t hc = mGlitchLcg ^ ((uint32_t)sliceIdx * 2654435761u + 0x2545F491u);
    hc = hc * 1103515245u + 12345u;
    fx.h3 = (float)((hc >> 16) & 0xFFFFu) * (1.0f / 65535.0f);

    static const uint8_t kModeOf[kNumFx] = {
      FX_STUTTER, FX_CRUSH, FX_SCRUB, FX_REVERSE, FX_PITCH,
      FX_STEP, FX_CHOP, FX_FILTER, FX_COMB,
      FX_ENV, FX_SCATTER, FX_RING, FX_FREEZE, FX_SWEEP
    };
    for (int i = 0; i < kNumFx; i++)
      if (r < t[i]) { fx.mode = kModeOf[i]; return; }
  }

  // Offset is an ANCHOR, not a bias. `offset` maps to a centre in 0..1 and
  // each randomized dimension diverges around it, REFLECTING at the edges so
  // the spread survives everywhere.
  //
  // The previous model added Offset to the hash and clamped, which meant the
  // knob progressively destroyed the per-slice variation it was supposed to be
  // shaping: at Offset +0.5 every slice with h > 0.5 collapsed onto the same
  // value, and at +1 the whole population sat on one point. Anchoring keeps
  // full variety at every position and only moves where the variety sits.
  static inline float anchored(float centre, float h, float spread)
  {
    float v = centre + (h - 0.5f) * spread;
    if (v < 0.0f) v = -v;               // reflect, do not clamp
    if (v > 1.0f) v = 2.0f - v;
    if (v < 0.0f) v = 0.0f;
    return v;
  }

  void Breccia::deriveFx(SliceFx &fx, float offset, float sr)
  {
    // Offset -1..+1 -> anchor 0..1. Spread is deliberately wide: the point is
    // that random still roams even when the anchor is parked at an extreme.
    const float a = (offset + 1.0f) * 0.5f;
    const float kSpread = 0.6f;
    const float p = anchored(a, fx.h1, kSpread);
    const float q = anchored(a, fx.h2, kSpread);

    switch (fx.mode)
    {
      case FX_STUTTER:
        // Floor of 3, not 2: two repeats measured barely above dry.
        fx.segFrac = 1.0f / (3.0f + (float)((int)(p * 6.0f)));
        break;

      case FX_REVERSE:
      {
        // Randomized: reverse now runs inside a sub-segment, so it ranges from
        // one whole-slice reversal to several short reversed chunks. Previously
        // it was a fixed whole-slice flip that responded to nothing.
        fx.dir = -1.0f;
        fx.segFrac = 1.0f / (1.0f + (float)((int)(p * 4.0f)));  // 1..4 chunks
        break;
      }

      case FX_PITCH:
      {
        // Randomized: was two fixed effects at exactly +/-1 octave. Now one
        // effect drawing a DISCRETE interval from a musical set spanning two
        // octaves either way, with Offset anchoring where in that set it lands.
        static const float kSet[8] = {
          0.25f,      // -2 oct
          0.5f,       // -1 oct
          0.6674f,    // -7 st
          0.7492f,    // -5 st
          1.3348f,    // +5 st
          1.4983f,    // +7 st
          2.0f,       // +1 oct
          4.0f        // +2 oct
        };
        int i = (int)(p * 7.999f); if (i < 0) i = 0; else if (i > 7) i = 7;
        fx.rate = kSet[i];
        break;
      }

      case FX_CRUSH:
      {
        // Both dimensions diverge independently around the same anchor, so
        // bits and decimation are no longer locked together.
        const int bits = 12 - (int)(p * 9.0f);
        const float step = 2.0f / (float)(1u << (bits < 2 ? 2 : bits));
        fx.crush = step; fx.crushInv = 1.0f / step; fx.crushMix = 1.0f;
        fx.decim = 1 + (int)(q * 24.0f);
        break;
      }

      case FX_SCRUB:
        // Rate anchors; DEPTH stays fully random, because depth is amount and
        // Offset is not an intensity control.
        fx.scrub = 0.004f + fx.h2 * 0.04f;
        // Floor of 1.5 cycles: below one cycle per slice the playhead barely
        // moves and the effect reads as clean.
        fx.scrubRate = 1.5f + p * 18.5f;
        break;

      case FX_STEP:
      {
        static const float kOct[5]  = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        static const float kFifth[5]= { 0.5f, 0.749154f, 1.0f, 1.498307f, 2.0f };
        static const float kChrom[5]= { 0.890899f, 0.943874f, 1.0f, 1.059463f, 1.122462f };
        const float *set = (p < 0.34f) ? kChrom : ((p < 0.67f) ? kFifth : kOct);
        const int n = 2 + (int)(fx.h2 * 6.0f);
        fx.stepCount = n;
        uint32_t hh = 0x9E3779B9u ^ (uint32_t)(fx.h3 * 65535.0f);
        for (int k = 0; k < n; k++)
        {
          hh = hh * 1103515245u + 12345u;
          fx.stepRate[k] = set[((hh >> 16) & 0xFFFFu) % 5u];
        }
        break;
      }

      case FX_CHOP:
        fx.chop = 2.0f + fx.h1 * 14.0f;          // rate stays fully random
        fx.chopDuty = 0.12f + (1.0f - q) * 0.76f;
        break;

      case FX_FILTER:
      {
        // Cutoff is RANDOM, anchored by Offset. Q is fully random. Type is
        // rolled fresh from its own hash and is no longer tied to Offset -
        // previously Offset dragged every filter slice from lowpass to
        // highpass, which coupled two unrelated things.
        // Ceiling pulled from 12 kHz to 5 kHz: a 12 kHz lowpass on this material
        // is indistinguishable from bypass, so the top of the travel was null.
        const float fc = 120.0f * powf(42.0f, p);
        const float srr = (sr > 0.0f) ? sr : 48000.0f;
        float w = 3.14159265f * fc / srr;
        if (w > 1.0f) w = 1.0f;
        const float w2 = w * w;
        const float g = w * (1.0f + w2 * (0.3333333f + w2 * (0.1333333f + w2 * 0.0539683f)));
        const float Q = 0.7f + fx.h2 * 9.3f;
        const float k = 1.0f / Q;
        fx.fg = g; fx.fk = k;
        fx.fa1 = 1.0f / (1.0f + g * (g + k));
        fx.fa2 = g * fx.fa1;
        fx.fa3 = g * fx.fa2;
        const int sel = (int)(fx.h3 * 2.999f);
        fx.fmLp = (sel == 0) ? 1.0f : 0.0f;
        fx.fmBp = (sel == 1) ? 1.0f : 0.0f;
        fx.fmHp = (sel == 2) ? 1.0f : 0.0f;
        break;
      }

      case FX_COMB:
      {
        // Fully randomized: delay anchors on Offset, tap gain is random, and
        // the tap can SUBTRACT as well as add - a notch comb instead of a peak
        // comb, which is a different colour entirely. It was one fixed 0.7 tap.
        // Was 20 samples at the low end, a 1.2 kHz comb that measured
        // 0.124 against dry. Now 1 ms to 25 ms, logarithmic, and the gain
        // floor is raised - a feedforward comb only ripples +/-6 dB, so it
        // needs the deep end of its gain range to register at all.
        fx.combDelay = sr * 0.001f * powf(25.0f, p);
        const float gmag = 0.6f + fx.h2 * 0.37f;
        fx.combGain = (fx.h3 < 0.5f) ? -gmag : gmag;
        fx.combMix = 1.0f;
        break;
      }
      case FX_ENV:
      {
        // ONE parameter spanning swell through flat to percussive. Below the
        // midpoint the slice fades in and does not decay; above it the attack
        // is instant and the tail falls away. Offset anchors where in that
        // span the population sits, so a world can lean percussive or swelling
        // while individual slices still diverge.
        const float srr = (sr > 0.0f) ? sr : 48000.0f;
        // The first version put FLAT at the midpoint, which measured 0.071
        // spectrogram distance at Offset 0 - the effect was dead at the
        // default knob position, and dead across the whole negative half.
        // Now attack and decay BOTH move together, so there is no null
        // anywhere in the travel: p sweeps swell -> hump -> percussive and
        // every point on it is a formed envelope. Geometric, because the
        // audible difference between 3 ms and 30 ms dwarfs 300 ms to 330 ms.
        const float atkS = srr * 0.003f * powf(200.0f, 1.0f - p);
        const float decS = srr * 0.012f * powf(250.0f, 1.0f - p);
        fx.envAtk = 1.0f / atkS;
        fx.envDec = expf(-1.0f / decS);
        break;
      }

      case FX_SCATTER:
      {
        // Sub-slice permutation. Fisher-Yates from the slice's own hash, so
        // the ordering is deterministic for the roll but different every time
        // it fires. Being a permutation it preserves the material exactly.
        int n = 2 + (int)(p * 6.99f); if (n > 8) n = 8;
        fx.scatCount = n;
        for (int k = 0; k < n; k++) fx.scatPerm[k] = (uint8_t)k;
        uint32_t hh = 0x85EBCA6Bu ^ (uint32_t)(fx.h2 * 65535.0f);
        for (int k = n - 1; k > 0; k--)
        {
          hh = hh * 1103515245u + 12345u;
          const int j = (int)(((hh >> 16) & 0xFFFFu) % (uint32_t)(k + 1));
          const uint8_t t = fx.scatPerm[k];
          fx.scatPerm[k] = fx.scatPerm[j]; fx.scatPerm[j] = t;
        }
        break;
      }

      case FX_RING:
      {
        // Modulator frequency anchored by Offset over ~7 octaves. Depth stays
        // fully random: depth is amount, and Offset is not an intensity knob.
        const float srr = (sr > 0.0f) ? sr : 48000.0f;
        // Floor raised from 18 Hz to 80 Hz: below about 50 Hz ring
        // modulation is just tremolo, not sideband generation, so the
        // bottom third of the travel was not doing the effect's job. Mix
        // floor raised too - at 0.45 more than half the output was dry.
        const float f = 80.0f * powf(44.0f, p);
        fx.ringInc = f / srr;
        fx.ringMix = 0.7f + fx.h2 * 0.3f;
        break;
      }

      case FX_FREEZE:
      {
        // ABSOLUTE window, 2 ms to ~90 ms, so it does not scale with the
        // slice. Short end is a pitched drone at 1/window (500 Hz at 2 ms),
        // long end is a recognisable loop.
        const float srr = (sr > 0.0f) ? sr : 48000.0f;
        fx.segFrac = 0.0f;
        fx.segAbs = srr * (0.002f + p * 0.088f);
        break;
      }

      case FX_SWEEP:
      {
        // Resonant cutoff TRAVELLING across the slice. Direction is rolled
        // from its own hash so up-sweeps and down-sweeps are equally likely.
        const float srr = (sr > 0.0f) ? sr : 48000.0f;
        const float span = 6.0f + fx.h2 * 26.0f;      // sweep width in octaves-ish
        // Base pulled down: at the old top the sweep started at 3.6 kHz and
        // ran upward, which is inaudible on most material.
        float lo = 90.0f * powf(12.0f, p);
        float hi = lo * span;
        if (hi > srr * 0.45f) hi = srr * 0.45f;
        const bool down = (fx.h3 < 0.5f);
        const float f0 = down ? hi : lo;
        const float f1 = down ? lo : hi;
        const float Q = 1.2f + fx.h1 * 7.0f;
        const float k = 1.0f / Q;
        float c0[3], c1[3];
        for (int e = 0; e < 2; e++)
        {
          float w = 3.14159265f * (e ? f1 : f0) / srr;
          if (w > 1.0f) w = 1.0f;
          const float w2 = w * w;
          const float g = w * (1.0f + w2 * (0.3333333f + w2 * (0.1333333f + w2 * 0.0539683f)));
          const float A1 = 1.0f / (1.0f + g * (g + k));
          const float A2 = g * A1;
          const float A3 = g * A2;
          if (e) { c1[0] = A1; c1[1] = A2; c1[2] = A3; }
          else   { c0[0] = A1; c0[1] = A2; c0[2] = A3; fx.fg = g; }
        }
        fx.fk = k;
        fx.fa1 = c0[0]; fx.fa2 = c0[1]; fx.fa3 = c0[2];
        fx.fd1 = c1[0] - c0[0];
        fx.fd2 = c1[1] - c0[1];
        fx.fd3 = c1[2] - c0[2];
        // Sweeps read best as lowpass; band and high lose the gesture.
        fx.fmLp = 1.0f; fx.fmBp = 0.0f; fx.fmHp = 0.0f;
        break;
      }

      default: break;
    }
  }

  // 3 ms is the measured knee for equal-power splices: level holds to -0.24 dB
  // there, but dips -1.05 dB at 1 ms because that is too short to average
  // against a low-frequency waveform period.
  void Breccia::rebuildFade()
  {
    const float sr = globalConfig.sampleRate > 0.0f ? globalConfig.sampleRate : 48000.0f;
    int n = (int)(0.003f * sr);
    if (n < 4) n = 4;
    if (n > kMaxXfade) n = kMaxXfade;
    mXfade = n;
    for (int i = 0; i < n; i++)
    {
      const double w = (double)i / (double)n;
      mFadeIn[i] = (float)sin(w * M_PI * 0.5);
      mFadeOut[i] = (float)cos(w * M_PI * 0.5);
    }
  }

  float Breccia::readSource(double srcPos) const
  {
    od::Sample *s = mpSample;
    if (s == 0 || s->mSampleCount == 0) return 0.0f;
    const int n = (int)s->mSampleCount;
    const int ch = (int)s->mChannelCount;

    if (srcPos < 0.0) srcPos = 0.0;
    int i0 = (int)srcPos;
    if (i0 >= n) i0 = n - 1;
    int i1 = i0 + 1;
    if (i1 >= n) i1 = n - 1;
    const float fr = (float)(srcPos - (double)i0);

    float a = s->get(i0, 0);
    float b = s->get(i1, 0);
    if (ch > 1)
    {
      a = 0.5f * (a + s->get(i0, 1));
      b = 0.5f * (b + s->get(i1, 1));
    }
    return a + (b - a) * fr;
  }

  void Breccia::process()
  {
    float *shuf = mShuffle.buffer();
    float *spd = mSpeed.buffer();
    float *out = mOutput.buffer();

    if (globalConfig.sampleRate != mLastSampleRate)
    {
      mLastSampleRate = globalConfig.sampleRate;
      rebuildFade();
      if (mpSample && mpSample->mSampleRate > 0.0f)
        mRateRatio = (double)mpSample->mSampleRate / (double)globalConfig.sampleRate;
    }

    const float level = CLAMP(0.0f, 1.0f, mLevel.value());
    const int total = mpSample ? (int)mpSample->mSampleCount : 0;
    if (total < 8)
    {
      for (int i = 0; i < FRAMELENGTH; i++) out[i] = 0.0f;
      return;
    }

    // Loop length in OUTPUT samples: the whole buffer at the correct pitch.
    const double loopOut = (double)total / mRateRatio;

    const float glitchAmt = CLAMP(0.0f, 1.0f, mGlitch.value());
    const float layerAmt = CLAMP(0.0f, 1.0f, mLayer.value());
    const float offsetAmt = CLAMP(-1.0f, 1.0f, mOffset.value());
    const float sr = globalConfig.sampleRate;

    // Blend the world weight vectors at BLOCK rate, once, and hand the result
    // to each roll. Continuous rather than a discrete picker so World is
    // CV-modulatable and can be walked slowly, which is how you "land" in a
    // world rather than choosing one. Each row already sums to the same total,
    // so any convex blend of two rows does too: density stays owned by Glitch.
    float wgt[kNumFx];
    {
      const float wp = CLAMP(0.0f, 1.0f, mWorld.value()) * (float)(kNumWorlds - 1);
      int w0 = (int)wp; if (w0 > kNumWorlds - 2) w0 = kNumWorlds - 2;
      if (w0 < 0) w0 = 0;
      const float f = wp - (float)w0;
      for (int i = 0; i < kNumFx; i++)
        wgt[i] = (kWorld[w0][i] + (kWorld[w0 + 1][i] - kWorld[w0][i]) * f) * glitchAmt;
    }
    // Offset re-shapes the CURRENT pattern live, without re-rolling it. Slices
    // can be seconds long, so waiting for the next boundary would make the knob
    // feel dead; re-deriving at block rate is ~2.7 ms granularity for the cost
    // of 7 derives per block, and only when the knob actually moves.
    if (glitchAmt <= 0.0f)
    {
      // Glitch 0 is a hard bypass, enforced at BLOCK rate. Relying on the next
      // slice roll to clear the coefficients means a long slice - or a stalled
      // playhead - keeps its crush/decimate latched on with the knob at zero.
      // mFxPrev too: the splice tail multiplies by mFxPrev[L].gain, and a
      // stale 0 from a muted slice would notch the first crossfade window.
      for (int L = 0; L < kMaxLayers; L++)
      {
        mEnvA[L] = 0.0f; mEnvD[L] = 1.0f; mRingPhase[L] = 0.0f;
        identityFx(mFx[L]);
        identityFx(mFxPrev[L]);
      }
      mDecimHeld = 0.0f;
      mLastOffset = -99.0f;
    }
    else if (offsetAmt != mLastOffset)
    {
      mLastOffset = offsetAmt;
      for (int L = 0; L < kMaxLayers; L++) deriveFx(mFx[L], offsetAmt, sr);
    }

    // One normalized control, exponential in both modes, and in both of them
    // higher Size = bigger slices.
    const float sz = CLAMP(0.0f, 1.0f, mSize.value());
    int req;
    if (mSizeMode.value() == 2)
    {
      // Length: absolute duration 2 ms .. 10 s, count follows from the buffer.
      const double want = 0.002 * pow(5000.0, (double)sz)
                          * (double)globalConfig.sampleRate;
      req = (int)(loopOut / want + 0.5);
    }
    else
    {
      // Count: kMaxSlices down to 2, so the slice grows with the knob.
      req = (int)(pow((double)kMaxSlices * 0.5, 1.0 - (double)sz) * 2.0 + 0.5);
    }
    req = CLAMP(2, kMaxSlices, req);
    if (req != mSlices) mPendingSlices = req;

    const int xf = mXfade;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Threshold 0.5, not 0.0 (feedback_comparator_gate_threshold).
      const bool high = shuf[i] > 0.5f;
      if (high && !mShuffleWasHigh) mShufflePending = true;
      mShuffleWasHigh = high;

      double sliceOut = loopOut / (double)mSlices;
      int idx = (int)(mPlayPos / sliceOut);
      if (idx >= mSlices) idx = mSlices - 1;
      if (idx < 0) idx = 0;

      // CROSSING A SLICE BOUNDARY. Detected by the slice index CHANGING, not by
      // catching mPlayPos inside a one-sample window: with Speed above 1 the
      // playhead steps over that window and the test silently misses. At speed
      // 2 half of all boundaries were missed and at speed 4 three quarters,
      // which is why Shuffle and Size appeared to need wiggling before they
      // took effect.
      if (idx != mFxIdx)
      {
        // Size first - it redefines the grid, so idx has to be recomputed.
        if (mPendingSlices && mPendingSlices != mSlices)
        {
          const double oldSlice = sliceOut;
          mSlices = mPendingSlices;
          mPendingSlices = 0;
          // The old permutation has the wrong length. Rebuild at the new size
          // KEEPING the shuffled state, or the size knob silently un-shuffles.
          if (mIsShuffled) reshuffle(mSlices);
          else identity(mSlices);
          // Snap to the nearest boundary of the NEW grid: keeps the position in
          // the loop and lands clean, instead of jumping to the top.
          sliceOut = loopOut / (double)mSlices;
          double k = (mPlayPos + 0.5 * oldSlice) / sliceOut;
          k = (double)(long)k;
          mPlayPos = k * sliceOut;
          if (mPlayPos >= loopOut || mPlayPos < 0.0) mPlayPos = 0.0;
          idx = (int)(mPlayPos / sliceOut);
          if (idx >= mSlices) idx = mSlices - 1;
          if (idx < 0) idx = 0;
        }

        if (mShufflePending)
        {
          reshuffle(mSlices);
          mIsShuffled = true;
          mShufflePending = false;
        }

        // Roll FRESH on every slice playback rather than precomputing a
        // pattern: holding the seed meant a given position always drew the same
        // effect, so every pass repeated an identical glitch rhythm.
        mGlitchLcg = mGlitchLcg * 1103515245u + 12345u;
        for (int L = 0; L < kMaxLayers; L++)
        {
          mFxPrev[L] = mFx[L];
          // The layer offset stays in the hash so the 7 voices decorrelate.
          rollFx(idx + kLayerOffset[L], wgt, mFx[L]);
          deriveFx(mFx[L], offsetAmt, sr);
        }
        mFxIdx = idx;
        mScrubPhase = 0.0;
        mChopPhase = 0.0;
        mDecimPhase = 0;
        // Per-slice DSP state, or a filtered/decimated slice bleeds its
        // character into the next one and the effect appears to stick.
        mDecimHeld = 0.0f;
        for (int L = 0; L < kMaxLayers; L++)
        {
          mSvfIc1[L] = 0.0f; mSvfIc2[L] = 0.0f;
          // Envelope and modulator restart with the slice, or a percussive
          // slice's decayed tail and the modulator's phase bleed into the
          // next one and the effect appears to stick.
          mEnvA[L] = 0.0f; mEnvD[L] = 1.0f; mRingPhase[L] = 0.0f;
        }
      }

      const double inSlice = mPlayPos - (double)idx * sliceOut;
      const double sliceIn = sliceOut * mRateRatio;   // slice length in SOURCE samples
      const double base = (double)mPerm[idx] * sliceIn;

      // ---- layered playback ----
      // Ring k fades in over its own third of the Layer travel, so the control
      // reads as a partial count: focus alone, then +/-1, then +/-2, then +/-3.
      const float ringPos = layerAmt * 3.0f;
      float acc = 0.0f, accPrev = 0.0f, gSum = 0.0f;
      const int nLayers = (ringPos > 0.0f) ? kMaxLayers : 1;

      for (int L = 0; L < nLayers; L++)
      {
        const int ring = (kLayerOffset[L] < 0) ? -kLayerOffset[L] : kLayerOffset[L];
        float g = 1.0f;
        if (ring > 0)
        {
          g = ringPos - (float)(ring - 1);
          if (g <= 0.0f) continue;      // ring not reached yet
          if (g > 1.0f) g = 1.0f;
        }
        gSum += g * g;

        const SliceFx &fx = mFx[L];
        int sIdx = idx + kLayerOffset[L];
        while (sIdx < 0) sIdx += mSlices;
        while (sIdx >= mSlices) sIdx -= mSlices;
        const double lbase = (double)mPerm[sIdx] * sliceIn;

        // ---- per-slice glitch as coefficients, no mode branch ----
        // Exactly one term is live. Identity gives sliceIn * 1.0 + 0.0.
        const double segLen = sliceIn * (double)fx.segFrac + (double)fx.segAbs;
        // Layer harmonic stack x per-slice octave glitch. Both are just read
        // rates, so they compose as a product.
        // STEP: index a held rate. stepCount == 1 selects stepRate[0] == 1,
        // so every other effect passes through at fx.rate unchanged.
        const double frac = (sliceOut > 0.0) ? (inSlice / sliceOut) : 0.0;
        int st = (int)(frac * (double)fx.stepCount);
        if (st < 0) st = 0; else if (st > 7) st = 7;
        const double rate = (double)kLayerRate[L] * (double)fx.rate
                            * (double)fx.stepRate[st];
        double u = inSlice * mRateRatio * rate;
        u -= segLen * (double)(long)(u / segLen);            // STUTTER wrap
        const double flip = (1.0 - (double)fx.dir) * 0.5;    // REVERSE, branchless
        u = u + flip * ((segLen - 1.0) - 2.0 * u);
        // SCATTER: sub-slice permutation. Read segment perm[k] while playing
        // segment k, so the slice's own material is reordered inside itself.
        if (fx.scatCount > 1)
        {
          const double sl = sliceIn / (double)fx.scatCount;
          int k = (int)(u / sl);
          if (k < 0) k = 0; else if (k >= fx.scatCount) k = fx.scatCount - 1;
          u += (double)((int)fx.scatPerm[k] - k) * sl;
        }
        const double d = mScrubPhase - 0.5;                  // SCRUB triangle
        const double tri = 4.0 * (d < 0.0 ? -d : d) - 1.0;
        u += (double)fx.scrub * sliceIn * tri;
        if (u < 0.0) u = 0.0;
        else if (u > sliceIn) u = sliceIn;

        float v = readSource(lbase + u);
        // RINGMOD: table sine, never runtime sinf. Guarded like COMB and
        // FILTER so an inactive slice pays nothing.
        if (fx.ringMix > 0.0f)
        {
          float ph = mRingPhase[L] + fx.ringInc;
          ph -= (float)(int)ph;
          mRingPhase[L] = ph;
          const float fp = ph * (float)kSinLut;
          int si = (int)fp;
          if (si < 0) si = 0; else if (si >= kSinLut) si = kSinLut - 1;
          const float sf = fp - (float)si;
          const float sv = mSinLut[si] + (mSinLut[si + 1] - mSinLut[si]) * sf;
          v += fx.ringMix * (v * sv - v);
        }
        // CRUSH: round via an explicit int conversion, NOT the 12582912.0f
        // magic-number trick. Under -ffast-math GCC reassociates (x + K) - K
        // to x and deletes the round entirely - measured: the constant was
        // absent from the shipped .o on BOTH arches, so the crusher never
        // quantized at all. The int cast is a semantic conversion the
        // optimizer cannot elide; copysignf inlines to bit ops (no libm).
        const float xr = v * fx.crushInv;
        const float q = (float)(int)(xr + copysignf(0.5f, xr)) * fx.crush;
        v = v + fx.crushMix * (q - v);
        // COMB: feedforward tap. Reads the SOURCE behind the current position,
        // so it can cross the slice boundary into preceding material - the same
        // thing Larets does against its rolling record buffer, and what makes
        // it sound like a comb rather than a resonator.
        if (fx.combMix > 0.0f)
          v += fx.combGain * readSource(lbase + u - (double)fx.combDelay);

        // FILTER: TPT state-variable, 2-pole, resonant. Guarded because it
        // carries state; fg == 0 means the slice has no filter.
        if (fx.fg > 0.0f)
        {
          // SWEEP lerps the coefficients across the slice. Static FILTER has
          // zero deltas, so a1 = fa1 + 0 * frac is exact and this costs it
          // three multiply-adds rather than a per-sample divide.
          const float ft = (float)frac;
          const float a1 = fx.fa1 + fx.fd1 * ft;
          const float a2 = fx.fa2 + fx.fd2 * ft;
          const float a3 = fx.fa3 + fx.fd3 * ft;
          const float v3 = v - mSvfIc2[L];
          const float v1 = a1 * mSvfIc1[L] + a2 * v3;
          const float v2 = mSvfIc2[L] + a2 * mSvfIc1[L] + a3 * v3;
          mSvfIc1[L] = 2.0f * v1 - mSvfIc1[L];
          mSvfIc2[L] = 2.0f * v2 - mSvfIc2[L];
          v = fx.fmLp * v2 + fx.fmBp * v1 + fx.fmHp * (v - fx.fk * v1 - v2);
        }

        // CHOP: gate the slice. chop == 0 leaves chopGain at 1.
        float chopGain = 1.0f;
        if (fx.chop > 0.0f)
          chopGain = ((float)(mChopPhase - (double)(long)mChopPhase) < fx.chopDuty) ? 1.0f : 0.0f;
        // ENVELOPE. Identity is atk 1 / dec 1, which lands on exactly 1.0f
        // from the first sample, so the Glitch=0 path multiplies by one.
        float ea = mEnvA[L] + fx.envAtk;
        if (ea > 1.0f) ea = 1.0f;
        mEnvA[L] = ea;
        mEnvD[L] *= fx.envDec;
        acc += v * fx.gain * chopGain * (ea * mEnvD[L]) * g;

        // the same layer one slice earlier, for the splice
        if (inSlice < (double)xf)
        {
          int pIdx = sIdx - 1;
          while (pIdx < 0) pIdx += mSlices;
          const double pb = (double)mPerm[pIdx] * sliceIn;
          accPrev += readSource(pb + (sliceIn + inSlice * mRateRatio))
                     * mFxPrev[L].gain * g;
        }
      }

      mScrubPhase += (double)mFx[0].scrubRate / (sliceOut > 1.0 ? sliceOut : 1.0);
      if (mScrubPhase >= 1.0) mScrubPhase -= 1.0;
      mChopPhase += (double)mFx[0].chop / (sliceOut > 1.0 ? sliceOut : 1.0);
      if (mChopPhase >= 1.0) mChopPhase -= 1.0;

      // Equal-power normalization: the layers are different slices, so treat
      // them as decorrelated. Without this, 7 voices would be ~7x hotter.
      float y = acc;
      float yPrev = accPrev;
      if (gSum > 1.0f)
      {
        const float inv = 1.0f / sqrtf(gSum);
        y *= inv; yPrev *= inv;
      }

      // DECIMATE is applied once to the summed voice, not per layer.
      mDecimPhase++;
      if (mDecimPhase >= mFx[0].decim) { mDecimPhase = 0; mDecimHeld = y; }
      y = mDecimHeld;

      if (inSlice < (double)xf)
      {
        const int k = (int)inSlice;
        y = yPrev * mFadeOut[k] + y * mFadeIn[k];
      }

      out[i] = y * level;
      mCurrentIndex = (int)(base + inSlice * mRateRatio);

      // Variable-rate playhead. Speed comes in per sample from the Lua graph
      // (V/Oct through VoltPerOctave, multiplied by the Speed control, clipped)
      // exactly as the built-in players do. Negative speed plays backwards, so
      // the loop has to wrap at both ends.
      double sp = (double)spd[i];
      if (sp > 64.0) sp = 64.0; else if (sp < -64.0) sp = -64.0;
      mPlayPos += sp;
      if (mPlayPos >= loopOut) mPlayPos -= loopOut * (double)(long)(mPlayPos / loopOut);
      if (mPlayPos < 0.0) mPlayPos += loopOut * (1.0 + (double)(long)(-mPlayPos / loopOut));
      if (mPlayPos < 0.0 || mPlayPos >= loopOut) mPlayPos = 0.0;
    }
  }

} // namespace stolmine
