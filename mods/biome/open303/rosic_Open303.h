#ifndef rosic_Open303_h
#define rosic_Open303_h

#include "rosic_BlendOscillator.h"
#include "rosic_BiquadFilter.h"
#include "rosic_TeeBeeFilter.h"
#include "rosic_AnalogEnvelope.h"
#include "rosic_DecayEnvelope.h"
#include "rosic_LeakyIntegrator.h"
#include "o3_config.h"
#include "o3_halfband.h"

namespace rosic
{

  /**

  This is a monophonic bass-synth that aims to emulate the sound of the famous Roland TB 303 and
  goes a bit beyond.

  */

  class Open303
  {

  public:

    //-----------------------------------------------------------------------------------------------
    // construction/destruction:

    /** Constructor. */
    Open303();

    /** Destructor. */
    ~Open303();

    //-----------------------------------------------------------------------------------------------
    // parameter settings:

    /** Sets the sample-rate (in Hz). */
    void setSampleRate(double newSampleRate);

    /** Sets up the waveform continuously between saw and square - the input should be in the range 
    0...1 where 0 means pure saw and 1 means pure square. */
    void setWaveform(double newWaveform) { oscillator.setBlendFactor(newWaveform); }

    /** Sets the master tuning frequency for note A4 (usually 440 Hz). */
    void setTuning(double newTuning) { tuning = newTuning; }

    /** Sets the filter's nominal cutoff frequency (in Hz). */
    void setCutoff(double newCutoff); 

    /** Sets the resonance amount for the filter. */
    void setResonance(double newResonance) { filter.setResonance(newResonance); }

    /** Sets the modulation depth of the filter's cutoff frequency by the filter-envelope generator 
    (in percent). */
    void setEnvMod(double newEnvMod);

    /** Sets the main envelope's decay time for non-accented notes (in milliseconds). 
    Devil Fish provides range of 30...3000 ms for this parameter. On the normal 303, this 
    parameter had a range of 200...2000 ms.  */
    void setDecay(double newDecay) { normalDecay = newDecay; }

    /** Sets the accent (in percent).  */
    void setAccent(double newAccent);

    /** Sets the master volume level (in dB). */
    void setVolume(double newVolume);     

    //  from here: parameter settings which were not available to the user in the 303:

    /** Sets the amplitudes envelope's sustain level in decibels. Devil Fish uses the second half 
    of the range of the (amplitude) decay pot for this and lets the user adjust it between 0 
    and 100% of the full volume. In the normal 303, this parameter was fixed to zero. */
    void setAmpSustain(double newAmpSustain) { ampEnv.setSustainInDecibels(newAmpSustain); }

    /** Sets the drive (in dB) for the tanh-shaper for 303-square waveform - internal parameter, to 
    be scrapped eventually. */
    void setTanhShaperDrive(double newDrive) 
    { waveTable2.setTanhShaperDriveFor303Square(newDrive); }

    /** Sets the offset (as raw value for the tanh-shaper for 303-square waveform - internal 
    parameter, to be scrapped eventually. */
    void setTanhShaperOffset(double newOffset) 
    { waveTable2.setTanhShaperOffsetFor303Square(newOffset); }

    /** Sets the cutoff frequency for the highpass before the main filter. */
    void setPreFilterHighpass(double newCutoff) { highpass1.setCutoff(newCutoff); }

    /** Sets the cutoff frequency for the highpass inside the feedback loop of the main filter. */
    void setFeedbackHighpass(double newCutoff) { filter.setFeedbackHighpassCutoff(newCutoff); }

    /** Sets the cutoff frequency for the highpass after the main filter. */
    void setPostFilterHighpass(double newCutoff) { highpass2.setCutoff(newCutoff); }

    /** Sets the phase shift of tanh-shaped square wave with respect to the saw-wave (in degrees)
    - this is important when the two are mixed. */
    void setSquarePhaseShift(double newShift) { waveTable2.set303SquarePhaseShift(newShift); }

    /** Sets the slide-time (in ms). The TB-303 had a slide time of 60 ms. */
    void setSlideTime(double newSlideTime);

    /** Sets the filter envelope's attack time for non-accented notes (in milliseconds). 
    Devil Fish provides range of 0.3...30 ms for this parameter. */
    void setNormalAttack(double newNormalAttack) 
    { 
      normalAttack = newNormalAttack; 
      rc1.setTimeConstant(normalAttack);
    }

    /** Sets the filter envelope's attack time for accented notes (in milliseconds). In the 
    Devil Fish, accented notes have a fixed attack time of 3 ms.  */
    void setAccentAttack(double newAccentAttack) 
    { 
      accentAttack = newAccentAttack; 
      rc2.setTimeConstant(accentAttack);
    }

