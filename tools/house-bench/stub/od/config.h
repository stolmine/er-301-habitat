#pragma once
#include <cstdint>
#include <cstddef>
namespace od {
  struct ConfigData { int sampleRate; int frameLength; };
  extern ConfigData globalConfig;
}
using od::globalConfig;
#define FRAMELENGTH ((int)(4 * (globalConfig.frameLength / 4)))
