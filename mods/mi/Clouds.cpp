// Mutable Instruments Clouds granular processor wrapper for ER-301
// Based on code by Émilie Gillet, MIT License

#include "Clouds.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

// Undef ER-301 SDK macros that collide with Clouds enum names
#undef INTERPOLATION_LINEAR

#include "clouds/dsp/granular_processor.h"

namespace clouds_unit
{

  struct Clouds::Internal
  {
    clouds::GranularProcessor processor;

    uint8_t large_buffer[118784]; // ~116KB
    uint8_t small_buffer[65536];  // ~64KB

    int cachedMode = -1;
    bool lastTrig = false;

    void Init()
    {
      // Zero entire struct — GranularProcessor has no constructor,
      // original firmware relies on BSS zero-init for globals
      memset(this, 0, sizeof(Internal));
      cachedMode = -1;
      processor.Init(large_buffer, sizeof(large_buffer),
                     small_buffer, sizeof(small_buffer));
      processor.set_playback_mode(clouds::PLAYBACK_MODE_GRANULAR);
      processor.set_quality(0);
      cachedMode = 0;
    }
  };

  Clouds::Clouds()
  {
    addInput(mInL);
    addInput(mInR);
    addInput(mTrigger);
    addInput(mFreeze);
    addOutput(mOutL);
    addOutput(mOutR);
    addParameter(mPosition);
    addParameter(mSize);
    addParameter(mPitch);
    addParameter(mDensity);
    addParameter(mTexture);
    addParameter(mDryWet);
    addParameter(mFeedback);
    addParameter(mSpread);
    addParameter(mMode);
    addOption(mQuality);
    addOption(mPreamp);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Clouds::~Clouds()
  {
    delete mpInternal;
  }

  void Clouds::process()
  {
    Internal &s = *mpInternal;

    float *inL = mInL.buffer();
    float *inR = mInR.buffer();
    float *trig = mTrigger.buffer();
    float *freeze = mFreeze.buffer();
    float *outL = mOutL.buffer();
    float *outR = mOutR.buffer();

    // Set playback mode (only on change to avoid heavy reinit in Prepare)
    // 0=granular, 1=delay (spectral moved to Kryos unit)
    static const clouds::PlaybackMode modeMap[] = {
        clouds::PLAYBACK_MODE_GRANULAR,      // 0
        clouds::PLAYBACK_MODE_LOOPING_DELAY, // 1
        clouds::PLAYBACK_MODE_SPECTRAL       // 2
    };
    int mode = CLAMP(0, 2, (int)(mMode.value() + 0.5f));
    if (mode != s.cachedMode)
    {
      s.processor.set_playback_mode(modeMap[mode]);
      s.cachedMode = mode;
    }

    // Set quality (force mono for spectral mode — CPU budget)
    if (mode == 2) {
      s.processor.set_quality(1);
    } else {
      s.processor.set_quality(mQuality.value());
    }

    // Map parameters
    clouds::Parameters *p = s.processor.mutable_parameters();
    p->position = CLAMP(0.0f, 1.0f, mPosition.value());
    p->size = CLAMP(0.0f, 1.0f, mSize.value());
    p->pitch = CLAMP(-48.0f, 48.0f, mPitch.value());
    p->density = CLAMP(0.0f, 1.0f, mDensity.value() * 0.5f + 0.5f);
    p->texture = CLAMP(0.0f, 1.0f, mTexture.value());
    p->dry_wet = CLAMP(0.0f, 1.0f, mDryWet.value());
    p->feedback = CLAMP(0.0f, 1.0f, mFeedback.value());
    p->stereo_spread = CLAMP(0.0f, 1.0f, mSpread.value());
    p->reverb = 0.0f; // No internal reverb — use Stratos

    // Initial freeze state (trigger/gate handled per block below)
    p->freeze = freeze[0] > 0.1f;

    // Preamp gain
    static const float preampGain[] = {1.0f, 2.0f, 3.0f};
    float gain = preampGain[CLAMP(0, 2, mPreamp.value())];

    // Process in chunks of kMaxBlockSize (32)
    const int blockSize = clouds::kMaxBlockSize;
    int pos = 0;

    while (pos < FRAMELENGTH)
    {
      int remaining = FRAMELENGTH - pos;
      int chunk = (remaining >= blockSize) ? blockSize : remaining;

      // Convert float → ShortFrame for input
      clouds::ShortFrame input[blockSize];
      clouds::ShortFrame output[blockSize];
      for (int i = 0; i < chunk; i++)
      {
        input[i].l = (short)CLAMP(-32767, 32767,
                                   (int)(inL[pos + i] * gain * 32767.0f));
        input[i].r = (short)CLAMP(-32767, 32767,
                                   (int)(inR[pos + i] * gain * 32767.0f));
      }

      // Update trigger/freeze per block
      bool trigHigh = trig[pos] > 0.1f;
      p->trigger = trigHigh && !s.lastTrig;
      p->gate = trigHigh;
      s.lastTrig = trigHigh;
      p->freeze = freeze[pos] > 0.1f;

      s.processor.Prepare();
      s.processor.Process(input, output, chunk);

      // Convert ShortFrame → float for output
      for (int i = 0; i < chunk; i++)
      {
        outL[pos + i] = output[i].l / 32768.0f;
        outR[pos + i] = output[i].r / 32768.0f;
      }

      pos += chunk;
    }
  }

} // namespace clouds_unit
