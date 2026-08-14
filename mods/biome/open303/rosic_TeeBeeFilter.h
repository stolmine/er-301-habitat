#ifndef rosic_TeeBeeFilter_h
#define rosic_TeeBeeFilter_h

// Open303, (c) 2009 Robin Schmidt, MIT. See LICENSE-Open303.txt.
// habitat port: audio path converted o3Float -> o3Float (float).
// See planning/open303-port.md.

#include "o3_config.h"

// standard-library includes:
#include <stdlib.h>          // for the NULL macro

// rosic-indcludes:
#include "rosic_OnePoleFilter.h"

namespace rosic
{

  /**

  This class is a filter that aims to emulate the filter in the Roland TB 303. It's a variation of 
  the Moog ladder filter which includes a highpass in the feedback path that reduces the resonance
  on low cutoff frequencies. Moreover, it has a highpass and an allpass filter in the input path to
  pre-shape the input signal (important for the sonic character of internal and subsequent 
  nonlinearities).

  ...18 vs. 24 dB? blah?

  */

  class TeeBeeFilter
  {

  public:

    /** Enumeration of the available filter modes. */
    enum modes
    {
      FLAT = 0,
      LP_6,
      LP_12,
      LP_18,
      LP_24,
      HP_6,
      HP_12,
      HP_18,
      HP_24,
      BP_12_12,
      BP_6_18,
      BP_18_6,
      BP_6_12,
      BP_12_6,
      BP_6_6,
      TB_303,      // ala mystran & kunn (page 40 in the kvr-thread)

      NUM_MODES
    };

    //---------------------------------------------------------------------------------------------
    // construction/destruction:

    /** Constructor. */
    TeeBeeFilter();

    /** Destructor. */
    ~TeeBeeFilter();

    //---------------------------------------------------------------------------------------------
    // parameter settings:

    /** Sets the sample-rate for this filter. */
    void setSampleRate(o3Float newSampleRate);

    /** Sets the cutoff frequency for this filter - the actual coefficient calculation may be 
    supressed by passing 'false' as second parameter, in this case, it should be triggered
    manually later by calling calculateCoefficients. */
    INLINE void setCutoff(o3Float newCutoff, bool updateCoefficients = true);

    /** Sets the resonance in percent where 100% is self oscillation. */
    INLINE void setResonance(o3Float newResonance, bool updateCoefficients = true);

    /** Sets the input drive in decibels. */
    void setDrive(o3Float newDrive);

    /** Sets the mode of the filter, @see: modes */
    void setMode(int newMode);

    /** Sets the cutoff frequency for the highpass filter in the feedback path. */
    void setFeedbackHighpassCutoff(o3Float newCutoff) { feedbackHighpass.setCutoff(newCutoff); }

    //---------------------------------------------------------------------------------------------
    // inquiry:

    /** Returns the cutoff frequency of this filter. */
    o3Float getCutoff() const { return cutoff; }

    /** Returns the resonance parameter of this filter. */
    o3Float getResonance() const { return 100.0 * resonanceRaw; }

    /** Returns the drive parameter in decibels. */
    o3Float getDrive() const { return drive; }

    /** Returns the slected filter mode. */
    int getMode() const { return mode; }

    /** Returns the cutoff frequency for the highpass filter in the feedback path. */
    o3Float getFeedbackHighpassCutoff() const { return feedbackHighpass.getCutoff(); }

    //---------------------------------------------------------------------------------------------
    // audio processing:

    /** Calculates one output sample at a time. */
    INLINE o3Float getSample(o3Float in);

    //---------------------------------------------------------------------------------------------
    // others:

    /** Causes the filter to re-calculate the coeffiecients via the exact formulas. */
    INLINE void calculateCoefficientsExact();

    /** Causes the filter to re-calculate the coeffiecients using an approximation that is valid
    for normalized radian cutoff frequencies up to pi/4. */
    INLINE void calculateCoefficientsApprox4();

    /** Implements the waveshaping nonlinearity between the stages. */
    INLINE o3Float shape(o3Float x);

    /** Resets the internal state variables. */
    void reset();

    //=============================================================================================

  protected:

    o3Float b0, a1;              // coefficients for the first order sections
    o3Float y1, y2, y3, y4;      // output signals of the 4 filter stages 
    o3Float c0, c1, c2, c3, c4;  // coefficients for combining various ouput stages
    o3Float k;                   // feedback factor in the loop
    o3Float g;                   // output gain
    o3Float driveFactor;         // filter drive as raw factor
    o3Float cutoff;              // cutoff frequency
    o3Float drive;               // filter drive in decibels
    o3Float resonanceRaw;        // resonance parameter (normalized to 0...1)
    o3Float resonanceSkewed;     // mapped resonance parameter to make it behave more musical
    o3Float sampleRate;          // the sample rate in Hz
    o3Float twoPiOverSampleRate; // 2*PI/sampleRate
    int    mode;                // the selected filter-mode

