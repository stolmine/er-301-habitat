#ifndef rosic_BiquadFilter_h
#define rosic_BiquadFilter_h

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

  class BiquadFilter
  {

  public:

    /** Enumeration of the available filter modes. */
    enum modes
    {
      BYPASS = 0,
      LOWPASS6,
      LOWPASS12,
      HIGHPASS6,
      HIGHPASS12,
      BANDPASS,
      BANDREJECT,
      PEAK,
      LOW_SHELF,
      //HIGH_SHELF,
      //ALLPASS,

      NUM_FILTER_MODES
    };

    //---------------------------------------------------------------------------------------------
    // construction/destruction:

    /** Constructor. */
    BiquadFilter();   

    //---------------------------------------------------------------------------------------------
    // parameter settings:

    /** Sets the sample-rate (in Hz) at which the filter runs. */
    void setSampleRate(o3Float newSampleRate);

    /** Sets the filter mode as one of the values in enum modes. */
    void setMode(int newMode);

    /** Sets the center frequency in Hz. */
    void setFrequency(o3Float newFrequency);

    /** Sets the boost/cut gain in dB. */
    void setGain(o3Float newGain);

    /** Sets the bandwidth in octaves. */
    void setBandwidth(o3Float newBandwidth);

    //---------------------------------------------------------------------------------------------
    // inquiry

    /** Sets the filter mode as one of the values in enum modes. */
    int getMode() const { return mode; }

    /** Returns the center frequency in Hz. */
    o3Float getFrequency() const { return frequency; }

    /** Returns the boost/cut gain in dB. */
    o3Float getGain() const { return gain; }

    /** Returns the bandwidth in octaves. */
    o3Float getBandwidth() const { return bandwidth; }

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

    // internal functions:
    void calcCoeffs();  // calculates filter coefficients from filter parameters

    o3Float b0, b1, b2, a1, a2;
    o3Float x1, x2, y1, y2;

    o3Float frequency, gain, bandwidth;
    o3Float sampleRate;
    int    mode;

  };

  //-----------------------------------------------------------------------------------------------
  // inlined functions:

  INLINE o3Float BiquadFilter::getSample(o3Float in)
  {
    // calculate the output sample:
    o3Float y = b0*in + b1*x1 + b2*x2 + a1*y1 + a2*y2 + TINY;

    // update the buffer variables:
    x2 = x1;
    x1 = in;
    y2 = y1;
    y1 = y;

    return y;
  }

} // end namespace rosic

#endif // rosic_BiquadFilter_h
