#!/usr/bin/env bash
# Live Line5 return meter (robust text meter; see meter.py). Ctrl-C to stop.
exec python3 "$(dirname "$0")/meter.py" "$@"
