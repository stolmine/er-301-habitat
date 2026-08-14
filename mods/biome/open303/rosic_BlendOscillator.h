#ifndef rosic_BlendOscillator_h
#define rosic_BlendOscillator_h

// Open303, (c) 2009 Robin Schmidt, MIT. See LICENSE-Open303.txt.
//
// habitat port changes (2026-08-10, planning/open303-port.md):
//   - float audio path (phase, increment, output).
//   - the mip level index comes from EXPOFFLT on the float increment; upstream
//     used EXPOFDBL on a double one. Same value, since the increment's binary
//     exponent is what selects the octave, and it now goes through the
//     memcpy-based helper rather than a strict-aliasing violation.
//   - getValueLinear() takes the phase directly and does its own per-level
//     rescale, because the pyramid's levels no longer share one length.

// rosic-indcludes:
#include "o3_config.h"
#include "rosic_MipMappedWaveTable.h"

namespace rosic
{

  /**

  This is an oscillator that can continuously blend between two waveforms - this is more efficient
  than using two separate oscillators because the phase-accumulator has to be calculated only once
  for both waveforms.

  */

  class BlendOscillator
  {

  public:

    //---------------------------------------------------------------------------------------------
    // construction/destruction:

    /** Constructor. */
    BlendOscillator();

    /** Destructor. */
    ~BlendOscillator();

    //---------------------------------------------------------------------------------------------
    // parameter settings:

    /** Sets the sample-rateRate(). */
    void setSampleRate(double newSampleRate);

    /** Sets the 1st waveform of the oscillator. */
    void setWaveForm1(int newWaveForm1);

    /** Sets the 2nd waveform of the oscillator. */
    void setWaveForm2(int newWaveForm2);

    /** Set start phase (range 0 - 360 degrees). */
    void setStartPhase(double StartPhase);

    /** An object of class WaveTable should be passed with this function which will be used in the
    oscillator. */
    void setWaveTable1(MipMappedWaveTable* newWaveTable1);

    /** Sets the 2nd wavetable. @see setWaveTable1 */
    void setWaveTable2(MipMappedWaveTable* newWaveTable2);

    /** Sets the blend/mix factor between the two waveforms. The value is expected between 0...1
    where 0 means waveform1 only, 1 means waveform2 only - in between there will be a linear blend
    between the two waveforms. */
    void setBlendFactor(double newBlendFactor) { blend = (o3Float) newBlendFactor; }

    /** Sets the frequency of the oscillator. */
    O3_ALWAYS_INLINE void setFrequency(o3Float newFrequency);

    /** Sets the pulse width (or symmetry) of the oscillator. */
    O3_ALWAYS_INLINE void setPulseWidth(double newPulseWidth);

    /** Sets the phase increment from outside. */
    O3_ALWAYS_INLINE void setIncrement(o3Float newIncrement) { increment = newIncrement; }

    //---------------------------------------------------------------------------------------------
    // inquiry:

    double  getBlendFactor() const { return blend; }
    O3_ALWAYS_INLINE o3Float getIncrement() const { return increment; }

    //---------------------------------------------------------------------------------------------
    // audio processing:

    /** Calculates one output sample at a time. */
    O3_ALWAYS_INLINE o3Float getSample();

    //---------------------------------------------------------------------------------------------
    // others:

    /** Calculates the phase-increments for first and second half-period according to freq and
    pulseWidth. */
    O3_ALWAYS_INLINE void calculateIncrement();

    /** Resets the phaseIndex to startIndex. */
    void resetPhase();

    /** Reset the phaseIndex to startIndex+PhaseIndex. */
    void setPhase(double PhaseIndex);

    //=============================================================================================

  protected:

    o3Float tableLengthDbl;   // tableLength as float (name kept from upstream)
    o3Float phaseIndex;       // current phase index, in LEVEL 0 units
    o3Float freq;             // frequency of the oscillator
    o3Float increment;        // phase increment per sample
    o3Float blend;            // the blend factor between the two waveforms
    o3Float startIndex;       // start-phase-index of the osc (range: 0 - tableLength)
    double  sampleRate;       // the samplerate
    double  sampleRateRec;    // 1/sampleRate

    MipMappedWaveTable *waveTable1, *waveTable2; // the 2 wavetables between which we blend

  };

  //-----------------------------------------------------------------------------------------------
  // inlined functions:

  O3_ALWAYS_INLINE void BlendOscillator::setFrequency(o3Float newFrequency)
  {
    if( (newFrequency > 0.0f) && (newFrequency < 20000.0f) )
      freq = newFrequency;
  }

  O3_ALWAYS_INLINE void BlendOscillator::setPulseWidth(double newPulseWidth)
  {
    waveTable1->setSymmetry(0.01*newPulseWidth);
    waveTable2->setSymmetry(0.01*newPulseWidth);
  }

  O3_ALWAYS_INLINE void BlendOscillator::calculateIncrement()
  {
    increment = (o3Float)(tableLengthDbl*freq*sampleRateRec);
  }

  O3_ALWAYS_INLINE o3Float BlendOscillator::getSample()
  {
    if( waveTable1 == NULL || waveTable2 == NULL )
      return 0.0f;

    // from this increment, decide which table is to be used:
    int tableNumber  = EXPOFFLT(increment);
    tableNumber += 2;             // generate frequencies up to nyquist/4 on the highest note

    // wraparound if necessary:
    while( phaseIndex >= tableLengthDbl )
      phaseIndex -= tableLengthDbl;

    o3Float out1 = (1.0f-blend) * waveTable1->getValueLinear(phaseIndex, tableNumber);
    o3Float out2 =       blend  * waveTable2->getValueLinear(phaseIndex, tableNumber);

    out2 *= 0.5f; // upstream: preliminary scaling for the square

    phaseIndex += increment;
    return out1 + out2;
  }

} // end namespace rosic

#endif // rosic_BlendOscillator_h
