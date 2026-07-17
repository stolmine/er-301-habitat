#!/usr/bin/env bash
# Regenerate the loopback calibration tones: 1 kHz sines, stereo (avoids
# PipeWire downmix halving), 48 kHz / 24-bit, 6 s. Named by RMS dBFS;
# gain arg = peak dBFS = RMS + 3.01 (sine). cal_1k_rms-18 is the nominal
# operating level; cal_1k_hot is the -1 dBFS ceiling / THD-floor check.
set -e
cd "$(dirname "$0")"
gen() { sox -n -r 48000 -c 2 -b 24 "$1" synth 6 sine 1000 gain "$2"; }
gen cal_1k_rms-30.wav -27
gen cal_1k_rms-24.wav -21
gen cal_1k_rms-18.wav -15
gen cal_1k_rms-12.wav -9
gen cal_1k_rms-06.wav -3
gen cal_1k_hot.wav    -1
echo "regenerated calibration tones in $(pwd)"
