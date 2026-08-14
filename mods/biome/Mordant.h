#pragma once

#include <od/objects/Object.h>
#include <od/config.h>

namespace rosic { class Open303; }

namespace stolmine
{

  // Mordant - monophonic acid bassline voice.
  //
  // DSP is the vendored Open303 engine under mods/biome/open303/ ((c) 2009
  // Robin Schmidt, MIT), with its internal sequencer stripped and the voice
  // driven from ER-301 inlets. See planning/open303-port.md for the port's
  // optimization work (float audio path, shrinking mip pyramid, oscillator
  // lifted out of the oversampling loop, halfband decimator, 2x am335x tier).
  //
  // The engine is held behind a forward declaration deliberately: its header
  // pulls in the whole rosic tree, and anything that lands in the SWIG wrapper
  // TU compiles at -Os with no -ffast-math. Keeping it out of this header means
  // the hot path is only ever compiled from Mordant.cpp at CFLAGS.speed. That
  // build-config trap is what made anamnesis' DSP silently slow.
  class Mordant : public od::Object
  {
  public:
    Mordant();
    virtual ~Mordant();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mVOct{"V/Oct"};
    od::Inlet mGate{"Gate"};
    od::Inlet mAccent{"Accent"};
    od::Inlet mSlide{"Slide"};
    od::Outlet mOutput{"Out"};

    od::Parameter mFundamental{"Fundamental", 55.0f};
    od::Parameter mCutoff{"Cutoff", 500.0f};
    od::Parameter mResonance{"Resonance", 0.5f};
    od::Parameter mEnvMod{"Env Mod", 0.25f};
    od::Parameter mDecay{"Decay", 0.4f};
    od::Parameter mAccentAmount{"Accent Amount", 0.5f};
    od::Parameter mWaveform{"Waveform", 0.0f};
    od::Parameter mLevel{"Level", 0.5f};
#endif

  private:
    rosic::Open303 *mpVoice;
    bool mGateWasHigh;
    float mLastSampleRate;
    // Last note pushed to the engine by the held-note tracking branch. Guards
    // setNoteNumber() so its double exp() runs on pitch CHANGES, not per
    // sample. See Mordant.cpp.
    float mLastTrackedNote;
  };

} // namespace stolmine
