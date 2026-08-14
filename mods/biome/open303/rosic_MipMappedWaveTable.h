#ifndef rosic_MipMappedWaveTable_h
#define rosic_MipMappedWaveTable_h

// Open303, (c) 2009 Robin Schmidt, MIT. See LICENSE-Open303.txt.
//
// habitat port changes (2026-08-10, planning/open303-port.md):
//   - the mip set is a SHRINKING PYRAMID stored in float, not 12 full-length
//     double tables: 385 KB for the pair -> ~33 KB, which is what gets the
//     engine inside the am335x 256 KB L2. Level t is band-limited to harmonics
//     below 1024/2^t by generateMipMap(), so it needs only 2048>>t samples;
//     decimating the full-length band-limited level down to that length is
//     exact, not lossy.
//   - GENERATION stays in double (prototype table, FFT, per-level render). It
//     runs once at insert, off the hot path, and keeping it exact preserves
//     table fidelity. Only the stored result is float.
//   - scratch buffers are function-local statics, never stack locals: a 2052
//     double buffer is ~16 KB and insert-time paths are not guaranteed the
//     32 KB app stack (feedback_draw_path_busy_stack).

// rosic-indcludes:
#include "o3_config.h"
#include "rosic_FunctionTemplates.h"
#include "rosic_FourierTransformerRadix2.h"

namespace rosic
{

  /**

  This is a class for generating and storing a single-cycle-waveform in a lookup-table and
  retrieving values form it at arbitrary positions by means of interpolation.

  */

  class MipMappedWaveTable
  {

    // Oscillator classes need access to certain protected member-variables
    // (namely the tableLength and related quantities), so we declare them as friend-classes:
    friend class BlendOscillator;

  public:

    enum waveforms
    {
      SILENCE = 0,
      SINE,
      TRIANGLE,
      SQUARE,
      SAW,
      SQUARE303,
      SAW303
    };

    //---------------------------------------------------------------------------------------------
    // pyramid geometry (habitat port)

    static const int tableLength = 2048;  // length of mip level 0
    static const int numTables   = 12;

    /** Shortest level we bother storing. The top levels hold no harmonics at
    all (level 11 is silent), so there is nothing left to represent, but the
    interpolator still wants a few samples plus its guard. */
    static const int kMinLevelLen = 8;

    /** Guard samples appended to each level so linear interpolation at the last
    sample needs no wrap test. */
    static const int kGuard = 4;

    /** Samples stored for mip level t. */
    static int levelLength(int t)
    {
      int len = tableLength >> t;
      return (len < kMinLevelLen) ? kMinLevelLen : len;
    }

    //---------------------------------------------------------------------------------------------
    // construction/destruction:

    /** Constructor. */
    MipMappedWaveTable();

    /** Destructor. */
    ~MipMappedWaveTable();

    //---------------------------------------------------------------------------------------------
    // parmeter-settings:

    /** Selects a waveform from the set of built-in wavforms. The object generates the
    prototype-waveform by some algorithmic rules and renders various bandlimited version of it via
    FFT/iFFT. */
    void setWaveform(int newWaveform);

    /** Sets the time symmetry between the first and second half-wave (as value between 0...1) -
    for a square wave, this is also known as pulse-width. Currently only implemented for square and
    saw waveforms. */
    void setSymmetry(double newSymmetry);

    // internal 'back-panel' parameters:

    /** Sets the drive (in dB) for the tanh-shaper for 303-square waveform - internal parameter, to
    be scrapped eventually. */
    void setTanhShaperDriveFor303Square(double newDrive)
    { tanhShaperFactor = dB2amp(newDrive); fillWithSquare303(); }

    /** Sets the offset (as raw value for the tanh-shaper for 303-square waveform - internal
    parameter, to be scrapped eventually. */
    void setTanhShaperOffsetFor303Square(double newOffset)
    { tanhShaperOffset = newOffset; fillWithSquare303(); }

    /** Sets the phase shift of tanh-shaped square wave with respect to the saw-wave (in degrees)
    - this is important when the two are mixed. */
    void set303SquarePhaseShift(double newShift)
    { squarePhaseShift = newShift; fillWithSquare303(); }

