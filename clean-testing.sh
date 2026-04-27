#!/bin/bash
# Remove stale package versions from testing/{am335x,linux}, keeping only
# the latest version per package basename. Vanilla and "-stolmine"-style
# forked builds of the same upstream package are tracked separately so
# the latest of each is retained.
#
# Usage:
#   ./clean-testing.sh           # preview (prints DELETE / KEEP lines)
#   ./clean-testing.sh --apply   # actually delete the stale .pkg files

MODE="${1:-preview}"

classify() {
  local dir="$1"
  ls "$dir"/*.pkg 2>/dev/null | awk '
    {
      bn = $0; sub(/.*\//, "", bn); sub(/\.pkg$/, "", bn)
      if (match(bn, /-[0-9]/)) {
        name = substr(bn, 1, RSTART - 1); rest = substr(bn, RSTART + 1)
        if (match(rest, /^[0-9][0-9.]*/)) {
          ver = substr(rest, 1, RLENGTH)
          suf = substr(rest, RLENGTH + 1); sub(/^-/, "", suf)
          key = (suf == "") ? name : name "-" suf
          printf "%s\t%s\t%s\n", key, ver, $0
        }
      }
    }
  ' | sort -k1,1 -k2V | awk -F'\t' '
    NR > 1 { if ($1 == prev_key) print "DELETE " prev_file; else print "KEEP   " prev_file }
    { prev_key = $1; prev_file = $3 }
    END { if (NR > 0) print "KEEP   " prev_file }
  '
}

for d in testing/am335x testing/linux; do
  [ -d "$d" ] || continue
  echo "=== $d ==="
  classify "$d"
done

case "$MODE" in
  --apply)
    echo
    echo "Deleting stale packages..."
    for d in testing/am335x testing/linux; do
      [ -d "$d" ] || continue
      classify "$d" | awk '/^DELETE / { sub(/^DELETE +/, ""); print }' \
        | xargs -d '\n' -r rm -v
    done
    ;;
  preview)
    echo
    echo "(preview only -- pass --apply to delete)"
    ;;
esac
