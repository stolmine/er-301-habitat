// zaum::APFTank
//
// Phase 1 north-star primitive for the Zaum woven-reverb package.
// A Dattorro/Griesinger figure-8 recirculating allpass tank extended
// with Gardner nested allpasses and decorrelated Brownian delay-line
// modulation — the believable-room substrate that the standalone
// Fabula unit ships, and that the north-star Zaum (Phase 5) reuses
// verbatim. Internal-stereo: one Object owns both L and R tank state.
//
// Plan: planning/fabula-design.md (DSP architecture, delay tables,
// modulation, governor). Roadmap: planning/zaum-roadmap.md §"Phase 1".
//
// BUILD SUB-PHASE 0.1.0.1 — package scaffold + passthrough stub.
//   This header currently declares the full 8-parameter surface and
//   the stereo inlet/outlet pair, but process() is a clean passthrough
//   (In L -> Out L, In R -> Out R). No diffusion, no tank, no
//   modulation yet. The DSP lands incrementally across sub-phases
//   0.1.0.2 .. 0.1.0.6 per fabula-design.md §9. Gate for this stub:
//   package installs and Fabula appears in the emu browser.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace zaum
{

  class APFTank : public od::Object
  {
  public:
    APFTank()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mSize);
      addParameter(mDecay);
      addParameter(mDamp);
      addParameter(mDiffusion);
      addParameter(mMod);
      addParameter(mModRate);
      addParameter(mPredelay);
      addParameter(mMix);
    }

    virtual ~APFTank() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mSize{"Size", 0.5f};
    od::Parameter mDecay{"Decay", 0.5f};
    od::Parameter mDamp{"Damp", 0.25f};
    od::Parameter mDiffusion{"Diffusion", 0.6f};
    od::Parameter mMod{"Mod", 0.3f};
    od::Parameter mModRate{"ModRate", 0.2f};
    od::Parameter mPredelay{"Predelay", 0.0f};
    od::Parameter mMix{"Mix", 0.5f};

    virtual void process()
    {
      // 0.1.0.1 stub: pure passthrough. The tank DSP arrives in the
      // following sub-phases (see file header). Until then Fabula is
      // a unity wire so the package can be installed and auditioned
      // in the emu browser.
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      int sampleFrames = FRAMELENGTH;
      memcpy(out1, in1, sampleFrames * sizeof(float));
      memcpy(out2, in2, sampleFrames * sizeof(float));
    }
#endif
  };
}
