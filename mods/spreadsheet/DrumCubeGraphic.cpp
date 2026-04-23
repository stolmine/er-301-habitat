#include "DrumCubeGraphic.h"
#include <stdint.h>

namespace stolmine {

static const float kVerts[8][3] = {
    {-1,-1,-1}, {+1,-1,-1}, {+1,+1,-1}, {-1,+1,-1},
    {-1,-1,+1}, {+1,-1,+1}, {+1,+1,+1}, {-1,+1,+1},
};
static const int kFaces[6][4] = {
    {0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {4,5,1,0}, {3,2,6,7},
};

DrumCubeGraphic::DrumCubeGraphic(int left, int bottom, int width, int height)
    : od::Graphic(left, bottom, width, height) {}

DrumCubeGraphic::~DrumCubeGraphic() {
    if (mpDrum) mpDrum->release();
}

void DrumCubeGraphic::follow(DrumVoice *p) {
    if (mpDrum) mpDrum->release();
    mpDrum = p;
    if (mpDrum) mpDrum->attach();
}

void DrumCubeGraphic::fillTriangle(od::FrameBuffer &fb, int gray, int dotting, int faceIdx, bool gritNoise,
                                    int x0, int y0, int x1, int y1, int x2, int y2) {
    if (y1 < y0) { int t; t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; }
    if (y2 < y0) { int t; t=x0;x0=x2;x2=t; t=y0;y0=y2;y2=t; }
    if (y2 < y1) { int t; t=x1;x1=x2;x2=t; t=y1;y1=y2;y2=t; }

    int left = mWorldLeft;
    int right = mWorldLeft + mWidth - 1;
    int bot = mWorldBottom;
    int top = mWorldBottom + mHeight - 1;

    for (int y = y0; y <= y2; y++) {
        if (y < bot || y > top) continue;
        float t02 = (y2 == y0) ? 1.0f : (float)(y - y0) / (float)(y2 - y0);
        int xa = x0 + (int)((x2 - x0) * t02);
        int xb;
        if (y <= y1) {
            float t01 = (y1 == y0) ? 1.0f : (float)(y - y0) / (float)(y1 - y0);
            xb = x0 + (int)((x1 - x0) * t01);
        } else {
            float t12 = (y2 == y1) ? 1.0f : (float)(y - y1) / (float)(y2 - y1);
            xb = x1 + (int)((x2 - x1) * t12);
        }
        int xl = xa < xb ? xa : xb;
        int xr = xa < xb ? xb : xa;
        if (xl < left) xl = left;
        if (xr > right) xr = right;
        int lineDotting = dotting;
        if (gritNoise) lineDotting ^= ((y * 7 + faceIdx) & 1);
        if (xl <= xr) fb.hline(gray, xl, xr, y, lineDotting);
    }
}

void DrumCubeGraphic::fillQuad(od::FrameBuffer &fb, int gray, int dotting, int faceIdx, bool gritNoise,
                                int x0, int y0, int x1, int y1,
                                int x2, int y2, int x3, int y3) {
    fillTriangle(fb, gray, dotting, faceIdx, gritNoise, x0, y0, x1, y1, x2, y2);
    fillTriangle(fb, gray, dotting, faceIdx, gritNoise, x0, y0, x2, y2, x3, y3);
}

void DrumCubeGraphic::draw(od::FrameBuffer &fb) {
    fb.fill(BLACK, mWorldLeft, mWorldBottom, mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);

    if (!mpDrum) return;

    float character = mpDrum->getCharacter();
    float shape     = mpDrum->getShape();
    float grit      = mpDrum->getGrit();
    float envLevel  = mpDrum->getEnvLevel();

    float attackCoeff = 1.0f;
    float decayCoeff  = 0.18f;
    float coeff = (envLevel > mPunchEnergy) ? attackCoeff : decayCoeff;
    mPunchEnergy += (envLevel - mPunchEnergy) * coeff;

    mAngleY += 0.015f + grit * 0.04f;
    mAngleX += 0.007f + grit * 0.02f;
    static const float kTwoPi = 6.28318530718f;
    if (mAngleY > kTwoPi) mAngleY -= kTwoPi;
    if (mAngleX > kTwoPi) mAngleX -= kTwoPi;

    float vx[8], vy[8], vz[8];
    for (int i = 0; i < 8; i++) {
        vx[i] = kVerts[i][0];
        vy[i] = kVerts[i][1];
        vz[i] = kVerts[i][2];
    }

    float sphereT = character * 2.0f;
    if (sphereT > 1.0f) sphereT = 1.0f;
    float foldT = (character > 0.5f) ? (character - 0.5f) * 2.0f : 0.0f;
    static const float kSqrt3 = 1.73205080757f;

    for (int i = 0; i < 8; i++) {
        float x = vx[i], y = vy[i], z = vz[i];
        float len = sqrtf(x*x + y*y + z*z);
        if (len > 0.0001f) {
            float nx = x / len, ny = y / len, nz = z / len;
            float sx = nx * kSqrt3, sy = ny * kSqrt3, sz = nz * kSqrt3;
            vx[i] = x + (sx - x) * sphereT;
            vy[i] = y + (sy - y) * sphereT;
            vz[i] = z + (sz - z) * sphereT;
            if (foldT > 0.0f) {
                float wave = sinf(len * 6.0f + mAngleY * 3.0f);
                float disp = foldT * 0.25f * wave;
                vx[i] += nx * disp;
                vy[i] += ny * disp;
                vz[i] += nz * disp;
            }
        }
    }

    static const int kBottomFaceVerts[4] = {4, 5, 1, 0};
    static const int kTopFaceVerts[4]    = {3, 2, 6, 7};
    for (int k = 0; k < 4; k++) {
        vy[kBottomFaceVerts[k]] += shape * 0.6f;
        vy[kTopFaceVerts[k]]    -= shape * 0.6f;
    }

    for (int i = 0; i < 8; i++) {
        uint32_t h = ((uint32_t)i * 2654435761u) ^ (uint32_t)(mAngleY * 1000.0f);
        float jx = (float)((h & 0xFF))        / 255.0f - 0.5f;
        float jy = (float)((h >> 8  & 0xFF))  / 255.0f - 0.5f;
        float jz = (float)((h >> 16 & 0xFF))  / 255.0f - 0.5f;
        vx[i] += jx * grit * 0.3f;
        vy[i] += jy * grit * 0.3f;
        vz[i] += jz * grit * 0.3f;
    }

    float cosY = cosf(mAngleY), sinY = sinf(mAngleY);
    float cosX = cosf(mAngleX), sinX = sinf(mAngleX);

    int cx = mWorldLeft + mWidth / 2;
    int cy = mWorldBottom + mHeight / 2;
    float punchScale = 1.0f + mPunchEnergy * 0.5f;

    int px[8], py[8];
    float pz[8];
    for (int i = 0; i < 8; i++) {
        float x = vx[i], y = vy[i], z = vz[i];
        float rx  =  x * cosY + z * sinY;
        float rz  = -x * sinY + z * cosY;
        float ry  =  y * cosX - rz * sinX;
        float rzF =  y * sinX + rz * cosX;
        px[i] = cx + (int)(rx * 12.0f * punchScale);
        py[i] = cy + (int)(ry * 17.0f * punchScale);
        pz[i] = rzF;
    }

    int visIdx[6];
    float visZ[6];
    int visCount = 0;

    for (int f = 0; f < 6; f++) {
        int i0 = kFaces[f][0], i1 = kFaces[f][1], i2 = kFaces[f][2];
        int ax = px[i1] - px[i0], ay = py[i1] - py[i0];
        int bx = px[i2] - px[i1], by = py[i2] - py[i1];
        int cross = ax * by - ay * bx;
        if (cross > 0) {
            float avgZ = (pz[i0] + pz[i1] + pz[i2] + pz[kFaces[f][3]]) * 0.25f;
            visIdx[visCount] = f;
            visZ[visCount]   = avgZ;
            visCount++;
        }
    }

    for (int a = 0; a < visCount - 1; a++) {
        for (int b = a + 1; b < visCount; b++) {
            if (visZ[b] > visZ[a]) {
                float tz = visZ[a]; visZ[a] = visZ[b]; visZ[b] = tz;
                int ti  = visIdx[a]; visIdx[a] = visIdx[b]; visIdx[b] = ti;
            }
        }
    }

    int dotting = (int)((1.0f - character) * 3.0f);
    bool gritNoise = grit > 0.3f;

    for (int fi = 0; fi < visCount; fi++) {
        int f = visIdx[fi];
        float avgZ = visZ[fi];
        float depthT = (avgZ + 2.0f) * 0.25f;
        if (depthT < 0.0f) depthT = 0.0f;
        if (depthT > 1.0f) depthT = 1.0f;
        int gray = 3 + (int)(depthT * 11.0f);
        if (mPunchEnergy > 0.1f) {
            gray += (mPunchEnergy > 0.5f) ? 2 : 1;
        }
        if (gray > WHITE) gray = WHITE;

        int faceGray = gray;
        if (mPunchEnergy > 0.3f)
            faceGray = gray - (int)(mPunchEnergy * 4.0f);
        if (faceGray < 2) faceGray = 2;

        int i0 = kFaces[f][0], i1 = kFaces[f][1], i2 = kFaces[f][2], i3 = kFaces[f][3];
        fillQuad(fb, faceGray, dotting, f, gritNoise, px[i0], py[i0], px[i1], py[i1], px[i2], py[i2], px[i3], py[i3]);

        int edgeGray = gray + 3;
        if (edgeGray > WHITE) edgeGray = WHITE;
        fb.line(edgeGray, px[i0], py[i0], px[i1], py[i1]);
        fb.line(edgeGray, px[i1], py[i1], px[i2], py[i2]);
        fb.line(edgeGray, px[i2], py[i2], px[i3], py[i3]);
        fb.line(edgeGray, px[i3], py[i3], px[i0], py[i0]);
    }
}

} // namespace stolmine
