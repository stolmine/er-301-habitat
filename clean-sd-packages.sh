#!/bin/bash
# Remove stale package versions from the ER-301 SD card, keeping only
# the latest version per package basename. Vanilla and "-stolmine"-style
# forked builds of the same upstream package are tracked separately so
# the latest of each is retained.
#
# Usage:
#   ./clean-sd-packages.sh           # preview (prints DELETE / KEEP lines)
#   ./clean-sd-packages.sh --apply   # delete stale .pkg files (uses sudo)
#
# Override the SD path with SD_PACKAGES if mounted somewhere other than
# /mnt/ER-301/packages.

SD_PACKAGES="${SD_PACKAGES:-/mnt/ER-301/packages}"
MODE="${1:-preview}"

if [ ! -d "$SD_PACKAGES" ]; then
  echo "SD packages dir not found: $SD_PACKAGES"
  echo "(set SD_PACKAGES=/path/to/packages if mounted elsewhere)"
  exit 1
fi

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

echo "=== $SD_PACKAGES ==="
classify "$SD_PACKAGES"

case "$MODE" in
  --apply)
    echo
    echo "Deleting stale packages (sudo required)..."
    classify "$SD_PACKAGES" | awk '/^DELETE / { sub(/^DELETE +/, ""); print }' \
      | sudo xargs -d '\n' -r rm -v
    ;;
  preview)
    echo
    echo "(preview only -- pass --apply to delete)"
    ;;
esac
