#pragma once
struct _GlobalConfig { int frameLength = 128; float sampleRate = 48000.0f; };
extern _GlobalConfig globalConfig;
#define FRAMELENGTH ((int)(4 * (globalConfig.frameLength / 4)))
