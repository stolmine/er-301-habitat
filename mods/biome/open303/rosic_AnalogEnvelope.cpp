// Open303, (c) 2009 Robin Schmidt, MIT. See LICENSE-Open303.txt.
// habitat port: audio path converted o3Float -> o3Float (float).
#include "rosic_AnalogEnvelope.h"
using namespace rosic;

//-------------------------------------------------------------------------------------------------
// construction/destruction:

AnalogEnvelope::AnalogEnvelope()
{
  sampleRate     = 44100.0;
  startLevel     = 0.0;
  attackTime     = 0.0;
  peakLevel      = 1.0;
  holdTime       = 0.0;
  decayTime      = 0.1;
  sustainLevel   = 0.5;
  releaseTime    = 0.01;
  endLevel       = 0.0;
  time           = 0.0;
  timeScale      = 1.0;
  peakByVel      = 1.0;
  peakByKey      = 1.0;
  timeScaleByVel = 1.0;
  timeScaleByKey = 1.0;
  increment      = 1000.0*timeScale/sampleRate;
  tauScale       = 1.0;
  peakScale      = 1.0;
  noteIsOn       = false;
  outputIsZero   = true;

  previousOutput = 0.0;

  // call these functions to trigger the coefficient calculations:
  setAttack(attackTime);
  setDecay(decayTime);
  setRelease(releaseTime);
}

AnalogEnvelope::~AnalogEnvelope()
{

}

//-------------------------------------------------------------------------------------------------
// parameter settings:

void AnalogEnvelope::setSampleRate(o3Float newSampleRate)
{
  if( newSampleRate > 0.0 )
    sampleRate = newSampleRate;

  // adjust time increment:
  increment = 1000.0*timeScale/sampleRate;

  //re-calculate coefficients for the 3 filters:
  setAttack (attackTime);
  setDecay  (decayTime);
  setRelease(releaseTime);
}

void AnalogEnvelope::setAttack(o3Float newAttackTime)
{
  if( newAttackTime > 0.0 )
  {
    attackTime  = newAttackTime;
    o3Float tau  = (sampleRate*0.001*attackTime) * tauScale/timeScale;
    attackCoeff = 1.0 - exp( -1.0 / tau );
  }
  else // newAttackTime <= 0
  {
    attackTime  = 0.0;
    attackCoeff = 1.0;
  }
  calculateAccumulatedTimes();
}

void AnalogEnvelope::setHold(o3Float newHoldTime)
{
  if( newHoldTime >= 0 )
    holdTime = newHoldTime;
  calculateAccumulatedTimes();
}

void AnalogEnvelope::setDecay(o3Float newDecayTime)
{
  if( newDecayTime > 0.0 )
  {
    decayTime  = newDecayTime;
    o3Float tau = (sampleRate*0.001*decayTime) * tauScale/timeScale;
    decayCoeff = 1.0 - exp( -1.0 / tau  );
  }
  else // newDecayTime <= 0
  {
    decayTime  = 0.0;
    decayCoeff = 1.0;
  }
  calculateAccumulatedTimes();
}

void AnalogEnvelope::setRelease(o3Float newReleaseTime)
{
  if( newReleaseTime > 0.0 )
  {
    releaseTime  = newReleaseTime;
    o3Float tau   = (sampleRate*0.001*releaseTime) * tauScale/timeScale;
    releaseCoeff = 1.0 - exp( -1.0 / tau  );
  }
  else // newReleaseTime <= 0
  {
    releaseTime  = 0.0;
    releaseCoeff = 1.0;
  }
  calculateAccumulatedTimes();
}

void AnalogEnvelope::setTimeScale(o3Float newTimeScale)
{
  if( newTimeScale > 0 )
    timeScale = newTimeScale;

  increment  = 1000.0*timeScale/sampleRate;

  //re-calculate coefficients for the 3 filters:
  setAttack (attackTime);
  setDecay  (decayTime);
  setRelease(releaseTime);
}

void AnalogEnvelope::setTauScale(o3Float newTauScale)
{
  if( newTauScale > 0 )
    tauScale = newTauScale;

  setAttack(attackTime);
  setDecay(decayTime);
  setRelease(releaseTime);
}

void AnalogEnvelope::setPeakScale(o3Float newPeakScale)
{
  if( newPeakScale > 0 )
    peakScale = newPeakScale;
}

//-------------------------------------------------------------------------------------------------
// others:

void AnalogEnvelope::reset()
{
  time = 0.0;
}

void AnalogEnvelope::noteOn(bool startFromCurrentLevel, int newKey, int newVel)
{
  if( !startFromCurrentLevel )
    previousOutput = startLevel;  // may lead to clicks


  // \todo: calculate key and velocity scale factors for duration and peak-value...


  // reset time for the new note:
  time         = 0.0;
  noteIsOn     = true;
  outputIsZero = false;
}

void AnalogEnvelope::noteOff()
{
  noteIsOn = false;

  // advance time to the beginnig of the release phase:
  time = (attackTime + holdTime + decayTime + increment);
}

bool AnalogEnvelope::endIsReached()
{
  //return false; // test

  if( noteIsOn == false && previousOutput < 0.000001 )
    return true;
  else
    return false;
}

//-------------------------------------------------------------------------------------------------
// internal functions:

void AnalogEnvelope::calculateAccumulatedTimes()
{
  attPlusHld               = attackTime + holdTime;
  attPlusHldPlusDec        = attPlusHld + decayTime;
  attPlusHldPlusDecPlusRel = attPlusHldPlusDec + releaseTime;
}
