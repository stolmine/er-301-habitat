#!/usr/bin/env bash
# Scan am335x objects for process() functions with an oversized stack frame.
#
# Background: the SYS/BIOS audio task stack is 2048 BYTES TOTAL, shared by
# every unit in a chain (they process sequentially, so the deepest single
# frame plus its callees is what matters). A unit that puts work buffers on
# process()'s stack instead of in its Internal struct eats that budget.
#
# Pecto did exactly this and blew the stack in the field: five arrays at
# kMaxCombTaps=64 (frac/sA/sB/idx0/idx1 = 1024 bytes) gave it a 1072-byte
# frame, 52% of the whole audio stack. It shipped that way from v2.3.0 and
# went undetected until firmware 9.5.x added stack canaries -- until then it
# was silently corrupting whatever sat past the stack. See
# planning/ledger.toml [hab:pecto-audio-stack-overflow] and CONTEXT.md
# "Audio Thread: Small stack -- use heap for work buffers in process()."
#
# process() AND EVERYTHING IT CALLS is checked. Restricting this to process()
# itself was exactly how [hab:anamnesis-insert-crash] hid: Anamnesis::process is
# a harmless 276 bytes, but reached noise::lut() two hops down, and lut held a
# 2048-byte `int perm[512]` stack local that blew the whole audio stack and wrote
# a Perlin permutation over the BIOS audio Task_Object. The walk below follows
# relocations out of process() transitively.
#
# draw(), constructors and Lua-invoked analysis are not reachable from process()
# and so are not flagged: they run on the UI thread, whose app stack is 32768
# bytes, where a 2 KB frame is unremarkable.
#
# Usage: tools/check-audio-stack.sh [dir ...]      (default: testing/am335x)
#        BUDGET=512 WARN=256 tools/check-audio-stack.sh
# Exit code: 0 clean (warnings allowed), 1 if any frame exceeds BUDGET.

set -euo pipefail

BUDGET="${BUDGET:-512}"
WARN="${WARN:-256}"
OBJDUMP="${OBJDUMP:-arm-none-eabi-objdump}"

if ! command -v "$OBJDUMP" >/dev/null 2>&1; then
  for c in "$HOME"/ti/*/bin/arm-none-eabi-objdump; do
    [[ -x "$c" ]] && OBJDUMP="$c" && break
  done
fi
if ! command -v "$OBJDUMP" >/dev/null 2>&1; then
  echo "error: arm-none-eabi-objdump not found (set OBJDUMP)" >&2
  exit 2
fi

paths=("${@:-testing/am335x}")
objs=()
for p in "${paths[@]}"; do
  [[ -e "$p" ]] || continue
  while IFS= read -r o; do objs+=("$o"); done < <(find "$p" -name '*.o' 2>/dev/null)
done

if [[ ${#objs[@]} -eq 0 ]]; then
  echo "no am335x objects under: ${paths[*]} (build first: make <pkg> ARCH=am335x)" >&2
  exit 2
fi

report=""
fails=0
warns=0

for o in "${objs[@]}"; do
  # Sum the prologue's sp adjustments: GCC splits a large frame across two
  # `sub sp, sp, #imm` when the value doesn't fit one rotated immediate, so
  # taking only the first would under-report. Window is the first 24
  # instructions, which is comfortably past any real prologue.
  # Frame size per symbol + the call graph from relocations, then a transitive
  # walk out of every ::process(). Built on MANGLED names deliberately: demangled
  # symbols contain spaces (e.g. "lut() [clone .part.42]") which breaks field
  # splitting and silently produces an empty graph. Demangle only for display.
  out="$("$OBJDUMP" -dr "$o" 2>/dev/null | python3 -c '
import sys, re, collections, subprocess
frame, calls, cur, n = {}, collections.defaultdict(set), None, 0
for line in sys.stdin:
    m = re.match(r"^[0-9a-f]+ <(.+)>:", line)
    if m:
        cur, n = m.group(1), 0
        frame.setdefault(cur, 0)
        continue
    if cur is None:
        continue
    if "R_ARM_" in line and re.search(r"R_ARM_(CALL|JUMP24|THM_CALL)", line):
        calls[cur].add(line.split()[-1])
        continue
    if line.startswith(" "):
        n += 1
        if n <= 24:
            s = re.search(r"sub\s+sp, sp, #(\d+)", line)
            if s:
                frame[cur] += int(s.group(1))
seen = {s for s in frame if s.endswith("7processEv")}
q = list(seen)
while q:
    for c in calls.get(q.pop(0), ()):
        if c not in seen:
            seen.add(c); q.append(c)
hits = [(frame[s], s) for s in seen if frame.get(s, 0) > 0]
if hits:
    dem = subprocess.run(["c++filt"] + [s for _, s in hits],
                         capture_output=True, text=True).stdout.split("\n")
    for (b, _), d in zip(hits, dem):
        print("%d\t%s" % (b, d.strip()))
')" || true

  while IFS=$'\t' read -r bytes sym; do
    [[ -z "${bytes:-}" ]] && continue
    if (( bytes > BUDGET )); then
      report+=$(printf '  FAIL %6d  %s\n         %s\n' "$bytes" "$sym" "$o")$'\n'
      fails=$((fails+1))
    elif (( bytes > WARN )); then
      report+=$(printf '  warn %6d  %s\n' "$bytes" "$sym")$'\n'
      warns=$((warns+1))
    fi
  done <<< "$out"
done

echo "audio-stack frame check (budget ${BUDGET}B, warn ${WARN}B, audio task stack is 2048B total)"
[[ -n "$report" ]] && printf '%s' "$report"

if (( fails > 0 )); then
  cat <<EOF

${fails} process() frame(s) over the ${BUDGET}-byte budget.

The audio task stack is 2048 bytes for the WHOLE chain. Move work buffers off
process()'s stack and into the unit's Internal struct (heap), per CONTEXT.md
"Audio Thread". Stack-local arrays are also the Cortex-A8 NEON alignment trap,
so this usually fixes two problems at once.
EOF
  exit 1
fi

echo "OK: no process() frame over ${BUDGET}B (${warns} warning(s))"
exit 0
