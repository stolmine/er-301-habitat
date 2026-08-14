#!/usr/bin/env bash
# Functional harness for the Spectral Freeze phase vocoder. Compiles the REAL
# mods/biome/SpectralFreeze.cpp with the shipping optimization semantics and drives it
# with synthetic audio. Asserts the properties the design doc claims.
set -e
cd "$(dirname "$0")"
g++ -O3 -ffast-math -fno-tree-vectorize -msse4 -std=gnu++11 -w \
    -Istub -I../../mods/biome \
    main.cpp ../../mods/biome/pffft.c -o /tmp/spectral-freeze-test -lm
exec /tmp/spectral-freeze-test
