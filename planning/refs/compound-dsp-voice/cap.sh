#!/usr/bin/env bash
# State-logging capture wrapper. Runs capture.sh AND appends the COMPLETE module
# state to states.tsv, so every capture is fully observable and drift is catchable.
#
# Usage:
#   ./cap.sh <name> <exc.wav> cutA=<> cutB=<> res=<> gainA=<> gainB=<> \
#            mode=<hp|ap|no|hid|lp|bp> clk=<a|b|both> alias=<lo|hi> \
#            route=<A|B|ser|par> input=<A|B|AB> tap=<e.g. Abp+Alp> vca=<x5..> [note=<>]
#
# Knob positions use the ccw/9/12/3/cw convention (or numeric). Any unspecified
# field logs "?" - fill them ALL for real captures.
set -u
NAME="${1:?name}"; EXC="${2:?exc}"; shift 2
declare -A S=( [cutA]="?" [cutB]="?" [res]="?" [gainA]="?" [gainB]="?" [mode]="?"
               [clk]="?" [alias]="?" [route]="?" [input]="?" [tap]="?" [vca]="?" [note]="" )
for kv in "$@"; do k="${kv%%=*}"; v="${kv#*=}"; S[$k]="$v"; done

DIR="$(dirname "$0")"; ST="$DIR/states.tsv"
if [ ! -f "$ST" ]; then
  printf "name\tcutA\tcutB\tres\tgainA\tgainB\tmode\tclk\talias\troute\tinput\ttap\tvca\texc\tnote\n" > "$ST"
fi

"$DIR/capture.sh" "$NAME" "$EXC"
rc=$?
printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
  "$NAME" "${S[cutA]}" "${S[cutB]}" "${S[res]}" "${S[gainA]}" "${S[gainB]}" \
  "${S[mode]}" "${S[clk]}" "${S[alias]}" "${S[route]}" "${S[input]}" "${S[tap]}" \
  "${S[vca]}" "$EXC" "${S[note]}" >> "$ST"
echo "  logged state -> states.tsv"
exit $rc
