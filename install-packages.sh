#!/bin/bash
# Install habitat and community packages to both hardware SD and emulator
#
# Usage:
#   ./install-packages.sh              # install dev builds (TXo firmware)
#   ./install-packages.sh --release    # install v1.3.2 release builds (vanilla compatible)
#   ./install-packages.sh --third-party # install third-party release packages (Accents, tomf)
#
# Exits non-zero if any copy fails. In particular a read-only / write-protected
# SD is caught loudly: the destination directory still EXISTS on a ro mount, so
# a plain `-d` test passes and every `cp` silently fails -- which used to look
# like a successful run. We probe writability up front and check every cp.
# (This script never prunes older versions; use the separate prune script.)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

HW_DEST=/mnt/ER-301/packages
HW_SRC="$SCRIPT_DIR/testing/am335x"
THIRD_PARTY="$SCRIPT_DIR/testing/third-party"

EMU_DEST="$HOME/.od/front/ER-301/packages"
EMU_SRC="$SCRIPT_DIR/testing/linux"

SUFFIX="-stolmine"

ERRORS=0

# Classify a destination directory:
#   0 = present and writable
#   1 = present but READ-ONLY (write-protected SD / ro mount -- the dir exists,
#       so a `-d` test passes, but every cp silently fails)
#   2 = not present
dest_status() {
  local dir="$1"
  [ -d "$dir" ] || return 2
  local probe="$dir/.install-write-test.$$"
  if ( : > "$probe" ) 2>/dev/null; then
    rm -f "$probe"
    return 0
  fi
  return 1
}

# Report a read-only destination and count it as an error.
warn_readonly() {
  local dir="$1" label="$2"
  echo "$label: $dir is READ-ONLY -- nothing copied (write-protected SD or ro mount)." >&2
  echo "  Fix: 'sudo mount -o remount,rw <mountpoint>' (or clear the SD lock tab / fsck), then re-run." >&2
  ERRORS=$((ERRORS + 1))
}

# cp that reports and records failures, so a failed copy is visible and affects
# the exit code instead of being a silent no-op.
cp_checked() {
  if cp "$1" "$2" 2>/dev/null; then
    return 0
  fi
  echo "  !! FAILED: $(basename "$1") -> $2" >&2
  ERRORS=$((ERRORS + 1))
  return 1
}

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
    dest_status "$HW_DEST"; st=$?
    [ $st -eq 2 ] && { echo "SD card not mounted at $HW_DEST" >&2; exit 1; }
    [ $st -eq 1 ] && { warn_readonly "$HW_DEST" "Hardware"; exit 1; }
    echo "Release packages (v1.3.2, vanilla compatible):"

    # stolmine (NR, Canals, Discont, LatchFilter, GestureSeq, Excel, Ballot)
    for pkg in "$THIRD_PARTY"/stolmine-*.pkg; do
      [ -f "$pkg" ] || continue
      name=$(basename "$pkg" .pkg)
      echo "  $name"
      cp_checked "$pkg" "$HW_DEST/${name}${SUFFIX}.pkg"
    done

    # MI ports + scope (release builds)
    for pkg in "$THIRD_PARTY"/release-*.pkg; do
      [ -f "$pkg" ] || continue
      name=$(basename "$pkg" .pkg | sed 's/^release-//')
      echo "  $name"
      cp_checked "$pkg" "$HW_DEST/${name}${SUFFIX}.pkg"
    done
    ;;

  --third-party)
    dest_status "$HW_DEST"; st=$?
    [ $st -eq 2 ] && { echo "SD card not mounted at $HW_DEST" >&2; exit 1; }
    [ $st -eq 1 ] && { warn_readonly "$HW_DEST" "Hardware"; exit 1; }
    echo "Third-party packages:"
    for pkg in Accents lojik polygon sloop strike; do
      f=$(ls "$THIRD_PARTY"/${pkg}-*.pkg 2>/dev/null | head -1)
      if [ -n "$f" ]; then
        name=$(basename "$f" .pkg)
        echo "  $name"
        cp_checked "$f" "$HW_DEST/${name}${SUFFIX}.pkg"
      fi
    done
    ;;

  *)
    # --- Hardware (am335x) ---
    # Process substitution (not a pipe) so cp_checked runs in THIS shell and its
    # ERRORS increments survive.
    dest_status "$HW_DEST"; st=$?
    if [ $st -eq 0 ]; then
      echo "Hardware (am335x):"
      while IFS= read -r pkg; do
        [ -f "$pkg" ] || continue
        name=$(basename "$pkg" .pkg)
        echo "  $name"
        cp_checked "$pkg" "$HW_DEST/${name}${SUFFIX}.pkg"
      done < <(pick_latest_pkgs "$HW_SRC")
    elif [ $st -eq 1 ]; then
      warn_readonly "$HW_DEST" "Hardware"
    else
      echo "Hardware: SD card not mounted at $HW_DEST, skipping"
    fi

    # --- Emulator (linux) ---
    dest_status "$EMU_DEST"; st=$?
    if [ $st -eq 0 ]; then
      echo "Emulator (linux):"
      while IFS= read -r pkg; do
        [ -f "$pkg" ] || continue
        name=$(basename "$pkg")
        echo "  $name"
        cp_checked "$pkg" "$EMU_DEST/$name"
      done < <(pick_latest_pkgs "$EMU_SRC")
    elif [ $st -eq 1 ]; then
      warn_readonly "$EMU_DEST" "Emulator"
    fi
    ;;
esac

if [ "$ERRORS" -gt 0 ]; then
  echo "install-packages: $ERRORS copy error(s) -- some packages did NOT install." >&2
  exit 1
fi
echo "Done."
