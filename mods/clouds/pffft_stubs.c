// Provide libc/libm symbols that pffft needs but the ER-301 ELF loader
// doesn't export. These are only called during pffft_new_setup() (init).

#include <stddef.h>

// cosf/sinf — Bhaskara approximation, adequate for twiddle factor init
static const float PI_F = 3.14159265358979323846f;

float cosf(float x) {
  float tp = 1.0f / (2.0f * PI_F);
  x *= tp;
  x -= 0.25f + (float)(int)(x + 0.25f);
  x *= 16.0f * ((x < 0 ? -x : x) - 0.5f);
  x += 0.225f * x * ((x < 0 ? -x : x) - 1.0f);
  return x;
}

float sinf(float x) {
  return cosf(x - PI_F * 0.5f);
}

// Bump allocator — resets each time pffft_new_setup runs via malloc(sizeof(PFFFT_Setup))
// pffft calls: malloc(setup), pffft_aligned_malloc(data), and our shim adds 3 aligned bufs
// Total per init cycle: ~80KB. Reset on each new setup creation.
static char pffft_heap[131072] __attribute__((aligned(64)));
static size_t pffft_heap_pos = 0;
static int pffft_setup_count = 0;

void *malloc(size_t size) {
  // Reset heap when a new PFFFT_Setup is allocated (first malloc per init cycle)
  // pffft_new_setup's first malloc is for the setup struct (~64-128 bytes)
  if (size < 256) {
    pffft_setup_count++;
    // Reset on first setup of each init cycle (phase_vocoder calls init twice for stereo)
    if (pffft_setup_count == 1) {
      pffft_heap_pos = 0;
    }
  }

  // 64-byte alignment for NEON cache line optimization
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
  // Track setup destructions to know when to allow reset
  if (p) pffft_setup_count--;
}
