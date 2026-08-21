#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
rm -rf stub; mkdir -p stub/od/objects stub/hal
cp ../parametric-eq-test/stub/od/config.h stub/od/ 2>/dev/null || cat > stub/od/config.h <<'H'
#pragma once
struct _GlobalConfig { int frameLength = 128; float sampleRate = 48000.0f; };
extern _GlobalConfig globalConfig;
#define FRAMELENGTH ((int)(4 * (globalConfig.frameLength / 4)))
H
bash ../parametric-eq-test/run.sh >/dev/null 2>&1 || true
cp ../parametric-eq-test/stub/od/config.h stub/od/config.h
cp ../parametric-eq-test/stub/hal/ops.h stub/hal/ops.h
cp ../parametric-eq-test/stub/od/objects/Object.h stub/od/objects/Object.h
g++ -O3 -ffast-math -fno-tree-vectorize -msse4 -std=gnu++11 -w \
    -Istub -I../../mods/house/atoms main.cpp -o /tmp/channel-strip-test -lm
exec /tmp/channel-strip-test
