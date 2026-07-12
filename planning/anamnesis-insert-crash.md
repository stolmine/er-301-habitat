# Anamnesis: data-abort on insert (audio ISR near-null deref)

**Status:** open bug, needs fix + ledger item (file it when you pick this up).
**Severity:** high. Inserting Anamnesis crashes the ER-301 within ~0.2s.
**Where:** the bug is in `mods/anamnesis` (a memory-corruption on insert). The
crash *surfaces* in firmware kernel code, but the *cause* is Anamnesis.

## What happens

On am335x hardware, inserting Anamnesis into a chain produces a **data-abort**
about 0.2s later, in the audio interrupt handler. The device warm-reboots. This
was captured by the stolmine crash-diagnostics facility (fw 9.5.2.56, crash
diagnostics armed) with a full flight-recorder trigger + symbolized fault.

The user perceived this as a "hang on insert"; it is actually a trap (the
exception hook caught it, not the hang monitor).

## The captured report (verbatim)

```
---CRASH REPORT BEGIN
Schema: 2
Kind: data-abort
Time Since Boot: 15.160s
Firmware Version: 0.7.0-stolmine.9.5.2.56
Thread: hwi (handle=0x0)
--- Registers ---
 pc=803d9c0c lr=81a5aaa4 sp=803d9c0c psr=819a4668
 dfsr=00000005 ifsr=00000000 dfar=00000062 ifar=00000000
 r0=0000004a r1=00000001 r2=00000001 r3=8050e884 r4=805370e8 r5=805370ec r6=a000011f
 r7=00000001 r8=49002070 r9=00000000 r10=00000001 r11=819a4668 r12=8000011f
--- Module Map ---
 kernel                   text=80000000..804f2d50
 0:/v0.7/libs/anamnesis/libanamnesis.so text=80e31600..80e3ca7c  data=80e21e00..80e23b64
 0:/v0.7/libs/teletype/libteletype.so text=80df2f80..80dfa2bc  data=80dd5380..80dd6074
 0:/v0.7/libs/txo/libtxo.so text=80dfa300..80e003b4  data=80dd7640..80dd8324
--- Fault Resolution ---
 pc in kernel + 0x803d9c0c
 lr in ?
--- Flight Recorder ---
   14.939s  insert Anamnesis
--- Recent Log ---
 (not captured C-side; see flight recorder above)
---CRASH REPORT END
```

## Symbolization (against the 9.5.2.56 firmware app.elf)

```
pc=0x803d9c0c -> ti_sysbios_knl_Event_post  (Event.c:285)
```

`Event.c:285` is, inside `Event_post`:
```c
elem = (Event_PendElem *)Queue_head(pendQ);          // pendQ = &event->pendQ
elem->matchingEvents = Event_checkEvents(event, elem->andMask, elem->orMask);  // <- faults here
```

- `dfar=0x00000062`, `dfsr=0x5` = **translation fault on a near-null address**
  (a null-ish pointer dereferenced at field offset 0x62). This is a genuine
  bad-pointer deref, NOT an alignment/NEON `:64` fault (that would be `dfsr=0x1`).
- `Thread: hwi`, `r8=0x49002070` = the AM335x **EDMA** register block, i.e. this
  is the **audio EDMA completion interrupt**.

## Root-cause model

The firmware audio driver (`arch/am335x/hal/audio.c` in er-301-stolmine) drives
audio with a SYS/BIOS `Event`: the EDMA completion ISR calls
`Event_post(local.hEvents, pingDone)` to wake the audio task. That `Event` object
(and its pend queue) is a small, long-lived allocation created once at boot.

Inserting Anamnesis **corrupts memory that overlaps that Event object or its pend
queue** (a stray write leaves the pend-queue pointer chain near-null). The very
next audio interrupt calls `Event_post`, walks the corrupted queue, dereferences a
near-null element, and traps.

The fault landing in kernel `Event_post` rather than in `libanamnesis.so` is the
signature of **heap / adjacent-memory corruption**: the crash surfaces in whoever
next touches the clobbered region, not in the code that clobbered it. The
flight-recorder `insert Anamnesis` 0.2s prior ties the corruption to Anamnesis's
insert path (its constructor and/or its first `process()`).

