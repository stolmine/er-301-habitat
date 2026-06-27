// anamnesis::field -- the shared "Pond of Recollection" flow-field math.
//
// One continuous streamline field painted across Anamnesis's whole main-display
// ply strip (planning/spatial-glitch-impl/07-allover-viz.md). Every ply renders
// its 42px window of the SAME field, indexed by a CONTENT x-coordinate (pixels
// across the entire strip), so adjacent ply slices align at their seams by
// construction. Header-only, pure math, no od deps -- safe to include anywhere.
//
// Phase 5b foundation: the baseline braided "current". Per-ply features
// (ripples / vortex / crystal / moire / fade) compose on top in Phase C.

#pragma once

#include <math.h>

namespace anamnesis
{
  namespace field
  {

    // Number of horizontal flow lines drawn down the column.
    static const int kStreamlines = 7;

    // Content-stride between ply slices: 42px ply + 1px SpottedStrip gap.
    static const int kStride = 43;

    // Baseline y (px) of streamline s within a height-h column.
    inline float baseline(int s, int n, int h)
    {
      return ((float)s + 0.5f) * (float)h / (float)n;
    }

    // Smooth, x-continuous vertical displacement of the flowing line at
    // content-x `cx`, baseline `yb`, animation `phase`. A sum of traveling
    // sines -> a gentle braided current. Continuous in cx (no per-ply term),
    // so neighboring slices meet exactly at the seam.
    inline float flow(float cx, float yb, float phase)
    {
      float d = 0.0f;
      d += 2.6f * sinf(0.055f * cx + phase + yb * 0.045f);
      d += 1.3f * sinf(0.115f * cx - 0.70f * phase + yb * 0.090f);
      d += 0.7f * sinf(0.210f * cx + 1.30f * phase);
      return d;
    }

  } // namespace field
} // namespace anamnesis
