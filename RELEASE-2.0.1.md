# er-301-habitat v2.0.1

Release date: 2026-04-11

Hotfix for Plaits engine switch crash.

## Fix

**Plaits v1.4.0**: Fixed hard system crash when switching engines above index 7-10. The shared arena allocator was not re-initialized on engine change at runtime. Only the last engine's buffer allocations were valid in memory after Voice::Init(). Switching to engines with large allocations (particle diffuser = 16KB+) used stale arena memory.

Fix: store allocator pointer in Voice, call Free()+Init() on the new engine before LoadUserData/Reset. Same approach Clouds uses for mode switches. Arena also restored to 64KB (was incorrectly at 32KB).

## Install

Replace `plaits-1.3.0-stolmine.pkg` with `plaits-1.4.0-stolmine.pkg` on the SD card. Delete the installed lib to force reinstall:

```
sudo rm -rf /mnt/v0.7/libs/plaits/
```

No other packages changed.
