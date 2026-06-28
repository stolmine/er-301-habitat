// anamnesis::AnamFieldGraphic -- one ply's window into the all-over flow-field.
//
// Each ply's main graphic (replacing the stock fader) renders content-X
// [index*kStride .. +width] of the shared "Pond of Recollection" field, so the
// whole SpottedStrip reads as one continuous flowing image that pans as you
// scrub. Holds an Anamnesis* and reads its viz getters directly (Helicase
// pattern). planning/spatial-glitch-impl/07-allover-viz.md
//
// Phase 5b foundation: the baseline braided current only. Per-ply features
// (ripples / vortex / crystal / moire / fade) keyed off `mFeature` land in Phase C.

#pragma once

#include <od/graphics/Graphic.h>
#include "atoms/Anamnesis.h"
#include "AnamField.h"

namespace anamnesis
{

  class AnamFieldGraphic : public od::Graphic
  {
  public:
    AnamFieldGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height) {}

    virtual ~AnamFieldGraphic()
    {
      if (mpOp)
        mpOp->release();
    }

    void follow(Anamnesis *op)
    {
      if (mpOp)
        mpOp->release();
      mpOp = op;
      if (mpOp)
        mpOp->attach();
    }

    // Slice `index` of `count` plies; `feature` selects the per-ply motif (Phase C).
    void setCanvas(int index, int count)
    {
      mIndex = index;
      mCount = count;
    }
    void setFeature(int feature) { mFeature = feature; }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      const int w = mWidth;
      const int h = mHeight;
      const int left = mWorldLeft;
      const int bot = mWorldBottom;
      const int x0 = mIndex * field::kStride; // content-x of this slice's left edge

      const float phase = mpOp ? mpOp->vizPhase() : 0.0f;
      const float mixN = mpOp ? mpOp->vizMix() : 1.0f;
      // Base streamline brightness scales with Mix; droplet glow adds on top.
      const float baseB = field::kBaseDim + (field::kBaseBright - field::kBaseDim) * mixN;

      // Density drives the WEAVE (merge/split), not the line count: a fixed set of
      // streamlines that converge at drifting merge nodes and diverge after
      // (wood-grain / dendrite braid). Passed to weaveDispY per control point.
      float density = mpOp ? mpOp->vizDensity() : 0.5f;
      if (density < 0.0f) density = 0.0f; else if (density > 1.0f) density = 1.0f;
      const float size = mpOp ? mpOp->vizSize() : 0.5f; // Size -> flow feature scale
      const float diffuse = mpOp ? mpOp->vizDiffusion() : 0.0f; // Diffusion -> line halo
      const int   haloR = (int)(field::kHaloRadius * diffuse + 0.5f); // 0 -> no halo
      const float haloG = field::kHaloGain * diffuse;
      const int n = field::kStreamN;

      // Cache the active rain droplets once (epicenters in content-x / column-y).
      int nd = 0;
      float dX[kVizMaxDrops], dY[kVizMaxDrops], dAge[kVizMaxDrops];
      float dC[kVizMaxDrops], dAmp[kVizMaxDrops];
      if (mpOp)
      {
        for (int i = 0; i < kVizMaxDrops; i++)
        {
          const float age = mpOp->vizDropAge(i);
          if (age < 0.0f)
            continue;
          dX[nd] = mpOp->vizDropX(i);
          dY[nd] = mpOp->vizDropY(i);
          dAge[nd] = age;
          dC[nd] = mpOp->vizDropSpeed(i);
          dAmp[nd] = mpOp->vizDropAmp(i);
          nd++;
        }
      }

