// CMSIS-compatible shim backed by pffft (NEON-optimized FFT)
// Provides arm_rfft_fast_* functions expected by Clouds' USE_ARM_FFT path

#ifndef ARM_MATH_H
#define ARM_MATH_H

#include "pffft.h"
#include <string.h>
#include <stdlib.h>

typedef float float32_t;
typedef int arm_status;
#define ARM_MATH_SUCCESS 0

typedef struct {
  PFFFT_Setup *setup;
  float *work;
  float *aligned_in;
  float *aligned_out;
  int fftLen;
} arm_rfft_fast_instance_f32;

typedef arm_rfft_fast_instance_f32 arm_cfft_instance_f32;

static inline arm_status arm_rfft_fast_init_f32(
    arm_rfft_fast_instance_f32 *S, unsigned short fftLen)
{
  S->fftLen = fftLen;
  S->setup = pffft_new_setup(fftLen, PFFFT_REAL);
  S->work = (float *)pffft_aligned_malloc(fftLen * sizeof(float));
  S->aligned_in = (float *)pffft_aligned_malloc(fftLen * sizeof(float));
  S->aligned_out = (float *)pffft_aligned_malloc(fftLen * sizeof(float));
  return ARM_MATH_SUCCESS;
}

static inline void arm_rfft_fast_f32(
    arm_rfft_fast_instance_f32 *S,
    float *p, float *pOut, unsigned char ifftFlag)
{
  int N = S->fftLen;

  // Copy to aligned buffer
  memcpy(S->aligned_in, p, N * sizeof(float));

  if (ifftFlag == 0)
  {
    // Forward FFT
    pffft_transform_ordered(S->setup, S->aligned_in, S->aligned_out,
                            S->work, PFFFT_FORWARD);
  }
  else
  {
    // Inverse FFT + normalize by 1/N (CMSIS convention)
    pffft_transform_ordered(S->setup, S->aligned_in, S->aligned_out,
                            S->work, PFFFT_BACKWARD);
    float scale = 1.0f / N;
    for (int i = 0; i < N; i++)
    {
      S->aligned_out[i] *= scale;
    }
  }

  // Copy from aligned buffer
  memcpy(pOut, S->aligned_out, N * sizeof(float));
}

#endif // ARM_MATH_H
