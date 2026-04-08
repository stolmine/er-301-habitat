#pragma once

// Sfera Z-Plane filter configurations
// Each config: up to 7 pole pairs + 7 zero pairs (angle in radians, radius 0-1)
// Configs are referenced by MorphCubes which define 4-corner interpolation

namespace stolmine
{

  struct PoleZero
  {
    float angle;  // 0 to pi (normalized angular frequency)
    float radius; // 0 to ~1 for poles, 0 to ~2 for zeros
  };

  struct FilterConfig
  {
    PoleZero poles[7];
    PoleZero zeros[7];
    int numSections;
    float gain;
  };

  struct MorphCube
  {
    int config[4]; // corners: [X0Y0, X1Y0, X0Y1, X1Y1]
    const char *name;
  };

  // --- Filter configurations pool ---
  // Angles: pi * freq / (sr/2), so pi = Nyquist
  // Common reference: 1kHz at 48kHz = pi * 1000/24000 = 0.1309

  static const float kPi = 3.14159265f;

  // Helper: normalized angle from Hz (assumes 48kHz)
  // At runtime, cutoff offset scales these
  #define ANG(hz) (kPi * (hz) / 24000.0f)

  static const FilterConfig kConfigs[] = {
    // 0: Bypass (flat)
    { {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 0, 1.0f },

    // --- Butterworth family ---
    // 1: Butterworth LP 2-pole 1kHz
    { {{ANG(1000), 0.90f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{kPi, 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 1.0f },

    // 2: Butterworth LP 4-pole 1kHz
    { {{ANG(900), 0.92f},{ANG(1100), 0.92f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{kPi, 1.0f},{kPi, 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 1.0f },

    // 3: Butterworth LP 6-pole 1kHz
    { {{ANG(800), 0.93f},{ANG(1000), 0.93f},{ANG(1200), 0.93f},{0,0},{0,0},{0,0},{0,0}},
      {{kPi, 1.0f},{kPi, 1.0f},{kPi, 1.0f},{0,0},{0,0},{0,0},{0,0}}, 3, 1.0f },

    // 4: Butterworth HP 2-pole 1kHz
    { {{ANG(1000), 0.90f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0.001f, 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 1.0f },

    // 5: Butterworth HP 4-pole 1kHz
    { {{ANG(900), 0.92f},{ANG(1100), 0.92f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0.001f, 1.0f},{0.001f, 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 1.0f },

    // 6: Bandpass 1kHz narrow
    { {{ANG(1000), 0.95f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0.001f, 1.0f},{kPi, 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 2.0f },

    // 7: Notch 1kHz
    { {{ANG(800), 0.90f},{ANG(1200), 0.90f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(1000), 1.0f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 1.0f },

    // --- Moog ladder family ---
    // 8: Moog 4-pole LP low Q
    { {{ANG(1000), 0.75f},{ANG(1000), 0.75f},{ANG(1000), 0.80f},{ANG(1000), 0.80f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 0.5f },

    // 9: Moog 4-pole LP medium Q
    { {{ANG(1000), 0.85f},{ANG(1000), 0.85f},{ANG(1000), 0.88f},{ANG(1000), 0.88f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 0.6f },

    // 10: Moog 4-pole LP high Q
    { {{ANG(1000), 0.92f},{ANG(1000), 0.92f},{ANG(1000), 0.94f},{ANG(1000), 0.94f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 0.8f },

    // 11: Moog 4-pole LP self-osc
    { {{ANG(1000), 0.97f},{ANG(1000), 0.97f},{ANG(1000), 0.98f},{ANG(1000), 0.98f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 1.0f },

    // --- Formant vowels (bass voice F1/F2/F3/F4) ---
    // 12: Vowel A (730/1090/2440/3400 Hz)
    { {{ANG(730), 0.95f},{ANG(1090), 0.93f},{ANG(2440), 0.90f},{ANG(3400), 0.88f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 0.3f },

    // 13: Vowel E (660/1720/2410/3400 Hz)
    { {{ANG(660), 0.95f},{ANG(1720), 0.93f},{ANG(2410), 0.90f},{ANG(3400), 0.88f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 0.3f },

    // 14: Vowel I (270/2290/3010/3400 Hz)
    { {{ANG(270), 0.95f},{ANG(2290), 0.93f},{ANG(3010), 0.90f},{ANG(3400), 0.88f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 0.3f },

    // 15: Vowel O (570/840/2410/3400 Hz)
    { {{ANG(570), 0.95f},{ANG(840), 0.93f},{ANG(2410), 0.90f},{ANG(3400), 0.88f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 0.3f },

    // 16: Vowel U (300/870/2240/3400 Hz)
    { {{ANG(300), 0.95f},{ANG(870), 0.93f},{ANG(2240), 0.90f},{ANG(3400), 0.88f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 0.3f },

    // --- Soprano vowels ---
    // 17: Soprano A (800/1150/2900/3900 Hz)
    { {{ANG(800), 0.94f},{ANG(1150), 0.92f},{ANG(2900), 0.89f},{ANG(3900), 0.87f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 0.3f },

    // 18: Soprano E (350/2000/2800/3600 Hz)
    { {{ANG(350), 0.94f},{ANG(2000), 0.92f},{ANG(2800), 0.89f},{ANG(3600), 0.87f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 0.3f },

    // 19: Soprano I (270/2140/2950/3900 Hz)
    { {{ANG(270), 0.94f},{ANG(2140), 0.92f},{ANG(2950), 0.89f},{ANG(3900), 0.87f},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 4, 0.3f },

    // --- Comb family ---
    // 20: Comb 4-tooth bright
    { {{ANG(500), 0.92f},{ANG(1000), 0.92f},{ANG(1500), 0.92f},{ANG(2000), 0.92f},{0,0},{0,0},{0,0}},
      {{ANG(750), 0.90f},{ANG(1250), 0.90f},{ANG(1750), 0.90f},{0,0},{0,0},{0,0},{0,0}}, 4, 0.5f },

    // 21: Comb 4-tooth damped
    { {{ANG(500), 0.80f},{ANG(1000), 0.80f},{ANG(1500), 0.80f},{ANG(2000), 0.80f},{0,0},{0,0},{0,0}},
      {{ANG(750), 0.85f},{ANG(1250), 0.85f},{ANG(1750), 0.85f},{0,0},{0,0},{0,0},{0,0}}, 4, 0.7f },

    // 22: Comb 7-tooth harmonic
    { {{ANG(400), 0.90f},{ANG(800), 0.90f},{ANG(1200), 0.90f},{ANG(1600), 0.90f},{ANG(2000), 0.90f},{ANG(2400), 0.90f},{ANG(2800), 0.90f}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 7, 0.3f },

    // --- Phaser family ---
    // 23: Phaser 2-stage
    { {{ANG(800), 0.90f},{ANG(1600), 0.90f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(800), 1.11f},{ANG(1600), 1.11f},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 1.0f },

    // 24: Phaser 4-stage
    { {{ANG(500), 0.90f},{ANG(1000), 0.90f},{ANG(2000), 0.90f},{ANG(4000), 0.90f},{0,0},{0,0},{0,0}},
      {{ANG(500), 1.11f},{ANG(1000), 1.11f},{ANG(2000), 1.11f},{ANG(4000), 1.11f},{0,0},{0,0},{0,0}}, 4, 1.0f },

    // --- Resonator family ---
    // 25: Single resonator
    { {{ANG(1000), 0.97f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 0.5f },

    // 26: Dual resonator (octave)
    { {{ANG(500), 0.96f},{ANG(1000), 0.96f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 0.4f },

    // 27: Triple resonator (chord)
    { {{ANG(500), 0.95f},{ANG(630), 0.95f},{ANG(750), 0.95f},{0,0},{0,0},{0,0},{0,0}},
      {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 3, 0.3f },

    // --- EQ shapes ---
    // 28: Low shelf boost
    { {{ANG(200), 0.85f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(200), 0.60f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 1.0f },

    // 29: High shelf boost
    { {{ANG(4000), 0.85f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(4000), 0.60f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 1.0f },

    // 30: Mid scoop
    { {{ANG(500), 0.85f},{ANG(4000), 0.85f},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(1500), 0.95f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 2, 1.0f },

    // 31: Presence boost
    { {{ANG(3000), 0.93f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
      {{ANG(3000), 0.70f},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}, 1, 1.0f },
  };

  static const int kNumConfigs = sizeof(kConfigs) / sizeof(kConfigs[0]);

  // --- Morph Cubes ---
  // Each cube: 4 corner config indices [X0Y0, X1Y0, X0Y1, X1Y1]
  // X sweeps left-right, Y sweeps bottom-top

  static const MorphCube kCubes[] = {
    // 0-7: Butterworth sweeps
    { {1, 4, 2, 5}, "BW LP>HP" },    // X: LP to HP, Y: 2-pole to 4-pole
    { {1, 6, 4, 7}, "BW LP>BP" },    // X: LP to BP, Y: HP to notch
    { {1, 2, 2, 3}, "BW 2>6p" },     // X: 2-pole to 4-pole, Y: 4-pole to 6-pole
    { {6, 7, 1, 4}, "BP>Ntch" },     // X: BP to notch, Y: LP to HP
    { {1, 7, 6, 4}, "BW Quad" },     // all 4 types at corners
    { {0, 1, 0, 4}, "BW Fade" },     // X: flat to LP, Y: flat to HP
    { {1, 6, 7, 4}, "BW Ring" },     // circular arrangement
    { {2, 5, 3, 7}, "BW Deep" },     // 4-pole LP to HP, 6-pole LP to notch

    // 8-11: Moog ladder
    { {8, 9, 10, 11}, "Moog Q" },    // X: low to med Q, Y: high to self-osc
    { {8, 11, 1, 4}, "Moog>BW" },    // X: Moog to self-osc, Y: BW LP to HP
    { {8, 10, 0, 11}, "Moog Sw" },   // X: low to high Q, Y: flat to self-osc
    { {9, 10, 11, 8}, "Moog Rng" },  // circular

    // 12-19: Formant vowels
    { {12, 13, 14, 15}, "AEIO" },    // bass vowels A/E/I/O
    { {12, 16, 15, 14}, "AUOI" },    // bass A/U/O/I
    { {13, 14, 16, 12}, "EIUA" },    // bass E/I/U/A
    { {17, 18, 19, 12}, "Sop>Bas" }, // soprano A/E/I to bass A
    { {12, 17, 13, 18}, "B>S AE" },  // bass to soprano, A and E
    { {14, 19, 15, 17}, "B>S IO" },  // bass to soprano, I and O
    { {12, 15, 17, 19}, "Vox Dia" }, // diagonal vowel morph
    { {16, 13, 12, 14}, "Vox Rev" }, // reverse vowel sweep

    // 20-23: Comb
    { {20, 21, 22, 20}, "Comb" },    // bright to damped, to 7-tooth
    { {20, 22, 0, 21}, "Cmb>Flt" },  // comb to flat
    { {22, 20, 21, 22}, "Cmb Shf" }, // comb shifting
    { {20, 25, 21, 26}, "Cmb>Res" }, // comb to resonator

    // 24-27: Phaser
    { {23, 24, 0, 23}, "Phase" },    // 2-stage to 4-stage, to flat
    { {23, 24, 1, 4}, "Phs>BW" },    // phaser to butterworth
    { {23, 25, 24, 26}, "Phs>Res" }, // phaser to resonator
    { {24, 23, 23, 24}, "Phs Osc" }, // phaser oscillating

    // 28-31: Resonator
    { {25, 26, 27, 25}, "Reson" },   // single to dual to triple
    { {25, 27, 0, 26}, "Res>Flt" },  // resonator to flat
    { {25, 12, 26, 13}, "Res>Vox" }, // resonator to vowel
    { {27, 22, 25, 20}, "Res>Cmb" }, // resonator to comb

    // Placeholder cubes (32-127) -- filled with permutations
  };

  static const int kNumCubes = sizeof(kCubes) / sizeof(kCubes[0]);

  // Generate remaining cubes programmatically at init
  // by permuting existing configs across frequency ranges

} // namespace stolmine