      // Each flow line is sampled at control points every kCtrlStep px (the
      // expensive flow + rain evals), then Catmull-Rom interpolated to per-pixel
      // y -> smooth curves, cheap enough to scale the line count.
      const int cstep = field::kCtrlStep;
      // GLOBAL control grid: control points sit at multiples of cstep in CONTENT
      // x, shared by every ply. The 43px ply stride isn't a multiple of cstep, so
      // a ply-relative grid would misalign at seams and the spline would break
      // where a ripple bends it. Sharing the grid -> neighbours sample identical
      // control points at the boundary -> one continuous curve across the seam.
      const int g0 = x0 / cstep - 1;        // first grid index (one margin before)
      const int gLast = (x0 + w) / cstep + 2; // covers the 1px bridge column too
      int mctrl = gLast - g0 + 1;
      if (mctrl > 40) mctrl = 40;
      // Bridge the 1px SpottedStrip gap: every ply but the last draws one extra
      // content column (px = left+w) so the continuous curve crosses into the
      // next ply with no hairline seam. The last ply stops at its own edge.
      const int wext = (mIndex < mCount - 1) ? 1 : 0;
      // Per-BAND renderer: both lines of band b PLUS the negative space between
      // them FILLED with background, so the band occludes bubbles drawn behind it
      // (lower z). Invoked in z-order -> bubbles weave through the bands.
      auto renderBand = [&](int b)
      {
        const int s0 = 2 * b, s1 = 2 * b + 1;
        const float yb0 = ((float)s0 + 0.5f) * (float)h / (float)n;
        const float yb1 = ((float)s1 + 0.5f) * (float)h / (float)n;
        float cY0[40], cB0[40], cY1[40], cB1[40];
        for (int i = 0; i < mctrl; i++)
        {
          const float cx = (float)((g0 + i) * cstep);
          float y0 = yb0 + field::flow(cx, yb0, phase, size);
          float y1 = yb1 + field::flow(cx, yb1, phase, size);
          float gl0 = 0.0f, gl1 = 0.0f;
          for (int d = 0; d < nd; d++)
          {
            const field::RippleHit a0 = field::rippleEval(cx - dX[d], y0 - dY[d], dAge[d], dC[d]);
            y0 += dAmp[d] * a0.bend; gl0 += dAmp[d] * a0.glow;
            const field::RippleHit a1 = field::rippleEval(cx - dX[d], y1 - dY[d], dAge[d], dC[d]);
            y1 += dAmp[d] * a1.bend; gl1 += dAmp[d] * a1.glow;
          }
          cY0[i] = y0; cB0[i] = gl0; cY1[i] = y1; cB1[i] = gl1;
        }
        float prev0 = -1000.0f, prev1 = -1000.0f;
        for (int lx = 0; lx < w + wext; lx++)
        {
          const int cx = x0 + lx;
          const int seg = cx / cstep;
          const int idx = seg - g0;
          const float t = (float)(cx - seg * cstep) / (float)cstep;
          float y0 = field::catmull(cY0[idx - 1], cY0[idx], cY0[idx + 1], cY0[idx + 2], t);
          float y1 = field::catmull(cY1[idx - 1], cY1[idx], cY1[idx + 1], cY1[idx + 2], t);
          float gl0 = field::catmull(cB0[idx - 1], cB0[idx], cB0[idx + 1], cB0[idx + 2], t);
          float gl1 = field::catmull(cB1[idx - 1], cB1[idx], cB1[idx + 1], cB1[idx + 2], t);
          if (y0 < 0.0f) y0 = 0.0f; else if (y0 > (float)(h - 1)) y0 = (float)(h - 1);
          if (y1 < 0.0f) y1 = 0.0f; else if (y1 > (float)(h - 1)) y1 = (float)(h - 1);
          const int px = left + lx;
          // Fill the negative space between the pair with background -> occlude.
          const int flo = (int)(y0 < y1 ? y0 : y1) + 1;
          const int fhi = (int)(y0 < y1 ? y1 : y0);
          for (int yy = flo; yy < fhi; yy++) fb.pixel(0, px, bot + yy);
          drawLinePix(fb, px, bot, h, y0, gl0, baseB, prev0, haloG, haloR);
          drawLinePix(fb, px, bot, h, y1, gl1, baseB, prev1, haloG, haloR);
        }
      };

      // Bubbles = 2D METABALLS per z-LEVEL (lava-lamp): a thresholded field (FBM
      // noise + Gaussian bumps at the level's bubbles) traced by marching squares
      // -> smooth iso-contours that morph + split/join. 2 levels brighter than the
      // lines; fill (occlude lower-z) then bright contour. Clipped to the ply.
      int bubB = (int)(baseB + 2.5f);
      if (bubB > 15) bubB = 15;
      const int bXlo = left, bXhi = left + w + wext, bYlo = bot, bYhi = bot + h;
      const float bMorph = phase * field::kMetaMorph; // noise scroll (freezes w/ flow)

