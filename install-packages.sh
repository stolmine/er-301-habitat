#!/bin/bash
DEST=/mnt/ER-301/packages
SRC=/home/sure/repos/er-301-habitat/testing/am335x

# Habitat packages (auto-detect from build output)
for pkg in "$SRC"/*.pkg; do
  name=$(basename "$pkg" .pkg)
  sudo cp "$pkg" "$DEST/${name}-stolmine.pkg"
  echo "  $name"
done

# Community packages
sudo cp /home/sure/repos/Accents/testing/am335x/Accents-0.6.16.pkg "$DEST/Accents-0.6.16-stolmine.pkg"
sudo cp /home/sure/repos/er-301-custom-units/testing/am335x/sloop-1.0.3.pkg "$DEST/sloop-1.0.3-stolmine.pkg"
sudo cp /home/sure/repos/er-301-custom-units/testing/am335x/lojik-1.2.0.pkg "$DEST/lojik-1.2.0-stolmine.pkg"
sudo cp /home/sure/repos/er-301-custom-units/testing/am335x/strike-2.0.0.pkg "$DEST/strike-2.0.0-stolmine.pkg"
sudo cp /home/sure/repos/er-301-custom-units/testing/am335x/polygon-1.0.0.pkg "$DEST/polygon-1.0.0-stolmine.pkg"

echo "Done. Installed packages:"
ls "$DEST"/*stolmine*
