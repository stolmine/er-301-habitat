// Mutable Instruments Plaits voice wrapper for ER-301
// Based on code by Émilie Gillet, MIT License

#include "PlaitsVoice.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

#include "plaits/dsp/voice.h"
#include "plaits/resources.h"

namespace mi
{

  // Internal state hidden from the header
  struct PlaitsVoice::Internal
  {
    plaits::Voice voice;
    plaits::Patch patch;
    plaits::Modulations modulations;
    plaits::Voice::Frame frames[plaits::kMaxBlockSize];
    stmlib::BufferAllocator allocator;

    // Arena for engine allocations (~32KB should be generous)
    static const size_t kArenaSize = 32768;
    uint8_t arena[kArenaSize];

    void Init()
    {
      memset(&patch, 0, sizeof(patch));
      memset(&modulations, 0, sizeof(modulations));
      allocator.Init(arena, kArenaSize);
      voice.Init(&allocator);

      // Defaults
      patch.engine = 0;
      patch.note = 48.0f;
      patch.harmonics = 0.5f;
      patch.timbre = 0.5f;
      patch.morph = 0.5f;
      patch.decay = 0.5f;
      patch.lpg_colour = 0.5f;
    }
  };

  PlaitsVoice::PlaitsVoice()
  {
    addInput(mVOct);
    addInput(mTrigger);
    addInput(mLevel);
    addInput(mFM);
    addInput(mTimbreMod);
    addInput(mMorphMod);
    addInput(mHarmonicsMod);
    addOutput(mOut);
    addOutput(mAux);
    addParameter(mEngine);
    addParameter(mHarmonics);
    addParameter(mTimbre);
    addParameter(mMorph);
    addParameter(mFreq);
    addParameter(mFMAmount);
    addParameter(mTimbreAmount);
    addParameter(mMorphAmount);
    addParameter(mDecay);
    addParameter(mLPGColour);
    addOption(mOutputMode);
    addOption(mTrigMode);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  PlaitsVoice::~PlaitsVoice()
  {
    delete mpInternal;
  }

  void PlaitsVoice::process()
  {
    Internal &s = *mpInternal;

    float *outBuf = mOut.buffer();
    float *auxBuf = mAux.buffer();
    float *voct = mVOct.buffer();
    float *trig = mTrigger.buffer();
    float *level = mLevel.buffer();
    float *fm = mFM.buffer();
    float *timbreMod = mTimbreMod.buffer();
    float *morphMod = mMorphMod.buffer();
    float *harmonicsMod = mHarmonicsMod.buffer();

    // Remap engine selector: original 16 first, then v1.2 additions
    static const int engineOrder[24] = {
        8, 9, 10, 11, 12, 13, 14, 15,  // original pitched
        16, 17, 18, 19, 20, 21, 22, 23, // original noise/perc
        0, 1, 2, 3, 4, 5, 6, 7          // v1.2 additions
    };
    int sel = (int)CLAMP(0, 23, (int)mEngine.value());
    s.patch.engine = engineOrder[sel];
    s.patch.harmonics = CLAMP(0.0f, 1.0f, mHarmonics.value());
    s.patch.timbre = CLAMP(0.0f, 1.0f, mTimbre.value());
    s.patch.morph = CLAMP(0.0f, 1.0f, mMorph.value());
    s.patch.frequency_modulation_amount = CLAMP(-1.0f, 1.0f, mFMAmount.value());
    s.patch.timbre_modulation_amount = CLAMP(-1.0f, 1.0f, mTimbreAmount.value());
    s.patch.morph_modulation_amount = CLAMP(-1.0f, 1.0f, mMorphAmount.value());
    s.patch.decay = CLAMP(0.0f, 1.0f, mDecay.value());
    s.patch.lpg_colour = CLAMP(0.0f, 1.0f, mLPGColour.value());

    // Set patched flags based on inlet connections
    s.modulations.frequency_patched = mFM.isConnected();
    s.modulations.timbre_patched = mTimbreMod.isConnected();
    s.modulations.morph_patched = mMorphMod.isConnected();

    // In osc mode: unpatched trigger/level = free-running oscillator
    bool oscMode = mTrigMode.value() == 1;
    if (oscMode)
    {
      s.modulations.trigger_patched = false;
      s.modulations.level_patched = false;
      s.modulations.trigger = 0.0f;
      s.modulations.level = 1.0f;
    }
    else
    {
      s.modulations.trigger_patched = mTrigger.isConnected();
      s.modulations.level_patched = mLevel.isConnected();
    }

    // Process in kBlockSize (12) sample chunks
    const int blockSize = plaits::kBlockSize;
    int pos = 0;

    while (pos < FRAMELENGTH)
    {
      int remaining = FRAMELENGTH - pos;
      int chunk = (remaining >= blockSize) ? blockSize : remaining;

      // Sample modulation inputs at block boundaries
      // V/Oct: FULLSCALE_IN_VOLTS=10, 12 semitones per octave.
      // ConstantOffset outputs 0.1 at 1200 cents. 0.1 * 120 = 12 semitones.
      s.patch.note = 60.0f + mFreq.value() + voct[pos] * 120.0f;

      s.modulations.note = 0.0f;
      s.modulations.frequency = fm[pos] * 6.0f;
      s.modulations.harmonics = harmonicsMod[pos];
      s.modulations.timbre = timbreMod[pos];
      s.modulations.morph = morphMod[pos];
      s.modulations.trigger = trig[pos];
      s.modulations.level = level[pos];

      // Render the voice
      s.voice.Render(s.patch, s.modulations, s.frames, chunk);

      // Convert from int16 to float, route based on output mode
      int mode = mOutputMode.value();
      for (int i = 0; i < chunk; i++)
      {
        float main = s.frames[i].out / 32768.0f;
        float aux = s.frames[i].aux / 32768.0f;

        switch (mode)
        {
        case 0: // main only (mono) or main+aux (stereo)
          outBuf[pos + i] = main;
          auxBuf[pos + i] = aux;
          break;
        case 1: // aux only
          outBuf[pos + i] = aux;
          auxBuf[pos + i] = aux;
          break;
        case 2: // main to both
          outBuf[pos + i] = main;
          auxBuf[pos + i] = main;
          break;
        case 3: // aux to both
          outBuf[pos + i] = aux;
          auxBuf[pos + i] = aux;
          break;
        default:
          outBuf[pos + i] = main;
          auxBuf[pos + i] = aux;
          break;
        }
      }

      pos += chunk;
    }
  }

} // namespace mi
