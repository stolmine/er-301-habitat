---
name: Auto-install linux builds to the emulator
description: After every successful `make ARCH=linux` of an ER-301 package in this repo, immediately copy the produced .pkg into `~/.od/rear/` so the emulator picks it up on next launch. Don't wait to be asked.
type: feedback
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---
The user always wants linux builds installed to the emulator as a matter of course. Skip the "do you want me to install?" step.

## How to apply

After any `make <pkg> ARCH=linux` (or equivalent) that produces `testing/linux/<pkgname>-<version>.pkg`, run:

```
cp testing/linux/<pkgname>-<version>.pkg ~/.od/rear/
```

The emulator's rear-SD root is `~/.od/rear/`. Existing same-named packages get overwritten cleanly.

The version-bump rule still applies (see `feedback_package_version_bump.md`): the emulator only re-extracts the package on version change, so the same install workflow as hardware — bump version on every iteration — applies here too.

## Why

Faster iteration: the user wants the next emu launch to have the new build live without an extra round-trip. Telling them "ready to install" wastes a turn.
