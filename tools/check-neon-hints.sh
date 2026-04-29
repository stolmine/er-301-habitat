#!/usr/bin/env bash
# Scan an am335x .o for NEON load/store ops with strict-alignment hints
# (`:64`, `:128`). Per the codex (project_ngoma_codex.md, feedback_neon_*),
# Cortex-A8 traps when the hinted address misses the alignment guarantee.
#
# Usage: tools/check-neon-hints.sh <object-file>
#
# Output groups every hinted vld1/vst1 by symbol and flags suspect patterns.
# Classification rules (planning/ngoma-debug-pipeline.md):
#   safe    - [sp :64] on a single-D-register store (AAPCS guarantees 8-byte SP)
#   suspect - [sp :64] with quad-D operand (register-spill across a call)
#   suspect - [reg :64] / [reg :128] on a non-SP base register
#                       (stack-local NEON or auto-vectorized init/ctor)
#
# Exit code: 0 always (advisory tool). Counts are summarized at the end.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <object-file>" >&2
  exit 2
fi

OBJ="$1"
if [[ ! -f "$OBJ" ]]; then
  echo "error: $OBJ not found" >&2
  exit 2
fi

OBJDUMP="${OBJDUMP:-arm-none-eabi-objdump}"
if ! command -v "$OBJDUMP" >/dev/null 2>&1; then
  echo "error: $OBJDUMP not on PATH (set OBJDUMP env var to override)" >&2
  exit 2
fi

DUMP="$("$OBJDUMP" -d -C "$OBJ")"

awk -v obj="$OBJ" '
  function flush_symbol(   line, hidx, hstr) {
    if (sym == "" || hint_count == 0) return
    printf "\n== %s\n", sym
    for (hidx = 0; hidx < hint_count; hidx++) {
      hstr = hints[hidx]
      printf "  %s\n", hstr
    }
  }

  /^[0-9a-f]+ <.*>:$/ {
    flush_symbol()
    sym = $0
    sub(/^[0-9a-f]+ </, "", sym)
    sub(/>:$/, "", sym)
    hint_count = 0
    next
  }

  # Match vld1/vst1 lines that include an alignment hint (`:64` or `:128`)
  /\<v(ld|st)1\.[0-9]+/ && /:(64|128)\]/ {
    op_total++
    line = $0
    sub(/^[ \t]+/, "", line)

    # Classify
    is_quad = (line ~ /\{d[0-9]+-d[0-9]+\}/) || (line ~ /\{q[0-9]+\}/)
    is_sp   = (line ~ /\[sp[, ]/) || (line ~ /\[sp\]/)

    tag = "?"
    if (is_sp && !is_quad) {
      tag = "safe   "
      safe_count++
    } else if (is_sp && is_quad) {
      tag = "SUSPECT"
      suspect_count++
      suspect_kinds["sp-quad-spill"]++
    } else {
      tag = "SUSPECT"
      suspect_count++
      suspect_kinds["non-sp-hint"]++
    }

    hints[hint_count++] = sprintf("[%s] %s", tag, line)
  }

  END {
    flush_symbol()
    printf "\n--- summary for %s ---\n", obj
    printf "total hinted vld1/vst1 ops : %d\n", op_total + 0
    printf "  safe (sp single-D)       : %d\n", safe_count + 0
    printf "  suspect (sp quad-D)      : %d\n", suspect_kinds["sp-quad-spill"] + 0
    printf "  suspect (non-sp hint)    : %d\n", suspect_kinds["non-sp-hint"] + 0
    if ((suspect_count + 0) > 0) {
      printf "\nNOTE: Cortex-A8 with -O3 -ffast-math can trap on suspect hints.\n"
      printf "      See feedback_neon_intrinsics_drumvoice.md / feedback_neon_hint_surfaces.md.\n"
    }
  }
' <<< "$DUMP"