    //---------------------------------------------------------------------------------------------
    // inquiry:

    double getTanhShaperDriveFor303Square() const { return amp2dB(tanhShaperFactor); }
    double getTanhShaperOffsetFor303Square() const { return tanhShaperOffset; }
    double get303SquarePhaseShift() const { return squarePhaseShift; }

    //---------------------------------------------------------------------------------------------
    // audio processing:

    /** Returns the value at 'phaseIndex' of mip level 'tableIndex' with linear interpolation.
    phaseIndex is always in LEVEL 0 UNITS, i.e. [0, tableLength) - the per-level rescale happens in
    here, so callers do not need to know the pyramid geometry. */
    O3_ALWAYS_INLINE o3Float getValueLinear(o3Float phaseIndex, int tableIndex) const;

  protected:

    // functions to fill table with the built-in waveforms (these functions are
    // called from setWaveform(int newWaveform):
    void fillWithSine();
    void fillWithTriangle();
    void fillWithSquare();
    void fillWithSaw();
    void fillWithSquare303();
    void fillWithSaw303();

    void initPrototypeTable();
    void initTableSet();
    void removeDC();
    void normalize();

    /** Renders the prototype waveform and generates the mip-map from that. */
    void renderWaveform();

    void generateMipMap();
      // generates a multisample from the prototype table, where each of the
      // successive tables contains one half of the spectrum of the previous one

    /** Stores one full-length band-limited level into the pyramid, decimating to that level's
    length. Exact: level t carries no harmonic at or above its own Nyquist. */
    void storeLevel(int t, const double *fullLength);

    /** Fills in mLevelOffset / mLevelLen / mLevelPhaseScale. */
    void buildPyramidGeometry();

    double symmetry; // symmetry between 1st and 2nd half-wave

    int    waveform;   // index of the currently chosen native waveform
    double sampleRate; // the sampleRate

    double prototypeTable[tableLength + kGuard];

    // --- the pyramid (habitat port) ---
    // Flat storage, one level after another. Level t occupies
    // [mLevelOffset[t], mLevelOffset[t] + levelLength(t) + kGuard).
    // Level lengths: 2048 1024 512 256 128 64 32 16 8 8 8 8 = 4112 samples.
    static const int kTotalLen = 4112 + numTables * kGuard;

    float mTable[kTotalLen];
    int   mLevelOffset[numTables];
    int   mLevelLen[numTables];
    float mLevelPhaseScale[numTables]; // levelLen / tableLength

    // embedded objects:
    FourierTransformerRadix2 fourierTransformer;

    // internal parameters:
    double tanhShaperFactor, tanhShaperOffset, squarePhaseShift;

  };

  //-----------------------------------------------------------------------------------------------
  // inlined functions:

  O3_ALWAYS_INLINE o3Float MipMappedWaveTable::getValueLinear(o3Float phaseIndex, int tableIndex) const
  {
    if( tableIndex < 0 )
      tableIndex = 0;
    else if( tableIndex >= numTables )
      tableIndex = numTables - 1;

    // level-0 phase -> this level's phase
    o3Float p = phaseIndex * mLevelPhaseScale[tableIndex];

    int     i    = (int) p;
    o3Float frac = p - (o3Float) i;

    // Defensive clamp. p comes from an already-wrapped phase so i should always
    // be in range, but a float rounding edge can land exactly on the level
    // length - the same one-past-the-end ulp case that bit the shipped readTap
    // (feedback_multitap_idx_wrap_ulp). The guard samples cover i+1.
    const int len = mLevelLen[tableIndex];
    if( i >= len )
    {
      i    = len - 1;
      frac = 0.0f;
    }
    else if( i < 0 )
    {
      i    = 0;
      frac = 0.0f;
    }

    const float *t = mTable + mLevelOffset[tableIndex];
    return t[i] + frac * (t[i+1] - t[i]);
  }

} // end namespace rosic

#endif // rosic_MipMappedWaveTable_h
