#!/usr/bin/env bash
# Builds and runs the Breccia Glitch=0 bit-identity harness against the REAL
# mods/spreadsheet/Breccia.cpp with the shipping optimization semantics.
# Exit 0 = invariant holds (all scenarios bit-identical to the plain slicer).
set -e
cd "$(dirname "$0")"
g++ -O3 -ffast-math -fno-tree-vectorize -msse4 -std=gnu++11 -w \
    -Istub -I../../mods/spreadsheet main.cpp -o /tmp/breccia-inv-test
exec /tmp/breccia-inv-test