    /** Sets the filter envelope's decay time for accented notes (in milliseconds). 
    Devil Fish provides range of 30...3000 ms for this parameter. On the normal 303, this 
    parameter was fixed to 200 ms.  */
    void setAccentDecay(double newAccentDecay) { accentDecay = newAccentDecay; }

    /** Sets the amplitudes envelope's decay time (in milliseconds). Devil Fish provides range of 
    16...3000 ms for this parameter. On the normal 303, this parameter was fixed to 
    approximately 3-4 seconds.  */
    void setAmpDecay(double newAmpDecay) { ampEnv.setDecay(newAmpDecay); }

    /** Sets the amplitudes envelope's release time (in milliseconds). On the normal 303, this 
    parameter was fixed to .....  */
    void setAmpRelease(double newAmpRelease) 
    { 
      normalAmpRelease = newAmpRelease;
      ampEnv.setRelease(newAmpRelease); 
    }

    //-----------------------------------------------------------------------------------------------
    // inquiry:

    /** Returns the waveform as a continuous value between 0...1 where 0 means pure saw and 1 means 
    pure square. */
    double getWaveform() const { return oscillator.getBlendFactor(); }

    /** Sets the master tuning frequency for note A4 (usually 440 Hz). */
    double getTuning() const { return tuning; }

    /** Returns the filter's nominal cutoff frequency (in Hz). */
    double getCutoff() const { return cutoff; }

    /** Returns the filter's resonance amount (in percent) */
    double getResonance() const { return filter.getResonance(); }

    /** Returns the modulation depth of the filter's cutoff frequency by the filter-envelope 
    generator (in percent). */
    double getEnvMod() const { return envMod; }

    /** Returns the filter envelope's decay time for non-accented notes (in milliseconds). */
    double getDecay() const { return normalDecay; }

    /** Returns the accent (in percent). */
    double getAccent() const { return 100.0 * accent; }

    /** Returns the master volume level (in dB). */
    double getVolume() const { return level; }

    //  from here: parameters which were not available to the user in the 303:

    /** Returns the amplitudes envelope's sustain level (in dB). */
    double getAmpSustain() const { return amp2dB(ampEnv.getSustain()); }

    /** Returns the drive (in dB) for the tanh-shaper for 303-square waveform - internal parameter, 
    to be scrapped eventually. */
    double getTanhShaperDrive() const 
    { return waveTable2.getTanhShaperDriveFor303Square(); }

    /** Returns the offset (as raw value for the tanh-shaper for 303-square waveform - internal 
    parameter, to be scrapped eventually. */   
    double getTanhShaperOffset() const 
    { return waveTable2.getTanhShaperOffsetFor303Square(); }

    /** Returns the cutoff frequency for the highpass before the main filter. */
    double getPreFilterHighpass() const { return highpass1.getCutoff(); }

    /** Retruns the cutoff frequency for the highpass inside the feedback loop of the main 
    filter. */
    double getFeedbackHighpass() const { return filter.getFeedbackHighpassCutoff(); }

    /** Returns the cutoff frequency for the highpass after the main filter. */
    double getPostFilterHighpass() const { return highpass2.getCutoff(); }

    /** Returns the phase shift of tanh-shaped square wave with respect to the saw-wave (in degrees)
    - this is important when the two are mixed. */
    double getSquarePhaseShift() const { return waveTable2.get303SquarePhaseShift(); }

    /** Returns the slide-time (in ms). */
    double getSlideTime() const { return slideTime; }

    /** Returns the filter envelope's attack time for non-accented notes (in milliseconds). */
    double getNormalAttack() const { return normalAttack; }

    /** Returns the filter envelope's attack time for non-accented notes (in milliseconds). */
    double getAccentAttack() const { return accentAttack; }

    /** Returns the filter envelope's decay time for non-accented notes (in milliseconds). */
    double getAccentDecay() const { return accentDecay; }

    /** Returns the amplitudes envelope's decay time (in milliseconds). */
    double getAmpDecay() const { return ampEnv.getDecay(); }

    /** Returns the amplitudes envelope's release time (in milliseconds). */
    double getAmpRelease() const { return normalAmpRelease; }

    //-----------------------------------------------------------------------------------------------
    // audio processing:

    /** Calculates onse output sample at a time. */
    INLINE o3Float getSample(); 

    //-----------------------------------------------------------------------------------------------
    // event handling:

    // habitat port: noteOn() is gone. It existed to arbitrate between the
    // internal sequencer and a MIDI note-list stack, neither of which survives
    // the port - the unit calls triggerNote / slideToNote / releaseNote below
    // straight from its inlets.

    /** Turns all possibly running notes off. */
    void allNotesOff();

    /** Sets the pitchbend value in semitones. */ 
    void setPitchBend(double newPitchBend);  

    //-----------------------------------------------------------------------------------------------
    // embedded objects: 

