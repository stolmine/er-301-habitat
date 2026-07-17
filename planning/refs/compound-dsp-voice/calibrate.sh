#!/usr/bin/env bash
# Loopback level-calibration helper.
#
# Targets the MOTU nodes EXPLICITLY -- WirePlumber re-picks the "default"
# source, so never rely on it. 48 kHz throughout (the 301's rate).
#   SINK = MOTU Line2 (outputs 3-4; out 3 -> 301 input)
#   SRC  = MOTU Line5 (inputs 3-4; euro->line return, mono duplicated to stereo)
# Override with SINK=/SRC= env vars if the routing changes.
#
# Usage:  ./calibrate.sh cal_1k_rms-18.wav [seconds]
# Rerun after each 301-VCA nudge until return RMS matches the tone's RMS.
set -u
SINK="${SINK:-alsa_output.usb-MOTU_M4_M4MA0617JK-00.HiFi__Line2__sink}"
SRC="${SRC:-alsa_input.usb-MOTU_M4_M4MA0617JK-00.HiFi__Line5__source}"
TONE="${1:?usage: calibrate.sh <tone.wav> [seconds]}"
SECS="${2:-3}"
OUT="${TONE%.wav}__return.wav"

pw-play --target "$SINK" "$TONE" >/dev/null 2>&1 &
PLAY=$!
sleep 0.4                                        # let the tone reach steady state
parec -d "$SRC" --rate=48000 --channels=2 --format=float32le --no-remix 2>/dev/null \
  | sox -t raw -r 48000 -c 2 -e float -b 32 - "$OUT" trim 0 "$SECS"
kill "$PLAY" 2>/dev/null; wait "$PLAY" 2>/dev/null

echo "tone   : $(basename "$TONE")"
echo "return : $(basename "$OUT")  (Line5)"
sox "$OUT" -n stats 2>&1 | grep -E "Pk lev dB|RMS lev dB|Pk count|Crest factor"
