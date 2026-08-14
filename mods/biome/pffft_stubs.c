// Provide libc/libm symbols that pffft needs but the ER-301 ELF loader
// doesn't export. These are only called during pffft_new_setup() (init).
//
// WARNING: malloc/free defined here override the global allocator for
// the entire shared library. Only safe because pffft is the only code
// that calls malloc/free directly (C++ new/delete use the firmware's
// allocator via a different path on the ER-301).

#include <stddef.h>

// cosf/sinf — needed by pffft for twiddle factor init.
// On ER-301 ARM, these aren't exported by the ELF loader.
// Bhaskara approximation, adequate for init-time use only.
static const float PI_F = 3.14159265358979323846f;

// Use weak attribute so firmware's real sinf/cosf take priority if available
float __attribute__((weak)) cosf(float x) {
  float tp = 1.0f / (2.0f * PI_F);
  x *= tp;
  x -= 0.25f + (float)(int)(x + 0.25f);
  x *= 16.0f * ((x < 0 ? -x : x) - 0.5f);
  x += 0.225f * x * ((x < 0 ? -x : x) - 1.0f);
  return x;
}

float __attribute__((weak)) sinf(float x) {
  return cosf(x - PI_F * 0.5f);
}

// Bump allocator for pffft_new_setup and pffft_aligned_malloc.
// Total per init cycle: ~4KB for 256-point FFT.
static char pffft_heap[32768] __attribute__((aligned(64)));
static size_t pffft_heap_pos = 0;

void *malloc(size_t size) {
  // Reset heap on setup allocation (small size = PFFFT_Setup struct)
  if (size < 256) {
    pffft_heap_pos = 0;
  }

  // 64-byte alignment
  pffft_heap_pos = (pffft_heap_pos + 63) & ~(size_t)63;
  if (pffft_heap_pos + size > sizeof(pffft_heap)) {
    return (void *)0;
  }
  void *p = &pffft_heap[pffft_heap_pos];
  pffft_heap_pos += size;
  return p;
}

void free(void *p) {
  (void)p;
}
