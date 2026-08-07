// anamnesis::noise -- tileable Perlin noise via a baked LUT. Lifted/adapted from
// the stolmine/spreadsheet RaindropGraphic contour engine (Ken Perlin gradient
// noise baked once into a power-of-two LUT, then bilinearly sampled at runtime).
// Used to wobble bubble radii into organic blobs (07/08-viz docs). Header-only,
// no od deps; the LUT is a function-static baked on first use (one TU).

#pragma once

#include <math.h>
#include <stdint.h>

namespace anamnesis
{
  namespace noise
  {
    static const int kLUT = 32; // power of two (mask = kLUT-1)

    inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
    inline float lerpf(float a, float b, float t) { return a + t * (b - a); }
    inline float grad(int hash, float x, float y)
    {
      int h = hash & 7;
      float u = h < 4 ? x : y;
      float v = h < 4 ? y : x;
      return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }
    inline float perlinEval(const int *perm, float x, float y)
    {
      int xi = (int)floorf(x) & 255;
      int yi = (int)floorf(y) & 255;
      float xf = x - floorf(x), yf = y - floorf(y);
      float u = fade(xf), v = fade(yf);
      int aa = perm[perm[xi] + yi];
      int ab = perm[perm[xi] + yi + 1];
      int ba = perm[perm[xi + 1] + yi];
      int bb = perm[perm[xi + 1] + yi + 1];
      return lerpf(lerpf(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u),
                   lerpf(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u), v);
    }

    // The baked LUT (filled with 4 periods so it tiles at the boundaries).
    //
    // HARDWARE CONSTRAINT [hab:anamnesis-insert-crash]: `perm` MUST be static.
    // As a stack local it is a 2048-byte frame, and the first caller of lut()
    // on am335x is process() (bubble physics -> field::flow -> noise::sample)
    // on the 2048-byte SYS/BIOS audio task stack: the bake frame alone blows
    // the whole stack and Fisher-Yates writes the 0..255 permutation over the
    // audio Task_Object + audio Event just below it (data-abort in Event_post,
    // dfar=0xb5). The Anamnesis ctor also pre-bakes (bake()) so the one-time
    // fill runs at insert on the 32 KB app stack, never on the audio thread.
    inline const float *lut()
    {
      static float L[kLUT * kLUT];
      static bool baked = false;
      if (!baked)
      {
        static int perm[512]; // .bss, NOT the (tiny) audio task stack
        for (int i = 0; i < 256; i++) perm[i] = i;
        uint32_t rng = 0x1234567u;
        for (int i = 255; i > 0; i--)
        {
          rng = rng * 1664525u + 1013904223u;
          int j = (int)((rng >> 8) % (unsigned)(i + 1));
          int t = perm[i]; perm[i] = perm[j]; perm[j] = t;
        }
        for (int i = 0; i < 256; i++) perm[i + 256] = perm[i];
        for (int y = 0; y < kLUT; y++)
        {
          float ny = (float)y * (4.0f / (float)kLUT);
          for (int x = 0; x < kLUT; x++)
            L[y * kLUT + x] = perlinEval(perm, (float)x * (4.0f / (float)kLUT), ny);
        }
        baked = true;
      }
      return L;
    }

    // Force the one-time LUT bake NOW (call from a construction-time / UI-thread
    // context). Keeps the bake cost + its writes off the audio thread entirely.
    inline void bake() { (void)lut(); }

    // Bilinear, tiling sample. Returns roughly [-1, 1].
    inline float sample(float u, float v)
    {
      const float *L = lut();
      u = u - floorf(u);
      v = v - floorf(v);
      float fx = u * (float)kLUT, fy = v * (float)kLUT;
      int x0 = (int)fx, y0 = (int)fy;
      float sx = fx - (float)x0, sy = fy - (float)y0;
      x0 &= (kLUT - 1); y0 &= (kLUT - 1);
      int x1 = (x0 + 1) & (kLUT - 1), y1 = (y0 + 1) & (kLUT - 1);
      float v00 = L[y0 * kLUT + x0], v10 = L[y0 * kLUT + x1];
      float v01 = L[y1 * kLUT + x0], v11 = L[y1 * kLUT + x1];
      return lerpf(lerpf(v00, v10, sx), lerpf(v01, v11, sx), sy);
    }

    // 3-octave fractal noise (~[-0.9, 0.9]).
    inline float fbm(float u, float v)
    {
      float s = sample(u, v) * 0.5f;
      s += sample(u * 2.0f, v * 2.0f) * 0.25f;
      s += sample(u * 4.0f, v * 4.0f) * 0.125f;
      return s;
    }
  } // namespace noise
} // namespace anamnesis
