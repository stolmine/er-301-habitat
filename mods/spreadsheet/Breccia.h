#pragma once

#include <od/objects/heads/TapeHead.h>
#include <od/config.h>
#include <stdint.h>

namespace stolmine
{

  // Breccia - buffer slicer and shuffler.
  //
  // Takes a card-loaded sample, cuts it into N equal slices, and plays back
  // continuously through a rearrangement. A gate shuffles the order.
  //
  // SLICE COUNT, NOT SLICE LENGTH. Count is length-agnostic: the buffer always
  // divides into exactly N pieces whatever its duration, the loop period stays
  // equal to the buffer, and every slice plays exactly once per cycle. That
  // last part matters - the playback is a PERMUTATION, so the buffer's content
  // is preserved exactly and only arrival order changes. Slice length would
  // make the count drift with buffer duration and leave a ragged remainder.
  //
  // Deliberately small. Size and a shuffle gate, plus level. Everything else
  // (direction, repeats, probability, sync) is a later conversation.
  class Breccia : public od::TapeHead
  {
  public:
    Breccia();
    virtual ~Breccia();

    virtual void setSample(od::Sample *sample);

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mShuffle{"Shuffle"};
    // Speed is an INLET, not a Parameter: it is the audio-rate playback rate
    // and the Lua graph feeds it V/Oct through the framework's VoltPerOctave,
    // exactly as the built-in players do. feedback_inlet_vs_parameter_audio
    // _rate_mod - a ParameterAdapter here would be block-rate and flappy.
    od::Inlet mSpeed{"Speed"};
    od::Outlet mOutput{"Out"};

    // ONE normalized size control. Size Mode decides what it addresses, so
    // there is never a second exposed control sitting inert. In BOTH modes
    // higher Size means BIGGER slices - if count mode counted upward the knob
    // would reverse direction when the option is flipped, which reads as broken.
    od::Parameter mSize{"Size", 0.5f};      // 0..1
    od::Parameter mGlitch{"Glitch", 0.0f};  // 0..1 character macro, per-slice effects
    // Concurrent slice playback. 0 = the focused slice alone; turning up fades
    // in symmetric neighbours (+/-1, then +/-2, then +/-3) at the same
    // intra-slice phase, up to 7 voices. Shaped like an additive-synth partial
    // count: each ring rises over its own third of the travel.
    od::Parameter mLayer{"Layer", 0.0f};
    // Global bipolar bias on every slice's effect INTENSITY, leaving the effect
    // TYPE alone. Modelled on Larets' Param Offset (Larets.cpp:505,
    // effParam = s.param[step] + paramOffset), which is the same idea for its
    // per-step effects: one knob sweeps the whole pattern's severity live while
    // the per-item variation is preserved.
    od::Parameter mOffset{"Offset", 0.0f};   // -1..1
    // Which "world" of effects the roll draws from. Glitch is one-dimensional
    // on its own: it scales HOW OFTEN an effect fires but never WHICH KIND, so
    // the statistical mix is identical at every setting and the unit tends to
    // the same territory. World morphs between weight vectors that emphasise
    // different families. Every vector sums to the same total, so World changes
    // character WITHOUT changing density - otherwise it would just be a second
    // intensity control.
    od::Parameter mWorld{"World", 0.0f};     // 0..1, morphs across the sets
    od::Parameter mLevel{"Level", 0.5f};

    // Which control decides the grid. od::Option values are 1-based; 0 means
    // UNKNOWN and must never be used.
    //   1 = Count  - N slices, each buffer/N. Length-agnostic, musical
    //                divisions line up, but slice duration scales with the
    //                buffer, so a long sample cannot reach short slices.
    //   2 = Length - a slice is an absolute duration and N follows from the
    //                buffer. Predictable grain size on any source; the
    //                divisions stop being musical and there is a remainder.
    // Loosely after the sampler slice-mapping conventions (VariSpeed's
    // "CV-to-Slice Mapping": nearest / index / 12TET) - same shape of choice,
    // different axis, so the parallel is oblique but the idiom carries.
    od::Option mSizeMode{"Size Mode", 1};
#endif

