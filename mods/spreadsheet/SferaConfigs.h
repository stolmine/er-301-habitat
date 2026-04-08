#pragma once

// Sfera Z-Plane filter configurations
// Pre-baked biquad coefficients for each config. Morphing interpolates
// coefficients directly (not pole/zero positions) for smooth transitions.

#include <math.h>

namespace stolmine
{

  static const int kSferaMaxSections = 7;

  struct BiquadCoeffs
  {
    float b0, b1, b2, a1, a2;
  };

  struct FilterConfig
  {
    BiquadCoeffs sections[kSferaMaxSections];
    int numSections;
    float gain;
    // Pole/zero positions stored for visualization only
    float poleAngle[kSferaMaxSections];
    float poleRadius[kSferaMaxSections];
    float zeroAngle[kSferaMaxSections];
    float zeroRadius[kSferaMaxSections];
  };

  struct MorphCube
  {
    int config[4]; // corners: [X0Y0, X1Y0, X0Y1, X1Y1]
    const char *name;
  };

  static const float kPi = 3.14159265f;

  // Convert pole/zero pair to biquad coefficients at init time
  static inline BiquadCoeffs pzToCoeffs(float pAngle, float pRadius,
                                         float zAngle, float zRadius)
  {
    BiquadCoeffs c;
    c.a1 = -2.0f * pRadius * cosf(pAngle);
    c.a2 = pRadius * pRadius;
    if (zRadius > 0.001f)
    {
      c.b0 = 1.0f;
      c.b1 = -2.0f * zRadius * cosf(zAngle);
      c.b2 = zRadius * zRadius;
    }
    else
    {
      c.b0 = 1.0f;
      c.b1 = 0.0f;
      c.b2 = 0.0f;
    }
    return c;
  }

