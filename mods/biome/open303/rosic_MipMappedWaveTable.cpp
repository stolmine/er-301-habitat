// Open303, (c) 2009 Robin Schmidt, MIT. See LICENSE-Open303.txt.
// habitat port: float shrinking-pyramid storage, double generation.
// See rosic_MipMappedWaveTable.h and planning/open303-port.md.

#include "rosic_MipMappedWaveTable.h"
using namespace rosic;

MipMappedWaveTable::MipMappedWaveTable()
{
  // init member variables:
  sampleRate = 44100.0;
  waveform   = 0;
  symmetry   = 0.5;

  // initialize internal 'back-panel' parameters
  tanhShaperFactor = dB2amp(36.9);
  tanhShaperOffset = 4.37;
  squarePhaseShift = 180.0;

  // set up the fourier-transformer:
  fourierTransformer.setBlockSize(tableLength);

  // initialize the buffers:
  buildPyramidGeometry();
  initPrototypeTable();
  initTableSet();
}

MipMappedWaveTable::~MipMappedWaveTable()
{

}

//-------------------------------------------------------------------------------------------------
// pyramid geometry (habitat port):

void MipMappedWaveTable::buildPyramidGeometry()
{
  int offset = 0;
  for(int t=0; t<numTables; t++)
  {
    const int len = levelLength(t);
    mLevelOffset[t]     = offset;
    mLevelLen[t]        = len;
    mLevelPhaseScale[t] = (float) len / (float) tableLength;
    offset += len + kGuard;
  }
  // If this ever trips, kTotalLen and levelLength() have drifted apart.
  rassert( offset == kTotalLen );
}

void MipMappedWaveTable::storeLevel(int t, const double *fullLength)
{
  const int len  = mLevelLen[t];
  const int step = tableLength / len;   // exact power of two by construction
  float *dst = mTable + mLevelOffset[t];

  // Plain decimation is exact here: generateMipMap() has already removed every
  // harmonic at or above this level's own Nyquist, so there is nothing left to
  // alias when we drop samples.
  for(int i=0; i<len; i++)
    dst[i] = (float) fullLength[i*step];

  // guard samples wrap the head of the level
  for(int g=0; g<kGuard; g++)
    dst[len+g] = dst[g];
}

//-------------------------------------------------------------------------------------------------
// internal functions:

void MipMappedWaveTable::initPrototypeTable()
{
  for(int i=0; i<(tableLength+kGuard); i++)
    prototypeTable[i] = 0.0;
}

void MipMappedWaveTable::initTableSet()
{
  for(int i=0; i<kTotalLen; i++)
    mTable[i] = 0.0f;
}

void MipMappedWaveTable::removeDC()
{
  // calculate DC-offset (= average value of the table):
  double dcOffset = 0.0;
  int i;
  for(i=0; i<tableLength; i++)
    dcOffset += prototypeTable[i];
  dcOffset = dcOffset / tableLength;

  // remove DC-Offset:
  for(i=0; i<tableLength; i++)
    prototypeTable[i] -= dcOffset;
}

void MipMappedWaveTable::normalize()
{
  // find maximum:
  double max = 0.0;
  int    i;
  for(i=0; i<tableLength; i++)
    if( fabs(prototypeTable[i]) > max)
      max = fabs(prototypeTable[i]);

  if( max <= 0.0 )
    return;

  // normalize to amplitude 1.0:
  double scale = 1.0/max;
  for(i=0; i<tableLength; i++)
    prototypeTable[i] *= scale;
}

void MipMappedWaveTable::renderWaveform()
{
  switch( waveform )
  {
  case   SINE:      fillWithSine();        break;
  case   TRIANGLE:  fillWithTriangle();    break;
  case   SQUARE:    fillWithSquare();      break;
  case   SAW:       fillWithSaw();         break;
  case   SQUARE303: fillWithSquare303();   break;
  case   SAW303:    fillWithSaw303();      break;

  default :  fillWithSine();
  }
}

