#!/bin/bash
# Install habitat and community packages to both hardware SD and emulator
#
# Usage:
#   ./install-packages.sh              # install dev builds (TXo firmware)
#   ./install-packages.sh --release    # install v1.3.2 release builds (vanilla compatible)
#   ./install-packages.sh --third-party # install third-party release packages (Accents, tomf)

HW_DEST=/mnt/ER-301/packages
HW_SRC=/home/sure/repos/er-301-habitat/testing/am335x
THIRD_PARTY=/home/sure/repos/er-301-habitat/testing/third-party

EMU_DEST=/home/sure/.od/front/ER-301/packages
EMU_SRC=/home/sure/repos/er-301-habitat/testing/linux

SUFFIX="-stolmine"

# Emit one path per package basename, picking the highest version. The
# "basename" is the filename minus its -<version> chunk; if a non-numeric
# suffix follows the version (e.g. "-stolmine.9.1.0.4"), it's folded
# into the basename so vanilla and forked builds of the same upstream
# package are tracked separately.
pick_latest_pkgs() {
  local dir="$1"
  ls "$dir"/*.pkg 2>/dev/null | awk '
    {
      bn = $0; sub(/.*\//, "", bn); sub(/\.pkg$/, "", bn)
      key = bn; ver = "0"
      if (match(bn, /-[0-9]/)) {
        name = substr(bn, 1, RSTART - 1)
        rest = substr(bn, RSTART + 1)
        if (match(rest, /^[0-9][0-9.]*/)) {
          ver = substr(rest, 1, RLENGTH)
          suf = substr(rest, RLENGTH + 1); sub(/^-/, "", suf)
          key = (suf == "") ? name : name "-" suf
        }
      }
      printf "%s\t%s\t%s\n", key, ver, $0
    }
  ' | sort -k1,1 -k2V | awk -F'\t' '
    { latest[$1] = $3 }
    END { for (k in latest) print latest[k] }
  ' | sort
}

case "${1}" in
  --release)
    if [ ! -d "$HW_DEST" ]; then
      echo "SD card not mounted at $HW_DEST"
      exit 1
    fi
    echo "Release packages (v1.3.2, vanilla compatible):"

    # stolmine (NR, Canals, Discont, LatchFilter, GestureSeq, Excel, Ballot)
    for pkg in "$THIRD_PARTY"/stolmine-*.pkg; do
      [ -f "$pkg" ] || continue
      name=$(basename "$pkg" .pkg)
      echo "  $name"
      cp "$pkg" "$HW_DEST/${name}${SUFFIX}.pkg"
    done

    # MI ports + scope (release builds)
    for pkg in "$THIRD_PARTY"/release-*.pkg; do
      [ -f "$pkg" ] || continue
      name=$(basename "$pkg" .pkg | sed 's/^release-//')
      echo "  $name"
      cp "$pkg" "$HW_DEST/${name}${SUFFIX}.pkg"
    done
    echo "Done."
    ;;

  --third-party)
    if [ ! -d "$HW_DEST" ]; then
      echo "SD card not mounted at $HW_DEST"
      exit 1
    fi
    echo "Third-party packages:"
    for pkg in Accents lojik polygon sloop strike; do
      f=$(ls "$THIRD_PARTY"/${pkg}-*.pkg 2>/dev/null | head -1)
      if [ -n "$f" ]; then
        name=$(basename "$f" .pkg)
        echo "  $name"
        cp "$f" "$HW_DEST/${name}${SUFFIX}.pkg"
      fi
    done
    echo "Done."
    ;;

  *)
    # --- Hardware (am335x) ---
    if [ -d "$HW_DEST" ]; then
      echo "Hardware (am335x):"
      pick_latest_pkgs "$HW_SRC" | while IFS= read -r pkg; do
        [ -f "$pkg" ] || continue
        name=$(basename "$pkg" .pkg)
        echo "  $name"
        cp "$pkg" "$HW_DEST/${name}${SUFFIX}.pkg"
      done
    else
      echo "Hardware: SD card not mounted at $HW_DEST, skipping"
    fi

    # --- Emulator (linux) ---
    if [ -d "$EMU_DEST" ]; then
      echo "Emulator (linux):"
      pick_latest_pkgs "$EMU_SRC" | while IFS= read -r pkg; do
        [ -f "$pkg" ] || continue
        name=$(basename "$pkg")
        echo "  $name"
        cp "$pkg" "$EMU_DEST/$name"
      done
    fi
    echo "Done."
    ;;
esac