So: this is an **out-of-bounds write / bad-pointer store in Anamnesis**, most
likely during construction (which runs at insert) or the first process block.

## Where to look

Anamnesis is a large DSP atom with many fixed-size buffers initialized in its
constructor, which is exactly the code that runs on insert:

- `mods/anamnesis/atoms/Anamnesis.h`
  - **Constructor (~L138-224):** a long run of `memset`/loop-initializers over
    fixed arrays: `mLoopBuf`, `mLine`, `mAp`, `mApWr`, `mTapBuf`, `mHannLut`,
    `mGrain*`, `mDrop*`, `mBub*`, `mBandZ`, the pan/tap/FDN LFO tables. Audit every
    loop bound against its array's declared size. A single loop that runs past the
    end of one array writes into the adjacent object (which, in the DSP heap, can
    be the audio Event / another unit / the allocator's own bookkeeping).
    - Checked one lead already: the `mBandZ` Fisher-Yates init uses `nb =
      kStreamN/2 = 7` and looks in-bounds, so start elsewhere.
  - **`process()` (~L356+):** interpolated reads/writes into `mLoopBuf`, `mLine`,
    `mTapBuf` via computed indices (`mLoopBuf[i0..i3]`, `mLine[i][...]`). Confirm
    every computed index is clamped to `[0, kLen)` on the WRITE side. A write index
    that can go negative or past the buffer on the first block is a prime suspect.
- `mods/anamnesis/AnamNoise.h` — `perm[512]` and the `static float L[kLUT*kLUT]`
  LUT build; confirm `perm[perm[x]+y]` indices stay < 512 and the LUT fill is in
  bounds.
- `mods/anamnesis/AnamField.h`, `mods/anamnesis/anamnesis.cpp.swig` — any buffer
  sized from a parameter (Length/Density/etc.) that a first-block `process()`
  writes before it is fully sized.

Favor the **constructor and first-`process()` write paths** (the crash fires at
insert, ~0.2s in), and favor **write** indices over reads (a bad read would fault
inside Anamnesis, not corrupt a neighbor).

## How to reproduce / verify (emu first, per habitat convention)

1. **Insert Anamnesis in the emulator.** A logic OOB write reproduces on x86, so it
   may crash or misbehave there too. If it does, this is diagnosable without
   hardware. Build the mod with a sanitizer if the emu build allows
   (`-fsanitize=address`) and insert Anamnesis: ASan should name the exact
   out-of-bounds store. Even without ASan, the emu + a code audit of the write
   paths above should find it.
2. If the emu is clean but hardware still traps, it is am335x-specific (alignment /
   NEON / SWIG size), but `dfar=0x62` (a null-ish deref, `dfsr=0x5`) argues for a
   plain logic OOB that should reproduce off-hardware.
3. **Confirm the fix on am335x hardware:** with crash diagnostics armed (Admin >
   System Settings > "Enable crash diagnostics?"), insert Anamnesis repeatedly and
   confirm no `data-abort in Event_post`. Suggested ledger verify (file when you
   pick this up, area `dsp`, tag `[hab:<id>]`): "insert Anamnesis in the emu
   (ASan-clean if available) AND repeatedly on am335x hardware with crash
   diagnostics armed produces no corruption / no Event_post data-abort."

## Provenance

Captured by the er-301-stolmine crash-diagnostics facility (fw 9.5.2.56, normal
build, `enableCrashDiagnostics` armed). The exception hook snapshotted the fault
context + module map + flight recorder into the warm-reboot-surviving panic buffer;
next boot flushed it to `front/crash.log`; symbolized offline via
`tools/symbolize_crash.py` against the matching `app.elf`. The `sp==pc` and bogus
`psr` in the register dump are a known SYS/BIOS-6.46 A8 trap-frame limitation
(tracked in stolmine as `crashdiag-fix-partial-register-capture`); `pc`, `dfar`,
`dfsr`, the module map, and the flight recorder are the load-bearing evidence and
are all correct.
