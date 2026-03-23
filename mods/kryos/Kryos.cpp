// Kryos — spectral freeze unit for ER-301
// Spectral analysis inspired by Mutable Instruments Clouds, code by Émilie Gillet, MIT License

#include "Kryos.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

namespace kryos
{

  static const float PI = 3.14159265358979323846f;
  static const float TWO_PI = 2.0f * PI;

  // 12 bands, log-spaced from ~80Hz to ~10kHz (~2/3 octave spacing)
  static const float BASE_FREQS[NUM_BANDS] = {
      80.0f, 127.0f, 201.6f, 320.0f, 508.0f, 806.3f,
      1280.0f, 2032.0f, 3225.0f, 5120.0f, 8127.5f, 10000.0f};

  struct Band
  {
    // Biquad BPF state
    float x1, x2, y1, y2;
    float b0, b1, b2, a1, a2;

    // Envelope follower
    float env;
    float heldEnv; // captured on freeze

    // Sine oscillator for resynthesis
    float phase;

    // Per-band LFO for texture shimmer
    float lfoPhase;
  };

  struct Kryos::Internal
  {
    Band bands[NUM_BANDS];
    bool wasFrozen;

    void Init()
    {
      memset(this, 0, sizeof(Internal));
      // Stagger LFO phases across bands for varied shimmer
      // Pre-compute safe filter coefficients
      for (int i = 0; i < NUM_BANDS; i++)
      {
        bands[i].lfoPhase = (float)i / NUM_BANDS;
        SetBandpass(bands[i], BASE_FREQS[i], 2.0f, 48000.0f);
      }
    }

    // Sin/cos for coefficient computation (called 12x per frame, not per sample)
    static float approxSin(float x)
    {
      // Normalize radians to [0,1) phase
      x *= (1.0f / TWO_PI);
      x -= (float)(int)x;
      if (x < 0.0f) x += 1.0f;
      // Center around nearest integer (same as FastSin)
      x = x - (float)(int)(x + 0.5f);
      // Parabolic approximation with correction
      float y = x * (8.0f - 16.0f * (x < 0 ? -x : x));
      y += 0.225f * y * ((y < 0 ? -y : y) - 1.0f);
      return y;
    }

    static float approxCos(float x)
    {
      return approxSin(x + PI * 0.5f);
    }

    // Compute biquad bandpass coefficients
    void SetBandpass(Band &b, float freq, float q, float sr)
    {
      float w0 = TWO_PI * freq / sr;
      float s = approxSin(w0);
      float c = approxCos(w0);
      float alpha = s / (2.0f * q);

      float a0_inv = 1.0f / (1.0f + alpha);
      b.b0 = alpha * a0_inv;
      b.b1 = 0.0f;
      b.b2 = -alpha * a0_inv;
      b.a1 = -2.0f * c * a0_inv;
      b.a2 = (1.0f - alpha) * a0_inv;
    }

    // Process one sample through biquad with NaN/inf protection
    float ProcessBiquad(Band &b, float in)
    {
      float out = b.b0 * in + b.b1 * b.x1 + b.b2 * b.x2
                  - b.a1 * b.y1 - b.a2 * b.y2;
      // Prevent NaN/inf from propagating (kills audio thread on ER-301)
      if (out > 10.0f) out = 10.0f;
      if (out < -10.0f) out = -10.0f;
      if (out != out) out = 0.0f; // NaN check
      b.x2 = b.x1;
      b.x1 = in;
      b.y2 = b.y1;
      b.y1 = out;
      return out;
    }

    // Fast sine approximation (Bhaskara-style, continuous phase)
    float FastSin(float phase)
    {
      // Normalize to [-0.5, 0.5]
      float x = phase - (float)(int)(phase + 0.5f);
      // Parabolic approximation with correction
      float y = x * (8.0f - 16.0f * (x < 0 ? -x : x));
      y += 0.225f * y * ((y < 0 ? -y : y) - 1.0f);
      return y;
    }
  };

