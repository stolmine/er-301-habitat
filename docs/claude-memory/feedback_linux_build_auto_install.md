---
name: Auto-install builds via ./install-packages.sh
description: After every successful build of an ER-301 package in this repo, install via `./install-packages.sh` (not manual `cp`). Run as user for emu only; run with `sudo` for hardware (write to `/mnt/ER-301/packages/`). Don't wait to be asked.
type: feedback
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---
The user always wants new builds installed as a matter of course. Skip the "do you want me to install?" step. **Updated 2026-05-11:** use the repo's `./install-packages.sh` rather than hand-rolled `cp` commands.

## How to apply

After `make ARCH=linux` and/or `make ARCH=am335x` produces fresh `.pkg` files under `testing/`:

- **Emu install** (user, no sudo needed): `./install-packages.sh` copies the latest linux build of every package into `$HOME/.od/front/ER-301/packages/`.
- **Hardware install** (sudo, because `/mnt` is root-owned vfat): suggest the user run `! sudo ./install-packages.sh`. Sudo's `HOME=/root` makes the script's `[ -d $EMU_DEST ]` check fail silently, so the hardware section runs while the emu section is a clean no-op.

The script auto-picks the latest version per package basename. It appends a `-stolmine` suffix on the hardware copy (vfat filenames at `/mnt/ER-301/packages/`), no suffix on the emu copy.

The version-bump rule still applies (see `feedback_package_version_bump.md`): the device / emulator only re-extracts a package when PKGVERSION changes.

## Why

Faster iteration without inventing one-off `cp` paths; one canonical install path means the hardware and emu pickups stay in sync. The script is the source of truth for install destinations — keep using it even when only one package was rebuilt.

## Anti-pattern (superseded)

Earlier guidance was `cp testing/linux/<pkg>.pkg ~/.od/rear/`. That writes to the rear-SD extraction root, which is a different path than the script uses (`$HOME/.od/front/ER-301/packages/`). Prefer the script so the two paths don't diverge over time.
