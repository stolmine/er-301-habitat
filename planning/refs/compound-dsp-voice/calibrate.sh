#!/usr/bin/env bash
# Loopback level calibration helper.
#
# Plays a calibration tone out the default sink, records the return off the
# default source, and prints the return's peak/RMS (and clip count). Rerun it
# while nudging the 301 input VCA until return RMS matches the tone's RMS.
#
# One-time setup: point the PipeWire defaults at the MOTU endpoints in the loop.
#   pactl list short sinks ;  pactl list short sources
#   pactl set-default-sink   <MOTU line out feeding the 301>
#   pactl set-default-source <MOTU input taking the return>
#
# Usage:  ./calibrate.sh cal_1k_rms-18.wav
#         ./calibrate.sh cal_1k_rms-18.wav 3      # capture 3 s (default 3)
set -u
TONE="${1:?usage: calibrate.sh <tone.wav> [seconds]}"
SECS="${2:-3}"
OUT="$(mktemp --suffix=.wav)"

pw-play "$TONE" >/dev/null 2>&1 &
PLAY=$!
sleep 0.4                                   # let the tone reach steady state
arecord -f S24_3LE -r 48000 -c 2 -d "$SECS" "$OUT" >/dev/null 2>&1 \
  || arecord -f S16_LE -r 48000 -c 2 -d "$SECS" "$OUT" >/dev/null 2>&1
kill "$PLAY" 2>/dev/null; wait "$PLAY" 2>/dev/null

echo "tone : $(basename "$TONE")"
echo "== return =="
sox "$OUT" -n stats 2>&1 | grep -E "Pk lev dB|RMS lev dB|Pk count|Crest factor"
rm -f "$OUT"
