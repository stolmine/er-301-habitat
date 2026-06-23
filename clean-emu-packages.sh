#!/bin/bash
# Remove stale package versions from the emulator package dirs, keeping
# only the latest version per package basename. Covers BOTH emu package
# locations:
#   ~/.od/rear                     (mod.mk `install` + dev auto-install target)
#   ~/.od/front/ER-301/packages    (install-packages.sh emu target)
# Vanilla and "-stolmine"-style forked builds of the same upstream
# package are tracked separately so the latest of each is retained.
#
# Usage:
#   ./clean-emu-packages.sh           # preview (prints DELETE / KEEP lines)
#   ./clean-emu-packages.sh --apply   # delete stale .pkg files
#
# No sudo needed — both dirs live under $HOME. Override the locations
# with EMU_DIRS (space-separated) if your .od lives elsewhere.

EMU_DIRS="${EMU_DIRS:-$HOME/.od/rear $HOME/.od/front/ER-301/packages}"
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

found_any=0
for d in $EMU_DIRS; do
  [ -d "$d" ] || continue
  found_any=1
  echo "=== $d ==="
  classify "$d"
done

if [ "$found_any" -eq 0 ]; then
  echo "No emu package dirs found (looked in: $EMU_DIRS)"
  echo "(set EMU_DIRS='/path/a /path/b' if your .od lives elsewhere)"
  exit 1
fi

case "$MODE" in
  --apply)
    echo
    echo "Deleting stale packages..."
    for d in $EMU_DIRS; do
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