  Kryos::Kryos()
  {
    addInput(mIn);
    addInput(mFreeze);
    addOutput(mOut);
    addParameter(mPosition);
    addParameter(mPitch);
    addParameter(mSize);
    addParameter(mTexture);
    addParameter(mDecay);
    addParameter(mMix);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Kryos::~Kryos()
  {
    delete mpInternal;
  }

  void Kryos::process()
  {
    Internal &s = *mpInternal;

    float *in = mIn.buffer();
    float *freezeIn = mFreeze.buffer();
    float *out = mOut.buffer();

    // Position: 0-1, sweeps a spectral focus window across bands
    float position = CLAMP(0.0f, 1.0f, mPosition.value());
    // Pitch: semitones, transpose resynthesized oscillators
    float pitch = CLAMP(-24.0f, 24.0f, mPitch.value());
    float size = CLAMP(0.5f, 8.0f, 0.5f + mSize.value() * 7.5f); // Q: 0.5 to 8
    float texture = CLAMP(0.0f, 1.0f, mTexture.value());
    float decayParam = CLAMP(0.0f, 1.0f, mDecay.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());

    // Pitch ratio from semitones
    // 2^(semitones/12) approximation
    float pitchRatio = 1.0f;
    {
      float oct = pitch / 12.0f;
      // Fast 2^x: exp2 approximation via repeated squaring of known values
      // Use piecewise linear on small range
      pitchRatio = 1.0f + oct * 0.6931f; // first-order Taylor of 2^x
      if (pitchRatio < 0.25f) pitchRatio = 0.25f;
      if (pitchRatio > 4.0f) pitchRatio = 4.0f;
    }

    // Spectral window: position selects center band, nearby bands are louder
    // Window width is ~6 bands wide (Gaussian-ish)
    float windowCenter = position * (NUM_BANDS - 1);
    float windowWidth = 4.0f; // bands on each side of center
    float bandGain[NUM_BANDS];
    for (int b = 0; b < NUM_BANDS; b++)
    {
      float dist = (float)b - windowCenter;
      // Gaussian window: exp(-dist^2 / (2*width^2))
      float g = dist * dist / (2.0f * windowWidth * windowWidth);
      bandGain[b] = (g < 6.0f) ? (1.0f - g / 6.0f) : 0.0f; // linear approx of gaussian
      if (bandGain[b] < 0.0f) bandGain[b] = 0.0f;
      // Taper upper bands to reduce harshness
      if (b >= NUM_BANDS - 4)
      {
        float taper = (float)(NUM_BANDS - b) / 4.0f;
        bandGain[b] *= taper;
      }
    }

    float sr = (float)globalConfig.sampleRate;
    if (sr < 1.0f) sr = 48000.0f;
    float srInv = 1.0f / sr;

    // Update filter coefficients (once per frame) — analysis filters stay fixed
    for (int b = 0; b < NUM_BANDS; b++)
    {
      float freq = BASE_FREQS[b];
      s.SetBandpass(s.bands[b], freq, size, sr);
    }

    // Envelope timing
    float envAttack = 0.01f;   // ~10ms attack
    float envRelease = 0.005f; // ~5ms release for tracking

    // LFO for texture
    float lfoRate = 0.2f + texture * 3.0f; // 0.2 to 3.2 Hz
    float lfoInc = lfoRate * srInv;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool frozen = freezeIn[i] > 0.1f;

      s.wasFrozen = frozen;

      float wet = 0.0f;

      for (int b = 0; b < NUM_BANDS; b++)
      {
        Band &band = s.bands[b];

        // Always run the analysis filter (keeps envelope tracking live)
        float filtered = s.ProcessBiquad(band, in[i]);

        // Envelope follower
        float absVal = filtered < 0 ? -filtered : filtered;
        float coeff = absVal > band.env ? envAttack : envRelease;
        band.env += coeff * (absVal - band.env);

        if (frozen)
        {
          // Peak-hold: continuously capture if input envelope is louder
          if (band.env > band.heldEnv)
          {
            band.heldEnv = band.env;
          }

          // Resynthesis: sine oscillator at band frequency × pitch × held amplitude
          float freq = BASE_FREQS[b] * pitchRatio;
          if (freq > sr * 0.45f) freq = sr * 0.45f;

          // Texture: slow LFO modulates frequency slightly for shimmer
          band.lfoPhase += lfoInc;
          if (band.lfoPhase >= 1.0f) band.lfoPhase -= 1.0f;
          float lfo = s.FastSin(band.lfoPhase);
          float freqMod = 1.0f + lfo * texture * 0.02f; // ±2% detune

          // Advance oscillator phase
          band.phase += freq * freqMod * srInv;
          if (band.phase >= 1.0f) band.phase -= 1.0f;

          // Sine output scaled by held envelope and spectral window
          float synth = s.FastSin(band.phase) * band.heldEnv * bandGain[b];

          // Slowly decay held envelope — decay controls how long it sustains
          band.heldEnv *= (1.0f - (1.0f - decayParam) * 0.00002f);

          wet += synth;
        }
        else
        {
          // Not frozen: reset held envelope so next freeze captures fresh
          band.heldEnv = 0.0f;
          // Pass through filtered signal with spectral window
          wet += filtered * bandGain[b];
        }
      }

      // Normalize — bandpass splits energy across 24 bands, need significant boost
      wet *= 2.0f;

      // Dry/wet mix
      float result = in[i] * (1.0f - mix) + wet * mix;
      // Final safety clamp
      if (result > 10.0f) result = 10.0f;
      if (result < -10.0f) result = -10.0f;
      if (result != result) result = 0.0f;
      out[i] = result;
    }
  }

} // namespace kryos