      // Cache active bubbles (content-x / column-y / radius / level).
      int nb = 0;
      float bX[kVizMaxBubbles], bY[kVizMaxBubbles], bR[kVizMaxBubbles], bSeed[kVizMaxBubbles];
      int bLvl[kVizMaxBubbles];
      bool levelUsed[field::kBubLevels];
      for (int L = 0; L < field::kBubLevels; L++) levelUsed[L] = false;
      if (mpOp)
      {
        for (int i = 0; i < kVizMaxBubbles; i++)
        {
          const float br = mpOp->vizBubR(i);
          if (br <= 0.0f) continue;
          bX[nb] = mpOp->vizBubX(i);
          bY[nb] = mpOp->vizBubY(i);
          bR[nb] = br;
          bSeed[nb] = mpOp->vizBubSeed(i);
          int L = (int)(mpOp->vizBubZ(i) + 0.5f);
          if (L < 0) L = 0; else if (L >= field::kBubLevels) L = field::kBubLevels - 1;
          bLvl[nb] = L;
          levelUsed[L] = true;
          nb++;
        }
      }

      // Expand each bubble into a CLUSTER of sub-bumps whose offsets are read from
      // the noise topography at the bubble -> they walk the LUT as it moves, so the
      // compound contour pinches and SPLITS. (The "invisible compounded blobs".)
      // Drifting point layer (content-space, shared by all plies/levels).
      const int NP = field::kNumPoints;
      float ptX[field::kNumPoints], ptY[field::kNumPoints];
      const float ptt = phase * field::kPointDriftRate;
      const float reacht = phase * field::kReachRate;
      for (int p = 0; p < NP; p++)
      {
        ptX[p] = field::hash01(p, 1) * field::kVizStripW + field::kPointDrift * anamnesis::noise::sample((float)p * 0.37f, ptt);
        ptY[p] = 6.0f + field::hash01(p, 2) * 52.0f + field::kPointDrift * anamnesis::noise::sample((float)p * 0.37f + 40.0f, ptt + 7.0f);
      }

      // Each bubble = a CORE bump + momentary lobes latched onto nearby points,
      // each weighted by distance (smooth fade) -> lobes grow & shed -> splits.
      const int kSBcap = kVizMaxBubbles * (1 + field::kMaxLobes);
      int nsb = 0;
      float sbX[kSBcap], sbY[kSBcap], sbR[kSBcap], sbAmp[kSBcap];
      int sbLvl[kSBcap];
      for (int b = 0; b < nb; b++)
      {
        if (nsb < kSBcap)
        {
          sbX[nsb] = bX[b]; sbY[nsb] = bY[b]; sbR[nsb] = bR[b] * field::kCoreK;
          sbAmp[nsb] = 1.0f; sbLvl[nsb] = bLvl[b]; nsb++;
        }
        const float rnz = anamnesis::noise::sample(bX[b] * field::kReachFreq, bY[b] * field::kReachFreq + reacht);
        float reach = (bR[b] * field::kLatchK + field::kLatchBase) * (1.0f + field::kReachVar * rnz);
        if (reach < field::kLatchBase) reach = field::kLatchBase; // breathes -> sometimes grabs far points
        const float reach2 = reach * reach;
        int lobes = 0;
        for (int p = 0; p < NP && lobes < field::kMaxLobes && nsb < kSBcap; p++)
        {
          const float dx = ptX[p] - bX[b], dy = ptY[p] - bY[b];
          const float d2 = dx * dx + dy * dy;
          if (d2 >= reach2) continue;
          const float w0 = 1.0f - field::smooth01(reach * field::kLatchFull, reach, sqrtf(d2)); // full, fade at edge
          // Per-bubble AFFINITY: this bubble's attraction to point p (stable from
          // its seed). Below kAffBias -> not latched, so each bubble ignores some
          // nearby points (distinct personalities; sets up flow/droplet throws).
          float aff = 0.5f + 0.5f * anamnesis::noise::sample(bSeed[b] * 0.7f + (float)p * 0.13f, (float)p * 0.31f + 5.0f);
          aff = (aff - field::kAffBias) / (1.0f - field::kAffBias);
          if (aff <= 0.0f) continue;
          const float w = w0 * aff;
          if (w < 0.05f) continue;
          // Per-point STRENGTH -> lobe size (some points spawn bigger lobes).
          const float str = field::kPointStrMin + (1.0f - field::kPointStrMin) * field::hash01(p, 5);
          sbX[nsb] = ptX[p]; sbY[nsb] = ptY[p]; sbR[nsb] = field::kLobeR * (0.6f + 0.9f * str);
          sbAmp[nsb] = w; sbLvl[nsb] = bLvl[b]; nsb++; lobes++;
        }
      }

