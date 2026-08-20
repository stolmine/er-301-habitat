#!/usr/bin/env bash
# Measures the REAL mods/house/atoms/ParametricBand.h with shipping flags.
set -e
cd "$(dirname "$0")"
mkdir -p stub/od
cat > stub/od/config.h <<'H'
#pragma once
struct _GlobalConfig { int frameLength = 128; float sampleRate = 48000.0f; };
extern _GlobalConfig globalConfig;
#define FRAMELENGTH ((int)(4 * (globalConfig.frameLength / 4)))
H
g++ -O3 -ffast-math -fno-tree-vectorize -msse4 -std=gnu++11 -w \
    -Istub -I../../mods/house/atoms main.cpp -o /tmp/parametric-band-test -lm
exec /tmp/parametric-band-test