    OnePoleFilter feedbackHighpass;

  };

  //-----------------------------------------------------------------------------------------------
  // inlined functions:

  INLINE void TeeBeeFilter::setCutoff(o3Float newCutoff, bool updateCoefficients)
  {
    if( newCutoff != cutoff )
    {
      if( newCutoff < 200.0 )  // an absolute floor for the cutoff frequency - tweakable
        cutoff = 200.0;  
      else if( newCutoff > 20000.0 )
        cutoff = 20000.0;
      else
        cutoff = newCutoff;

      if( updateCoefficients == true )
        calculateCoefficientsApprox4();
    }
  }

  INLINE void TeeBeeFilter::setResonance(o3Float newResonance, bool updateCoefficients)
  {
    resonanceRaw    = 0.01 * newResonance;
    resonanceSkewed = (1.0-exp(-3.0*resonanceRaw)) / (1.0-exp(-3.0));
    if( updateCoefficients == true )
      calculateCoefficientsApprox4();
  }

  INLINE void TeeBeeFilter::calculateCoefficientsExact()
  {
    // habitat port: this path is DEAD in this configuration - setCutoff() calls
    // calculateCoefficientsApprox4() directly - but it is kept for provenance.
    // Its locals stay double because sinCos/tan/exp here are double-only and
    // nothing calls this on the audio path.
    double wc = (double)twoPiOverSampleRate * (double)cutoff;
    double s, c;
    sinCos(wc, &s, &c);             // c = cos(wc); s = sin(wc);
    double t  = tan(0.25*(wc-PI));
    double r  = (double)resonanceSkewed;

    // calculate filter a1-coefficient tuned such the resonance frequency is just right:
    double a1_fullRes = t / (s-c*t);

    // calculate filter a1-coefficient as if there were no resonance:
    double x        = exp(-wc);
    double a1_noRes = -x;

    // use a weighted sum between the resonance-tuned and no-resonance coefficient:
    a1 = (o3Float)(r*a1_fullRes + (1.0-r)*a1_noRes);

    // calculate the b0-coefficient from the condition that each stage should be a leaky
    // integrator:
    b0 = 1.0f+a1;

    // calculate feedback factor by dividing the resonance parameter by the magnitude at the
    // resonant frequency:
    double gsq = (double)b0*(double)b0 / (1.0 + (double)a1*(double)a1 + 2.0*(double)a1*c);
    k          = (o3Float)(r / (gsq*gsq));

    if( mode == TB_303 )
      k *= (17.0/4.0);
  }