      // Marching-squares segment table for OUR convention (config bit1=TL, 2=TR,
      // 4=BL, 8=BR; edges 0=top,1=right,2=bottom,3=left). Each pair = one segment
      // between two CROSSING edges. Saddles (6=TR+BL, 9=TL+BR) emit two segments.
      // (The screensaver's table was for a different ordering -> spurious spikes.)
      static const int kSeg[16][4] = {
          {-1, -1, -1, -1}, // 0
          {0, 3, -1, -1},   // 1  TL
          {0, 1, -1, -1},   // 2  TR
          {1, 3, -1, -1},   // 3  TL+TR
          {2, 3, -1, -1},   // 4  BL
          {0, 2, -1, -1},   // 5  TL+BL
          {0, 1, 2, 3},     // 6  TR+BL (saddle)
          {1, 2, -1, -1},   // 7  TL+TR+BL
          {1, 2, -1, -1},   // 8  BR
          {0, 3, 1, 2},     // 9  TL+BR (saddle)
          {0, 2, -1, -1},   // 10 TR+BR
          {2, 3, -1, -1},   // 11 TL+TR+BR
          {1, 3, -1, -1},   // 12 BL+BR
          {0, 1, -1, -1},   // 13 TL+BL+BR
          {0, 3, -1, -1},   // 14 TR+BL+BR
          {-1, -1, -1, -1}};// 15
      const int C = field::kMetaCell;
      const int gx0n = x0 / C - 1;        // first grid node (content-x = gx0n*C); global -> seams align
      int gw = w / C + 4; if (gw > kMetaGW) gw = kMetaGW;
      int gh = h / C + 2; if (gh > kMetaGH) gh = kMetaGH;
      const float T = field::kMetaThresh;

