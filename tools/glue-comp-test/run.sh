#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
rm -rf stub; mkdir -p stub/od
cat > stub/od/config.h <<'H'
#pragma once
struct _GlobalConfig { int frameLength = 128; float sampleRate = 48000.0f; };
extern _GlobalConfig globalConfig;
#define FRAMELENGTH ((int)(4 * (globalConfig.frameLength / 4)))
H
g++ -O3 -ffast-math -fno-tree-vectorize -msse4 -std=gnu++11 -w \
    -Istub -I../../mods/house/atoms main.cpp -o /tmp/glue-comp-test -lm
exec /tmp/glue-comp-test