  private:
    typedef od::TapeHead Base;

    static const int kMaxSlices = 1024;
    static const int kMaxXfade = 512;     // 3 ms at 48k is 144

    // Per-slice glitch, resolved to BRANCHLESS COEFFICIENTS for the slice
    // being entered. feedback_runtime_branched_dsp_dispatch records that a
    // runtime switch on a per-item mode inside a sample loop has hung A8; the
    // advice is to pull the work out of the conditionals and mask it, which is
    // what these are. At Glitch = 0 every field takes its identity value and
    // the sample path is a bit-identical bypass.
    //
    // Computed LAZILY, one slice at a time at each boundary - O(1). Assigning
    // all N up front was O(N) with a powf inside, which at 1024 slices put a
    // thousand-iteration burst inside a single sample slot.
    struct SliceFx
    {
      float gain;      // 1 normal, 0 MUTE
      float dir;       // +1 forward, -1 REVERSE
      // Sub-segment length, as segLen = sliceIn * segFrac + segAbs. Exactly one
      // term is ever live. RELATIVE (segFrac) is STUTTER and REVERSE, which
      // divide the slice into k parts and so track its length. ABSOLUTE
      // (segAbs) is FREEZE, whose whole point is a window that does NOT scale:
      // held short enough the repeat stops reading as a repeat and becomes a
      // pitched drone at 1/window. Measured spectral flatness 0.0001 against
      // stutter's 0.075, a factor of 750, so it is a distinct effect and not a
      // setting of stutter. Summing instead of branching keeps identity exact:
      // segFrac 1, segAbs 0 gives sliceIn * 1.0 + 0.0 == sliceIn.
      float segFrac;
      float segAbs;
      float scrub;     // 0 off, else depth as a fraction of the slice
      float scrubRate; // SCRUB wobble cycles per slice
      float crush;     // CRUSH quantization step
      float crushInv;  // 1/step, 0 when off
      float crushMix;  // 0 off, 1 crushed
      float rate;      // 1 off, 2 = OCTAVE UP, 0.5 = OCTAVE DOWN
      // STEP: a discrete pitch ladder across the slice. Rates are drawn from a
      // fixed musical set and HELD for each sub-step - no interpolation, so the
      // pitch jumps rather than slewing into position.
      float stepRate[8];
      int   stepCount; // 1 = off
      float chop;      // 0 off, else CHOP gate frequency in cycles per slice
      float chopDuty;  // CHOP duty cycle
      // FILTER: TPT state-variable, 2-pole. A one-pole cannot resonate, and
      // resonance is the point. TPT is unconditionally stable at high Q.
      float fg;        // tan(pi*fc/sr) prewarp, 0 = filter off
      float fk;        // 1/Q damping
      float fa1, fa2, fa3;
      float fmLp, fmBp, fmHp;   // output select, exactly one is 1
      float combDelay; // COMB tap distance in source samples
      float combMix;   // 0 = off
      float combGain;  // COMB tap gain, signed so the comb can subtract
      // FILTER SWEEP: per-slice cutoff travel. The TPT coefficients are LERPed
      // across the slice rather than recomputed, because recomputing a1
      // needs a divide per sample per layer (7 divides a sample at ~20 cycles
      // each on A8). Deltas of 0 leave a1 = fa1 + 0 * frac exactly, so static
      // FILTER and the Glitch=0 bypass are untouched.
      float fd1, fd2, fd3;
      // ENVELOPE: the one axis nothing else touched. Every slice used to play
      // flat-gain; measured envelope tilt across all nine shipping effects
      // spanned only 0.98 to 1.90. Attack is an increment saturating at 1,
      // decay a per-sample multiplier, so swell / flat / percussive is one
      // continuous parameter for two multiplies. Identity is atk 1, dec 1,
      // which reaches exactly 1.0f on the first sample.
      float envAtk;
      float envDec;
      // RINGMOD: the only effect that ADDS inharmonic content. Measured
      // inharmonicity 0.949, the highest of eighteen candidates, and uniquely
      // envCorr 0.97 with waveCorr 0.02 - it destroys the waveform while
      // leaving the amplitude envelope intact, so it recolours without
      // disturbing rhythm. Phase increment is cycles per sample.
      float ringInc;
      float ringMix;
      // SCATTER: sub-slice permutation, the unit's own premise one level down.
      // Being a permutation it cannot damage the material, only reorder it.
      uint8_t scatPerm[8];
      int scatCount;   // 1 = off
      // Raw per-slice hashes, kept so the character params can be RE-DERIVED
      // when Offset moves without re-rolling which effect the slice got.
      uint8_t mode;
      float h1, h2, h3;   // three independent per-slice hashes
      int   decim;     // 1 off, else sample-and-hold factor
    };
    // Up to 7 concurrent voices: the focus plus 3 symmetric pairs. Offsets are
    // in PLAYBACK order, so the layers stack the sequence against itself.
    static const int kMaxLayers = 7;
    static const int kLayerOffset[kMaxLayers];
    // Layers are stacked HARMONICALLY, octaves and fifths outward from the
    // focus, so the poly reads as a drawbar register rather than a cluster.
    static const float kLayerRate[kMaxLayers];

