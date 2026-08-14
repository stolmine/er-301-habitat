#!/usr/bin/env bash
# Clone an ER-301 front SD card onto a freshly formatted target.
#
# DRY RUN BY DEFAULT. Nothing is written, formatted or unmounted unless you
# pass --execute. The point of this script is that formatting the wrong device
# is unrecoverable, and on a multi-slot card reader the source and target look
# nearly identical (same vendor, same model, serials differing only by LUN).
# So: expose every attribute that distinguishes them, run every safety check
# BEFORE anything destructive, and print the exact commands that would run.
#
# Usage:
#   tools/sd-card-clone.sh --target /dev/sdX                 # dry run (default)
#   tools/sd-card-clone.sh --target /dev/sdX --lean          # skip bulk media
#   tools/sd-card-clone.sh --target /dev/sdX --execute       # actually do it
#   tools/sd-card-clone.sh                                    # just survey devices
#
# The dry run works unprivileged. --execute needs root.

set -uo pipefail

SOURCE_MNT="${SOURCE_MNT:-/mnt}"
TARGET_DEV=""
WORK_MNT="/mnt2"
LABEL="ER301"
EXECUTE=0
LEAN=0

EXCLUDES=(
  "System Volume Information"
  ".Spotlight-V100"
  ".fseventsd"
  ".Trashes"
  ".TemporaryItems"
)
LEAN_EXCLUDES=(
  "mirrorcanal.mp4"
  "ER-301/recorded"
)

RED=$'\033[31m'; GRN=$'\033[32m'; YEL=$'\033[33m'; CYN=$'\033[36m'; BLD=$'\033[1m'; RST=$'\033[0m'
say()  { printf '%s\n' "$*"; }
hdr()  { printf '\n%s== %s ==%s\n' "$BLD" "$*" "$RST"; }
ok()   { printf '  %sOK%s   %s\n' "$GRN" "$RST" "$*"; }
warn() { printf '  %sWARN%s %s\n' "$YEL" "$RST" "$*"; }
die()  { printf '  %sFAIL%s %s\n' "$RED" "$RST" "$*"; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --target)  TARGET_DEV="${2:-}"; shift 2 ;;
    --source)  SOURCE_MNT="${2:-}"; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    --lean)    LEAN=1; shift ;;
    --label)   LABEL="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

[ $LEAN -eq 1 ] && EXCLUDES+=("${LEAN_EXCLUDES[@]}")

# ---------------------------------------------------------------- survey ----
# Everything that distinguishes one anonymous USB card reader slot from another.
# NOTE: findmnt can print the same mount more than once. Every use of it here
# takes only the first line - two lines poisoned disk_of() and made the
# source-vs-target guard PASS while pointed at the source disk. Caught by
# testing the refusal paths, not by reading the code.
mnt_source() { findmnt -no SOURCE "$1" 2>/dev/null | head -1; }
mnt_opts()   { findmnt -no OPTIONS "$1" 2>/dev/null | head -1; }
disk_of()  { lsblk -no PKNAME "$1" 2>/dev/null | head -1; }
root_disk() {
  local src; src=$(mnt_source /)
  local pk;  pk=$(lsblk -no PKNAME "$src" 2>/dev/null | head -1)
  [ -n "$pk" ] && echo "/dev/$pk" || echo "$src"
}
# "disk" or "part" from lsblk, never guessed from the name: nvme0n1 and mmcblk0
# both end in a digit, so a name heuristic wrongly calls them partitions and
# would refuse a legitimate mmcblk SD target.
dev_type() { lsblk -dno TYPE "$1" 2>/dev/null | head -1; }