    MipMappedWaveTable        waveTable1, waveTable2;
    BlendOscillator           oscillator;
    TeeBeeFilter              filter;
    AnalogEnvelope            ampEnv; 
    DecayEnvelope             mainEnv;
    LeakyIntegrator           pitchSlewLimiter;
    //LeakyIntegrator           ampDeClicker;
    BiquadFilter              ampDeClicker;
    LeakyIntegrator           rc1, rc2;
    OnePoleFilter             highpass1, highpass2, allpass; 
    BiquadFilter              notch;
    O3Decimator               antiAliasFilter;

  public:

    // habitat port: these were protected, driven by noteOn() and the internal
    // sequencer. The ER-301 unit drives them directly from its gate / slide /
    // accent inlets, so they are the public note API now.

    // habitat port: noteNumber is a DOUBLE, not an int. Upstream took MIDI note
    // integers, which would quantize a modular V/Oct input to semitones.
    // pitchToFreq() is continuous already, so widening the parameter is the
    // whole change.

    /** Triggers a note. With glide=false the pitch jumps (the slew limiter is
    force-set); with glide=true the envelopes still fire but the slew limiter
    keeps its state, so the pitch glides in from wherever it was. The glide
    flag exists for gate-driven use: upstream only ever slid mid-legato while
    a note was already sounding, but a modular gate falls between steps, so a
    slide that skips the envelopes lands on a released (silent) amp env. See
    slideToNote() below. */
    void triggerNote(double noteNumber, bool hasAccent, bool glide = false);

    /** Slides to a note (no re-trigger; pitch glides via the slew limiter).
    NOTE: this deliberately does not touch the amp envelope - it is only
    correct while a note is currently SOUNDING (upstream called it from its
    MIDI note-list when a second key went down). If the amp env has released,
    use triggerNote(note, accent, true) instead. */
    void slideToNote(double noteNumber, bool hasAccent);

    /** Releases the current note. */
    void releaseNote();

    /** Retargets pitch without touching the envelopes, so held-note V/Oct
    tracking glides through the same slew limiter a slide uses. */
    void setNoteNumber(double noteNumber) { oscFreq = pitchToFreq(noteNumber, tuning); }

  protected:

    /** Sets the decay-time of the main envelope and updates the normalizers n1, n2 accordingly. */
    void setMainEnvDecay(double newDecay);

    void calculateEnvModScalerAndOffset();

    /** Updates the normalizer n1 according to the time-constant of rc1 and the decay-time of the
    main envelope generator. */
    void updateNormalizer1();

    /** Updates the normalizer n2 according to the time-constant of rc2 and the decay-time of the
    main envelope generator. */
    void updateNormalizer2();

    // Tier-selected: 2x on am335x, 4x elsewhere. See o3_config.h.
    static const int oversampling = O3_OVERSAMPLING;

    // habitat port: state for the out-of-loop oscillator (see getSample).
    // Class members, never stack locals, per feedback_neon_intrinsics_drumvoice.
    o3Float mOsBuf[O3_OVERSAMPLING];
    o3Float mPrevOsc;

    // habitat port: the members read on the PER-SAMPLE path are o3Float, not
    // double. Left as double they forced a vcvt pair plus scalar VFP double
    // arithmetic on every sample - Cortex-A8 has no double-precision NEON, so
    // those fall back to nonpipelined VFPLite (feedback_cortex_a8_no_double_in
    // _hot_loops). Setters still compute in double; the narrowing happens once
    // at set time, not 48000 times a second.
    double  tuning;          // master tunung for A4 in Hz (set-time only)
    o3Float ampScaler;       // final volume as raw factor
    o3Float oscFreq;         // frequecy of the oscillator (without pitchbend)
    double sampleRate;       // the (non-oversampled) sample rate
    double level;            // master volume level (in dB)
    double levelByVel;       // velocity dependence of the level (in dB)
    double accent;           // scales all "byVel" parameters
    double slideTime;        // the time to slide from one note to another (in ms)
    o3Float cutoff;           // nominal cutoff frequency of the filter
    double envMod;           // strength of the envelope modulation in percent
    double envUpFraction;    // fraction of the envelope that goes upward
    o3Float envOffset;        // offset for the normalized envelope ('bipolarity' parameter)
    o3Float envScaler;        // scale-factor for the normalized envelope (derived from envMod)
    double normalAttack;     // attack time for the filter envelope on non-accented notes
    double accentAttack;     // attack time for the filter envelope on accented notes
    double normalDecay;      // decay time for the filter envelope on non-accented notes
    double accentDecay;      // decay time for the filter envelope on accented notes
    double normalAmpRelease; // amp-env release time for non-accented notes
    double accentAmpRelease; // amp-env release time for accented notes
    o3Float accentGain;       // between 0.0...1.0 - to scale the 3rd amp-envelope on accents
    o3Float pitchWheelFactor; // scale factor for oscillator frequency from pitch-wheel
    o3Float n1, n2;          // normalizers for the RCs that are driven by the MEG
    double currentNote;      // note which is currently played (-1 if none)
    int    currentVel;       // velocity of currently played note
    int    noteOffCountDown; // a countdown variable till next note-off in sequencer mode
    bool   slideToNextNote;  // indicate that we need to slide to the next note in sequencer mode
    bool   idle;             // flag to indicate that we have currently nothing to do in getSample


  };