      auto renderBubbleLevel = [&](int L)
      {
        float *G = &mSlewGrid[L * kMetaGW * kMetaGH];
        for (int j = 0; j < gh; j++) // build the target field, SLEW G toward it
        {
          const float cy = (float)(j * C);
          for (int i = 0; i < gw; i++)
          {
            const float cx = (float)((gx0n + i) * C);
            float bumps = 0.0f;
            for (int b = 0; b < nsb; b++)
            {
              if (sbLvl[b] != L) continue;
              const float dx = cx - sbX[b], dy = cy - sbY[b];
              float s = sbR[b] * field::kMetaSigmaK; if (s < 1.0f) s = 1.0f;
              bumps += sbAmp[b] * field::kMetaBumpAmp * expf(-(dx * dx + dy * dy) / (2.0f * s * s));
            }
            // Each bump SHAPED by the local noise topography (multiplicative): the
            // edge follows peaks/valleys -> irregular/compound blobs that pinch &
            // split. Far from bumps (bumps~0) the field stays 0 (no spurious blobs).
            const float nz = anamnesis::noise::fbm(cx * field::kMetaNoiseFreq + (float)L * 3.1f,
                                                   cy * field::kMetaNoiseFreq - bMorph);
            const float f = bumps * (1.0f + field::kMetaNoiseGain * nz);
            const int idx = j * kMetaGW + i;
            G[idx] += field::kMetaSlew * (f - G[idx]); // temporal slew -> gentle morph
          }
        }
        for (int lx = 0; lx < w + wext; lx++) // FILL where field > T (occlude lower-z)
        {
          const float gxf = (float)(x0 + lx) / (float)C - (float)gx0n;
          const int gi = (int)gxf; if (gi < 0 || gi >= gw - 1) continue;
          const float fx = gxf - (float)gi;
          const int px = left + lx;
          for (int py = bYlo; py < bYhi; py++)
          {
            const float gyf = (float)(py - bot) / (float)C;
            const int gj = (int)gyf; if (gj < 0 || gj >= gh - 1) continue;
            const float fy = gyf - (float)gj;
            const float v00 = G[gj * kMetaGW + gi], v10 = G[gj * kMetaGW + gi + 1];
            const float v01 = G[(gj + 1) * kMetaGW + gi], v11 = G[(gj + 1) * kMetaGW + gi + 1];
            const float v = (v00 * (1.0f - fx) + v10 * fx) * (1.0f - fy) + (v01 * (1.0f - fx) + v11 * fx) * fy;
            if (v > T) fb.pixel(0, px, py);
          }
        }
        for (int j = 0; j < gh - 1; j++) // marching-squares contour (smooth edges)
          for (int i = 0; i < gw - 1; i++)
          {
            const float v00 = G[j * kMetaGW + i], v10 = G[j * kMetaGW + i + 1];
            const float v01 = G[(j + 1) * kMetaGW + i], v11 = G[(j + 1) * kMetaGW + i + 1];
            int cfg = 0;
            if (v00 > T) cfg |= 1;
            if (v10 > T) cfg |= 2;
            if (v01 > T) cfg |= 4;
            if (v11 > T) cfg |= 8;
            if (cfg == 0 || cfg == 15) continue;
            const int *sg = kSeg[cfg];
            float ex[4], ey[4];
            if (sg[0] == 0 || sg[1] == 0 || sg[2] == 0 || sg[3] == 0) { float t = (T - v00) / (v10 - v00); ex[0] = (float)i + t; ey[0] = (float)j; }
            if (sg[0] == 1 || sg[1] == 1 || sg[2] == 1 || sg[3] == 1) { float t = (T - v10) / (v11 - v10); ex[1] = (float)(i + 1); ey[1] = (float)j + t; }
            if (sg[0] == 2 || sg[1] == 2 || sg[2] == 2 || sg[3] == 2) { float t = (T - v01) / (v11 - v01); ex[2] = (float)i + t; ey[2] = (float)(j + 1); }
            if (sg[0] == 3 || sg[1] == 3 || sg[2] == 3 || sg[3] == 3) { float t = (T - v00) / (v01 - v00); ex[3] = (float)i; ey[3] = (float)j + t; }
            for (int e = 0; e < 4; e += 2)
            {
              if (sg[e] < 0 || sg[e + 1] < 0) continue;
              const float ax = (float)left + ((float)gx0n + ex[sg[e]]) * (float)C - (float)x0;
              const float ay = (float)bot + ey[sg[e]] * (float)C;
              const float bx2 = (float)left + ((float)gx0n + ex[sg[e + 1]]) * (float)C - (float)x0;
              const float by2 = (float)bot + ey[sg[e + 1]] * (float)C;
              drawAALineClip(fb, ax, ay, bx2, by2, bubB, bXlo, bXhi, bYlo, bYhi);
            }
          }
      };

      // Z-ORDER COMPOSITE: bands (randomized z) + bubble-LEVELS (each a metaball
      // field), drawn back->front so the levels weave through the bands by z.
      const int nBands = n / 2;
      struct ZItem { float z; int type; int idx; };
      ZItem items[field::kStreamN / 2 + field::kBubLevels];
      int ni = 0;
      for (int b = 0; b < nBands; b++) { items[ni].z = mpOp ? (float)mpOp->vizBandZ(b) : (float)b; items[ni].type = 0; items[ni].idx = b; ni++; }
      for (int L = 0; L < field::kBubLevels; L++)
        if (levelUsed[L]) { items[ni].z = ((float)L + 0.5f) * (float)nBands / (float)field::kBubLevels; items[ni].type = 1; items[ni].idx = L; ni++; }
      for (int a = 1; a < ni; a++) { ZItem key = items[a]; int j = a - 1; while (j >= 0 && items[j].z > key.z) { items[j + 1] = items[j]; j--; } items[j + 1] = key; }
      for (int it = 0; it < ni; it++)
      {
        if (items[it].type == 0) renderBand(items[it].idx);
        else renderBubbleLevel(items[it].idx);
      }

