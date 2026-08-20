#!/usr/bin/env bash
# Measures the REAL mods/spreadsheet/atoms/StftFrontEnd.h with shipping flags.
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
    -Istub -I../../mods/spreadsheet/atoms -I../../mods/spreadsheet \
    main.cpp ../../mods/spreadsheet/pffft.c -o /tmp/stft-frontend-test -lm 2>/dev/null || \
g++ -O3 -ffast-math -fno-tree-vectorize -msse4 -std=gnu++11 -w \
    -Istub -I../../mods/spreadsheet/atoms -I../../mods/biome \
    main.cpp ../../mods/biome/pffft.c -o /tmp/stft-frontend-test -lm
exec /tmp/stft-frontend-test
