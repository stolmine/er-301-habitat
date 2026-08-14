#ifndef rosic_LeakyIntegrator_h
#define rosic_LeakyIntegrator_h

// Open303, (c) 2009 Robin Schmidt, MIT. See LICENSE-Open303.txt.
// habitat port: audio path converted o3Float -> o3Float (float).
// See planning/open303-port.md.

#include "o3_config.h"

// rosic-indcludes:
#include "rosic_RealFunctions.h"

namespace rosic
{

  /**

  This is a leaky integrator type lowpass filter with user adjustable time constant which is set 
  up in milliseconds.

  */

  class LeakyIntegrator  
  {

  public:

    //---------------------------------------------------------------------------------------------
    // construction/destruction:

    /** Constructor. */
    LeakyIntegrator();  

    /** Destructor. */
    ~LeakyIntegrator();  

    //---------------------------------------------------------------------------------------------
    // parameter settings:

    /** Sets the sample-rate. */
    void setSampleRate(o3Float newSampleRate);    

    /** Sets the time constant (tau), value is expected in milliseconds. */
    void setTimeConstant(o3Float newTimeConstant); 

    /** Sets the internal state of the filter to the passed value. */
    void setState(o3Float newState) { y1 = newState; }

    //---------------------------------------------------------------------------------------------
    // inquiry:

    /** Returns the time constant (tau) in milliseconds. */
    o3Float getTimeConstant() const { return tau; }

    /** Returns the normalizer, required to normalize the impulse response of a series connection 
    of two digital RC-type filters with time constants tau1 and tau2 (in milliseconds) to unity at 
    the given samplerate. */
    static o3Float getNormalizer(o3Float tau1, o3Float tau2, o3Float sampleRate);

    //---------------------------------------------------------------------------------------------
    // audio processing:

    /** Calculates one sample at a time. */
    INLINE o3Float getSample(o3Float in);

    //---------------------------------------------------------------------------------------------
    // others:

    /** Resets the internal state of the filter. */
    void reset();

    //=============================================================================================

  protected:

    /** Calculates the filter coefficient. */
    void calculateCoefficient();

    o3Float coeff;        // filter coefficient
    o3Float y1;           // previous output sample
    o3Float sampleRate;   // the samplerate
    o3Float tau;          // time constant in milliseconds

  };

  //-----------------------------------------------------------------------------------------------
  // inlined functions:

  INLINE o3Float LeakyIntegrator::getSample(o3Float in)
  {
    return y1 = in + coeff*(y1-in);
  }

} // end namespace rosic

#endif 
