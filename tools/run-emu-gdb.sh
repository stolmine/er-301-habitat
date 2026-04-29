#!/usr/bin/env bash
# Launch the linux emu under gdb with auto-bt on SIGSEGV / SIGBUS / SIGFPE.
# Tier 1 of the Ngoma debug pipeline (planning/ngoma-debug-pipeline.md).
#
# Stdout/stderr is teed to /tmp/emu-ngoma.log. DRUMVOICE_TRACE output goes
# to stderr — the same stream — so traces and faults interleave by time.
#
# Usage: tools/run-emu-gdb.sh
#
# Env overrides:
#   EMU_ELF=/path/to/emu.elf   (default: er-301-stolmine emu)
#   GDB_BREAK="symbol1,symbol2" (additional breakpoints to set)

set -euo pipefail

EMU_ELF="${EMU_ELF:-$HOME/repos/er-301-stolmine/testing/linux-x86_64/emu/emu.elf}"
LOG="${EMU_LOG:-/tmp/emu-ngoma.log}"

if [[ ! -x "$EMU_ELF" ]]; then
  echo "error: emu binary not found / not executable: $EMU_ELF" >&2
  exit 2
fi

if ! command -v gdb >/dev/null 2>&1; then
  echo "error: gdb not on PATH" >&2
  exit 2
fi

GDB_CMDS=$(mktemp -t gdb-emu.XXXXXX)
trap 'rm -f "$GDB_CMDS"' EXIT

cat > "$GDB_CMDS" <<'GDB'
set pagination off
set print thread-events off
handle SIGSEGV stop print pass
handle SIGBUS  stop print pass
handle SIGFPE  stop print pass
# SDL / pthread wakeup signals - silent
handle SIG32   nostop noprint pass
handle SIG33   nostop noprint pass
handle SIG34   nostop noprint pass
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass
GDB

# Optional symbol breakpoints via GDB_BREAK="sym1,sym2,..."
if [[ -n "${GDB_BREAK:-}" ]]; then
  IFS=',' read -ra _BREAKS <<< "$GDB_BREAK"
  for sym in "${_BREAKS[@]}"; do
    echo "break ${sym}" >> "$GDB_CMDS"
  done
fi

cat >> "$GDB_CMDS" <<'GDB'
# Auto-bt on stop, then continue *only* for non-fatal stops we set above.
# For SIGSEGV/SIGBUS/SIGFPE the default is "stop print pass" → gdb halts so
# we can inspect. The user can `c` or quit.
define hookpost-stop
  bt
end
echo --- run ---\n
run
GDB

echo "[run-emu-gdb] launching: $EMU_ELF"
echo "[run-emu-gdb] log: $LOG"
echo "[run-emu-gdb] gdb cmds: $GDB_CMDS"
echo

exec gdb -q -x "$GDB_CMDS" "$EMU_ELF" 2>&1 | tee "$LOG"
