#pragma once
static const int BLACK=0, WHITE=15, GRAY7=7, GRAY5=5, GRAY3=3;
// Minimal graphics stub for offline DSP tests of scope units. Not for rendering.
namespace od {
  static const int BLACK = 0, WHITE = 15, GRAY7 = 7;
  class FrameBuffer {
  public:
    // Buffer-backed (4-bit gray in a byte plane) so offline benches of draw()
    // paths do real reads/writes -- an all-no-op stub lets the compiler
    // dead-code-eliminate fill loops and under-measures them.
    static const int kW = 256, kH = 64;
    unsigned char mPlane[kW * kH] = {};
    void fill(int, int, int, int, int) {}
    void line(int, int, int, int, int) {}
    void pixel(int c, int x, int y)
    {
      if (x >= 0 && x < kW && y >= 0 && y < kH) mPlane[y * kW + x] = (unsigned char)c;
    }
    int readPixel(int x, int y)
    {
      if (x >= 0 && x < kW && y >= 0 && y < kH) return mPlane[y * kW + x];
      return 0;
    }
    void fillCircle(int, int, int, int) {}
    void clear(int) {}
    void text(int, int, int, const char*, int) {}
    void hline(int, int, int, int, int d=0) {}
    void vline(int, int, int, int, int d=0) {}
  };
  class ReferenceCounted { public: void attach() {} void release() {} };
  class Graphic {
  public:
    Graphic(int l, int b, int w, int h)
      : mWorldLeft(l), mWorldBottom(b), mWidth(w), mHeight(h) {}
    virtual ~Graphic() {}
    virtual void draw(FrameBuffer &) {}
    void addChild(Graphic *) {}
    int mWorldLeft, mWorldBottom, mWidth, mHeight;
  };
}
