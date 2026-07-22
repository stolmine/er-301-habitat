#pragma once
static const int BLACK=0, WHITE=15, GRAY7=7, GRAY5=5, GRAY3=3;
// Minimal graphics stub for offline DSP tests of scope units. Not for rendering.
namespace od {
  static const int BLACK = 0, WHITE = 15, GRAY7 = 7;
  class FrameBuffer {
  public:
    void fill(int, int, int, int, int) {}
    void line(int, int, int, int, int) {}
    void pixel(int, int, int) {}
    void clear(int) {}
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
