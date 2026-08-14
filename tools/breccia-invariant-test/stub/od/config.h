// Test stub for the Breccia invariant harness. Mirrors the fields of the real
// od/config.h that Breccia.cpp touches. frameLength 128 / sampleRate 48k.
#pragma once

struct _GlobalConfig
{
  int frameLength = 128;
  float sampleRate = 48000.0f;
};

extern _GlobalConfig globalConfig;

#define FRAMELENGTH ((int)(4 * (globalConfig.frameLength / 4)))