  //-------------------------------------------------------------------------------------------------
  // inlined functions:

  // habitat port. Three changes to this function, all from planning/open303-port.md:
  //
  //  1. THE SEQUENCER BRANCH IS GONE. Upstream ran its internal AcidSequencer
  //     from here whenever the mode was not OFF. The port drives notes from
  //     ER-301 inlets instead, which is exactly the "run it OFF" path, so the
  //     whole block and its AcidNote/slide bookkeeping fall away.
  //
  //  2. THE OSCILLATOR IS OUT OF THE OVERSAMPLING LOOP (item A, the big one).
  //     Upstream called oscillator.getSample() once per oversampled step, so
  //     each output sample cost 8 mip-table gathers - the memory-bound part of
  //     the engine and the reason it did not fit L2. The oscillator does not
  //     need the oversampling: it is already band-limited by its own mip
  //     tables, and the 4x exists for the filter's nonlinearity. So it now runs
  //     ONCE per output sample and is linearly interpolated up onto the
  //     filter's grid. Gathers per output: 8 -> 2.
  //     This also re-points the mip selection correctly with no fudge needed.
  //     The oscillator's sample rate is now the base rate, so its increment is
  //     `oversampling` times larger, its binary exponent rises by log2 of that,
  //     and the level it picks is exactly as band-limited in absolute Hz as
  //     upstream's was. Upstream's `+2` constant carries over untouched.
  //
  //  3. THE PER-SAMPLE pow(2.0, ...) IS GONE, replaced by o3FastExp2 - it was
  //     the only libm call left on the per-sample path. The result feeds a
  //     cutoff that is immediately clamped to [200, 20000] Hz, so the
  //     polynomial's ~1e-6 error is far below what the consumer can notice.
  INLINE o3Float Open303::getSample()
  {
    if( idle )
      return 0.0f;

    // calculate instantaneous oscillator frequency and set up the oscillator:
    o3Float instFreq = pitchSlewLimiter.getSample(oscFreq);
    oscillator.setFrequency(instFreq*pitchWheelFactor);
    oscillator.calculateIncrement();

    // calculate instantaneous cutoff frequency from the nominal cutoff and all its modifiers and
    // set up the filter:
    o3Float mainEnvOut = mainEnv.getSample();
    o3Float tmp1       = n1 * rc1.getSample(mainEnvOut);
    o3Float tmp2       = 0.0f;
    if( accentGain > 0.0 )
      tmp2 = mainEnvOut;
    tmp2 = n2 * rc2.getSample(tmp2);
    tmp1 = envScaler * ( tmp1 - envOffset );  // seems not to work yet
    tmp2 = accentGain*tmp2;
    o3Float instCutoff = cutoff * o3FastExp2(tmp1+tmp2);
    filter.setCutoff(instCutoff);

    o3Float ampEnvOut = ampEnv.getSample();
    if( ampEnv.isNoteOn() )
      ampEnvOut += 0.45f*mainEnvOut + accentGain*4.0f*mainEnvOut;
    ampEnvOut = ampDeClicker.getSample(ampEnvOut);

    // ONE oscillator sample per output sample, then linear-interpolate it onto
    // the oversampled grid the filter runs on.
    const o3Float oscNow = -oscillator.getSample();
    const o3Float oscStep = (oscNow - mPrevOsc) * (o3Float)(1.0/oversampling);

    o3Float osc = mPrevOsc;
    for(int i=0; i<oversampling; i++)
    {
      osc += oscStep;
      o3Float s = highpass1.getSample(osc);   // pre-filter highpass
      mOsBuf[i] = filter.getSample(s);        // now it's filtered
    }
    mPrevOsc = oscNow;

    // decimate back down (halfband cascade, see o3_halfband.h)
    o3Float tmp = antiAliasFilter.process(mOsBuf);

    // these filters may actually operate without oversampling (but only if we reset them in
    // triggerNote - avoid clicks)
    tmp  = allpass.getSample(tmp);
    tmp  = highpass2.getSample(tmp);
    tmp  = notch.getSample(tmp);
    tmp *= ampEnvOut;                       // amplified
    tmp *= ampScaler;

    // find out whether we may switch ourselves off for the next call:
    idle = false;

    return tmp;
  }

}

#endif 
