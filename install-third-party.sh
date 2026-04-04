#!/bin/bash
# Copy third-party release packages to ER-301 front SD card

SRC=testing/third-party
DEST=/mnt/ER-301/packages

if [ ! -d "$DEST" ]; then
  echo "SD card not mounted at /mnt or packages dir missing"
  exit 1
fi

for pkg in "$SRC"/*.pkg; do
  name=$(basename "$pkg")
  echo "  $name"
  cp "$pkg" "$DEST/$name"
done

echo "Done. $(ls "$SRC"/*.pkg | wc -l) packages copied to $DEST"
