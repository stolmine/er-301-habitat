// house::BrightAmbience3
//
// Airwindows BrightAmbience3 (Chris Johnson, MIT) port. Sparse-
// prime-tap delay summation reverb with resonant SVF feedback.
// Fourth AW atom in the house package after kWoodRoom, WoodenBox,
// CreamCoat. First non-FDN atom: structurally a windowed sum over
// 488 prime offsets into a 32768-sample circular buffer.
//
// PHASE 1 HYBRID FLOAT (per the post-CreamCoat default):
//   - pL/pR (sparse-tap state): `float` -- 256 KB stereo, fits L2
//     (full double would be 512 KB, blowing the 256 KB Cortex-A8
//     L2 cache 2x and pushing every sparse read out to L3/DRAM)
//   - lastRefL/R (interpolation tail): `float`
//   - feedbackA/B: `float`
//   - figureL/R (SVF state + coeffs): KEEP `double` -- per-sample
//     IIR feedback path is precision-sensitive; figureL/R is only
//     144 B total
//   - Block-rate scalars (overallscale, feedbackAmount, wet, K,
//     norm): KEEP `double`
//
// Hybrid float is not just a CPU optimization here -- it is
// MEMORY-NECESSARY. Full double exceeds L2 and would make every
// sparse tap read miss cache.
//
// LOAD-BEARING from AW source (preserved literally per
// feedback_identical_means_identical):
//   - mCycle init to 0, mGcount init to 32767 (descends)
//   - Cross-coupled feedback: L input adds feedbackB, R input
//     adds feedbackA (NOT a typo -- the cross is the stereo image)
//   - cycleEnd ladder (4/3/2/1) uses sequential if blocks, NOT
//     switch -- preserve as written
//   - Multi-pole averaging is a switch fallthrough (cases 4/3/2/1
//     each adding one averaging pass); preserve fallthrough
//     semantics literally
//
// Dropped per template:
//   - Per-sample 32-bit dither (frexpf + pow per sample)
//   - rand()-seeded fpdL/fpdR
//   - VST host deps
//
// Block-rate hoisting (one cycle ahead of source):
//   - cbrt(length) hoisted out of inner loop; stored as
//     `normalizer = 1.0 / cbrt(length)` and multiplied (one
//     divide and one cbrt saved per cycle)
//
// CPU sensitivity: the inner sparse-tap sum runs `length` times
// per cycle. At default Size=0.5 length is ~122 taps per side per
// cycle. At Size=1.0 length is 487 taps. This is the headline CPU
// dial -- ship faithful, document, revisit after hardware
// audition. Hardware CPU floor projection per the plan doc:
// ~20-30% stereo at default; ~50-70% at Size=1.0.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace house
{

  // Prime-offset tap arrays verbatim from AW BrightAmbience3.h
  // lines 26-27. Range 5..32719 (i.e. < 32768, so single-step
  // wrap math works for the circular buffer).
  static const int ba3_primeL[490] = {
    5, 5, 13, 17, 37, 41, 61, 67, 89, 109, 131, 157, 181, 191, 223, 241, 281, 283, 337, 353,
    373, 401, 433, 461, 521, 547, 569, 587, 601, 617, 719, 739, 787, 797, 863, 877, 929, 967,
    997, 1031, 1069, 1087, 1163, 1171, 1213, 1217, 1301, 1409, 1439, 1447, 1481, 1499, 1531,
    1597, 1627, 1669, 1733, 1741, 1789, 1823, 1861, 1913, 2029, 2063, 2083, 2099, 2237, 2269,
    2347, 2351, 2383, 2417, 2503, 2549, 2617, 2647, 2687, 2719, 2753, 2803, 2903, 2909, 3011,
    3019, 3079, 3109, 3181, 3229, 3271, 3299, 3323, 3407, 3491, 3517, 3571, 3593, 3643, 3733,
    3767, 3911, 3947, 4027, 4093, 4133, 4157, 4217, 4283, 4339, 4409, 4421, 4481, 4517, 4561,
    4567, 4673, 4759, 4789, 4801, 4889, 4933, 4951, 5021, 5077, 5107, 5197, 5281, 5387, 5441,
    5507, 5557, 5639, 5651, 5711, 5749, 5807, 5851, 5879, 6037, 6121, 6217, 6247, 6311, 6329,
    6353, 6367, 6469, 6607, 6653, 6673, 6691, 6827, 6841, 6869, 6899, 7069, 7109, 7207, 7283,
    7369, 7417, 7487, 7523, 7621, 7649, 7703, 7753, 7853, 7883, 8017, 8059, 8111, 8117, 8231,
    8233, 8291, 8377, 8419, 8513, 8537, 8581, 8731, 8747, 8779, 8807, 8861, 8923, 9001, 9041,
    9109, 9293, 9323, 9403, 9463, 9539, 9623, 9661, 9743, 9833, 9871, 9923, 10007, 10009,
    10091, 10169, 10271, 10433, 10459, 10487, 10567, 10589, 10639, 10663, 10691, 10723, 10859,
    10861, 10937, 11257, 11317, 11369, 11467, 11633, 11777, 11867, 11923, 11927, 11959, 12007,
    12101, 12113, 12149, 12203, 12323, 12409, 12433, 12457, 12487, 12503, 12553, 12647, 12781,
    12841, 12967, 13003, 13043, 13103, 13177, 13217, 13307, 13331, 13477, 13513, 13597, 13613,
    13669, 13693, 13711, 13757, 13873, 14051, 14143, 14159, 14197, 14437, 14489, 14503, 14593,
    14713, 14731, 14783, 14869, 14923, 14983, 15061, 15233, 15271, 15307, 15313, 15427, 15511,
    15643, 15683, 15859, 15973, 16063, 16073, 16097, 16127, 16183, 16253, 16417, 16451, 16529,
    16693, 16729, 16901, 16927, 17117, 17191, 17291, 17341, 17377, 17389, 17417, 17489, 17539,
    17657, 17659, 17783, 17911, 17989, 18049, 18169, 18181, 18223, 18229, 18313, 18433, 18451,
    18617, 18671, 18719, 18773, 18787, 18919, 19013, 19219, 19433, 19469, 19501, 19583, 19759,
    19793, 19819, 19919, 20047, 20071, 20107, 20173, 20231, 20323, 20341, 20443, 20477, 20731,
    20759, 20789, 20873, 20903, 20959, 21101, 21149, 21187, 21191, 21277, 21317, 21383, 21493,
    21557, 21587, 21737, 21757, 21821, 21937, 22031, 22067, 22109, 22367, 22567, 22651, 22727,
    22751, 22817, 22853, 22921, 23087, 23227, 23251, 23447, 23539, 23567, 23669, 23813, 23887,
    23909, 23929, 24023, 24071, 24109, 24137, 24151, 24203, 24251, 24391, 24419, 24443, 24509,
    24659, 24671, 24793, 24859, 24919, 25057, 25169, 25301, 25309, 25357, 25411, 25423, 25603,
    25733, 25771, 25841, 25931, 25969, 26017, 26189, 26267, 26371, 26431, 26489, 26597, 26693,
    26801, 26921, 26959, 27017, 27077, 27091, 27449, 27457, 27583, 27689, 27737, 27809, 27851,
    27943, 28069, 28109, 28283, 28307, 28403, 28573, 28649, 28657, 28813, 29101, 29147, 29153,
    29287, 29333, 29387, 29483, 29573, 29641, 29717, 29803, 30089, 30091, 30119, 30133, 30259,
    30557, 30593, 30661, 30713, 30781, 30839, 30869, 30893, 31033, 31079, 31181, 31193, 31267,
    31307, 31489, 31517, 31667, 31741, 32003, 32159, 32233, 32297, 32299, 32327, 32341, 32537,
    32603, 32749
  };

  static const int ba3_primeR[490] = {
    3, 7, 11, 19, 31, 43, 59, 71, 83, 113, 127, 163, 179, 193, 211, 251, 277, 293, 331, 359,
    367, 409, 431, 463, 509, 557, 563, 593, 599, 619, 709, 743, 773, 809, 859, 881, 919, 971,
    991, 1033, 1063, 1091, 1153, 1181, 1201, 1223, 1297, 1423, 1433, 1451, 1471, 1511, 1523,
    1601, 1621, 1693, 1723, 1747, 1787, 1831, 1847, 1931, 2027, 2069, 2081, 2111, 2221, 2273,
    2341, 2357, 2381, 2423, 2477, 2551, 2609, 2657, 2683, 2729, 2749, 2819, 2897, 2917, 3001,
    3023, 3067, 3119, 3169, 3251, 3259, 3301, 3319, 3413, 3469, 3527, 3559, 3607, 3637, 3739,
    3761, 3917, 3943, 4049, 4091, 4139, 4153, 4219, 4273, 4349, 4397, 4423, 4463, 4519, 4549,
    4583, 4663, 4783, 4787, 4813, 4877, 4937, 4943, 5023, 5059, 5113, 5189, 5297, 5381, 5443,
    5503, 5563, 5623, 5653, 5701, 5779, 5801, 5857, 5869, 6043, 6113, 6221, 6229, 6317, 6323,
    6359, 6361, 6473, 6599, 6659, 6661, 6701, 6823, 6857, 6863, 6907, 7057, 7121, 7193, 7297,
    7351, 7433, 7481, 7529, 7607, 7669, 7699, 7757, 7841, 7901, 8011, 8069, 8101, 8123, 8221,
    8237, 8287, 8387, 8389, 8521, 8527, 8597, 8719, 8753, 8761, 8819, 8849, 8929, 8999, 9043,
    9103, 9311, 9319, 9413, 9461, 9547, 9619, 9677, 9739, 9839, 9859, 9929, 9973, 10037, 10079,
    10177, 10267, 10453, 10457, 10499, 10559, 10597, 10631, 10667, 10687, 10729, 10853, 10867,
    10909, 11261, 11311, 11383, 11447, 11657, 11743, 11887, 11909, 11933, 11953, 12011, 12097,
    12119, 12143, 12211, 12301, 12413, 12421, 12473, 12479, 12511, 12547, 12653, 12763, 12853,
    12959, 13007, 13037, 13109, 13171, 13219, 13297, 13337, 13469, 13523, 13591, 13619, 13649,
    13697, 13709, 13759, 13859, 14057, 14107, 14173, 14177, 14447, 14479, 14519, 14591, 14717,
    14723, 14797, 14867, 14929, 14969, 15073, 15227, 15277, 15299, 15319, 15413, 15527, 15641,
    15727, 15823, 15991, 16061, 16087, 16091, 16139, 16141, 16267, 16411, 16453, 16519, 16699,
    16703, 16903, 16921, 17123, 17189, 17293, 17333, 17383, 17387, 17419, 17483, 17551, 17627,
    17669, 17761, 17921, 17987, 18059, 18149, 18191, 18217, 18233, 18311, 18439, 18443, 18637,
    18661, 18731, 18757, 18793, 18917, 19031, 19213, 19441, 19463, 19507, 19577, 19763, 19777,
    19841, 19913, 20051, 20063, 20113, 20161, 20233, 20297, 20347, 20441, 20479, 20719, 20771,
    20773, 20879, 20899, 20963, 21089, 21157, 21179, 21193, 21269, 21319, 21379, 21499, 21529,
    21589, 21727, 21767, 21817, 21943, 22027, 22073, 22093, 22369, 22549, 22669, 22721, 22769,
    22811, 22859, 22907, 23099, 23209, 23269, 23431, 23549, 23563, 23671, 23801, 23893, 23899,
    23957, 24019, 24077, 24107, 24133, 24169, 24197, 24281, 24379, 24421, 24439, 24517, 24631,
    24677, 24781, 24877, 24917, 25073, 25163, 25303, 25307, 25367, 25409, 25439, 25601, 25741,
    25763, 25847, 25919, 25981, 26003, 26203, 26263, 26387, 26423, 26497, 26591, 26699, 26783,
    26927, 26953, 27031, 27073, 27103, 27437, 27479, 27581, 27691, 27733, 27817, 27847, 27947,
    28057, 28111, 28279, 28309, 28393, 28579, 28643, 28661, 28807, 29123, 29137, 29167, 29269,
    29339, 29383, 29501, 29569, 29663, 29683, 29819, 30071, 30097, 30113, 30137, 30253, 30559,
    30577, 30671, 30707, 30803, 30829, 30871, 30881, 31039, 31069, 31183, 31189, 31271, 31277,
    31511, 31513, 31687, 31729, 32009, 32143, 32237, 32261, 32303, 32323, 32353, 32533, 32609,
    32719
  };

  static const int kBA3BufLen = 32768;
  static const int kBA3BufMask = 32767; // for wrap math (buffer is power-of-two sized)

  class BrightAmbience3 : public od::Object
  {
  public:
    BrightAmbience3()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mPosition);
      addParameter(mSize);
      addParameter(mBrightness);
      addParameter(mWetness);

      memset(pL, 0, sizeof(pL));
      memset(pR, 0, sizeof(pR));
      memset(figureL, 0, sizeof(figureL));
      memset(figureR, 0, sizeof(figureR));
      memset(lastRefL, 0, sizeof(lastRefL));
      memset(lastRefR, 0, sizeof(lastRefR));
      mFeedbackA = 0.0f;
      mFeedbackB = 0.0f;
      mGcount = 32767;
      mCycle = 0;
    }

    virtual ~BrightAmbience3() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mPosition{"Position", 0.5f};
    od::Parameter mSize{"Size", 0.5f};
    od::Parameter mBrightness{"Brightness", 0.5f};
    od::Parameter mWetness{"Wetness", 0.5f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      const float A = mPosition.value();
      const float B = mSize.value();
      const float C = mBrightness.value();
      const float D = mWetness.value();

      double overallscale = 1.0;
      overallscale /= 44100.0;
      overallscale *= (double)globalConfig.sampleRate;

      int cycleEnd = (int)floor(overallscale);
      if (cycleEnd < 1) cycleEnd = 1;
      if (cycleEnd > 4) cycleEnd = 4;
      if (mCycle > cycleEnd - 1) mCycle = cycleEnd - 1;

      int start = (int)(A * 400.0f) + 88;
      int length = (int)(pow((double)B, 2.0) * 487.0) + 1;
      if (start + length > 488) start = 488 - length;
      double feedbackAmount = (double)C * 0.25;
      double wet = (double)D;

      // SVF coefficients (block-rate).
      figureL[0] = figureR[0] = 1000.0 / (double)globalConfig.sampleRate;
      figureL[1] = figureR[1] = pow((double)length * 0.037 * feedbackAmount, 2.0) + 0.01;
      double K = tan(M_PI * figureR[0]);
      double norm = 1.0 / (1.0 + K / figureR[1] + K * K);
      figureL[2] = figureR[2] = K / figureR[1] * norm;
      figureL[4] = figureR[4] = -figureR[2];
      figureL[5] = figureR[5] = 2.0 * (K * K - 1.0) * norm;
      figureL[6] = figureR[6] = (1.0 - K / figureR[1] + K * K) * norm;

      // Hoist cbrt out of inner loop (one-divide-per-cycle saved).
      double normalizer = 1.0 / cbrt((double)length);

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        double inputSampleL = *in1;
        double inputSampleR = *in2;
        if (fabs(inputSampleL) < 1.18e-23) inputSampleL = 1.18e-17;
        if (fabs(inputSampleR) < 1.18e-23) inputSampleR = 1.18e-17;
        double drySampleL = inputSampleL;
        double drySampleR = inputSampleR;

        mCycle++;
        if (mCycle == cycleEnd)
        {
          double tempL = 0.0;
          double tempR = 0.0;
          if (mGcount < 0 || mGcount > 32767) mGcount = 32767;
          int count = mGcount;

          pL[count] = (float)(inputSampleL + (double)mFeedbackB);
          pR[count] = (float)(inputSampleR + (double)mFeedbackA);

          for (int offset = start; offset < start + length; offset++)
          {
            int idxL = count + ba3_primeL[offset];
            if (idxL > 32767) idxL -= 32768;
            int idxR = count + ba3_primeR[offset];
            if (idxR > 32767) idxR -= 32768;
            tempL += (double)pL[idxL];
            tempR += (double)pR[idxR];
          }

          inputSampleL = tempL * normalizer;
          inputSampleR = tempR * normalizer;

          double tempSample = (inputSampleL * figureL[2]) + figureL[7];
          figureL[7] = -(tempSample * figureL[5]) + figureL[8];
          figureL[8] = (inputSampleL * figureL[4]) - (tempSample * figureL[6]);
          mFeedbackA = (float)(sin(tempSample) * feedbackAmount);

          tempSample = (inputSampleR * figureR[2]) + figureR[7];
          figureR[7] = -(tempSample * figureR[5]) + figureR[8];
          figureR[8] = (inputSampleR * figureR[4]) - (tempSample * figureR[6]);
          mFeedbackB = (float)(sin(tempSample) * feedbackAmount);

          mGcount--;

          if (cycleEnd == 4)
          {
            lastRefL[0] = lastRefL[4];
            lastRefL[2] = (float)((lastRefL[0] + inputSampleL) * 0.5);
            lastRefL[1] = (float)((lastRefL[0] + lastRefL[2]) * 0.5);
            lastRefL[3] = (float)((lastRefL[2] + inputSampleL) * 0.5);
            lastRefL[4] = (float)inputSampleL;
            lastRefR[0] = lastRefR[4];
            lastRefR[2] = (float)((lastRefR[0] + inputSampleR) * 0.5);
            lastRefR[1] = (float)((lastRefR[0] + lastRefR[2]) * 0.5);
            lastRefR[3] = (float)((lastRefR[2] + inputSampleR) * 0.5);
            lastRefR[4] = (float)inputSampleR;
          }
          if (cycleEnd == 3)
          {
            lastRefL[0] = lastRefL[3];
            lastRefL[2] = (float)((lastRefL[0] + lastRefL[0] + inputSampleL) / 3.0);
            lastRefL[1] = (float)((lastRefL[0] + inputSampleL + inputSampleL) / 3.0);
            lastRefL[3] = (float)inputSampleL;
            lastRefR[0] = lastRefR[3];
            lastRefR[2] = (float)((lastRefR[0] + lastRefR[0] + inputSampleR) / 3.0);
            lastRefR[1] = (float)((lastRefR[0] + inputSampleR + inputSampleR) / 3.0);
            lastRefR[3] = (float)inputSampleR;
          }
          if (cycleEnd == 2)
          {
            lastRefL[0] = lastRefL[2];
            lastRefL[1] = (float)((lastRefL[0] + inputSampleL) * 0.5);
            lastRefL[2] = (float)inputSampleL;
            lastRefR[0] = lastRefR[2];
            lastRefR[1] = (float)((lastRefR[0] + inputSampleR) * 0.5);
            lastRefR[2] = (float)inputSampleR;
          }
          if (cycleEnd == 1)
          {
            lastRefL[0] = (float)inputSampleL;
            lastRefR[0] = (float)inputSampleR;
          }
          mCycle = 0;
          inputSampleL = (double)lastRefL[mCycle];
          inputSampleR = (double)lastRefR[mCycle];
        }
        else
        {
          inputSampleL = (double)lastRefL[mCycle];
          inputSampleR = (double)lastRefR[mCycle];
        }

        // Multi-pole averaging tail. Switch fallthrough preserved
        // literally per source -- each case adds one averaging pass
        // against lastRef[5..7], with lastRef[8] as the scratch
        // slot.
        switch (cycleEnd)
        {
          case 4:
            lastRefL[8] = (float)inputSampleL;
            inputSampleL = (inputSampleL + (double)lastRefL[7]) * 0.5;
            lastRefL[7] = lastRefL[8];
            lastRefR[8] = (float)inputSampleR;
            inputSampleR = (inputSampleR + (double)lastRefR[7]) * 0.5;
            lastRefR[7] = lastRefR[8];
            // fallthrough
          case 3:
            lastRefL[8] = (float)inputSampleL;
            inputSampleL = (inputSampleL + (double)lastRefL[6]) * 0.5;
            lastRefL[6] = lastRefL[8];
            lastRefR[8] = (float)inputSampleR;
            inputSampleR = (inputSampleR + (double)lastRefR[6]) * 0.5;
            lastRefR[6] = lastRefR[8];
            // fallthrough
          case 2:
            lastRefL[8] = (float)inputSampleL;
            inputSampleL = (inputSampleL + (double)lastRefL[5]) * 0.5;
            lastRefL[5] = lastRefL[8];
            lastRefR[8] = (float)inputSampleR;
            inputSampleR = (inputSampleR + (double)lastRefR[5]) * 0.5;
            lastRefR[5] = lastRefR[8];
            // fallthrough
          case 1:
            break;
        }

        if (wet != 1.0)
        {
          inputSampleL = (inputSampleL * wet) + (drySampleL * (1.0 - wet));
          inputSampleR = (inputSampleR * wet) + (drySampleR * (1.0 - wet));
        }

        *out1 = (float)inputSampleL;
        *out2 = (float)inputSampleR;
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    // Sparse-tap circular buffers (float per hybrid; 256 KB stereo).
    float pL[kBA3BufLen];
    float pR[kBA3BufLen];
    // SVF state + coeffs (double, precision-sensitive IIR).
    double figureL[9];
    double figureR[9];
    // Multi-pole averaging tail (float; only 10 slots, but per-
    // sample reads -- float reads pipeline better on Cortex-A8).
    float lastRefL[10];
    float lastRefR[10];
    // Feedback taps (float; magnitude bounded by feedbackAmount*0.25).
    float mFeedbackA;
    float mFeedbackB;
    int mGcount;
    int mCycle;
#endif
  };

} // namespace house