      // Impact splashes (front-most): rain flecks for drops in this ply's window.
      drawLooper(fb, left, bot, w, x0);
    }

    // ---- feature renderers ----

    // One streamline pixel column: sqrt-AA core + solid gap-fill for continuity.
    void drawLinePix(od::FrameBuffer &fb, int px, int bot, int h, float y, float glow, float baseB, float &prevY,
                     float haloG = 0.0f, int haloR = 0)
    {
      float bf = baseB + glow * field::kGlowGain;
      if (bf < 0.0f) bf = 0.0f; else if (bf > 15.0f) bf = 15.0f;
      const int bri = (int)(bf + 0.5f);
      const int yi = (int)y;
      const float f = y - (float)yi;
      fb.pixel((int)(bri * sqrtf(1.0f - f) + 0.5f), px, bot + yi);
      if (yi + 1 < h) fb.pixel((int)(bri * sqrtf(f) + 0.5f), px, bot + yi + 1);
      if (prevY > -999.0f)
      {
        const int a = (int)(prevY < y ? prevY : y);
        const int b = (int)(prevY < y ? y : prevY);
        for (int yy = a + 1; yy < b; yy++) fb.pixel(bri, px, bot + yy);
      }
      prevY = y;
      // Diffusion HALO: grade dimmer pixels out from the core, fading with
      // distance. MAX-blend (only brighten) so the sharp core + brighter
      // neighbours are never dimmed. haloR=0 (Diffusion=0) -> skipped entirely.
      if (haloR > 0)
      {
        const float peak = bf * haloG;
        for (int d = 1; d <= haloR; d++)
        {
          const int v = (int)(peak * (1.0f - (float)d / (float)(haloR + 1)) + 0.5f);
          if (v <= 0) break; // monotone decreasing -> nothing fainter follows
          const int ra = yi - d;     // above the core (yi)
          const int rb = yi + 1 + d; // below the core (yi+1)
          if (ra >= 0 && ra < h && v > fb.readPixel(px, bot + ra)) fb.pixel(v, px, bot + ra);
          if (rb >= 0 && rb < h && v > fb.readPixel(px, bot + rb)) fb.pixel(v, px, bot + rb);
        }
      }
    }

    // Plot a pixel only inside the clip window (bubbles are clipped to their ply).
    void plotClip(od::FrameBuffer &fb, int px, int py, int color, int xlo, int xhi, int ylo, int yhi)
    {
      if (px >= xlo && px < xhi && py >= ylo && py < yhi) fb.pixel(color, px, py);
    }

    // Clipped Bresenham line (blob outlines may cross the ply boundary).
    void drawLineClip(od::FrameBuffer &fb, int x0, int y0, int x1, int y1, int color,
                      int xlo, int xhi, int ylo, int yhi)
    {
      int dx = x1 - x0; if (dx < 0) dx = -dx;
      int dy = y1 - y0; if (dy < 0) dy = -dy; dy = -dy; // dy <= 0
      const int sx = x0 < x1 ? 1 : -1;
      const int sy = y0 < y1 ? 1 : -1;
      int err = dx + dy;
      for (;;)
      {
        if (x0 >= xlo && x0 < xhi && y0 >= ylo && y0 < yhi) fb.pixel(color, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
      }
    }

    void aaPlot(od::FrameBuffer &fb, int px, int py, float cov, int color,
                int xlo, int xhi, int ylo, int yhi)
    {
      if (cov <= 0.0f || px < xlo || px >= xhi || py < ylo || py >= yhi) return;
      int c = (int)((float)color * cov + 0.5f);
      if (c > 15) c = 15;
      if (c > 0) fb.pixel(c, px, py);
    }

    // Anti-aliased clipped line (Wu-style) -> the iso-contour edge moves fluidly
    // sub-pixel instead of jumping a whole pixel as the field morphs.
    void drawAALineClip(od::FrameBuffer &fb, float x0, float y0, float x1, float y1,
                        int color, int xlo, int xhi, int ylo, int yhi)
    {
      float dx = x1 - x0, dy = y1 - y0;
      const bool steep = (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx);
      if (steep) { float t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
      if (x0 > x1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
      dx = x1 - x0; dy = y1 - y0;
      const float grad = (dx == 0.0f) ? 0.0f : dy / dx;
      const int ix0 = (int)(x0 + 0.5f), ix1 = (int)(x1 + 0.5f);
      float y = y0 + grad * ((float)ix0 - x0);
      for (int x = ix0; x <= ix1; x++)
      {
        const int iy = (int)floorf(y);
        const float fy = y - (float)iy;
        if (steep)
        {
          aaPlot(fb, iy, x, 1.0f - fy, color, xlo, xhi, ylo, yhi);
          aaPlot(fb, iy + 1, x, fy, color, xlo, xhi, ylo, yhi);
        }
        else
        {
          aaPlot(fb, x, iy, 1.0f - fy, color, xlo, xhi, ylo, yhi);
          aaPlot(fb, x, iy + 1, fy, color, xlo, xhi, ylo, yhi);
        }
        y += grad;
      }
    }

    // A growing dendrite branch from (ox,oy) toward the neighbour line (+gap),
    // a quadratic-bezier curve drawn only to fraction g (the grown extent), with
    // a soft tip fade. seed varies lean/bow for an organic look.
    void drawBranch(od::FrameBuffer &fb, float ox, float oy, float gap, float g, int seed, float bright)
    {
      const float lean = (seed & 1) ? field::kConnLean : -field::kConnLean;
      const float tx = ox + lean, ty = oy + gap; // target ~ neighbour line
      const float cx = (ox + tx) * 0.5f + ((seed & 2) ? field::kConnBow : -field::kConnBow);
      const float cy = (oy + ty) * 0.5f;
      const int steps = 8;
      int ppx = -10000, ppy = 0;
      for (int i = 1; i <= steps; i++)
      {
        const float t = g * (float)i / (float)steps; // grow up to fraction g
        const float u = 1.0f - t;
        const float bx = u * u * ox + 2.0f * u * t * cx + t * t * tx;
        const float by = u * u * oy + 2.0f * u * t * cy + t * t * ty;
        float tip = (g - t) * 6.0f; // fade the growing tip
        if (tip < 0.0f) tip = 0.0f; else if (tip > 1.0f) tip = 1.0f;
        int c = (int)(bright * tip + 0.5f);
        if (c > 15) c = 15;
        const int px = (int)(bx + 0.5f), py = (int)(by + 0.5f);
        if (ppx > -10000 && c > 0) fb.line(c, ppx, ppy, px, py);
        ppx = px; ppy = py;
      }
    }

    // Looper: the impact splash. A bright fleck at each young droplet whose
    // epicenter falls in this ply's content window (the ring's line-bending is
    // already applied pond-wide above). Fades over the drop's first ~0.35 s.
    void drawLooper(od::FrameBuffer &fb, int left, int bot, int w, int x0)
    {
      if (!mpOp)
        return;
      for (int i = 0; i < kVizMaxDrops; i++)
      {
        const float age = mpOp->vizDropAge(i);
        if (age < 0.0f || age > 0.35f)
          continue;
        const float ex = mpOp->vizDropX(i);
        if (ex < (float)x0 || ex >= (float)(x0 + w))
          continue;
        const int px = left + (int)(ex - (float)x0);
        const int py = bot + (int)(mpOp->vizDropY(i) + 0.5f);
        float bright = 1.0f - age / 0.35f;
        int c = (int)(GRAY7 + (WHITE - GRAY7) * bright);
        if (c < GRAY7)
          c = GRAY7;
        else if (c > WHITE)
          c = WHITE;
        fb.fillCircle(c, px, py, 1);
      }
    }
#endif

  private:
    Anamnesis *mpOp = 0;
    int mIndex = 0;
    int mCount = 1;
    int mFeature = 0;
    static const int kMetaGW = 24, kMetaGH = 24; // metaball field grid capacity
    // Per-level SLEWED field, persisted across frames -> gentle morph (the
    // screensaver smooths its field too). Zero-init: blobs fade in on insert.
    float mSlewGrid[field::kBubLevels * kMetaGW * kMetaGH] = {};
  };

} // namespace anamnesis