  // Bypass section (identity)
  static inline BiquadCoeffs bypassCoeffs()
  {
    return {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  }

  // Helper: angle from Hz at 48kHz
  #define ANG(hz) (kPi * (hz) / 24000.0f)

  // Build a FilterConfig from pole/zero specs
  // Uses full cosf at init time (only called once)
  #define MAKE_PZ(pa, pr, za, zr) \
    pzToCoeffs(pa, pr, za, zr), pa, pr, za, zr

  // --- Configs are built at runtime in Sfera::Init() to avoid
  //     static initialization order issues with cosf ---
  // The config table is populated by initConfigs()

  static const int kNumConfigSpecs = 32;

  struct ConfigSpec
  {
    struct { float angle, radius; } poles[kSferaMaxSections];
    struct { float angle, radius; } zeros[kSferaMaxSections];
    int numSections;
    float gain;
  };

  static const ConfigSpec kConfigSpecs[] = {
    // 0: Bypass
    { {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 0, 1.0f },

    // --- Butterworth ---
    // 1: BW LP 2-pole
    { {{ANG(1000), 0.90f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{kPi, 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 1.0f },
    // 2: BW LP 4-pole
    { {{ANG(900), 0.92f},{ANG(1100), 0.92f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{kPi, 1.0f},{kPi, 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 1.0f },
    // 3: BW LP 6-pole
    { {{ANG(800), 0.93f},{ANG(1000), 0.93f},{ANG(1200), 0.93f},{0,0},{0,0},{0,0},{0,0}},
      {{kPi, 1.0f},{kPi, 1.0f},{kPi, 1.0f},{0,0},{0,0},{0,0},{0,0}}, 3, 1.0f },
    // 4: BW HP 2-pole
    { {{ANG(1000), 0.90f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0.01f, 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 1.0f },
    // 5: BW HP 4-pole
    { {{ANG(900), 0.92f},{ANG(1100), 0.92f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0.01f, 1.0f},{0.01f, 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 1.0f },
    // 6: BP narrow
    { {{ANG(1000), 0.95f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0.01f, 1.0f},{kPi, 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 2.0f },
    // 7: Notch
    { {{ANG(800), 0.90f},{ANG(1200), 0.90f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(1000), 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 1.0f },

    // --- Moog ladder ---
    // 8: Moog low Q
    { {{ANG(1000), 0.75f},{ANG(1000), 0.75f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 0.7f },
    // 9: Moog med Q
    { {{ANG(1000), 0.85f},{ANG(1000), 0.85f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 0.8f },
    // 10: Moog high Q
    { {{ANG(1000), 0.92f},{ANG(1000), 0.92f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 0.9f },
    // 11: Moog self-osc
    { {{ANG(1000), 0.97f},{ANG(1000), 0.97f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 1.0f },

    // --- Formant vowels (bass) ---
    // 12: A
    { {{ANG(730), 0.95f},{ANG(1090), 0.93f},{ANG(2440), 0.90f},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 3, 0.3f },
    // 13: E
    { {{ANG(660), 0.95f},{ANG(1720), 0.93f},{ANG(2410), 0.90f},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 3, 0.3f },
    // 14: I
    { {{ANG(270), 0.95f},{ANG(2290), 0.93f},{ANG(3010), 0.90f},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 3, 0.3f },
    // 15: O
    { {{ANG(570), 0.95f},{ANG(840), 0.93f},{ANG(2410), 0.90f},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 3, 0.3f },
    // 16: U
    { {{ANG(300), 0.95f},{ANG(870), 0.93f},{ANG(2240), 0.90f},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 3, 0.3f },

    // --- Soprano vowels ---
    // 17: Soprano A
    { {{ANG(800), 0.94f},{ANG(1150), 0.92f},{ANG(2900), 0.89f},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 3, 0.3f },
    // 18: Soprano E
    { {{ANG(350), 0.94f},{ANG(2000), 0.92f},{ANG(2800), 0.89f},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 3, 0.3f },
    // 19: Soprano I
    { {{ANG(270), 0.94f},{ANG(2140), 0.92f},{ANG(2950), 0.89f},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 3, 0.3f },

    // --- Comb ---
    // 20: 4-tooth bright
    { {{ANG(500), 0.92f},{ANG(1000), 0.92f},{ANG(1500), 0.92f},{ANG(2000), 0.92f},{0,0},{0,0},{0,0}},
      {{ANG(750), 0.90f},{ANG(1250), 0.90f},{ANG(1750), 0.90f},{0,0},{0,0},{0,0},{0,0}}, 4, 0.5f },
    // 21: 4-tooth damped
    { {{ANG(500), 0.80f},{ANG(1000), 0.80f},{ANG(1500), 0.80f},{ANG(2000), 0.80f},{0,0},{0,0},{0,0}},
      {{ANG(750), 0.85f},{ANG(1250), 0.85f},{ANG(1750), 0.85f},{0,0},{0,0},{0,0},{0,0}}, 4, 0.7f },
    // 22: 7-tooth harmonic
    { {{ANG(400), 0.90f},{ANG(800), 0.90f},{ANG(1200), 0.90f},{ANG(1600), 0.90f},{ANG(2000), 0.90f},{ANG(2400), 0.90f},{ANG(2800), 0.90f}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 7, 0.3f },

    // --- Phaser ---
    // 23: 2-stage
    { {{ANG(800), 0.90f},{ANG(1600), 0.90f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(800), 1.11f},{ANG(1600), 1.11f},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 1.0f },
    // 24: 4-stage
    { {{ANG(500), 0.90f},{ANG(1000), 0.90f},{ANG(2000), 0.90f},{ANG(4000), 0.90f},{0,0},{0,0},{0,0}},
      {{ANG(500), 1.11f},{ANG(1000), 1.11f},{ANG(2000), 1.11f},{ANG(4000), 1.11f},{0,0},{0,0},{0,0}}, 4, 1.0f },

    // --- Resonator ---
    // 25: Single
    { {{ANG(1000), 0.97f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 0.5f },
    // 26: Dual octave
    { {{ANG(500), 0.96f},{ANG(1000), 0.96f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 0.4f },
    // 27: Triple chord
    { {{ANG(500), 0.95f},{ANG(630), 0.95f},{ANG(750), 0.95f},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 3, 0.3f },

    // --- EQ ---
    // 28: Low shelf
    { {{ANG(200), 0.85f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(200), 0.60f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 1.0f },
    // 29: High shelf
    { {{ANG(4000), 0.85f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(4000), 0.60f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 1.0f },
    // 30: Mid scoop
    { {{ANG(500), 0.85f},{ANG(4000), 0.85f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(1500), 0.95f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 1.0f },
    // 31: Presence
    { {{ANG(3000), 0.93f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(3000), 0.70f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 1.0f },
  };

  static const MorphCube kCubes[] = {
    { {1, 4, 2, 5}, "BW LP>HP" },
    { {1, 6, 4, 7}, "BW LP>BP" },
    { {1, 2, 2, 3}, "BW 2>6p" },
    { {6, 7, 1, 4}, "BP>Ntch" },
    { {1, 7, 6, 4}, "BW Quad" },
    { {0, 1, 0, 4}, "BW Fade" },
    { {1, 6, 7, 4}, "BW Ring" },
    { {2, 5, 3, 7}, "BW Deep" },
    { {8, 9, 10, 11}, "Moog Q" },
    { {8, 11, 1, 4}, "Moog>BW" },
    { {8, 10, 0, 11}, "Moog Sw" },
    { {9, 10, 11, 8}, "Moog Rng" },
    { {12, 13, 14, 15}, "AEIO" },
    { {12, 16, 15, 14}, "AUOI" },
    { {13, 14, 16, 12}, "EIUA" },
    { {17, 18, 19, 12}, "Sop>Bas" },
    { {12, 17, 13, 18}, "B>S AE" },
    { {14, 19, 15, 17}, "B>S IO" },
    { {12, 15, 17, 19}, "Vox Dia" },
    { {16, 13, 12, 14}, "Vox Rev" },
    { {20, 21, 22, 20}, "Comb" },
    { {20, 22, 0, 21}, "Cmb>Flt" },
    { {22, 20, 21, 22}, "Cmb Shf" },
    { {20, 25, 21, 26}, "Cmb>Res" },
    { {23, 24, 0, 23}, "Phase" },
    { {23, 24, 1, 4}, "Phs>BW" },
    { {23, 25, 24, 26}, "Phs>Res" },
    { {24, 23, 23, 24}, "Phs Osc" },
    { {25, 26, 27, 25}, "Reson" },
    { {25, 27, 0, 26}, "Res>Flt" },
    { {25, 12, 26, 13}, "Res>Vox" },
    { {27, 22, 25, 20}, "Res>Cmb" },
  };

  static const int kNumCubeSpecs = sizeof(kCubes) / sizeof(kCubes[0]);

} // namespace stolmine
