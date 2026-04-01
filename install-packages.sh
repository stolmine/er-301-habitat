#!/bin/bash
# Install habitat and community packages to both hardware SD and emulator

HW_DEST=/mnt/ER-301/packages
HW_SRC=/home/sure/repos/er-301-habitat/testing/am335x

EMU_DEST=/home/sure/.od/front/ER-301/packages
EMU_SRC=/home/sure/repos/er-301-habitat/testing/linux

# --- Hardware (am335x) ---
if [ -d "$HW_DEST" ]; then
  echo "Hardware (am335x):"
  for pkg in "$HW_SRC"/*.pkg; do
    [ -f "$pkg" ] || continue
    name=$(basename "$pkg" .pkg)
    sudo cp "$pkg" "$HW_DEST/${name}-stolmine.pkg"
    echo "  $name"
  done

  # Community packages
  sudo cp /home/sure/repos/Accents/testing/am335x/Accents-0.6.16.pkg "$HW_DEST/Accents-0.6.16-stolmine.pkg" 2>/dev/null
  sudo cp /home/sure/repos/er-301-custom-units/testing/am335x/sloop-1.0.3.pkg "$HW_DEST/sloop-1.0.3-stolmine.pkg" 2>/dev/null
  sudo cp /home/sure/repos/er-301-custom-units/testing/am335x/lojik-1.2.0.pkg "$HW_DEST/lojik-1.2.0-stolmine.pkg" 2>/dev/null
  sudo cp /home/sure/repos/er-301-custom-units/testing/am335x/strike-2.0.0.pkg "$HW_DEST/strike-2.0.0-stolmine.pkg" 2>/dev/null
  sudo cp /home/sure/repos/er-301-custom-units/testing/am335x/polygon-1.0.0.pkg "$HW_DEST/polygon-1.0.0-stolmine.pkg" 2>/dev/null
else
  echo "Hardware: SD card not mounted at $HW_DEST, skipping"
fi

# --- Emulator (linux) ---
if [ -d "$EMU_DEST" ]; then
  echo "Emulator (linux):"
  # Remove stale packages
  rm -f "$EMU_DEST"/stolmine-*.pkg
  rm -f "$EMU_DEST"/clouds-*.pkg
  rm -f "$EMU_DEST"/plaits-*.pkg
  rm -f "$EMU_DEST"/grids-*.pkg
  rm -f "$EMU_DEST"/stratos-*.pkg
  rm -f "$EMU_DEST"/kryos-*.pkg
  rm -f "$EMU_DEST"/warps-*.pkg
  rm -f "$EMU_DEST"/rings-*.pkg
  rm -f "$EMU_DEST"/commotio-*.pkg
  rm -f "$EMU_DEST"/peaks-*.pkg
  rm -f "$EMU_DEST"/scope-*.pkg
  rm -f "$EMU_DEST"/marbles-*.pkg

  for pkg in "$EMU_SRC"/*.pkg; do
    [ -f "$pkg" ] || continue
    name=$(basename "$pkg")
    cp "$pkg" "$EMU_DEST/$name"
    echo "  $name"
  done
fi

echo "Done."
