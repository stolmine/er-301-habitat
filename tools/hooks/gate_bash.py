#!/usr/bin/env python3
"""PreToolUse(Bash) gate: force commit/push through scripts/dev.

Blocks raw `git commit` and `git push` (exit 2 = block, stderr shown to Claude),
redirecting to the gated scripts/dev entrypoint. Read-only git, `make`, and
everything else pass through untouched (this repo has no cmake/ctest to gate).

FAIL-OPEN by design: any parse/logic error exits 0 (allow), so a bug in this
hook can never brick the Bash tool.
"""
import json
import os
import re
import subprocess
import sys

# (regex on the command, the scripts/dev subcommand to use instead)
BLOCKED = [
    (re.compile(r"\bgit\s+commit\b"), "commit"),
    (re.compile(r"\bgit\s+push\b"), "push"),
]


def main():
    try:
        data = json.load(sys.stdin)
        cmd = (data.get("tool_input") or {}).get("command", "") or ""
    except Exception:
        return 0  # fail-open: couldn't parse → allow

    # The blessed path is always allowed (it legitimately calls git internally,
    # in a subprocess this hook never sees).
    if "scripts/dev" in cmd:
        return 0

    # Only gate the repo the ledger actually governs. Sessions rooted here routinely
    # touch sibling repos (e.g. the Trinity profiling harness) that have no scripts/dev
    # and no ledger, where this would be a false positive with no legitimate path out.
    cwd = data.get("cwd") or os.getcwd()
    # Honour a leading `cd <dir>` in the command itself: the session cwd stays at the
    # habitat root even when the command operates on a sibling repo.
    m = re.match(r"\s*cd\s+(\"[^\"]+\"|'[^']+'|\S+)\s*&&", cmd)
    if m:
        target = m.group(1).strip("\"'")
        target = os.path.expanduser(target)
        if not os.path.isabs(target):
            target = os.path.join(cwd, target)
        if os.path.isdir(target):
            cwd = target
    try:
        root = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            capture_output=True, text=True, timeout=2,
            cwd=cwd,
        ).stdout.strip()
    except Exception:
        return 0  # fail-open, per this hook's design
    if os.path.basename(root) != "er-301-habitat":
        return 0

    for rx, sub in BLOCKED:
        if rx.search(cmd):
            sys.stderr.write(
                f"Blocked: run this through the gate → `scripts/dev {sub}`.\n"
                f"(raw git commit/push are gated so the ledger check + TODO render "
                f"always run. Read-only git and make are fine.)\n"
            )
            return 2  # block the tool call
    return 0


if __name__ == "__main__":
    sys.exit(main())
