#ifndef rosic_OnePoleFilter_h
#define rosic_OnePoleFilter_h

// Open303, (c) 2009 Robin Schmidt, MIT. See LICENSE-Open303.txt.
// habitat port: audio path converted o3Float -> o3Float (float).
// See planning/open303-port.md.

#include "o3_config.h"

// rosic-indcludes:
#include "rosic_RealFunctions.h"

namespace rosic
{

  /**

  This is an implementation of a simple one-pole filter unit.

  */

  class OnePoleFilter
  {

  public:

    /** This is an enumeration of the available filter modes. */
    enum modes
    {
      BYPASS = 0,
      LOWPASS,
      HIGHPASS,
      LOWSHELV,
      HIGHSHELV,
      ALLPASS
    };
    // \todo (maybe): let the user choose between LP/HP versions obtained via bilinear trafo and 
    // impulse invariant trafo

    //---------------------------------------------------------------------------------------------
    // construction/destruction:

    /** Constructor. */
    OnePoleFilter();   

    //---------------------------------------------------------------------------------------------
    // parameter settings:

    /** Sets the sample-rate. */
    void setSampleRate(o3Float newSampleRate);

    /** Chooses the filter mode. See the enumeration for available modes. */
    void setMode(int newMode);

    /** Sets the cutoff-frequency for this filter. */
    void setCutoff(o3Float newCutoff);

    /** This will set the time constant 'tau' for the case, when lowpass mode is chosen. This is 
    the time, it takes for the impulse response to die away to 1/e = 0.368... or equivalently, the
    time it takes for the step response to raise to 1-1/e = 0.632... */
    void setLowpassTimeConstant(o3Float newTimeConstant) { setCutoff(1.0/(2*PI*newTimeConstant)); }

    /** Sets the gain factor for the shelving modes (this is not in decibels). */
    void setShelvingGain(o3Float newGain);

    /** Sets the gain for the shelving modes in decibels. */
    void setShelvingGainInDecibels(o3Float newGain);

    /** Sets the filter coefficients manually. */
    void setCoefficients(o3Float newB0, o3Float newB1, o3Float newA1);

    /** Sets up the internal state variables for both channels. */
    void setInternalState(o3Float newX1, o3Float newY1);

    //---------------------------------------------------------------------------------------------
    // inquiry

    /** Returns the cutoff-frequency. */
    o3Float getCutoff() const { return cutoff; }

    //---------------------------------------------------------------------------------------------
    // audio processing:

    /** Calculates a single filtered output-sample. */
    INLINE o3Float getSample(o3Float in);

    //---------------------------------------------------------------------------------------------
    // others:

    /** Resets the internal buffers (for the \f$ x[n-1], y[n-1] \f$-samples) to zero. */
    void reset();

    //=============================================================================================

  protected:

    // buffering:
    o3Float x1, y1;

    // filter coefficients:
    o3Float b0; // feedforward coeffs
    o3Float b1;
    o3Float a1; // feedback coeff

    // filter parameters:
    o3Float cutoff;
    o3Float shelvingGain;
    int    mode;  

    o3Float sampleRate; 
    o3Float sampleRateRec;  // reciprocal of the sampleRate

    // internal functions:
    void calcCoeffs();  // calculates filter coefficients from filter parameters

  };

  //-----------------------------------------------------------------------------------------------
  // inlined functions:

  INLINE o3Float OnePoleFilter::getSample(o3Float in)
  {
    // calculate the output sample:
    y1 = b0*in + b1*x1 + a1*y1 + TINY;

    // update the buffer variables:
    x1 = in;

    return y1;
  }

} // end namespace rosic

#endif // rosic_OnePoleFilter_h
