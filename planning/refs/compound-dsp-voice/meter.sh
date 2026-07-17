#!/usr/bin/env bash
# Live peak/RMS meter (dB) on the MOTU Line5 return, targeted explicitly so it
# ignores the WirePlumber default. Ctrl-C to quit; spacebar resets peak-hold.
# The return is mono duplicated to stereo, so both channels read the same.
SRC="${SRC:-alsa_input.usb-MOTU_M4_M4MA0617JK-00.HiFi__Line5__source}"
parec -d "$SRC" --rate=48000 --channels=2 --format=s16le --no-remix \
  | ecasignalview -f:s16_le,2,48000 -r:200 -L stdin
