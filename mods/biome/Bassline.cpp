#include "Bassline.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

#include "open303/rosic_Open303.h"

namespace stolmine
{

  // V/Oct: FULLSCALE_IN_VOLTS is 10, so one unit of inlet signal is 10 octaves,
  // i.e. 120 semitones. See CONTEXT.md.
  static const float kVoltsToSemitones = 120.0f;

  Bassline::Bassline()
  {
    addInput(mVOct);
    addInput(mGate);
    addInput(mAccent);
    addInput(mSlide);
    addOutput(mOutput);

    addParameter(mFundamental);
    addParameter(mCutoff);
    addParameter(mResonance);
    addParameter(mEnvMod);
    addParameter(mDecay);
    addParameter(mAccentAmount);
    addParameter(mWaveform);
    addParameter(mLevel);

    mpVoice = new rosic::Open303();
    mpVoice->setSampleRate(globalConfig.sampleRate);
    mLastSampleRate = globalConfig.sampleRate;
    mGateWasHigh = false;
    // Sentinel no real note can equal, so the first tracked sample always
    // pushes a pitch to the engine.
    mLastTrackedNote = -1.0e9f;
  }

  Bassline::~Bassline()
  {
    delete mpVoice;
  }

  void Bassline::process()
  {
    float *voct = mVOct.buffer();
    float *gate = mGate.buffer();
    float *accent = mAccent.buffer();
    float *slide = mSlide.buffer();
    float *out = mOutput.buffer();

    if (globalConfig.sampleRate != mLastSampleRate)
    {
      mLastSampleRate = globalConfig.sampleRate;
      mpVoice->setSampleRate(mLastSampleRate);
    }

    // Block-rate parameters. The engine recomputes filter coefficients per
    // sample from these anyway, so pushing them once per block is enough.
    const float f0 = CLAMP(8.0f, 2000.0f, mFundamental.value());
    mpVoice->setCutoff(CLAMP(200.0f, 20000.0f, mCutoff.value()));
    mpVoice->setResonance(CLAMP(0.0f, 1.0f, mResonance.value()) * 100.0f);
    mpVoice->setEnvMod(CLAMP(0.0f, 1.0f, mEnvMod.value()) * 100.0f);
    mpVoice->setAccent(CLAMP(0.0f, 1.0f, mAccentAmount.value()) * 100.0f);
    mpVoice->setWaveform(CLAMP(0.0f, 1.0f, mWaveform.value()));

    // Decay 0..1 -> 30..3000 ms, the Devil Fish range quoted by the engine.
    const float d = CLAMP(0.0f, 1.0f, mDecay.value());
    mpVoice->setDecay(30.0f * powf(100.0f, d));

    // Level is a LINEAR output gain 0..1, the same convention as the
    // package's other voices (VarishapeVoice drives a VCA with it directly).
    // The engine API takes dB and computes ampScaler = dB2amp(level), so
    // passing 20*log10(lv) makes ampScaler exactly lv. The previous mapping
    // (0..1 -> -60..0 dB) parked the default bias of 0.5 at -30 dB, which is
    // the quiet-output defect: ~0.03 peak against ~0.47 at the fixed default.
    const float lv = CLAMP(0.0f, 1.0f, mLevel.value());
    mpVoice->setVolume(lv <= 0.0f ? -120.0f : 20.0f * log10f(lv));

    // Continuous note number from the fundamental and the V/Oct input. The
    // engine's pitchToFreq is continuous, so this never quantizes to semitones.
    // Computed once per block: note = 69 + 12*log2(f0/tuning) + 120*voct.
    const float tuning = (float)mpVoice->getTuning();
    const float baseNote = 69.0f + 12.0f * log2f(f0 / tuning);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Gate threshold is 0.5, not 0.0 - a 0.0 threshold trips on fuzz and DC
      // and can hang the device (feedback_comparator_gate_threshold).
      const bool gateHigh = gate[i] > 0.5f;
      const float note = baseNote + kVoltsToSemitones * voct[i];

      if (gateHigh && !mGateWasHigh)
      {
        const bool hasAccent = accent[i] > 0.5f;
        // A note sounds on EVERY rising edge; Slide only selects whether the
        // pitch glides in (slew limiter keeps its state) or jumps. The old
        // code called slideToNote() here, but that never touches the amp
        // envelope - upstream only ever slid mid-legato while a note was
        // sounding. With a modular gate that falls between steps, the amp env
        // has already released, so the "slid" note was silent. True legato
        // (gate held across a pitch change) still avoids a re-attack via the
        // tracking branch below.
        mpVoice->triggerNote(note, hasAccent, slide[i] > 0.5f);
        mLastTrackedNote = note;
      }
      else if (!gateHigh && mGateWasHigh)
      {
        mpVoice->releaseNote();
      }
      else if (note != mLastTrackedNote)
      {
        // Track V/Oct while the note is held. This retargets pitch only, so it
        // glides through the same slew limiter a slide uses. The change guard
        // matters: setNoteNumber() costs a double-precision exp() in
        // pitchToFreq(), and unguarded it ran EVERY sample - the exact
        // per-sample libm-in-double the port removed from getSample()
        // (feedback_cortex_a8_no_double_in_hot_loops). Block-constant CV now
        // pays for at most one exp() per change.
        mpVoice->setNoteNumber(note);
        mLastTrackedNote = note;
      }

      mGateWasHigh = gateHigh;
      out[i] = mpVoice->getSample();
    }
  }

} // namespace stolmine
