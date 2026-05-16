# mi mode-selector crash — firmware-side bisect findings (2026-05-15)

Cross-reference: `er-301-stolmine/docs/HABITAT_MI_MODE_SELECTOR_CRASH.md`,
`planning/plaits-6op-os-rollback.md`.

## Status

**Open.** Hypothesis A (SequencerTask per-frame process side-effect)
ruled out by `.54-noseq` bisect. Hypothesis D variant (regression in
9.0.0 → 9.1.0 firmware window) ruled out by testing on
`v0.7.0-stolmine.9.1.0` directly: **mi crashes there too**, even
though that firmware tag predates ALL sequencer work and is known
to have worked previously with mi 1.0.2.

The mystery: **same firmware tag, same mi binary MD5 as the working
version, yet crashes now**.

## Bisect results (firmware-side)

| Firmware build | SequencerTask scheduled? | Plaits mode-switch | Clouds mode-switch | Rings model-switch |
|---|---|---|---|---|
| `v0.7.0-stolmine.9.1.0.53` | Yes | ✗ CRASH | ✗ CRASH | ✓ works |
| `v0.7.0-stolmine.9.1.0.54-noseq` (SequencerTask constructed, NOT scheduled) | No | ✗ CRASH | ✗ CRASH | (assumed ✓) |
| `v0.7.0-stolmine.9.1.0` (pre-sequencer tag, rebuilt today) | n/a | ✗ CRASH | (assumed ✗) | (assumed ✓) |

**Implications:**
- SequencerTask::process is innocent (`.54-noseq` rules it out).
- The 9.1.0 → .53 firmware diff (which is almost entirely Sequencer
  work) is NOT the trigger — `9.1.0` itself crashes.
- The trigger predates all sequencer work.
- Yet mi 1.0.2 + 9.1.0 was the known-good baseline before the
  rollback session. So the variable is something OTHER than the
  firmware source.

## Firmware-side rules out

- `od/objects/` (mi's entire API surface): no source changes
  between 9.0.0 and HEAD. Verified by `git diff --name-only`.
- Outlet / Inlet / Parameter / Option ABI: unchanged.
- SequencerTask construction-time heap usage: bisect didn't fully
  cover this — `.54-noseq` still constructs the 24 Outlets. But
  the 9.1.0 test (which has no SequencerTask at all in its source)
  also crashes, so this is also ruled out indirectly.

## Hypothesis (lifted from this bisect): mod.mk SWIG_HEADER_DEPS change

The rollback in `planning/plaits-6op-os-rollback.md` explicitly KEPT
the SWIG_HEADER_DEPS dep-tracking change and applied it across 8
packages: biome, catchall, kryos, mi, peaks, porcelain, scope,
stolmine. The plaits source files were reverted, but the build
system change stuck.

Although `SWIG_HEADER_DEPS` *shouldn't* change compiled output for
a clean rebuild from identical source, *something* in the rebuild +
reinstall sequence after the rollback changed device behavior. The
binary MD5 of mi-1.0.3.4-stolmine.pkg's libmi.so matches a pristine
build of a8c8a3c (mi 1.0.2). Yet the device behavior differs from
when mi 1.0.2 was last verified working.

**Why Clouds crashes alongside Plaits but Rings does not:**

Clouds isn't touched by the Plaits OS work directly. But:
- Plaits + Clouds both live in `libmi.so` (same package binary).
- Both do arena re-init on mode/engine switch (allocator Free + Init
  inside the audio thread's `Render` call).
- Rings is in the same `libmi.so` BUT its model-switch path does
  not do the same arena re-init dance.

So the trigger is something that's:
- Exercised by the arena-reinit pathway specifically
- Not exercised by Rings's straight-through model switch
- Not in the libmi.so binary itself (per MD5 verification)
- → Must be in adjacent state: kernel.bin, other packages' code
  layout, rear card data, or hardware state

## Most decisive next tests (in order)

### Test 1: rebuild mi WITHOUT the working-tree mod.mk changes

The `mods/mi/mod.mk` SWIG_HEADER_DEPS additions are still in
working tree (not committed yet). Build mi WITHOUT them and
install. If Plaits/Clouds work → the dep-tracking change subtly
broke the package install path. If they still crash → ruled out.

```bash
cd /home/bram/repos/er-301-habitat
git stash  # park working-tree mod.mk changes
git checkout a8c8a3c -- mods/mi/  # pristine pre-session mi source
ARCH=am335x make mi
md5sum testing/am335x/libmi.so  # should match the historical MD5
# install resulting mi-1.0.2.pkg on device
# git stash pop  # restore mod.mk changes after test
```

### Test 2: which packages were re-installed today?

SD `/mnt/ER-301/packages/` shows file timestamps. Anything modified
on 2026-05-15 (today) that wasn't there for the last working
session is a candidate trigger. Especially `biome-2.2.0-stolmine.pkg`
(noted at 20:12 today). If a package install correlates with
crash onset, that's the smoking gun.

If multiple packages were re-installed today, isolate by removing
them one at a time from `/mnt/ER-301/packages/` and rebooting.

### Test 3: full SD-side package reset

Bring the device back to the pre-rollback-session package state:
remove every package modified today, re-install the pre-session
versions only. Test mi engine-switch. Decisive test for "is it
state on the SD or something else".

### Test 4: rear card data drift

The rear card holds settings, quicksaves, sample cache. Less
likely but possible. Backup + wipe `/mnt-rear/settings.lua` and
`/mnt-rear/quicksaves/`, reboot, test.

## Firmware-side things to leave alone

- The `.54-noseq` firmware is a diagnostic build — revert
  `662c15f` (`SequencerTask::addTask` skip) on the firmware repo
  before shipping anything else. Currently tagged
  `v0.7.0-stolmine.9.1.0.54-noseq`.
- The actual `.54` firmware is fine; the trigger is NOT in any
  9.1.0-era firmware change.

## What this rules out about the bug class

- Cortex-A8 codegen sensitivity in the **firmware** isn't the
  primary trigger — `9.1.0` was a known-working firmware and yet
  crashes now without firmware changes.
- The bug is almost certainly in **package-side state** that
  changed during the rollback session, or in some artifact of how
  the rebuild-and-install dance interacted with the device.

The most likely culprit is the **mod.mk SWIG_HEADER_DEPS pattern**
applied across 8 packages, OR a specific package install that
correlates with crash onset (biome 2.6.2.x being the top
suspect by timestamp).

## When this gets resolved

Update this file + `er-301-stolmine/docs/HABITAT_MI_MODE_SELECTOR_CRASH.md`
with the resolution. If the fix is on habitat side (most likely),
no firmware tag bump needed.