survey_one() {
  local dev="$1" d; d=$(basename "$dev")
  local size rm model vendor serial ctime parts
  size=$(lsblk -bdno SIZE "$dev" 2>/dev/null)
  rm=$(cat "/sys/block/$d/removable" 2>/dev/null)
  vendor=$(cat "/sys/block/$d/device/vendor" 2>/dev/null | xargs)
  model=$(cat "/sys/block/$d/device/model" 2>/dev/null | xargs)
  serial=$(udevadm info --query=property --name="$dev" 2>/dev/null | sed -n 's/^ID_SERIAL=//p')
  ctime=$(stat -c '%y' "$dev" 2>/dev/null | cut -d. -f1)
  printf '  %s%-10s%s %8.2f GB  removable=%s  %s %s\n' \
    "$BLD" "$dev" "$RST" "$(awk -v b="$size" 'BEGIN{print b/1073741824}')" "${rm:-?}" "$vendor" "$model"
  printf '             serial: %s\n' "${serial:-unknown}"
  printf '             node created: %s%s\n' "$ctime" \
    "$([ "$dev" = "$TARGET_DEV" ] && echo "   <-- TARGET" || true)"
  # Partitions, filesystems, mountpoints, and whether it smells like an ER-301
  # card. lsblk -P (key="value") rather than column output: an empty LABEL or
  # MOUNTPOINT collapses in column mode and silently shifts every later field,
  # which is exactly the kind of misreading this script exists to prevent.
  local line NAME SIZE FSTYPE LABEL MOUNTPOINT PARTTYPE
  while IFS= read -r line; do
    [ -z "$line" ] && continue
    NAME=""; SIZE=""; FSTYPE=""; LABEL=""; MOUNTPOINT=""; PARTTYPE=""
    eval "$line"
    [ "/dev/$NAME" = "$dev" ] && continue   # skip the disk row itself
    local marker=""
    if [ -n "$MOUNTPOINT" ] && [ -d "$MOUNTPOINT/ER-301/packages" ]; then
      marker="  ${CYN}<-- looks like an ER-301 card${RST}"
    fi
    printf '             part %-8s %8s  fs=%-6s label=%-8s type=%-5s mount=%s%s\n' \
      "$NAME" "$SIZE" "${FSTYPE:--}" "${LABEL:--}" "${PARTTYPE:--}" "${MOUNTPOINT:--}" "$marker"
  done < <(lsblk -Pno NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT,PARTTYPE "$dev" 2>/dev/null)
}

hdr "REMOVABLE BLOCK DEVICES"
FOUND=0
for d in /sys/block/sd* /sys/block/mmcblk*; do
  [ -e "$d" ] || continue
  n=$(basename "$d")
  [ "$(cat "$d/removable" 2>/dev/null)" = "1" ] || continue
  survey_one "/dev/$n"
  FOUND=$((FOUND+1))
done
[ $FOUND -eq 0 ] && warn "no removable devices found"
say ""
say "  root filesystem is on: $(root_disk)   (never a valid target)"

if [ -z "$TARGET_DEV" ]; then
  hdr "NO TARGET GIVEN"
  say "  Survey only. Re-run with --target /dev/sdX to see the full plan."
  exit 0
fi

# ------------------------------------------------------------ safety net ----
hdr "SAFETY CHECKS"

[ -b "$TARGET_DEV" ] || die "$TARGET_DEV is not a block device"
ok "$TARGET_DEV is a block device"

TDISK=$(basename "$TARGET_DEV")
TTYPE=$(dev_type "$TARGET_DEV")
[ "$TTYPE" = "disk" ] || die "target must be a WHOLE DISK; $TARGET_DEV is type '${TTYPE:-unknown}'"
ok "target is a whole disk (lsblk type=disk), not a partition"

[ "$(cat "/sys/block/$TDISK/removable" 2>/dev/null)" = "1" ] \
  || die "$TARGET_DEV is not removable - refusing"
ok "target is removable"

RDISK=$(root_disk)
[ "$TARGET_DEV" != "$RDISK" ] || die "$TARGET_DEV holds the root filesystem"
ok "target is not the root disk ($RDISK)"

SRC_SRC=$(mnt_source "$SOURCE_MNT")
[ -n "$SRC_SRC" ] || die "source $SOURCE_MNT is not mounted"
SRC_PK=$(disk_of "$SRC_SRC")
[ -n "$SRC_PK" ] || die "cannot resolve the parent disk of $SRC_SRC - refusing to guess"
SRC_DISK="/dev/$SRC_PK"
ok "source $SOURCE_MNT is $SRC_SRC on $SRC_DISK"

[ "$SRC_DISK" != "$TARGET_DEV" ] \
  || die "target $TARGET_DEV IS the source disk - this would erase what you are copying"
ok "target is a different physical disk from the source"

if [ -d "$SOURCE_MNT/ER-301/packages" ]; then
  ok "source looks like an ER-301 card (ER-301/packages present)"
else
  warn "source does NOT contain ER-301/packages - is $SOURCE_MNT really the card?"
fi

SRC_OPTS=$(mnt_opts "$SOURCE_MNT")
case "$SRC_OPTS" in
  ro,*|*,ro|*,ro,*) ok "source is mounted READ-ONLY (safe to copy from)" ;;
  *) warn "source is mounted read-write - copying is still fine, just noting it" ;;
esac

# Anything on the target currently mounted?
TMOUNTS=$(lsblk -no MOUNTPOINT "$TARGET_DEV" 2>/dev/null | sed '/^$/d')
if [ -n "$TMOUNTS" ]; then
  warn "target has mounted filesystems (will be unmounted):"
  printf '         %s\n' $TMOUNTS
  for m in $TMOUNTS; do
    [ -d "$m/ER-301/packages" ] && die "target mount $m CONTAINS AN ER-301 CARD - wrong device?"
  done
else
  ok "nothing on the target is currently mounted"
fi