  INLINE void TeeBeeFilter::calculateCoefficientsApprox4()
  {
    // calculate intermediate variables:
    o3Float wc  = twoPiOverSampleRate * cutoff;
    o3Float wc2 = wc*wc;
    o3Float r   = resonanceSkewed;
    o3Float tmp;

    // habitat port: TB_303 is the shipped mode and it takes this branch FIRST,
    // because the two polynomials below are dead work in that mode - the branch
    // at the bottom overwrote b0, k and g, and the TB_303 getSample() never
    // reads a1. That was a 12th-order and an 8th-order polynomial evaluated and
    // discarded on EVERY sample (this function is called per sample from
    // setCutoff, since the envelope moves the cutoff continuously).
    //
    // The constants are also f-suffixed now. They were bare doubles, which
    // promoted the whole fx chain to double precision - 31 f64 ops per sample
    // including a vdiv.f64, on a core with no double-precision NEON
    // (feedback_cortex_a8_no_double_in_hot_loops). Float costs ~1e-4 relative
    // on k, far below the fit's own ~0.35% tuning error, and the A/B is in
    // planning/open303-port.md.
    if( mode == TB_303 )
    {
      const o3Float fx = wc * (o3Float)(ONE_OVER_SQRT2/(2*PI));
      b0 = (0.00045522346f + 6.1922189f * fx) / (1.0f + 12.358354f * fx + 4.4156345f * (fx * fx));
      o3Float kk = fx*(fx*(fx*(fx*(fx*(fx+7198.6997f)-5837.7917f)-476.47308f)+614.95611f)+213.87126f)+16.998792f;
      g  = kk * 0.058823529411764705882f;  // 17 reciprocal
      g  = (g - 1.0f) * r + 1.0f;          // r is 0 to 1.0
      g  = (g * (1.0f + r));
      k  = kk * r;                         // k is ready now
      return;
    }

    // compute the filter coefficient via a 12th order polynomial approximation (polynomial 
    // evaluation is done with a Horner-rule alike scheme with nested quadratic factors in the hope
    // for potentially better parallelization compared to Horner's rule as is):
    const o3Float pa12 = -1.341281325101042e-02;
    const o3Float pa11 =  8.168739417977708e-02;
    const o3Float pa10 = -2.365036766021623e-01;
    const o3Float pa09 =  4.439739664918068e-01;
    const o3Float pa08 = -6.297350825423579e-01;
    const o3Float pa07 =  7.529691648678890e-01;
    const o3Float pa06 = -8.249882473764324e-01;
    const o3Float pa05 =  8.736418933533319e-01;
    const o3Float pa04 = -9.164580250284832e-01;
    const o3Float pa03 =  9.583192455599817e-01;
    const o3Float pa02 = -9.999994950291231e-01;
    const o3Float pa01 =  9.999999927726119e-01;
    const o3Float pa00 = -9.999999999857464e-01;
    tmp  = wc2*pa12 + pa11*wc + pa10;
    tmp  = wc2*tmp  + pa09*wc + pa08;
    tmp  = wc2*tmp  + pa07*wc + pa06;
    tmp  = wc2*tmp  + pa05*wc + pa04;
    tmp  = wc2*tmp  + pa03*wc + pa02;
    a1   = wc2*tmp  + pa01*wc + pa00;
    b0   = 1.0 + a1;

    // compute the scale factor for the resonance parameter (the factor to obtain k from r) via an
    // 8th order polynomial approximation:
    const o3Float pr8 = -4.554677015609929e-05;
    const o3Float pr7 = -2.022131730719448e-05;
    const o3Float pr6 =  2.784706718370008e-03;
    const o3Float pr5 =  2.079921151733780e-03;
    const o3Float pr4 = -8.333236384240325e-02;
    const o3Float pr3 = -1.666668203490468e-01;
    const o3Float pr2 =  1.000000012124230e+00;
    const o3Float pr1 =  3.999999999650040e+00;
    const o3Float pr0 =  4.000000000000113e+00;
    tmp  = wc2*pr8 + pr7*wc + pr6;
    tmp  = wc2*tmp + pr5*wc + pr4;
    tmp  = wc2*tmp + pr3*wc + pr2;
    tmp  = wc2*tmp + pr1*wc + pr0; // this is now the scale factor
    k    = r * tmp;
    g    = 1.0;
  }

  INLINE o3Float TeeBeeFilter::shape(o3Float x)
  {
    // return tanhApprox(x); // \todo: find some more suitable nonlinearity here
    //return x; // test

    const o3Float r6 = 1.0/6.0;
    x = clip(x, (o3Float)-SQRT2, (o3Float)SQRT2);
    return x - r6*x*x*x;

    //return clip(x, -1.0, 1.0);
  }

  INLINE o3Float TeeBeeFilter::getSample(o3Float in)
  {
    o3Float y0;

    if( mode == TB_303 )
    {
      //y0  = in - feedbackHighpass.getSample(k * shape(y4));  
      y0 = in - feedbackHighpass.getSample(k*y4);  
      //y0  = in - k*shape(y4);  
      //y0  = in-k*y4;  
      y1 += 2*b0*(y0-y1+y2);
      y2 +=   b0*(y1-2*y2+y3);
      y3 +=   b0*(y2-2*y3+y4);
      y4 +=   b0*(y3-2*y4);
      return 2*g*y4;
      //return 3*y4;
    }

    // apply drive and feedback to obtain the filter's input signal:
    //o3Float y0 = inputFilter.getSample(0.125*driveFactor*in) - feedbackHighpass.getSample(k*y4);
    y0 = 0.125*driveFactor*in - feedbackHighpass.getSample(k*y4);  

    /*
    // cascade of four 1st order sections with nonlinearities:
    y1 = shape(b0*y0 - a1*y1);
    y2 = shape(b0*y1 - a1*y2);
    y3 = shape(b0*y2 - a1*y3);
    y4 = shape(b0*y3 - a1*y4);
    */

    // cascade of four 1st order sections with only 1 nonlinearity:
    /*
    y1 =       b0*y0 - a1*y1;
    y2 =       b0*y1 - a1*y2;
    y3 =       b0*y2 - a1*y3;
    y4 = shape(b0*y3 - a1*y4);
    */    
    y1 = y0 + a1*(y0-y1);
    y2 = y1 + a1*(y1-y2);
    y3 = y2 + a1*(y2-y3);
    y4 = y3 + a1*(y3-y4); // \todo: performance test both versions of the ladder
    //y4 = shape(y3 + a1*(y3-y4)); // \todo: performance test both versions of the ladder

    return 8.0 * (c0*y0 + c1*y1 + c2*y2 + c3*y3 + c4*y4);;
  }

}

#endif // rosic_TeeBeeFilter_h
