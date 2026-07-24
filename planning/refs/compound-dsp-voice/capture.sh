#!/usr/bin/env bash
# Capture-runner for the compound DSP voice matrix (see capture-matrix.md).
#
# Plays an excitation out the 301 (Line2 sink), STEREO-records the Line5 return
# (L = first tap, R = second tap), saves raw/<name>.wav, prints per-channel
# capture-time stats, and appends a line to raw/manifest.tsv.
#
# The recorder starts PREROLL seconds BEFORE the excitation so parec's ~2 s
# device-open latency never eats the start of the sweep (which would break the
# deconvolution). The saved file therefore has ~1-1.5 s of leading silence (a
# free noise-floor sample) then the excitation.
#
# Explicit MOTU targets (WirePlumber's default source is unreliable). 48 kHz.
#
# Usage:  ./capture.sh <name> <excitation.wav>
#   e.g.  ./capture.sh bl_C_A_no-lo-a_m+lp_base ess.wav
#         ./capture.sh bl_D_A_lp-lo-a_lp_gcw   s1k.wav
set -u
SINK="${SINK:-alsa_output.usb-MOTU_M4_M4MA0617JK-00.HiFi__Line2__sink}"
SRC="${SRC:-alsa_input.usb-MOTU_M4_M4MA0617JK-00.HiFi__Line5__source}"
NAME="${1:?usage: capture.sh <name> <excitation.wav>}"
EXC="${2:?usage: capture.sh <name> <excitation.wav>}"
[ -f "$EXC" ] || { echo "no such excitation: $EXC" >&2; exit 1; }

PREROLL=3.5; TAIL=0.5
EXCDUR=$(sox --i -D "$EXC")
CAPDUR=$(awk "BEGIN{printf \"%.2f\", $EXCDUR + $PREROLL + $TAIL}")
mkdir -p raw
OUT="raw/${NAME}.wav"

echo "capture '$NAME'  <- $EXC (${EXCDUR}s)   window ${CAPDUR}s"
# playback fires PREROLL seconds into the capture window (background subshell)
( sleep "$PREROLL"; pw-play --target "$SINK" "$EXC" >/dev/null 2>&1 ) &
# record the whole window; timeout closes parec -> sox finalizes the wav
timeout -s KILL "$CAPDUR" parec -d "$SRC" --rate=48000 --channels=2 \
  --format=float32le --no-remix 2>/dev/null \
  | sox -t raw -r 48000 -c 2 -e float -b 32 - "$OUT"
wait 2>/dev/null

if [ ! -s "$OUT" ]; then echo "CAPTURE FAILED (empty file)" >&2; exit 1; fi

echo "-> $OUT"
# Stats on the STEADY MID-REGION (skips the pre-roll silence + tail, which would
# otherwise dilute RMS). Read before any FFT (three-sisters "headline" discipline).
DUR=$(sox --i -D "$OUT")
MS=$(awk "BEGIN{printf \"%.2f\", $DUR*0.35}")
MD=$(awk "BEGIN{printf \"%.2f\", $DUR*0.45}")
STATS=$(sox "$OUT" -n trim "$MS" "$MD" stats 2>&1)
echo "$STATS" | grep -E "Pk lev dB|RMS lev dB|Crest factor" | sed 's/^/  /'
ROUGH=$(sox "$OUT" -n trim "$MS" "$MD" stat 2>&1 | awk '/Rough/{print $3}')
echo "  Rough freq (mid): ${ROUGH} Hz  (meaningful for tones, not sweeps)"
# Real clip check: peak at/near 0 dBFS (Pk count alone just means "samples at the
# peak level", which is normal below full scale).
echo "$STATS" | awk '/Pk lev dB/{if($5>-0.3||$6>-0.3) print "  *** CLIP: Pk "$5"/"$6" dBFS ***"}'

# manifest: name, exc, L/R RMS dB, L/R peak dB, rough freq
LR=$(echo "$STATS" | awk '/RMS lev dB/{r=$5" "$6} /Pk lev dB/{p=$5" "$6} END{print r, p}')
printf "%s\t%s\t%s\t%s\n" "$NAME" "$EXC" "$LR" "$ROUGH" >> raw/manifest.tsv