# capacity
SRC_BYTES=$(du -sb --exclude='System Volume Information' --exclude='.Spotlight-V100' \
            --exclude='.fseventsd' "$SOURCE_MNT" 2>/dev/null | cut -f1)
TGT_BYTES=$(lsblk -bdno SIZE "$TARGET_DEV" 2>/dev/null)
if [ -n "$SRC_BYTES" ] && [ -n "$TGT_BYTES" ]; then
  awk -v s="$SRC_BYTES" -v t="$TGT_BYTES" 'BEGIN{
    printf "  %s   source payload %.2f GB vs target capacity %.2f GB (%.0f%% full)\n",
      (s<t*0.95 ? "\033[32mOK\033[0m  " : "\033[33mWARN\033[0m"), s/1073741824, t/1073741824, 100*s/t }'
  [ "$SRC_BYTES" -lt "$TGT_BYTES" ] || die "source does not fit on target"
fi

# ------------------------------------------------------------- manifest ----
hdr "COPY MANIFEST"
say "  source : $SOURCE_MNT"
say "  target : ${TARGET_DEV}1  -> mounted at $WORK_MNT during the copy"
say "  label  : $LABEL"
say "  mode   : $([ $LEAN -eq 1 ] && echo 'LEAN (bulk media excluded)' || echo 'FULL')"
say "  excludes:"
for e in "${EXCLUDES[@]}"; do say "    - $e"; done

RSYNC_ARGS=(-rltD --modify-window=2)
for e in "${EXCLUDES[@]}"; do RSYNC_ARGS+=(--exclude "$e"); done

hdr "WHAT WOULD BE TRANSFERRED (rsync --dry-run, honors the excludes)"
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"' EXIT
rsync "${RSYNC_ARGS[@]}" --dry-run --stats "$SOURCE_MNT/" "$SCRATCH/" 2>/dev/null \
  | grep -E "Number of files|Number of created|Total file size|Total transferred" \
  | sed 's/^/  /'
say ""
say "  top-level breakdown:"
du -sh "$SOURCE_MNT"/* 2>/dev/null | sort -h | tail -8 | sed 's/^/    /'

# -------------------------------------------------------------- commands ----
hdr "COMMANDS THAT WOULD RUN"
# Shell-quoted with %q so what is printed is exactly what could be pasted.
# The excludes contain spaces ("System Volume Information"); printing them
# unquoted would show a command that silently does the wrong thing.
RSYNC_SHOWN=$(printf '%q ' "${RSYNC_ARGS[@]}")
DIFF_SHOWN=$(printf -- '-x %q ' "${EXCLUDES[@]}")
cat <<EOF
  # 1. unmount anything on the target
  umount ${TARGET_DEV}?* 2>/dev/null

  # 2. format the existing FAT32 partition in place
  #    (the partition table is left alone - no repartitioning)
  mkfs.vfat -F 32 -n $(printf '%q' "$LABEL") ${TARGET_DEV}1

  # 3. mount and copy
  mkdir -p $WORK_MNT
  mount ${TARGET_DEV}1 $WORK_MNT
  rsync ${RSYNC_SHOWN}--info=progress2 $(printf '%q' "$SOURCE_MNT")/ $WORK_MNT/
  sync

  # 4. verify and release
  diff -r -q ${DIFF_SHOWN}$(printf '%q' "$SOURCE_MNT") $WORK_MNT
  umount $WORK_MNT
EOF

# -------------------------------------------------------------- execute ----
if [ $EXECUTE -eq 0 ]; then
  hdr "DRY RUN COMPLETE"
  say "  Nothing was changed. Re-run with ${BLD}--execute${RST} (as root) to perform it."
  exit 0
fi

[ "$(id -u)" -eq 0 ] || die "--execute requires root"

hdr "${RED}DESTRUCTIVE${RST}"
say "  About to ERASE ${BLD}${TARGET_DEV}1${RST} and copy $SOURCE_MNT onto it."
printf '  Type the target device path exactly to confirm: '
read -r CONFIRM
[ "$CONFIRM" = "$TARGET_DEV" ] || die "confirmation did not match - aborted"

set -e
umount "${TARGET_DEV}"?* 2>/dev/null || true
mkfs.vfat -F 32 -n "$LABEL" "${TARGET_DEV}1"
mkdir -p "$WORK_MNT"
mount "${TARGET_DEV}1" "$WORK_MNT"
rsync "${RSYNC_ARGS[@]}" --info=progress2 "$SOURCE_MNT/" "$WORK_MNT/"
sync
hdr "VERIFY"
DIFF_ARGS=(); for e in "${EXCLUDES[@]}"; do DIFF_ARGS+=(-x "$e"); done
if diff -r -q "${DIFF_ARGS[@]}" "$SOURCE_MNT" "$WORK_MNT"; then
  ok "target matches source"
else
  warn "differences reported above"
fi
umount "$WORK_MNT"
hdr "DONE"
