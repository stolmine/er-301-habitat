// Open303, (c) 2009 Robin Schmidt, MIT. See LICENSE-Open303.txt.
// habitat port: audio path converted o3Float -> o3Float (float).
#include "rosic_LeakyIntegrator.h"
using namespace rosic;

//-------------------------------------------------------------------------------------------------
// construction/destruction:

LeakyIntegrator::LeakyIntegrator()
{
  sampleRate  = 44100.0f; 
  tau         = 10.0f;    
  y1          = 0.0;

  calculateCoefficient();
}

LeakyIntegrator::~LeakyIntegrator()
{

}

//-------------------------------------------------------------------------------------------------
// parameter settings:

void LeakyIntegrator::setSampleRate(o3Float newSampleRate)
{
  if( newSampleRate > 0.0 )
  {
    sampleRate = newSampleRate;
    calculateCoefficient();
  }
}

void LeakyIntegrator::setTimeConstant(o3Float newTimeConstant)
{
  if( newTimeConstant >= 0.0 && newTimeConstant != tau )
  {
    tau = newTimeConstant; 
    calculateCoefficient();
  }
}

//-------------------------------------------------------------------------------------------------
// inquiry:

o3Float LeakyIntegrator::getNormalizer(o3Float tau1, o3Float tau2, o3Float fs)
{
  o3Float td = 0.001*tau1;
  o3Float ta = 0.001*tau2;

  // catch some special cases:
  if( ta == 0.0 && td == 0.0 )
    return 1.0;
  else if( ta == 0.0 )
  {
    return 1.0 / (1.0-exp(-1.0/(fs*td)));
  }
  else if( td == 0.0 )
  {
    return 1.0 / (1.0-exp(-1.0/(fs*ta)));
  }

  // compute the filter coefficients:
  o3Float x  = exp( -1.0 / (fs*td)  );
  o3Float bd = 1-x;
  o3Float ad = -x;
  x         = exp( -1.0 / (fs*ta)  );
  o3Float ba = 1-x;
  o3Float aa = -x;

  // compute the location and height of the peak:
  o3Float xp;
  if( ta == td )
  {
    o3Float tp  = ta;
    o3Float np  = fs*tp;
    xp         = (np+1.0)*ba*ba*pow(aa, np);
  }
  else
  {
    o3Float tp  = log(ta/td) / ( (1.0/td) - (1.0/ta) );
    o3Float np  = fs*tp;
    o3Float s   = 1.0 / (aa-ad);
    o3Float b01 = s * aa*ba*bd;
    o3Float b02 = s * ad*ba*bd;
    o3Float a01 = s * (ad-aa)*aa;
    o3Float a02 = s * (ad-aa)*ad;
    xp         = b01*pow(a01, np) - b02*pow(a02, np);
  }

  // return the normalizer as reciprocal of the peak height:
  return 1.0/xp;
}

//-------------------------------------------------------------------------------------------------
// others:

void LeakyIntegrator::reset()
{
  y1 = 0;
}

void LeakyIntegrator::calculateCoefficient()
{
  if( tau > 0.0 )
    coeff = exp( -1.0 / (sampleRate*0.001*tau)  );
  else
    coeff = 0.0;
}

