#pragma once

#include <od/objects/Object.h>

namespace mi
{

  class PlaitsVoice : public od::Object
  {
  public:
    PlaitsVoice();
    virtual ~PlaitsVoice();

#ifndef SWIGLUA
    virtual void process();

    // Audio I/O
    od::Inlet mVOct{"V/Oct"};
    od::Inlet mTrigger{"Trigger"};
    od::Inlet mLevel{"Level"};
    od::Inlet mFM{"FM"};
    od::Inlet mTimbreMod{"Timbre Mod"};
    od::Inlet mMorphMod{"Morph Mod"};
    od::Inlet mHarmonicsMod{"Harmonics Mod"};
    od::Outlet mOut{"Out"};
    od::Outlet mAux{"Aux"};

    // Parameters (set via tie from ParameterAdapters)
    od::Parameter mEngine{"Engine", 0.0f};
    od::Parameter mHarmonics{"Harmonics", 0.5f};
    od::Parameter mTimbre{"Timbre", 0.5f};
    od::Parameter mMorph{"Morph", 0.5f};
    od::Parameter mFMAmount{"FM Amount", 0.0f};
    od::Parameter mTimbreAmount{"Timbre CV", 0.0f};
    od::Parameter mMorphAmount{"Morph CV", 0.0f};
    od::Parameter mDecay{"Decay", 0.5f};
    od::Parameter mLPGColour{"LPG Colour", 0.5f};
    od::Option mOutputMode{"Output Mode", 0}; // 0=main, 1=aux, 2=main+main, 3=aux+main
    od::Option mTrigMode{"Trig Mode", 1};    // 0=trig (enveloped), 1=osc (free-running)
#endif

  private:
    struct Internal;
    Internal *mpInternal;
  };

} // namespace mi