    SliceFx mFx[kMaxLayers];
    SliceFx mFxPrev[kMaxLayers];
    int mFxIdx = -1;

    float readSource(double srcPos) const;
    // Split deliberately. rollFx picks WHICH effect and stores the raw hashes;
    // deriveFx turns those plus the live Offset into coefficients. Offset can
    // therefore re-shape the character of an in-flight pattern without
    // re-rolling it - Larets recomputes per sample for exactly this reason
    // (Larets.cpp:301, "so the global offset" applies).
    static const int kNumFx = 14;
    static const int kNumWorlds = 4;
    static const float kWorld[kNumWorlds][kNumFx];
    void rollFx(int sliceIdx, const float *w, SliceFx &fx) const;
    static void deriveFx(SliceFx &fx, float offset, float sr);
    static void identityFx(SliceFx &fx);
    void reshuffle(int n);
    void identity(int n);
    void rebuildFade();

    // Equal-power crossfade ramps, precomputed with double sin/cos at
    // construction. Never runtime sinf/cosf: feedback_package_trig_lut records
    // that single-precision trig from a package .so miscomputes on am335x.
    float mFadeIn[kMaxXfade];
    float mFadeOut[kMaxXfade];
    int mXfade = 0;

    int mPerm[kMaxSlices];


    uint32_t mGlitchLcg = 0xFEEDF00Du;
    // decimator + scrub state, per sample
    int mDecimPhase = 0;
    float mDecimHeld = 0.0f;
    double mScrubPhase = 0.0;
    double mChopPhase = 0.0;
    float mSvfIc1[kMaxLayers];
    float mSvfIc2[kMaxLayers];
    float mEnvA[kMaxLayers];
    float mEnvD[kMaxLayers];
    float mRingPhase[kMaxLayers];
    // Sine table for RINGMOD, built once with DOUBLE sin at construction.
    // Never runtime sinf: feedback_package_trig_lut records single-precision
    // trig from a package .so miscomputing on am335x, and libm sin costs
    // 300-500 ns, which is unaffordable per sample per layer.
    static const int kSinLut = 256;
    float mSinLut[kSinLut + 1];
    float mLastOffset = -99.0f;
    int mSlices = 0;          // slice count the current permutation is for
    int mPendingSlices = 0;   // Size changed; adopt at the next slice boundary

    double mPlayPos = 0.0;    // position within the loop, in OUTPUT samples
    double mRateRatio = 1.0;  // source samples per output sample
    float mLastSampleRate = 0.0f;
    bool mShuffleWasHigh = false;
    bool mShufflePending = false;
    // Whether the current order is shuffled. Changing Size has to build a
    // permutation of a DIFFERENT length, and reverting to identity there
    // silently un-shuffles the unit every time the size knob moves.
    bool mIsShuffled = false;
  };

} // namespace stolmine