void MipMappedWaveTable::generateMipMap()
{
  // Function-local statics, not stack locals: together these are ~32 KB and
  // insert-time paths are not guaranteed the 32 KB app stack. Construction is
  // single-threaded at insert, so sharing them across instances is safe.
  static double spectrum[tableLength];
  static double level[tableLength + kGuard];

  int t, i;

  // level 0 is the full-bandwidth prototype
  for(i=0; i<tableLength; i++)
    level[i] = prototypeTable[i];
  for(i=0; i<kGuard; i++)
    level[tableLength+i] = level[i];
  storeLevel(0, level);

  // get the spectrum from the prototype-table:
  fourierTransformer.transformRealSignal(prototypeTable, spectrum);

  // ensure that DC and Nyquist are zero:
  spectrum[0] = 0.0;
  spectrum[1] = 0.0;

  // now, render the bandlimited versions by successively shrinking the
  // spectrum by one octave and iFFT'ing this spectrum:
  int lowBin, highBin;
  for(t=1; t<numTables; t++)
  {
    lowBin  = tableLength >> t;       // the cutoff-bin
    highBin = tableLength >> (t-1);   // the bin up to which the spectrum is currently still nonzero

    // zero out the bins above the cutoff-bin:
    for(i=lowBin; i<highBin; i++)
      spectrum[i] = 0.0;

    // transform the truncated spectrum back to the time-domain, then decimate
    // it into the pyramid slot for this level
    fourierTransformer.transformSymmetricSpectrum(spectrum, level);
    storeLevel(t, level);
  }
}

//-------------------------------------------------------------------------------------------------
// parameter settings:

void MipMappedWaveTable::setWaveform(int newWaveform)
{
  if( (newWaveform >= 0) && (newWaveform != waveform) )
  {
    waveform = newWaveform;
    renderWaveform();
  }
}

void MipMappedWaveTable::setSymmetry(double newSymmetry)
{
  symmetry = newSymmetry;
  renderWaveform();
}

//-------------------------------------------------------------------------------------------------
// fill the prototype-table with various standard waveforms:

void MipMappedWaveTable::fillWithSine()
{
  for (long i=0; i<tableLength; i++)
    prototypeTable[i] = sin( (2.0*PI*i) / (double) (tableLength) );
  generateMipMap();
}

void MipMappedWaveTable::fillWithTriangle()
{
  int i;
  for (i=0; i<(tableLength/4); i++)
    prototypeTable[i] = (double)(4*i) / (double)(tableLength);

  for (i=(tableLength/4); i<(3*tableLength/4); i++)
    prototypeTable[i] = 2.0 - ((double)(4*i) / (double)(tableLength));

  for (i=(3*tableLength/4); i<(tableLength); i++)
    prototypeTable[i] = -4.0+ ((double)(4*i) / (double)(tableLength));

  generateMipMap();
}

void MipMappedWaveTable::fillWithSquare()
{
  int    N  = tableLength;
  double k  = symmetry;
  int    N1 = clip(roundToInt(k*(N-1)), 1, N-1);
  for(int n=0; n<N1; n++)
    prototypeTable[n] = +1.0;
  for(int n=N1; n<N; n++)
    prototypeTable[n] = -1.0;

  generateMipMap();
}

void MipMappedWaveTable::fillWithSaw()
{
  int    N  = tableLength;
  double k  = symmetry;
  int    N1 = clip(roundToInt(k*(N-1)), 1, N-1);
  int    N2 = N-N1;
  double s1 = 1.0 / (N1-1);
  double s2 = 1.0 / N2;
  for(int n=0; n<N1; n++)
    prototypeTable[n] = s1*n;
  for(int n=N1; n<N; n++)
    prototypeTable[n] = -1.0 + s2*(n-N1);

  generateMipMap();
}

void MipMappedWaveTable::fillWithSquare303()
{
  // generate the saw-wave:
  int    N  = tableLength;
  double k  = 0.5;
  int    N1 = clip(roundToInt(k*(N-1)), 1, N-1);
  int    N2 = N-N1;
  double s1 = 1.0 / (N1-1);
  double s2 = 1.0 / N2;
  for(int n=0; n<N1; n++)
    prototypeTable[n] = s1*n;
  for(int n=N1; n<N; n++)
    prototypeTable[n] = -1.0 + s2*(n-N1);

  // switch polarity and apply tanh-shaping with dc-offset:
  for(int n=0; n<N; n++)
    prototypeTable[n] = -tanh(tanhShaperFactor*prototypeTable[n] + tanhShaperOffset);

  // do a circular shift to phase-align with the saw-wave, when both waveforms are mixed:
  int nShift = roundToInt(N*squarePhaseShift/360.0);
  circularShift(prototypeTable, N, nShift);

  generateMipMap();
}

void MipMappedWaveTable::fillWithSaw303()
{
  // generate the saw-wave:
  int    N  = tableLength;
  double k  = 0.5;
  int    N1 = clip(roundToInt(k*(N-1)), 1, N-1);
  int    N2 = N-N1;
  double s1 = 1.0 / (N1-1);
  double s2 = 1.0 / N2;
  for(int n=0; n<N1; n++)
    prototypeTable[n] = s1*n;
  for(int n=N1; n<N; n++)
    prototypeTable[n] = -1.0 + s2*(n-N1);

  generateMipMap();
}
